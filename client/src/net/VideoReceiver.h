#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>

class QUdpSocket;
class QTimer;

namespace rm {

// 官方图传接收（RoboMaster 2026 通信协议）：UDP 端口 3334，编码 HEVC(H.265)。
// 每个 UDP 包头部 8 字节：FrameID(2) + SliceID(2) + TotalBytes(4)，均为大端序，
// 之后为裸码流分片。按 FrameID/SliceID 重组出完整帧后交给 FFmpeg 解码。
// 设计为在工作线程中使用：start() 通过队列连接调用，解码后的 QImage 经
// frameReady 信号跨线程投递到 UI 线程。
class VideoReceiver : public QObject
{
    Q_OBJECT
public:
    explicit VideoReceiver(QObject* parent = nullptr);
    ~VideoReceiver() override;

    // 绑定地址/端口并开始接收（端口官方固定 3334）。
    bool start(const QString& bindAddress, quint16 port);
    void stop();
    bool isListening() const;

    // 是否启用硬件解码（VAAPI），失败自动回退软件解码。
    void setHardwareDecode(bool on);

signals:
    void frameReady(const QImage& image);
    void signalActive(bool active); // 1s 内有包视为有信号
    void errorOccurred(const QString& message);

private slots:
    void onReadyRead();
    void tickSignalMonitor();

private:
    struct Frame {
        quint32 total = 0;
        quint32 received = 0;
        qint64 ts = 0;
        QHash<quint16, QByteArray> chunks;
    };

    void deliverFrame(quint16 frameId, const QByteArray& nal);

    QUdpSocket* m_socket = nullptr;
    QTimer* m_signalTimer = nullptr;
    QHash<quint16, Frame> m_pending;
    struct Decoder;
    Decoder* m_decoder = nullptr;
    qint64 m_lastPacketMs = 0;
    bool m_active = false;
    bool m_hardware = true;
};

} // namespace rm
