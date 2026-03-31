#include "LyricsFetcher.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QTimer>

LyricsFetcher::LyricsFetcher(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_currentReply(nullptr)
    , m_timeoutTimer(new QTimer(this))
{
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(15000);  // 15 second timeout
    connect(m_timeoutTimer, &QTimer::timeout, this, &LyricsFetcher::onTimeout);
}

void LyricsFetcher::fetchLyrics(const QString& artist, const QString& title) {
    // Cancel any in-flight request
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_timeoutTimer->stop();

    // Build URL with proper encoding using QUrlQuery
    QUrl url(QStringLiteral("https://lrclib.net/api/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("artist_name"), artist);
    query.addQueryItem(QStringLiteral("track_name"), title);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "VinylMusicBox/1.0");
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_currentReply = m_networkManager->get(req);
    connect(m_currentReply, &QNetworkReply::finished, this, &LyricsFetcher::onNetworkReply);
    m_timeoutTimer->start();
}

void LyricsFetcher::onTimeout() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    emit fetchError(QStringLiteral("Request timeout"));
}

void LyricsFetcher::onNetworkReply() {
    m_timeoutTimer->stop();

    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit fetchError(QStringLiteral("Invalid network reply"));
        return;
    }

    // Check if reply was aborted by timeout
    if (reply != m_currentReply) {
        reply->deleteLater();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = QStringLiteral("Network error: %1").arg(reply->errorString());
        emit fetchError(errorMsg);
        reply->deleteLater();
        m_currentReply = nullptr;
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    m_currentReply = nullptr;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        emit fetchError(QStringLiteral("Invalid JSON response"));
        return;
    }

    QJsonObject json = doc.object();

    LyricsData lyrics;

    // Validate required fields exist
    if (json.contains(QStringLiteral("songName"))) {
        lyrics.songName = json.value(QStringLiteral("songName")).toString();
    }
    if (json.contains(QStringLiteral("artistName"))) {
        lyrics.artistName = json.value(QStringLiteral("artistName")).toString();
    }
    if (json.contains(QStringLiteral("albumName"))) {
        lyrics.albumName = json.value(QStringLiteral("albumName")).toString();
    }
    if (json.contains(QStringLiteral("duration"))) {
        lyrics.duration = json.value(QStringLiteral("duration")).toDouble();
    }

    // Check for synced lyrics first, then plain lyrics
    QString syncedLyrics;
    QString plainLyrics;

    if (json.contains(QStringLiteral("syncedLyrics"))) {
        syncedLyrics = json.value(QStringLiteral("syncedLyrics")).toString();
    }
    if (json.contains(QStringLiteral("plainLyrics"))) {
        plainLyrics = json.value(QStringLiteral("plainLyrics")).toString();
    }

    if (!syncedLyrics.isEmpty()) {
        lyrics.hasSyncedLyrics = true;
        parseLRC(syncedLyrics, lyrics);
        lyrics.plainLyrics = plainLyrics;
    } else if (!plainLyrics.isEmpty()) {
        lyrics.hasSyncedLyrics = false;
        lyrics.plainLyrics = plainLyrics;
    } else {
        emit fetchError(QStringLiteral("No lyrics found"));
        return;
    }

    emit lyricsReady(lyrics);
}

void LyricsFetcher::parseLRC(const QString& lrcText, LyricsData& out) {
    QStringList lines = lrcText.split('\n', Qt::SkipEmptyParts);

    for (const QString& line : lines) {
        // Match timestamp pattern [mm:ss.xx] or [mm:ss]
        QRegularExpression re(R"(\[(\d{1,2}):(\d{2})(?:\.(\d{2,3}))?\])");
        QRegularExpressionMatch match = re.match(line);

        if (match.hasMatch()) {
            int minutes = match.captured(1).toInt();
            int seconds = match.captured(2).toInt();
            int ms = 0;

            if (match.lastCapturedIndex() >= 3 && !match.captured(3).isEmpty()) {
                QString msStr = match.captured(3);
                // Normalize to milliseconds (2 or 3 digits)
                if (msStr.length() == 2) {
                    ms = msStr.toInt() * 10;
                } else {
                    ms = msStr.toInt();
                }
            }

            double timestamp = minutes * 60.0 + seconds + ms / 1000.0;

            // Extract text after timestamp
            int textStart = match.capturedEnd();
            QString text = line.mid(textStart).trimmed();

            if (!text.isEmpty()) {
                LyricLine lyricLine;
                lyricLine.timestamp = timestamp;
                lyricLine.text = text;
                out.lines.append(lyricLine);
            }
        }
    }
}
