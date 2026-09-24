#include "VideoSender.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QUdpSocket>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace rm {

namespace {
// 单个 UDP 载荷上限，避免 IP 分片。
constexpr int kMtu = 1400;

// 优先使用随包附带的 ffmpeg（独立发行版），否则回退到 PATH。
QString ffmpegProgram() {
  const QString dir = QCoreApplication::applicationDirPath();
  for (const QString& name : {QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg.exe")}) {
    const QString path = dir + QLatin1Char('/') + name;
    if (QFileInfo::exists(path))
      return path;
  }
  return QStringLiteral("ffmpeg");
}
} // namespace

VideoSender::VideoSender(QObject* parent) : QObject(parent) {
  m_parser = av_parser_init(AV_CODEC_ID_HEVC);
  const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  if (dec)
    m_parserCtx = avcodec_alloc_context3(dec);
}

VideoSender::~VideoSender() {
  stop();
  if (m_parser)
    av_parser_close(m_parser);
  if (m_parserCtx)
    avcodec_free_context(&m_parserCtx);
}

bool VideoSender::start(const QString& file, const QString& host, quint16 port) {
  stop();
  if (!m_parser || !m_parserCtx) {
    emit errorOccurred(tr("HEVC 解析器不可用"));
    return false;
  }
  m_file = file;
  m_host = host;
  m_port = port;
  m_frameId = 0;

  m_socket = new QUdpSocket(this);
  m_proc = new QProcess(this);
  connect(m_proc, &QProcess::readyReadStandardOutput, this, &VideoSender::onStdout);
  connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
          this, &VideoSender::onProcFinished);

  const QStringList args = {
      QStringLiteral("-hide_banner"),
      QStringLiteral("-loglevel"), QStringLiteral("error"),
      QStringLiteral("-stream_loop"), QStringLiteral("-1"), // 循环播放
      QStringLiteral("-re"),
      QStringLiteral("-i"), file,
      QStringLiteral("-an"),
      QStringLiteral("-vf"), QStringLiteral("scale=1280:-2"),
      QStringLiteral("-c:v"), QStringLiteral("libx265"),
      QStringLiteral("-preset"), QStringLiteral("ultrafast"),
      QStringLiteral("-tune"), QStringLiteral("zerolatency"),
      QStringLiteral("-x265-params"),
      QStringLiteral("keyint=30:min-keyint=30:bframes=0:repeat-headers=1:log-level=none"),
      QStringLiteral("-f"), QStringLiteral("hevc"),
      QStringLiteral("-"),
  };
  m_proc->start(ffmpegProgram(), args);
  if (!m_proc->waitForStarted(3000)) {
    emit errorOccurred(tr("无法启动 ffmpeg（请确认已安装）"));
    stop();
    return false;
  }
  emit started(file);
  return true;
}

void VideoSender::stop() {
  if (m_proc) {
    m_proc->disconnect(this);
    m_proc->kill();
    m_proc->waitForFinished(1000);
    m_proc->deleteLater();
    m_proc = nullptr;
  }
  if (m_socket) {
    m_socket->deleteLater();
    m_socket = nullptr;
  }
  if (m_parser) {
    av_parser_close(m_parser);
    const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    m_parser = dec ? av_parser_init(AV_CODEC_ID_HEVC) : nullptr;
  }
}

void VideoSender::sendAccessUnit(const char* data, int size) {
  if (!m_socket || size <= 0)
    return;
  const quint16 frameId = m_frameId++;
  quint16 sliceId = 0;
  int offset = 0;
  while (offset < size) {
    const int n = qMin(kMtu, size - offset);
    QByteArray pkt(8 + n, Qt::Uninitialized);
    auto* u = reinterpret_cast<unsigned char*>(pkt.data());
    u[0] = static_cast<unsigned char>(frameId >> 8);
    u[1] = static_cast<unsigned char>(frameId & 0xff);
    u[2] = static_cast<unsigned char>(sliceId >> 8);
    u[3] = static_cast<unsigned char>(sliceId & 0xff);
    const quint32 total = static_cast<quint32>(size);
    u[4] = static_cast<unsigned char>(total >> 24);
    u[5] = static_cast<unsigned char>((total >> 16) & 0xff);
    u[6] = static_cast<unsigned char>((total >> 8) & 0xff);
    u[7] = static_cast<unsigned char>(total & 0xff);
    memcpy(pkt.data() + 8, data + offset, n);
    m_socket->writeDatagram(pkt, QHostAddress(m_host), m_port);
    ++sliceId;
    offset += n;
  }
}

void VideoSender::feed(const QByteArray& data) {
  const auto* p = reinterpret_cast<const uint8_t*>(data.constData());
  int remaining = data.size();
  while (remaining > 0) {
    uint8_t* out = nullptr;
    int outSize = 0;
    const int used = av_parser_parse2(m_parser, m_parserCtx, &out, &outSize, p, remaining,
                                      AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (used < 0)
      break;
    p += used;
    remaining -= used;
    if (outSize > 0)
      sendAccessUnit(reinterpret_cast<const char*>(out), outSize);
    if (used == 0 && outSize == 0)
      break; // 解析器已缓冲，等待更多数据
  }
}

void VideoSender::flushParser() {
  if (!m_parser)
    return;
  uint8_t* out = nullptr;
  int outSize = 0;
  av_parser_parse2(m_parser, m_parserCtx, &out, &outSize, nullptr, 0, AV_NOPTS_VALUE,
                   AV_NOPTS_VALUE, 0);
  if (outSize > 0)
    sendAccessUnit(reinterpret_cast<const char*>(out), outSize);
}

void VideoSender::onStdout() {
  if (!m_proc)
    return;
  feed(m_proc->readAllStandardOutput());
}

void VideoSender::onProcFinished(int exitCode, int status) {
  Q_UNUSED(exitCode)
  Q_UNUSED(status)
  flushParser();
  if (m_proc) {
    m_proc->deleteLater();
    m_proc = nullptr;
  }
  if (m_socket) {
    m_socket->deleteLater();
    m_socket = nullptr;
  }
  emit stopped();
}

} // namespace rm
