#include "cloud_loader_service.h"
#include "core/logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>

static const QString TAG = QStringLiteral("CloudLoader");

namespace sakura {

CloudLoaderService& CloudLoaderService::instance()
{
    static CloudLoaderService s_instance;
    return s_instance;
}

CloudLoaderService::CloudLoaderService(QObject* parent)
    : QObject(parent)
    , m_apiBaseUrl(QUrl("https://edl-loaders.sakura.dev/api/v1"))
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                 + "/loaders";
    initNetworkManager();
}

CloudLoaderService::~CloudLoaderService() = default;

void CloudLoaderService::initNetworkManager()
{
    if (!m_netManager) {
        m_netManager = new QNetworkAccessManager(this);
    }
}

// ─── Configuration ───────────────────────────────────────────────────

void CloudLoaderService::setApiBaseUrl(const QUrl& url)
{
    m_apiBaseUrl = url;
}

void CloudLoaderService::setCacheDirectory(const QString& path)
{
    m_cacheDir = path;
    QDir().mkpath(m_cacheDir);
}

// ─── Match loader ────────────────────────────────────────────────────

void CloudLoaderService::matchLoader(uint32_t msmId, const QByteArray& pkHash,
                                      uint32_t oemId, MatchCallback callback)
{
    LOG_INFO_CAT(TAG, QString("Matching loader: MSM=0x%1, OEM=0x%2, PK=%3")
                    .arg(msmId, 4, 16, QChar('0'))
                    .arg(oemId, 4, 16, QChar('0'))
                    .arg(QString(pkHash.toHex().left(16)) + "..."));

    QUrl url(m_apiBaseUrl.toString() + "/match");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);

    QByteArray body = buildMatchRequest(msmId, pkHash, oemId);
    QNetworkReply* reply = m_netManager->post(request, body);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR_CAT(TAG, QString("Match request failed: %1").arg(reply->errorString()));
            emit errorOccurred(reply->errorString());
            if (callback)
                callback(false, {});
            return;
        }

        QByteArray data = reply->readAll();
        QList<LoaderResult> results = parseMatchResponse(data);

        LOG_INFO_CAT(TAG, QString("Found %1 matching loaders").arg(results.size()));
        emit matchCompleted(true, results.size());

        if (callback)
            callback(!results.isEmpty(), results);
    });
}

// ─── Download loader ─────────────────────────────────────────────────

void CloudLoaderService::downloadLoader(const QString& id, DownloadCallback callback)
{
    // Check cache first
    QByteArray cached = getCachedLoader(id);
    if (!cached.isEmpty()) {
        LOG_INFO_CAT(TAG, QString("Loader '%1' loaded from cache").arg(id));
        if (callback)
            callback(true, cached);
        emit downloadCompleted(true, id);
        return;
    }

    QUrl url(m_apiBaseUrl.toString() + "/download/" + id);
    downloadLoaderFromUrl(url, [this, id, callback](bool success, const QByteArray& data) {
        if (success && !data.isEmpty()) {
            cacheLoader(id, data);
        }
        emit downloadCompleted(success, id);
        if (callback)
            callback(success, data);
    });
}

void CloudLoaderService::downloadLoaderFromUrl(const QUrl& url, DownloadCallback callback)
{
    LOG_INFO_CAT(TAG, QString("Downloading loader from %1").arg(url.toString()));

    QNetworkRequest request(url);
    request.setTransferTimeout(REQUEST_TIMEOUT_MS * 4); // Allow more time for downloads

    QNetworkReply* reply = m_netManager->get(request);

    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     this, &CloudLoaderService::downloadProgress);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR_CAT(TAG, QString("Download failed: %1").arg(reply->errorString()));
            emit errorOccurred(reply->errorString());
            if (callback)
                callback(false, {});
            return;
        }

        QByteArray data = reply->readAll();
        LOG_INFO_CAT(TAG, QString("Downloaded %1 bytes").arg(data.size()));

        if (callback)
            callback(true, data);
    });
}

// ─── Loader list ─────────────────────────────────────────────────────

void CloudLoaderService::getLoaderList(MatchCallback callback)
{
    QUrl url(m_apiBaseUrl.toString() + "/list");
    QNetworkRequest request(url);
    request.setTransferTimeout(REQUEST_TIMEOUT_MS);

    QNetworkReply* reply = m_netManager->get(request);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR_CAT(TAG, QString("List request failed: %1").arg(reply->errorString()));
            if (callback)
                callback(false, {});
            return;
        }

        QByteArray data = reply->readAll();
        QList<LoaderResult> results = parseMatchResponse(data);
        if (callback)
            callback(!results.isEmpty(), results);
    });
}

// ─── Cache operations ────────────────────────────────────────────────

QByteArray CloudLoaderService::getCachedLoader(const QString& id)
{
    QString path = m_cacheDir + "/" + id + ".elf";
    QFile file(path);
    if (file.exists() && file.open(QIODevice::ReadOnly)) {
        return file.readAll();
    }
    return {};
}

bool CloudLoaderService::isCached(const QString& id) const
{
    return QFile::exists(m_cacheDir + "/" + id + ".elf");
}

bool CloudLoaderService::cacheLoader(const QString& id, const QByteArray& data)
{
    QDir().mkpath(m_cacheDir);
    QString path = m_cacheDir + "/" + id + ".elf";
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
        LOG_DEBUG_CAT(TAG, QString("Cached loader: %1").arg(path));
        return true;
    }
    return false;
}

// ─── Request/response helpers ────────────────────────────────────────

QByteArray CloudLoaderService::buildMatchRequest(uint32_t msmId, const QByteArray& pkHash,
                                                  uint32_t oemId)
{
    QJsonObject obj;
    obj["msm_id"] = QString("0x%1").arg(msmId, 4, 16, QChar('0'));
    obj["pk_hash"] = QString(pkHash.toHex());
    obj["oem_id"] = QString("0x%1").arg(oemId, 4, 16, QChar('0'));

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QList<LoaderResult> CloudLoaderService::parseMatchResponse(const QByteArray& data)
{
    QList<LoaderResult> results;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        LOG_WARNING_CAT(TAG, "Invalid JSON response");
        return results;
    }

    QJsonArray loaders = doc.object().value("loaders").toArray();
    for (const auto& val : loaders) {
        QJsonObject obj = val.toObject();
        LoaderResult lr;
        lr.id = obj.value("id").toString();
        lr.filename = obj.value("filename").toString();
        lr.vendor = obj.value("vendor").toString();
        lr.chip = obj.value("chip").toString();
        lr.authType = obj.value("auth_type").toString();
        lr.fileSize = obj.value("file_size").toInt();

        QString url = obj.value("download_url").toString();
        if (!url.isEmpty())
            lr.downloadUrl = QUrl(url);

        results.append(lr);
    }

    return results;
}

} // namespace sakura
