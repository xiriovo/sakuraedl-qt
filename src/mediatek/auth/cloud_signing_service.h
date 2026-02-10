#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

namespace sakura {

// ── Cloud DA signing service ────────────────────────────────────────────────
//
// For devices with SLA v2/v3, the DA must be signed with a key that matches
// the eFuse-burned public key. Some vendors provide a cloud signing service
// where the tool sends the DA hash and receives a valid signature.
//
// This service implements the client side of such a signing protocol.
//

struct CloudSigningRequest {
    QByteArray daHash;           // SHA-256 hash of the DA binary
    QByteArray challenge;        // SLA challenge from BROM
    uint16_t   hwCode = 0;
    uint32_t   slaVersion = 0;
};

struct CloudSigningResponse {
    QByteArray signedDa;         // Signature for the DA
    QByteArray signedChallenge;  // Signature for the SLA challenge
    QByteArray certificate;      // Certificate chain (if provided)
    bool       success = false;
    QString    errorMessage;
};

class CloudSigningService : public QObject {
    Q_OBJECT

public:
    explicit CloudSigningService(QObject* parent = nullptr);
    ~CloudSigningService() override;

    // Configuration
    void setServerUrl(const QUrl& url) { m_serverUrl = url; }
    QUrl serverUrl() const { return m_serverUrl; }

    void setApiKey(const QString& key) { m_apiKey = key; }
    bool isConfigured() const { return m_serverUrl.isValid() && !m_apiKey.isEmpty(); }

    // Signing operations
    CloudSigningResponse signDa(const CloudSigningRequest& request);
    CloudSigningResponse signChallenge(const CloudSigningRequest& request);

    // Async variants
    void signDaAsync(const CloudSigningRequest& request);
    void signChallengeAsync(const CloudSigningRequest& request);

signals:
    void signingCompleted(const CloudSigningResponse& response);
    void signingError(const QString& message);
    void signingProgress(const QString& message);

private:
    QByteArray buildRequestPayload(const CloudSigningRequest& request) const;
    CloudSigningResponse parseResponse(const QByteArray& data) const;
    CloudSigningResponse performHttpRequest(const QString& endpoint,
                                             const QByteArray& payload);
    void performHttpRequestAsync(const QString& endpoint,
                                  const QByteArray& payload);

    QUrl m_serverUrl;
    QString m_apiKey;
    static constexpr int REQUEST_TIMEOUT_MS = 30000;
};

} // namespace sakura
