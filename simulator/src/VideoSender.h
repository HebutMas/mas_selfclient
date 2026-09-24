#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QUdpSocket;
struct AVCodecParserContext;
struct AVCodecContext;

namespace rm {

/// 模拟器侧的图传发送端：调用系统 ffmpeg 把本地视频转成 HEVC 裸流，
/// 按官方 UDP 图传协议（8 字节头 + HEVC 载荷，端口 3334）切片发出。
/// 官方协议：FrameID(2) + SliceID(2) + TotalBytes(4) 全大端，其后为 HEVC 数据。
class VideoSender : public QObject {
  Q_OBJECT
public:
  explicit VideoSender(QObject* parent = nullptr);
  ~VideoSender() override;

  bool start(const QString& file, const QString& host, quint16 port);
  void stop();
  bool isRunning() const { return m_proc != nullptr; }
  QString currentFile() const { return m_file; }

signals:
  void started(const QString& file);
  void stopped();
  void errorOccurred(const QString& message);

private slots:
  void onStdout();
  void onProcFinished(int exitCode, int status);

private:
  void feed(const QByteArray& data);
  void flushParser();
  void sendAccessUnit(const char* data, int size);

  QProcess* m_proc = nullptr;
  QUdpSocket* m_socket = nullptr;
  AVCodecParserContext* m_parser = nullptr;
  AVCodecContext* m_parserCtx = nullptr;
  QString m_file;
  QString m_host = QStringLiteral("127.0.0.1");
  quint16 m_port = 3334;
  quint16 m_frameId = 0;
};

} // namespace rm
