#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>

namespace rm {

/// Thin Qt wrapper around Eclipse Paho MQTT C++ (async_client).
/// Paho's callback thread is bridged to the Qt thread via queued invocations,
/// so signals are always delivered on the owning thread.
/// Shared by the client (subscribes downlink / publishes uplink) and the
/// simulator (publishes downlink / subscribes uplink).
class MqttClient : public QObject {
  Q_OBJECT
public:
  explicit MqttClient(QObject* parent = nullptr);
  ~MqttClient() override;

  void connectToBroker(const QString& host, quint16 port, const QString& clientId);
  void disconnectFromBroker();
  void subscribe(const QString& topic);
  void publish(const QString& topic, const QByteArray& payload);
  bool isConnected() const;

signals:
  void connected();
  void disconnected(const QString& cause);
  void errorOccurred(const QString& message);
  void messageReceived(const QString& topic, const QByteArray& payload);

private slots:
  void onPahoConnected();
  void onPahoDisconnected(const QString& cause);
  void onPahoMessage(const QString& topic, const QByteArray& payload);

private:
  struct Impl;
  std::unique_ptr<Impl> d;
};

} // namespace rm
