#include "Simulator.h"
#include "SimulatorWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("rm_simulator"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("RoboMaster referee/game-engine simulator (MQTT match data)"));
  parser.addHelpOption();
  parser.addVersionOption();

  const QString envHost = qEnvironmentVariable("RM_MQTT_HOST", QStringLiteral("127.0.0.1"));
  const int envPort = qEnvironmentVariableIntValue("RM_MQTT_PORT") > 0
                          ? qEnvironmentVariableIntValue("RM_MQTT_PORT")
                          : 1883;
  const QString envId = qEnvironmentVariable("RM_CLIENT_ROBOT_ID", QStringLiteral("rm_simulator"));

  QCommandLineOption hostOpt({QStringLiteral("H"), QStringLiteral("mqtt-host")},
                             QStringLiteral("MQTT broker host"), QStringLiteral("host"), envHost);
  QCommandLineOption portOpt({QStringLiteral("P"), QStringLiteral("mqtt-port")},
                             QStringLiteral("MQTT broker port"), QStringLiteral("port"),
                             QString::number(envPort));
  QCommandLineOption idOpt({QStringLiteral("i"), QStringLiteral("client-id")},
                           QStringLiteral("MQTT client id"), QStringLiteral("id"), envId);
  parser.addOptions({hostOpt, portOpt, idOpt});
  parser.process(app);

  rm::Simulator::Config cfg;
  cfg.mqttHost = parser.value(hostOpt);
  cfg.mqttPort = static_cast<quint16>(parser.value(portOpt).toUShort());
  cfg.clientId = parser.value(idOpt);

  qInfo() << "[sim] starting: mqtt" << cfg.mqttHost << ":" << cfg.mqttPort
          << "clientId" << cfg.clientId;

  rm::Simulator sim(cfg);
  rm::SimulatorWindow window(&sim);
  window.resize(1180, 680);
  window.show();
  sim.start();
  return app.exec();
}
