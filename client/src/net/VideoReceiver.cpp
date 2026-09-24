#include "net/VideoReceiver.h"

#include <QDateTime>
#include <QHostAddress>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

namespace rm {

namespace {

// 官方包头：FrameID(2) + SliceID(2) + TotalBytes(4)，大端。
constexpr int kHeaderSize = 8;
constexpr quint32 kMaxFrameBytes = 4u * 1024 * 1024;
constexpr int kMaxPendingFrames = 64;
constexpr qint64 kFrameTimeoutMs = 2000;
constexpr qint64 kSignalTimeoutMs = 1000;
// 硬解尝试多少帧仍无输出则回退软解。
constexpr int kHwFallbackFrames = 60;

// 优先选用 VAAPI（AMD/Intel）或 CUDA/NVDEC（NVIDIA）硬解。
enum AVPixelFormat pickHardware(AVCodecContext*, const enum AVPixelFormat* formats)
{
    for (const enum AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_VAAPI || *p == AV_PIX_FMT_CUDA)
            return *p;
    }
    return AV_PIX_FMT_NONE;
}

quint16 readBe16(const uchar* p)
{
    return quint16(p[0] << 8 | p[1]);
}

quint32 readBe32(const uchar* p)
{
    return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | quint32(p[3]);
}

} // namespace

// FFmpeg HEVC 解码状态（软件解码 + 可选硬解，硬解自动回退）。
struct VideoReceiver::Decoder
{
    AVCodecContext* ctx = nullptr;
    AVBufferRef* hwDevice = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* swFrame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* sws = nullptr;
    enum AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
    bool ready = false;
    bool hardware = false;   // 当前解码器是否为硬解
    bool gotFrame = false;   // 是否成功解出过帧
    int inputCount = 0;      // 已送入的解码单元数（用于硬解失败回退判断）

    void close()
    {
        if (sws)
            sws_freeContext(sws);
        if (packet)
            av_packet_free(&packet);
        if (swFrame)
            av_frame_free(&swFrame);
        if (frame)
            av_frame_free(&frame);
        if (ctx)
            avcodec_free_context(&ctx);
        if (hwDevice)
            av_buffer_unref(&hwDevice);
        sws = nullptr;
        packet = nullptr;
        swFrame = nullptr;
        frame = nullptr;
        ctx = nullptr;
        hwDevice = nullptr;
        ready = false;
    }

    bool init(bool hardware)
    {
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
        if (!codec)
            return false;
        ctx = avcodec_alloc_context3(codec);
        if (!ctx)
            return false;
        this->hardware = hardware;
        gotFrame = false;
        inputCount = 0;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        if (hardware) {
            static const AVHWDeviceType kDevices[] = { AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_CUDA };
            for (AVHWDeviceType type : kDevices) {
                if (av_hwdevice_ctx_create(&hwDevice, type, nullptr, nullptr, 0) >= 0) {
                    ctx->hw_device_ctx = av_buffer_ref(hwDevice);
                    ctx->get_format = pickHardware;
                    ctx->extra_hw_frames = 16;
                    hwPixFmt = type == AV_HWDEVICE_TYPE_VAAPI ? AV_PIX_FMT_VAAPI : AV_PIX_FMT_CUDA;
                    break;
                }
            }
        }
        if (avcodec_open2(ctx, codec, nullptr) < 0)
            return false;
        frame = av_frame_alloc();
        swFrame = av_frame_alloc();
        packet = av_packet_alloc();
        ready = frame && swFrame && packet;
        return ready;
    }

    // 解码一帧裸 HEVC 数据；成功则填充 image。
    bool decode(const QByteArray& nal, QImage& image)
    {
        if (!ready)
            return false;
        ++inputCount;
        av_packet_unref(packet);
        packet->data = reinterpret_cast<uint8_t*>(const_cast<char*>(nal.constData()));
        packet->size = nal.size();
        if (avcodec_send_packet(ctx, packet) < 0)
            return false;

        bool produced = false;
        while (true) {
            const int r = avcodec_receive_frame(ctx, frame);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                break;
            if (r < 0)
                break;

            AVFrame* src = frame;
            if (frame->format == hwPixFmt) {
                av_frame_unref(swFrame);
                if (av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
                    av_frame_unref(frame);
                    continue;
                }
                src = swFrame;
            }

            const int w = src->width;
            const int h = src->height;
            if (w <= 0 || h <= 0) {
                av_frame_unref(frame);
                continue;
            }
            sws = sws_getCachedContext(sws, w, h, static_cast<AVPixelFormat>(src->format), w, h,
                                       AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) {
                av_frame_unref(frame);
                continue;
            }
            QImage out(w, h, QImage::Format_RGB32);
            uint8_t* dst[4] = { out.bits(), nullptr, nullptr, nullptr };
            int dstStride[4] = { int(out.bytesPerLine()), 0, 0, 0 };
            sws_scale(sws, src->data, src->linesize, 0, h, dst, dstStride);
            image = out;
            produced = true;
            gotFrame = true;
            av_frame_unref(src == swFrame ? swFrame : frame);
            if (src != frame)
                av_frame_unref(frame);
            break; // 单帧输入，取第一帧结果即可
        }
        return produced;
    }
};

