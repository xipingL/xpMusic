#include "BluetoothManager.h"
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusArgument>
#include <QDBusContext>

// BlueZ DBus service constants
static const QString BLUEZ_SERVICE = QStringLiteral("org.bluez");
static const QString BLUEZ_OBJECT_MANAGER = QStringLiteral("/");
static const QString PROFILE_MANAGER = QStringLiteral("org.bluez.Profile1");
static const QString PROFILE_PATH = QStringLiteral("/org/bluez/audio/profile/a2dp_sink");

// A2DP Sink UUID for audio reception
static const QString A2DP_SINK_UUID = QStringLiteral("0000110B-0000-1000-8000-00805F9B34FB");

BluetoothManager::BluetoothManager(QObject *parent) : QObject(parent) {
    qDebug() << "BluetoothManager initialized";

    // Register A2DP sink profile with BlueZ
    registerA2dpProfile();

    // Connect to BlueZ D-Bus for device and player signals
    QDBusConnection systemBus = QDBusConnection::systemBus();
    if (systemBus.isConnected()) {
        // Subscribe to BlueZ D-Bus signals for device discovery
        systemBus.connect(BLUEZ_SERVICE, BLUEZ_OBJECT_MANAGER,
                         "org.freedesktop.DBus.ObjectManager",
                         "InterfacesAdded",
                         this, SLOT(onInterfacesAdded(QDBusMessage)));

        systemBus.connect(BLUEZ_SERVICE, BLUEZ_OBJECT_MANAGER,
                         "org.freedesktop.DBus.ObjectManager",
                         "InterfacesRemoved",
                         this, SLOT(onInterfacesRemoved(QDBusMessage)));

        qDebug() << "Connected to BlueZ D-Bus";
    } else {
        qWarning() << "Failed to connect to system D-Bus";
    }
}

BluetoothManager::~BluetoothManager() {
    qDebug() << "BluetoothManager destroyed";
}

void BluetoothManager::registerA2dpProfile() {
    QDBusConnection systemBus = QDBusConnection::systemBus();
    if (!systemBus.isConnected()) {
        qWarning() << "Cannot register A2DP profile: not connected to system D-Bus";
        return;
    }

    // Create profile configuration
    QVariantMap options;
    options[QStringLiteral("Name")] = QStringLiteral("VinylMusicBox A2DP Sink");
    options[QStringLiteral("UUID")] = A2DP_SINK_UUID;
    options[QStringLiteral("Role")] = QStringLiteral("sink");

    // Service Record (SDP) for A2DP Sink
    // This is an abbreviated SDP record - a full implementation would include
    // proper service record with supported audio codecs (SBC, AAC, etc.)
    options[QStringLiteral("ServiceRecord")] = QVariant::fromValue(
        QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>"
            "<record>"
            "  <attribute id=\"0x0001\">"
            "    <sequence>"
            "      <uuid value=\"0x110B\"/>"
            "    </sequence>"
            "  </attribute>"
            "  <attribute id=\"0x0003\">"
            "    <uuid value=\"0x110B\"/>"
            "  </attribute>"
            "  <attribute id=\"0x0006\">"
            "    <sequence>"
            "      <text value=\"audio\"/>"
            "    </sequence>"
            "  </attribute>"
            "</record>"
        )
    );

    // Try to register profile with BlueZ ProfileManager
    QDBusMessage call = QDBusMessage::createMethodCall(
        BLUEZ_SERVICE,
        QStringLiteral("/org/bluez"),
        PROFILE_MANAGER,
        QStringLiteral("RegisterProfile")
    );
    call << QVariant::fromValue(PROFILE_PATH);
    call << QVariant::fromValue(A2DP_SINK_UUID);
    call << QVariant::fromValue(options);

    QDBusPendingCall pendingCall = systemBus.asyncCall(call);
    if (!pendingCall.isValid()) {
        qWarning() << "Failed to send RegisterProfile D-Bus message";
        return;
    }
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [](QDBusPendingCallWatcher *watcher) {
                QDBusPendingReply<> reply = *watcher;
                if (reply.isError()) {
                    qWarning() << "Failed to register A2DP profile:" << reply.error().message();
                } else {
                    qDebug() << "A2DP Sink profile registered with BlueZ";
                }
                watcher->deleteLater();
            });
}

