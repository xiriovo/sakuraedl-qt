#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "app_controller.h"
#include "qualcomm_controller.h"
#include "mediatek_controller.h"
#include "spreadtrum_controller.h"
#include "fastboot_controller.h"
#include "core/logger.h"
#include "core/language_manager.h"
#include "core/performance_config.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("SakuraEDL");
    app.setApplicationName("SakuraEDL");
    app.setApplicationVersion("3.0.0");
    QQuickStyle::setStyle("Basic");

    // Initialize core systems
    sakura::LanguageManager::instance().initialize();
    sakura::PerformanceConfig::instance().autoDetect();

    // Controllers
    sakura::AppController appController;
    sakura::QualcommController qualcommController;
    sakura::MediatekController mediatekController;
    sakura::SpreadtrumController spreadtrumController;
    sakura::FastbootController fastbootController;

    // QML Engine
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appController", &appController);
    engine.rootContext()->setContextProperty("qualcommController", &qualcommController);
    engine.rootContext()->setContextProperty("mediatekController", &mediatekController);
    engine.rootContext()->setContextProperty("spreadtrumController", &spreadtrumController);
    engine.rootContext()->setContextProperty("fastbootController", &fastbootController);
    engine.rootContext()->setContextProperty("langManager", &sakura::LanguageManager::instance());
    engine.rootContext()->setContextProperty("perfConfig", &sakura::PerformanceConfig::instance());

    using namespace Qt::StringLiterals;
    const QUrl url(u"qrc:/qt/qml/SakuraEDL/Main.qml"_s);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, [](const QUrl &u) {
            qCritical() << "QML LOAD FAILED:" << u;
            QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
        &app, [](const QList<QQmlError> &warnings) {
            for(const auto& w : warnings)
                qWarning() << "QML WARNING:" << w.toString();
        });
    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "QML engine has no root objects! Load failed.";
        return -1;
    }

    return app.exec();
}
