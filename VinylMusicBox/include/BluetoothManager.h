#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

class BluetoothManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QVariantMap currentTrack READ currentTrack NOTIFY currentTrackChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)

public:
    explicit BluetoothManager(QObject *parent = nullptr);
    ~BluetoothManager();

    QString connectionState() const { return m_connectionState; }
    QString deviceName() const { return m_deviceName; }
    QVariantMap currentTrack() const { return m_currentTrack; }
    bool isPlaying() const { return m_isPlaying; }

    Q_INVOKABLE void startPairing();
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void nextTrack();
    Q_INVOKABLE void previousTrack();

signals:
    void connectionStateChanged();
    void deviceNameChanged();
    void currentTrackChanged();
    void isPlayingChanged();

private:
    QString m_connectionState = "Disconnected";
    QString m_deviceName;
    QString m_devicePath;
    QVariantMap m_currentTrack;
    bool m_isPlaying = false;
};