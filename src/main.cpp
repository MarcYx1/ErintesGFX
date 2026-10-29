#include "generator.h"
#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    // erintesgfx --generate <project.touchgfx> [out dir, default: the project's folder]
    if (argc >= 3 && !strcmp(argv[1], "--generate")) {
        std::ifstream in(argv[2]);
        if (!in) {
            std::cerr << "cannot open " << argv[2] << "\n";
            return 1;
        }
        GenResult r = generate(Json::parse(in));
        for (const auto& w : r.warnings) std::cerr << "warning: " << w << "\n";
        std::string out = argc >= 4 ? argv[3] : std::filesystem::path(argv[2]).parent_path().string();
        std::cout << writeFiles(out, r) << " of " << r.files.size() << " files written\n";
        return 0;
    }

    QApplication app(argc, argv);
    app.setOrganizationName("ErintesGFX");
    app.setApplicationName("ErintesGFX");
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(37, 37, 40));
    pal.setColor(QPalette::WindowText, QColor(220, 220, 220));
    pal.setColor(QPalette::Base, QColor(27, 27, 30));
    pal.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    pal.setColor(QPalette::Text, QColor(220, 220, 220));
    pal.setColor(QPalette::Button, QColor(50, 50, 54));
    pal.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    pal.setColor(QPalette::Highlight, QColor(0, 120, 200));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(50, 50, 54));
    pal.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
    pal.setColor(QPalette::Dark, QColor(20, 20, 22));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    app.setPalette(pal);

    MainWindow w;
    w.show();
    const QString file = argc >= 2 ? QString::fromLocal8Bit(argv[1]) : QSettings().value("lastProject").toString();
    if (!file.isEmpty()) w.open(file);
    // Test hook: ERINTES_ACTION="Build target" runs that toolbar action and quits when its build ends.
    if (qEnvironmentVariableIsSet("ERINTES_ACTION")) {
        for (QAction* a : w.actions())
            if (a->text().startsWith(qEnvironmentVariable("ERINTES_ACTION"))) a->trigger();
        auto* t = new QTimer(&w);
        QObject::connect(t, &QTimer::timeout, &w, [&] {
            if (!w.busy()) app.quit();
        });
        t->start(500);
    }
    // Test hook: ERINTES_SHOT=out.png saves a screenshot of the window and quits.
    if (qEnvironmentVariableIsSet("ERINTES_SHOT"))
        QTimer::singleShot(1500, &w, [&] {
            w.grab().save(qEnvironmentVariable("ERINTES_SHOT"));
            app.quit();
        });
    return app.exec();
}
