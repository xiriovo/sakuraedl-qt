#include "cloud_signing_service.h"
#include "core/logger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>

namespace sakura {

static constexpr char LOG_TAG[] = "MTK-CLOUD";

CloudSigningService::CloudSigningService(QObject* parent)
    : QObject(parent)
{
}

CloudSigningService::~CloudSigningService() = default;

// ── Synchronous signing ─────────────────────────────────────────────────────

CloudSigningResponse CloudSigningService::signDa(const CloudSigningRequest& request)
{
    if (!isConfigured()) {
        return { {}, {}, {}, false, "Cloud signing service not configured" };
    }

    QByteArray payload = buildRequestPayload(request);
    return performHttpRequest("/api/v1/sign/da", payload);
}

CloudSigningResponse CloudSigningService::signChallenge(const CloudSigningRequest& request)
{
    if (!isConfigured()) {
        return { {}, {}, {}, false, "Cloud signing service not configured" };
    }

    QByteArray payload = buildRequestPayload(request);
    return performHttpRequest("/api/v1/sign/challenge", payload);
}

// ── Asynchronous signing ────────────────────────────────────────────────────

void CloudSigningService::signDaAsync(const CloudSigningRequest& request)
{
    // Non-blocking DA signing via QNetworkAccessManager
    if (!isConfigured()) {
        emit signingError("Cloud signing service not configured");
        return;
    }

    QByteArray payload = buildRequestPayload(request);
    performHttpRequestAsync("/api/v1/sign/da", payload);
}

void CloudSigningService::signChallengeAsync(const CloudSigningRequest& request)
{
    // Non-blocking challenge signing via QNetworkAccessManager
    if (!isConfigured()) {
        emit signingError("Cloud signing service not configured");
        return;
    }

    QByteArray payload = buildRequestPayload(request);
    performHttpRequestAsync("/api/v1/sign/challenge", payload);
}

// ── Private helpers ─────────────────────────────────────────────────────────

QByteArray CloudSigningService::buildRequestPayload(const CloudSigningRequest& request) const
{
    QJsonObject json;
    json["da_hash"]     = QString(request.daHash.toBase64());
    json["challenge"]   = QString(request.challenge.toBase64());
    json["hw_code"]     = static_cast<int>(request.hwCode);
    json["sla_version"] = static_cast<int>(request.slaVersion);

    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

CloudSigningResponse CloudSigningService::parseResponse(const QByteArray& data) const
{
    CloudSigningResponse response;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        response.errorMessage = "JSON parse error: " + parseError.errorString();
        return response;
    }

    QJsonObject json = doc.object();

    if (json.contains("error")) {
        response.errorMessage = json["error"].toString();
        return response;
    }

    if (json.contains("signed_da"))
        response.signedDa = QByteArray::fromBase64(json["signed_da"].toString().toLatin1());
    if (json.contains("signed_challenge"))
        response.signedChallenge = QByteArray::fromBase64(
            json["signed_challenge"].toString().toLatin1());
    if (json.contains("certificate"))
        response.certificate = QByteArray::fromBase64(
            json["certificate"].toString().toLatin1());

    response.success = true;
    return response;
}

CloudSigningResponse CloudSigningService::performHttpRequest(const QString& endpoint,
                                                               const QByteArray& payload)
{
    CloudSigningResponse response;

    QUrl url = m_serverUrl;
    url.setPath(endpoint);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.post(request, payload);

    // Synchronous wait with timeout
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(REQUEST_TIMEOUT_MS);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        response.errorMessage = "Request timed out";
        reply->deleteLater();
        return response;
    }

    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        response.errorMessage = reply->errorString();
        LOG_ERROR_CAT(LOG_TAG, QString("Cloud signing failed: %1").arg(response.errorMessage));
        reply->deleteLater();
        return response;
    }

    QByteArray responseData = reply->readAll();
    reply->deleteLater();

    return parseResponse(responseData);
}

void CloudSigningService::performHttpRequestAsync(const QString& endpoint,
                                                     const QByteArray& payload)
{
    QUrl url = m_serverUrl;
    url.setPath(endpoint);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    // Use a persistent NAM (allocated on heap, will be cleaned up with parent QObject)
    auto* nam = new QNetworkAccessManager(this);
    QNetworkReply* reply = nam->post(request, payload);

    // Timeout timer
    auto* timer = new QTimer(this);
    timer->setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, timer, nam]() {
        timer->stop();

        if (reply->error() != QNetworkReply::NoError) {
            QString err = reply->errorString();
            LOG_ERROR_CAT(LOG_TAG, QString("Async cloud signing failed: %1").arg(err));
            emit signingError(err);
        } else {
            QByteArray data = reply->readAll();
            CloudSigningResponse response = parseResponse(data);
            if (response.success)
                emit signingCompleted(response);
            else
                emit signingError(response.errorMessage);
        }

        reply->deleteLater();
        timer->deleteLater();
        nam->deleteLater();
    });

    QObject::connect(timer, &QTimer::timeout, this, [this, reply, timer, nam]() {
        reply->abort();
        emit signingError("Cloud signing request timed out");
        reply->deleteLater();
        timer->deleteLater();
        nam->deleteLater();
    });

    timer->start(REQUEST_TIMEOUT_MS);
    emit signingProgress("Sending signing request to cloud...");
}

} // namespace sakura
