#pragma once
#include <QObject>
#include <QString>
#include "LyricsFetcher.h"

class LyricsCache : public QObject {
    Q_OBJECT
public:
    explicit LyricsCache(QObject *parent = nullptr);
    ~LyricsCache();

    Q_INVOKABLE bool hasLyrics(const QString& artist, const QString& title);
    Q_INVOKABLE LyricsData getLyrics(const QString& artist, const QString& title);
    Q_INVOKABLE void saveLyrics(const LyricsData& lyrics);

private:
    QString generateHash(const QString& artist, const QString& title);
    void initDatabase();
    QString serializeSyncedLyrics(const LyricsData& lyrics);
    LyricsData deserializeSyncedLyrics(const QString& syncedText, const QString& plainText, double duration);

private:
    QString m_databasePath;
};