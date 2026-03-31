#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QDebug>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("VinylMusicBox");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VinylMusicBox");

    QQuickStyle::setStyle("Fusion");

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                qCritical() << "Failed to load QML:" << url;
                QCoreApplication::exit(1);
            }
        },
        Qt::QueuedConnection);
    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "No root objects after loading QML";
        return 1;
    }

    return app.exec();
}
