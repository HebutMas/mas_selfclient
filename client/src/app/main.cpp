#include <QApplication>

#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RM Custom Client"));
    QApplication::setOrganizationName(QStringLiteral("RM"));

    rm::MainWindow window;
    window.show();
    return app.exec();
}
