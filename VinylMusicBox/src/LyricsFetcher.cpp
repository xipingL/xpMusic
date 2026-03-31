#include "LyricsFetcher.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

LyricsFetcher::LyricsFetcher(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

void LyricsFetcher::fetchLyrics(const QString& artist, const QString& title) {
    QString url = QString("https://lrclib.net/api/get?artist_name=%1&track_name=%2")
                      .arg(QUrl::toPercentEncoding(artist))
                      .arg(QUrl::toPercentEncoding(title));

    QNetworkRequest req(QUrl(url));
    req.setHeader(QNetworkRequest::UserAgentHeader, "VinylMusicBox/1.0");

    QNetworkReply* reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, &LyricsFetcher::onNetworkReply);
}

void LyricsFetcher::onNetworkReply() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        emit fetchError("Invalid network reply");
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        QString errorMsg = QString("Network error: %1").arg(reply->errorString());
        emit fetchError(errorMsg);
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        emit fetchError("Invalid JSON response");
        return;
    }

    QJsonObject json = doc.object();

    LyricsData lyrics;
    lyrics.songName = json.value("songName").toString();
    lyrics.artistName = json.value("artistName").toString();
    lyrics.albumName = json.value("albumName").toString();
    lyrics.duration = json.value("duration").toDouble();

    // Check for synced lyrics first, then plain lyrics
    QString syncedLyrics = json.value("syncedLyrics").toString();
    QString plainLyrics = json.value("plainLyrics").toString();

    if (!syncedLyrics.isEmpty()) {
        lyrics.hasSyncedLyrics = true;
        parseLRC(syncedLyrics, lyrics);
        lyrics.plainLyrics = plainLyrics;
    } else if (!plainLyrics.isEmpty()) {
        lyrics.hasSyncedLyrics = false;
        lyrics.plainLyrics = plainLyrics;
    } else {
        emit fetchError("No lyrics found");
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

double LyricsFetcher::parseTimestamp(const QString& ts) {
    // Format: [mm:ss.xx] or [mm:ss]
    QString t = ts.mid(1, ts.length() - 2);  // Remove brackets
    QStringList parts = t.split(":");
    if (parts.size() < 2) return 0.0;

    int minutes = parts[0].toInt();
    double seconds = parts[1].toDouble();
    return minutes * 60 + seconds;
}
