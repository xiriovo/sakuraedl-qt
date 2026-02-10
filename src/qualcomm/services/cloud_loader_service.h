#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace sakura {

// ─── Loader match result ─────────────────────────────────────────────
struct LoaderResult {
    QString id;           // Unique loader identifier
    QString filename;     // e.g. "prog_firehose_ddr.elf"
    QString vendor;       // e.g. "Qualcomm", "Xiaomi"
    QString chip;         // e.g. "SDM845", "SM8250"
    QString authType;     // e.g. "none", "oneplus", "xiaomi"
    QByteArray data;      // Loader binary (populated after download)
    QUrl downloadUrl;     // URL for downloading
    int fileSize = 0;     // File size in bytes
};

// ─── Cloud loader matching service ───────────────────────────────────
// Singleton service for matching and downloading Firehose programmers
// from cloud databases based on MSM ID, PK hash, and OEM ID.
class CloudLoaderService : public QObject {
    Q_OBJECT

public:
    using MatchCallback = std::function<void(bool success, const QList<LoaderResult>& results)>;
    using DownloadCallback = std::function<void(bool success, const QByteArray& data)>;

    static CloudLoaderService& instance();

    // ── Loader matching ──────────────────────────────────────────────
    void matchLoader(uint32_t msmId, const QByteArray& pkHash, uint32_t oemId,
                     MatchCallback callback);

    // ── Loader download ──────────────────────────────────────────────
    void downloadLoader(const QString& id, DownloadCallback callback);
    void downloadLoaderFromUrl(const QUrl& url, DownloadCallback callback);

    // ── Loader list ──────────────────────────────────────────────────
    void getLoaderList(MatchCallback callback);

    // ── Configuration ────────────────────────────────────────────────
    void setApiBaseUrl(const QUrl& url);
    QUrl apiBaseUrl() const { return m_apiBaseUrl; }

    // ── Cache ────────────────────────────────────────────────────────
    void setCacheDirectory(const QString& path);
    QByteArray getCachedLoader(const QString& id);
    bool isCached(const QString& id) const;

signals:
    void matchCompleted(bool success, int matchCount);
    void downloadProgress(qint64 received, qint64 total);
    void downloadCompleted(bool success, const QString& id);
    void errorOccurred(const QString& error);

private:
    CloudLoaderService(QObject* parent = nullptr);
    ~CloudLoaderService();
    CloudLoaderService(const CloudLoaderService&) = delete;
    CloudLoaderService& operator=(const CloudLoaderService&) = delete;

    void initNetworkManager();
    QByteArray buildMatchRequest(uint32_t msmId, const QByteArray& pkHash, uint32_t oemId);
    QList<LoaderResult> parseMatchResponse(const QByteArray& data);
    bool cacheLoader(const QString& id, const QByteArray& data);

    QNetworkAccessManager* m_netManager = nullptr;
    QUrl m_apiBaseUrl;
    QString m_cacheDir;

    static constexpr int REQUEST_TIMEOUT_MS = 30000;
};

} // namespace sakura
