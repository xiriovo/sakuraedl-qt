#pragma once

#include "i_auth_strategy.h"
#include <QByteArray>
#include <QMap>
#include <QString>

namespace sakura {

// ─── Xiaomi Firehose authentication ──────────────────────────────────
// Xiaomi devices use pre-built signature blobs that are sent as
// <sig> elements during the Firehose handshake. The signatures are
// RSA-2048 signed hashes that authorize specific operations.
class XiaomiAuth : public IAuthStrategy {
public:
    XiaomiAuth();

    bool authenticateAsync(FirehoseClient* client) override;
    QString name() const override { return QStringLiteral("Xiaomi"); }

    // Load auth signatures from a Xiaomi auth file / token
    bool loadAuthFile(const QString& path);

    // Set the pre-built signature blob directly
    void setSignature(const QByteArray& sig);

    // Set the programmer signature (from MiFlash auth)
    void setProgrammerSignature(const QByteArray& sig);

    // Known device models that require auth
    static bool requiresAuth(const QString& chipName);

private:
    bool sendSignature(FirehoseClient* client, const QByteArray& sig);
    QByteArray buildAuthXml(const QByteArray& signature);

    QByteArray m_signature;
    QByteArray m_programmerSig;
    QMap<QString, QByteArray> m_authTokens;  // model -> token

    // Xiaomi auth protocol constants
    static constexpr int AUTH_SIGNATURE_SIZE = 256;  // RSA-2048
};

} // namespace sakura