void BluetoothManager::startPairing() {
    qDebug() << "Starting Bluetooth pairing mode";
    m_connectionState = QStringLiteral("Scanning");
    emit connectionStateChanged();

    QDBusConnection systemBus = QDBusConnection::systemBus();
    if (!systemBus.isConnected()) {
        qWarning() << "Cannot start pairing: not connected to system D-Bus";
        return;
    }

    // BlueZ Agent Manager API - register and activate agent
    QDBusMessage call = QDBusMessage::createMethodCall(
        BLUEZ_SERVICE,
        QStringLiteral("/org/bluez"),
        QStringLiteral("org.bluez.AgentManager1"),
        QStringLiteral("RegisterAgent")
    );
    call << QVariant::fromValue(QStringLiteral("/org/bluez/agent"));
    call << QVariant::fromValue(QStringLiteral("NoInputNoOutput"));
    systemBus.asyncCall(call);

    // Make this the default agent
    QDBusMessage defaultCall = QDBusMessage::createMethodCall(
        BLUEZ_SERVICE,
        QStringLiteral("/org/bluez"),
        QStringLiteral("org.bluez.AgentManager1"),
        QStringLiteral("RequestDefaultAgent")
    );
    defaultCall << QVariant::fromValue(QStringLiteral("/org/bluez/agent"));
    systemBus.asyncCall(defaultCall);

    // Start discovery on default adapter
    QDBusMessage discoveryCall = QDBusMessage::createMethodCall(
        BLUEZ_SERVICE,
        QStringLiteral("/org/bluez"),
        QStringLiteral("org.bluez.Adapter1"),
        QStringLiteral("StartDiscovery")
    );
    systemBus.asyncCall(discoveryCall);
}

void BluetoothManager::disconnect() {
    qDebug() << "Disconnecting Bluetooth device";
    m_connectionState = QStringLiteral("Disconnected");
    m_deviceName.clear();
    m_devicePath.clear();
    emit connectionStateChanged();
    emit deviceNameChanged();
}

void BluetoothManager::playPause() {
    qDebug() << "Play/Pause toggled, currently playing:" << m_isPlaying;
    m_isPlaying = !m_isPlaying;
    emit isPlayingChanged();

    // Send play/pause command to BlueZ MediaControl at correct device path
    if (m_devicePath.isEmpty()) {
        qWarning() << "No device path set, cannot send MediaControl command";
        return;
    }

    if (!QDBusConnection::systemBus().isConnected()) {
        qWarning() << "System D-Bus not connected";
        return;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        BLUEZ_SERVICE,
        m_devicePath,
        QStringLiteral("org.bluez.MediaControl1"),
        m_isPlaying ? QStringLiteral("Play") : QStringLiteral("Pause")
    );
    QDBusConnection::systemBus().asyncCall(call);
}

void BluetoothManager::nextTrack() {
    qDebug() << "Next track";

    if (m_devicePath.isEmpty()) return;
    if (!QDBusConnection::systemBus().isConnected()) return;

    QDBusMessage call = QDBusMessage::createMethodCall(
        BLUEZ_SERVICE,
        m_devicePath,
        QStringLiteral("org.bluez.MediaControl1"),
        QStringLiteral("Next")
    );
    QDBusConnection::systemBus().asyncCall(call);
}

