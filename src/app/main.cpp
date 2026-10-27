// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QMainWindow>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"Inkwell"_s);
    QApplication::setOrganizationName(u"Inkwell"_s);
    QApplication::setApplicationVersion(QStringLiteral(INKWELL_VERSION));

    QMainWindow window;
    window.setWindowTitle(QApplication::applicationName());
    window.resize(1100, 720);
    window.show();

    return QApplication::exec();
}
