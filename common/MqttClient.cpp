#include "MqttClient.h"

#include <mqtt/async_client.h>
#include <mqtt/callback.h>
#include <mqtt/connect_options.h>
#include <mqtt/message.h>

#include <QMetaObject>

namespace rm {

namespace {

// Paho 的错误串转成带原因的中文提示，方便现场定位（末段保留原始错误）。
QString friendlyMqttError(const QString& raw)
{
  struct Hint {
    const char* key;
    const char* zh;
  };
  static const Hint hints[] = {
      {"TCP/TLS connect failure", "无法建立 TCP 连接（检查服务器地址/端口/网线）"},
      {"connect timeout", "连接超时（服务器未响应）"},
      {"not authorized", "认证失败（用户名/密码或权限不足）"},
      {"bad user name or password", "用户名或密码错误"},
      {"server unavailable", "服务器不可用"},
      {"identifier rejected", "clientID 被拒绝（可能与其他客户端冲突）"},
      {"Invalid protocol version", "协议版本不匹配"},
      {"network is unreachable", "网络不可达（检查本机 IP/网段）"},
  };
  for (const auto& h : hints) {
    if (raw.contains(QLatin1String(h.key), Qt::CaseInsensitive))
      return QStringLiteral("%1：%2").arg(QString::fromUtf8(h.zh), raw);
  }
  return raw;
}

/// Forwards Paho callbacks into the Qt thread. The object must outlive the client.
class Bridge : public virtual mqtt::callback {
public:
  explicit Bridge(MqttClient* owner) : m_owner(owner) {}

  void connected(const std::string& /*cause*/) override {
    QMetaObject::invokeMethod(m_owner, "onPahoConnected", Qt::QueuedConnection);
  }

  void connection_lost(const std::string& cause) override {
    QMetaObject::invokeMethod(m_owner, "onPahoDisconnected", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(cause)));
  }

  void message_arrived(mqtt::const_message_ptr msg) override {
    const std::string& t = msg->get_topic();
    const std::string& p = msg->get_payload();
    QMetaObject::invokeMethod(
        m_owner, "onPahoMessage", Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(t)),
        Q_ARG(QByteArray, QByteArray(p.data(), static_cast<int>(p.size()))));
  }

  void delivery_complete(mqtt::delivery_token_ptr /*tok*/) override {}

private:
  MqttClient* m_owner;
};

} // namespace

struct MqttClient::Impl {
  std::unique_ptr<mqtt::async_client> client;
  std::unique_ptr<Bridge> bridge;
  bool connected = false;
};

MqttClient::MqttClient(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {}

MqttClient::~MqttClient() {
  if (d->client && d->connected) {
    try {
      d->client->disconnect()->wait();
    } catch (...) {
      // shutting down; ignore
    }
  }
}

void MqttClient::connectToBroker(const QString& host, quint16 port, const QString& clientId) {
  const std::string uri = "tcp://" + host.toStdString() + ":" + std::to_string(port);
  d->client = std::make_unique<mqtt::async_client>(uri, clientId.toStdString());
  d->bridge = std::make_unique<Bridge>(this);
  d->client->set_callback(*d->bridge);

  mqtt::connect_options opts;
  opts.set_clean_session(true);
  opts.set_automatic_reconnect(true);
  opts.set_connect_timeout(5);

  try {
    d->client->connect(opts)->wait();
  } catch (const std::exception& e) {
    emit errorOccurred(friendlyMqttError(QString::fromUtf8(e.what())));
  }
}

void MqttClient::disconnectFromBroker() {
  if (!d->client)
    return;
  try {
    if (d->client->is_connected())
      d->client->disconnect()->wait();
  } catch (const mqtt::exception&) {
    // already gone; ignore
  }
  d->connected = false;
}

void MqttClient::subscribe(const QString& topic) {
  if (!d->client) return;
  try {
    d->client->subscribe(topic.toStdString(), 1);
  } catch (const mqtt::exception& e) {
    emit errorOccurred(QString("MQTT subscribe failed (%1): %2").arg(topic, e.what()));
  }
}

void MqttClient::publish(const QString& topic, const QByteArray& payload) {
  if (!isConnected()) return;
  const std::string body(payload.constData(), static_cast<size_t>(payload.size()));
  try {
    d->client->publish(topic.toStdString(), body, 1, false);
  } catch (const mqtt::exception& e) {
    emit errorOccurred(QString("MQTT publish failed (%1): %2").arg(topic, e.what()));
  }
}

bool MqttClient::isConnected() const {
  return d->client && d->client->is_connected();
}

void MqttClient::onPahoConnected() {
  d->connected = true;
  emit connected();
}

void MqttClient::onPahoDisconnected(const QString& cause) {
  d->connected = false;
  emit disconnected(cause);
}

void MqttClient::onPahoMessage(const QString& topic, const QByteArray& payload) {
  emit messageReceived(topic, payload);
}

} // namespace rm