void BluetoothManager::previousTrack() {
    qDebug() << "Previous track";

    if (m_devicePath.isEmpty()) return;
    if (!QDBusConnection::systemBus().isConnected()) return;

    QDBusMessage call = QDBusMessage::createMethodCall(
        BLUEZ_SERVICE,
        m_devicePath,
        QStringLiteral("org.bluez.MediaControl1"),
        QStringLiteral("Previous")
    );
    QDBusConnection::systemBus().asyncCall(call);
}

void BluetoothManager::onInterfacesAdded(const QDBusMessage &msg) {
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2) return;

    // Extract object path
    QString path = args.at(0).value<QDBusObjectPath>().path();

    // args[1] is a{sv} - map of interface name to array of property dicts
    // BlueZ ObjectManager.InterfacesAdded: o a{sv}
    QVariant arg1 = args.at(1);
    QVariantMap interfacesMap = arg1.value<QVariantMap>();

    // Check for Device1 (Bluetooth device) - must handle a{s*} structure
    if (interfacesMap.contains(QStringLiteral("org.bluez.Device1"))) {
        QVariant deviceVar = interfacesMap.value(QStringLiteral("org.bluez.Device1"));
        QVariantMap deviceProps;

        // Handle both direct QVariantMap and nested QDBusArgument
        if (deviceVar.canConvert<QDBusArgument>()) {
            QDBusArgument dbusArg = deviceVar.value<QDBusArgument>();
            dbusArg.beginArray();
            while (!dbusArg.atEnd()) {
                QString key;
                QVariant value;
                dbusArg >> key >> value;
                deviceProps[key] = value;
            }
            dbusArg.endArray();
        } else if (deviceVar.canConvert<QVariantMap>()) {
            deviceProps = deviceVar.value<QVariantMap>();
        }

        bool connected = deviceProps.value(QStringLiteral("Connected")).toBool();

        if (connected) {
            // Use Alias (preferred) or fallback to Name
            QString name = deviceProps.value(QStringLiteral("Alias")).toString();
            if (name.isEmpty()) {
                name = deviceProps.value(QStringLiteral("Name")).toString();
            }

            if (!name.isEmpty()) {
                qDebug() << "Device connected:" << name << "at path:" << path;
                m_deviceName = name;
                m_devicePath = path;
                m_connectionState = QStringLiteral("Connected");
                emit deviceNameChanged();
                emit connectionStateChanged();

                // Subscribe to PropertiesChanged on this device path for real-time updates
                QDBusConnection systemBus = QDBusConnection::systemBus();
                systemBus.connect(BLUEZ_SERVICE, path,
                                 "org.freedesktop.DBus.Properties",
                                 "PropertiesChanged",
                                 this, SLOT(onPropertiesChanged(QDBusMessage)));
            }
        }
    }

    // Check for MediaPlayer1 (AVRCP player)
    if (interfacesMap.contains(QStringLiteral("org.bluez.MediaPlayer1"))) {
        QVariant playerVar = interfacesMap.value(QStringLiteral("org.bluez.MediaPlayer1"));
        QVariantMap playerProps;

        if (playerVar.canConvert<QDBusArgument>()) {
            QDBusArgument dbusArg = playerVar.value<QDBusArgument>();
            dbusArg.beginArray();
            while (!dbusArg.atEnd()) {
                QString key;
                QVariant value;
                dbusArg >> key >> value;
                playerProps[key] = value;
            }
            dbusArg.endArray();
        } else if (playerVar.canConvert<QVariantMap>()) {
            playerProps = playerVar.value<QVariantMap>();
        }

        qDebug() << "MediaPlayer discovered at:" << path;
        parsePlayerProperties(playerProps);
    }
}

void BluetoothManager::onInterfacesRemoved(const QDBusMessage &msg) {
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2) return;

    QString path = args.at(0).value<QDBusObjectPath>().path();
    QStringList interfaces = args.at(1).value<QStringList>();

    if (interfaces.contains(QStringLiteral("org.bluez.Device1"))) {
        qDebug() << "Device disconnected:" << path;
        disconnect();
    }
}

