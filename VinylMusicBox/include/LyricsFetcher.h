#pragma once
#include <QObject>
#include <QString>
#include <QList>

struct LyricLine {
    double timestamp;  // seconds
    QString text;
};

class LyricsData {
public:
    QString songName;
    QString artistName;
    QString albumName;
    double duration = 0.0;
    QList<LyricLine> lines;
    QString plainLyrics;
    bool hasSyncedLyrics = false;
};

class LyricsFetcher : public QObject {
    Q_OBJECT
public:
    explicit LyricsFetcher(QObject *parent = nullptr);
    Q_INVOKABLE void fetchLyrics(const QString& artist, const QString& title);

signals:
    void lyricsReady(const LyricsData& lyrics);
    void fetchError(const QString& error);

private slots:
    void onNetworkReply();

private:
    void parseLRC(const QString& lrcText, LyricsData& out);
    double parseTimestamp(const QString& ts);

    QNetworkAccessManager* m_networkManager = nullptr;
};
