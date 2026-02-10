#pragma once

#include "i_auth_strategy.h"
#include <QByteArray>

namespace sakura {

// ─── OnePlus Firehose authentication ─────────────────────────────────
// OnePlus devices use an AES-256 + SHA-256 challenge/response
// during Firehose initialization. The programmer sends a random nonce,
// which the host must encrypt with a device-specific key and return.
class OnePlusAuth : public IAuthStrategy {
public:
    OnePlusAuth();

    bool authenticateAsync(FirehoseClient* client) override;
    QString name() const override { return QStringLiteral("OnePlus"); }

    // Set the engineering key (typically extracted from the programmer)
    void setKey(const QByteArray& key);

private:
    // AES-256-CBC encrypt using OpenSSL
    QByteArray aesEncrypt(const QByteArray& plaintext, const QByteArray& key,
                          const QByteArray& iv);

    // SHA-256 hash
    QByteArray sha256(const QByteArray& data);

    // Derive device key from chip info
    QByteArray deriveKey(const QByteArray& chipSerial, const QByteArray& pkHash);

    QByteArray m_engineeringKey;

    // Known OnePlus keys (obfuscated, for reference)
    static const QByteArray s_defaultKeyV1;
    static const QByteArray s_defaultKeyV2;
};

} // namespace sakura
