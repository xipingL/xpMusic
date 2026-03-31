#include "LyricsCache.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QCryptographicHash>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <algorithm>

LyricsCache::LyricsCache(QObject *parent) : QObject(parent) {
    initDatabase();
}

LyricsCache::~LyricsCache() {
}

void LyricsCache::initDatabase() {
    // Use a unique connection name for this instance
    QString connectionName = "lyrics_cache_connection";
    {
        // If a connection with this name already exists, remove it first
        QSqlDatabase db = QSqlDatabase::database(connectionName, false);
        if (db.isValid()) {
            db.close();
        }
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);

    // Store the database path
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(cacheDir);
    m_databasePath = cacheDir + "/lyrics_cache.db";
    db.setDatabaseName(m_databasePath);

    if (!db.open()) {
        qWarning() << "Failed to open lyrics cache database:" << db.lastError().text();
        return;
    }

    QSqlQuery query(db);
    query.exec("CREATE TABLE IF NOT EXISTS lyrics ("
               "  hash TEXT PRIMARY KEY,"
               "  artist TEXT,"
               "  title TEXT,"
               "  plain_lyrics TEXT,"
               "  synced_lyrics TEXT,"
               "  duration REAL,"
               "  source TEXT,"
               "  cached_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
               ")");

    // Create index on artist/title for faster lookups
    query.exec("CREATE INDEX IF NOT EXISTS idx_artist_title ON lyrics(artist, title)");
}

QString LyricsCache::generateHash(const QString& artist, const QString& title) {
    QString key = artist.toLower().trimmed() + title.toLower().trimmed();
    return QString(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex());
}

bool LyricsCache::hasLyrics(const QString& artist, const QString& title) {
    QString hash = generateHash(artist, title);

    QSqlDatabase db = QSqlDatabase::database("lyrics_cache_connection");
    QSqlQuery query(db);
    query.prepare("SELECT hash FROM lyrics WHERE hash = :hash");
    query.bindValue(":hash", hash);

    if (!query.exec()) {
        qWarning() << "hasLyrics query failed:" << query.lastError().text();
        return false;
    }

    return query.next();
}

LyricsData LyricsCache::getLyrics(const QString& artist, const QString& title) {
    LyricsData result;
    QString hash = generateHash(artist, title);

    QSqlDatabase db = QSqlDatabase::database("lyrics_cache_connection");
    QSqlQuery query(db);
    query.prepare("SELECT artist, title, plain_lyrics, synced_lyrics, duration, source FROM lyrics WHERE hash = :hash");
    query.bindValue(":hash", hash);

    if (!query.exec()) {
        qWarning() << "getLyrics query failed:" << query.lastError().text();
        return result;
    }

    if (query.next()) {
        result.artistName = query.value(0).toString();
        result.songName = query.value(1).toString();
        QString plainLyrics = query.value(2).toString();
        QString syncedLyrics = query.value(3).toString();
        double duration = query.value(4).toDouble();
        // source is stored but not used in LyricsData currently

        result = deserializeSyncedLyrics(syncedLyrics, plainLyrics, duration);
    }

    return result;
}

void LyricsCache::saveLyrics(const LyricsData& lyrics) {
    if (lyrics.artistName.isEmpty() || lyrics.songName.isEmpty()) {
        qWarning() << "Cannot save lyrics: artist or title is empty";
        return;
    }

    QString hash = generateHash(lyrics.artistName, lyrics.songName);
    QString syncedText = serializeSyncedLyrics(lyrics);

    QSqlDatabase db = QSqlDatabase::database("lyrics_cache_connection");
    QSqlQuery query(db);
    query.prepare("INSERT OR REPLACE INTO lyrics (hash, artist, title, plain_lyrics, synced_lyrics, duration, source) "
                  "VALUES (:hash, :artist, :title, :plain_lyrics, :synced_lyrics, :duration, :source)");
    query.bindValue(":hash", hash);
    query.bindValue(":artist", lyrics.artistName);
    query.bindValue(":title", lyrics.songName);
    query.bindValue(":plain_lyrics", lyrics.plainLyrics);
    query.bindValue(":synced_lyrics", syncedText);
    query.bindValue(":duration", lyrics.duration);
    query.bindValue(":source", ""); // Source not stored in LyricsData

    if (!query.exec()) {
        qWarning() << "saveLyrics query failed:" << query.lastError().text();
    }
}

QString LyricsCache::serializeSyncedLyrics(const LyricsData& lyrics) {
    QString result;
    for (const LyricLine& line : lyrics.lines) {
        // Format: [mm:ss.xx]text
        int totalSeconds = static_cast<int>(line.timestamp);
        int minutes = totalSeconds / 60;
        int seconds = totalSeconds % 60;
        int hundredths = static_cast<int>((line.timestamp - totalSeconds) * 100);

        result += QString("[%1:%2.%3]%4\n")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'))
            .arg(hundredths, 2, 10, QChar('0'))
            .arg(line.text);
    }
    return result;
}

LyricsData LyricsCache::deserializeSyncedLyrics(const QString& syncedText, const QString& plainText, double duration) {
    LyricsData result;
    result.duration = duration;
    result.plainLyrics = plainText;
    result.hasSyncedLyrics = false;

    if (syncedText.isEmpty()) {
        return result;
    }

    QStringList lines = syncedText.split('\n', Qt::SkipEmptyParts);
    QRegularExpression timestampRegex(R"(\[(\d{2}):(\d{2})\.(\d{2})\](.*))");

    for (const QString& line : lines) {
        QRegularExpressionMatch match = timestampRegex.match(line);
        if (match.hasMatch()) {
            int minutes = match.captured(1).toInt();
            int seconds = match.captured(2).toInt();
            int hundredths = match.captured(3).toInt();
            QString text = match.captured(4);

            LyricLine lyricLine;
            lyricLine.timestamp = minutes * 60 + seconds + hundredths / 100.0;
            lyricLine.text = text;

            result.lines.append(lyricLine);
            result.hasSyncedLyrics = true;
        }
    }

    // Sort by timestamp
    std::sort(result.lines.begin(), result.lines.end(),
              [](const LyricLine& a, const LyricLine& b) {
                  return a.timestamp < b.timestamp;
              });

    return result;
}