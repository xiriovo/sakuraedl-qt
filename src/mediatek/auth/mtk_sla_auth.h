#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <cstdint>

namespace sakura {

class BromClient;

// ── SLA (Software Loader Authentication) ────────────────────────────────────
//
// MTK secure boot uses SLA challenge-response:
//   1. BROM sends a random challenge (16-32 bytes)
//   2. Tool signs the challenge using the DA's RSA private key
//   3. BROM verifies with the matching public key fused in eFuse
//   4. If valid, BROM accepts the DA binary
//

struct SlaChallenge {
    QByteArray challenge;       // Random bytes from BROM
    uint32_t   version = 0;    // SLA version (v1 / v2 / v3)
    bool       valid = false;
};

struct SlaResponse {
    QByteArray signature;       // Signed challenge
    QByteArray certificate;     // Optional: DA certificate chain
    bool       valid = false;
};

class MtkSlaAuth : public QObject {
    Q_OBJECT

public:
    explicit MtkSlaAuth(QObject* parent = nullptr);
    ~MtkSlaAuth() override;

    // Load authentication keys
    bool loadPrivateKey(const QString& pemPath);
    bool loadPrivateKey(const QByteArray& pemData);
    bool loadDaCertificate(const QString& certPath);
    bool loadDaCertificate(const QByteArray& certData);

    // Full SLA authentication flow
    bool authenticate(BromClient* brom);

    // Individual steps (for manual control)
    SlaChallenge getChallenge(BromClient* brom);
    SlaResponse signChallenge(const SlaChallenge& challenge);
    bool sendResponse(BromClient* brom, const SlaResponse& response);

    // Key info
    bool hasPrivateKey() const { return !m_privateKey.isEmpty(); }
    bool hasCertificate() const { return !m_certificate.isEmpty(); }

signals:
    void authProgress(const QString& message);

private:
    // RSA signing using OpenSSL
    QByteArray rsaSign(const QByteArray& data);

    QByteArray m_privateKey;     // PEM-encoded RSA private key
    QByteArray m_certificate;    // DER-encoded DA certificate
};

} // namespace sakura