VideoReceiver::VideoReceiver(QObject* parent)
    : QObject(parent)
    , m_socket(new QUdpSocket(this))
    , m_signalTimer(new QTimer(this))
    , m_decoder(new Decoder)
{
    connect(m_socket, &QUdpSocket::readyRead, this, &VideoReceiver::onReadyRead);
    m_signalTimer->setInterval(500);
    connect(m_signalTimer, &QTimer::timeout, this, &VideoReceiver::tickSignalMonitor);
}

VideoReceiver::~VideoReceiver()
{
    delete m_decoder;
}

bool VideoReceiver::start(const QString& bindAddress, quint16 port)
{
    stop();
    const QHostAddress address = bindAddress.isEmpty() || bindAddress == QLatin1String("0.0.0.0")
        ? QHostAddress::AnyIPv4
        : QHostAddress(bindAddress);
    if (!m_socket->bind(address, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit errorOccurred(tr("图传端口 %1 绑定失败：%2").arg(port).arg(m_socket->errorString()));
        return false;
    }
    m_lastPacketMs = QDateTime::currentMSecsSinceEpoch();
    m_signalTimer->start();
    return true;
}

void VideoReceiver::stop()
{
    m_signalTimer->stop();
    if (m_socket->state() == QAbstractSocket::BoundState)
        m_socket->close();
    m_pending.clear();
    m_active = false;
}

bool VideoReceiver::isListening() const
{
    return m_socket->state() == QAbstractSocket::BoundState;
}

void VideoReceiver::setHardwareDecode(bool on)
{
    m_hardware = on;
}

void VideoReceiver::onReadyRead()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket->pendingDatagramSize()));
        m_socket->readDatagram(datagram.data(), datagram.size());
        if (datagram.size() <= kHeaderSize)
            continue;
        m_lastPacketMs = now;

        const uchar* p = reinterpret_cast<const uchar*>(datagram.constData());
        const quint16 frameId = readBe16(p);
        const quint16 sliceId = readBe16(p + 2);
        const quint32 total = readBe32(p + 4);
        if (total == 0 || total > kMaxFrameBytes)
            continue;

        Frame& frame = m_pending[frameId];
        frame.total = total;
        frame.ts = now;
        if (frame.chunks.contains(sliceId))
            continue; // 重复分片
        const QByteArray chunk = datagram.mid(kHeaderSize);
        frame.received += quint32(chunk.size());
        frame.chunks.insert(sliceId, chunk);

        if (frame.received >= total) {
            QByteArray nal;
            nal.reserve(int(total));
            QList<quint16> ids = frame.chunks.keys();
            std::sort(ids.begin(), ids.end());
            for (quint16 id : ids)
                nal.append(frame.chunks.value(id));
            m_pending.remove(frameId);
            deliverFrame(frameId, nal);
        }
    }

    // 清理长期未组齐的帧，避免内存增长。
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now - it->ts > kFrameTimeoutMs || m_pending.size() > kMaxPendingFrames)
            it = m_pending.erase(it);
        else
            ++it;
    }
}

void VideoReceiver::deliverFrame(quint16, const QByteArray& nal)
{
    if (!m_decoder->ready && !m_decoder->init(m_hardware)) {
        emit errorOccurred(tr("HEVC 解码器初始化失败"));
        m_socket->close();
        return;
    }
    QImage image;
    if (m_decoder->decode(nal, image) && !image.isNull()) {
        emit frameReady(image);
        return;
    }
    // 硬解迟迟解不出帧（驱动不支持等）时自动回退软解，保证图传可用。
    if (m_decoder->hardware && !m_decoder->gotFrame && m_decoder->inputCount > kHwFallbackFrames) {
        m_hardware = false;
        m_decoder->close();
        if (m_decoder->init(false))
            emit errorOccurred(tr("HEVC 硬解不可用，已回退软件解码"));
    }
}

void VideoReceiver::tickSignalMonitor()
{
    const bool active = QDateTime::currentMSecsSinceEpoch() - m_lastPacketMs < kSignalTimeoutMs;
    if (active != m_active) {
        m_active = active;
        emit signalActive(active);
    }
}

} // namespace rm
