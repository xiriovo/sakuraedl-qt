#pragma once

#include "i_auth_strategy.h"
#include <QByteArray>
#include <QString>

namespace sakura {

// ─── VIP Firehose authentication ─────────────────────────────────────
// VIP auth uses a pre-built digest + signature pair sent via Firehose
// XML protocol. The digest is a hash of the programmer binary and the
// signature is the RSA-signed digest proving authorization.
class VipAuth : public IAuthStrategy {
public:
    VipAuth();

    bool authenticateAsync(FirehoseClient* client) override;
    QString name() const override { return QStringLiteral("VIP"); }

    // Load digest and signature from files
    bool loadDigest(const QString& path);
    bool loadSignature(const QString& path);

    // Set digest/signature directly
    void setDigest(const QByteArray& digest);
    void setSignature(const QByteArray& signature);

    bool isReady() const { return !m_digest.isEmpty() && !m_signature.isEmpty(); }

private:
    QByteArray m_digest;
    QByteArray m_signature;
};

} // namespace sakura