void BluetoothManager::parsePlayerProperties(const QVariantMap &props) {
    // Parse AVRCP metadata from MediaPlayer1 properties

    if (props.contains(QStringLiteral("Track"))) {
        QVariant trackVar = props.value(QStringLiteral("Track"));
        QVariantMap track;

        if (trackVar.canConvert<QDBusArgument>()) {
            QDBusArgument dbusArg = trackVar.value<QDBusArgument>();
            dbusArg.beginArray();
            while (!dbusArg.atEnd()) {
                QString key;
                QVariant value;
                dbusArg >> key >> value;
                track[key] = value;
            }
            dbusArg.endArray();
        } else if (trackVar.canConvert<QVariantMap>()) {
            track = trackVar.value<QVariantMap>();
        }

        m_currentTrack.clear();

        if (track.contains(QStringLiteral("Title"))) {
            m_currentTrack[QStringLiteral("title")] = track.value(QStringLiteral("Title"));
        }
        if (track.contains(QStringLiteral("Artist"))) {
            m_currentTrack[QStringLiteral("artist")] = track.value(QStringLiteral("Artist"));
        }
        if (track.contains(QStringLiteral("Album"))) {
            m_currentTrack[QStringLiteral("album")] = track.value(QStringLiteral("Album"));
        }
        if (track.contains(QStringLiteral("Genre"))) {
            m_currentTrack[QStringLiteral("genre")] = track.value(QStringLiteral("Genre"));
        }
        if (track.contains(QStringLiteral("NumberOfTracks"))) {
            m_currentTrack[QStringLiteral("trackNumber")] = track.value(QStringLiteral("NumberOfTracks"));
        }
        if (track.contains(QStringLiteral("Duration"))) {
            m_currentTrack[QStringLiteral("duration")] = track.value(QStringLiteral("Duration"));
        }

        qDebug() << "Track metadata updated:" << m_currentTrack;
        emit currentTrackChanged();
    }

    // Check playing status
    if (props.contains(QStringLiteral("Status"))) {
        QString status = props.value(QStringLiteral("Status")).toString();
        bool wasPlaying = m_isPlaying;
        m_isPlaying = (status == QStringLiteral("playing"));

        if (wasPlaying != m_isPlaying) {
            emit isPlayingChanged();
        }
    }
}

// Slot to handle property changes from D-Bus PropertiesChanged signals
void BluetoothManager::onPropertiesChanged(const QDBusMessage &msg) {
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 3) return;

    QString interface = args.at(0).toString();
    QVariantMap changedProps;

    QVariant arg1 = args.at(1);
    if (arg1.canConvert<QDBusArgument>()) {
        QDBusArgument dbusArg = arg1.value<QDBusArgument>();
        dbusArg.beginArray();
        while (!dbusArg.atEnd()) {
            QString key;
            QVariant value;
            dbusArg >> key >> value;
            changedProps[key] = value;
        }
        dbusArg.endArray();
    } else if (arg1.canConvert<QVariantMap>()) {
        changedProps = arg1.value<QVariantMap>();
    }

    if (interface == QStringLiteral("org.bluez.MediaPlayer1")) {
        parsePlayerProperties(changedProps);
    } else if (interface == QStringLiteral("org.bluez.Device1")) {
        // Handle device properties (e.g., Connected changed to false)
        if (changedProps.contains(QStringLiteral("Connected"))) {
            bool connected = changedProps.value(QStringLiteral("Connected")).toBool();
            if (!connected) {
                qDebug() << "Device disconnected via PropertiesChanged";
                disconnect();
            }
        }
    } else if (interface == QStringLiteral("org.bluez.MediaTransport1")) {
        // Handle transport properties (codec, volume, etc.)
        if (changedProps.contains(QStringLiteral("Volume"))) {
            qDebug() << "Volume changed:" << changedProps.value(QStringLiteral("Volume"));
        }
    }
}
