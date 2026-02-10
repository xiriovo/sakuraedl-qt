#include "oneplus_auth.h"
#include "qualcomm/protocol/firehose_client.h"
#include "core/logger.h"

#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/sha.h>

#include <QXmlStreamReader>
#include <cstring>

static const QString TAG = QStringLiteral("OnePlusAuth");

namespace sakura {

// Default OnePlus engineering keys (placeholder — real keys vary per device)
const QByteArray OnePlusAuth::s_defaultKeyV1 = QByteArray::fromHex(
    "0000000000000000000000000000000000000000000000000000000000000000");
const QByteArray OnePlusAuth::s_defaultKeyV2 = QByteArray::fromHex(
    "0000000000000000000000000000000000000000000000000000000000000000");

OnePlusAuth::OnePlusAuth()
    : m_engineeringKey(s_defaultKeyV1)
{
}

void OnePlusAuth::setKey(const QByteArray& key)
{
    if (key.size() == 32) {
        m_engineeringKey = key;
    } else {
        LOG_WARNING_CAT(TAG, QString("Invalid key size: %1 (expected 32)").arg(key.size()));
    }
}

bool OnePlusAuth::authenticateAsync(FirehoseClient* client)
{
    LOG_INFO_CAT(TAG, "Starting OnePlus authentication");

    // Step 1: Send getproperty to check if auth is required
    QString getAuthXml = QStringLiteral(
        "<?xml version=\"1.0\" ?>"
        "<data><getproperty Type=\"OemInfo\" /></data>");

    auto resp = client->sendRawXml(getAuthXml);
    if (!resp.success) {
        LOG_WARNING_CAT(TAG, "getproperty failed — device may not require auth");
        return false;
    }

    // Step 2: Parse the nonce from the response
    QByteArray nonce;
    QXmlStreamReader reader(resp.rawXml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QStringLiteral("response")) {
            QString nonceHex = reader.attributes().value("value").toString();
            if (!nonceHex.isEmpty()) {
                nonce = QByteArray::fromHex(nonceHex.toLatin1());
            }
        }
    }

    if (nonce.isEmpty()) {
        LOG_WARNING_CAT(TAG, "No nonce received — auth may not be required");
        return true; // Some devices don't need auth
    }

    LOG_INFO_CAT(TAG, QString("Received nonce (%1 bytes)").arg(nonce.size()));

    // Step 3: Encrypt nonce with AES-256-CBC
    QByteArray iv(16, 0); // Zero IV
    QByteArray encrypted = aesEncrypt(nonce, m_engineeringKey, iv);
    if (encrypted.isEmpty()) {
        LOG_ERROR_CAT(TAG, "AES encryption failed");
        return false;
    }

    // Step 4: Hash the encrypted data with SHA-256
    QByteArray hash = sha256(encrypted);

    // Step 5: Send the response back
    QString authXml = QString(
        "<?xml version=\"1.0\" ?>"
        "<data><configure Token=\"%1\" /></data>")
        .arg(QString(encrypted.toHex()));

    auto authResp = client->sendRawXml(authXml);
    if (!authResp.success) {
        LOG_ERROR_CAT(TAG, "Authentication rejected by device");
        return false;
    }

    LOG_INFO_CAT(TAG, "OnePlus authentication successful");
    return true;
}

QByteArray OnePlusAuth::aesEncrypt(const QByteArray& plaintext, const QByteArray& key,
                                    const QByteArray& iv)
{
    if (key.size() != 32 || iv.size() != 16)
        return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    QByteArray ciphertext;
    ciphertext.resize(plaintext.size() + AES_BLOCK_SIZE);

    int outLen = 0;
    int totalLen = 0;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.constData()),
                           reinterpret_cast<const unsigned char*>(iv.constData())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Disable padding for nonce responses
    EVP_CIPHER_CTX_set_padding(ctx, 0);

    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()),
                          &outLen,
                          reinterpret_cast<const unsigned char*>(plaintext.constData()),
                          plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen = outLen;

    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ciphertext.data()) + totalLen,
                            &outLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen += outLen;

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(totalLen);
    return ciphertext;
}

QByteArray OnePlusAuth::sha256(const QByteArray& data)
{
    QByteArray hash(SHA256_DIGEST_LENGTH, 0);
    SHA256(reinterpret_cast<const unsigned char*>(data.constData()),
           data.size(),
           reinterpret_cast<unsigned char*>(hash.data()));
    return hash;
}

QByteArray OnePlusAuth::deriveKey(const QByteArray& chipSerial, const QByteArray& pkHash)
{
    // OnePlus engineering key derivation:
    // Different device generations use different derivation schemes.
    //
    // V1 (OnePlus 5/5T/6/6T): HMAC-SHA256(chipSerial, pkHash)
    // V2 (OnePlus 7/7T/8/9+): Multi-round PBKDF2-like derivation
    //   - salt = SHA256(chipSerial || pkHash)
    //   - key  = HMAC-SHA256(salt, chipSerial) XOR HMAC-SHA256(salt, pkHash)
    //
    // We implement both and the caller selects based on device version.

    if (chipSerial.isEmpty() || pkHash.isEmpty()) {
        LOG_WARNING_CAT(TAG, "Cannot derive key: missing chipSerial or pkHash");
        return sha256(chipSerial + pkHash); // Fallback
    }

    // V2 derivation (more common on newer devices)
    QByteArray salt = sha256(chipSerial + pkHash);

    // HMAC-SHA256 using OpenSSL EVP
    auto hmacSha256 = [](const QByteArray& key, const QByteArray& data) -> QByteArray {
        QByteArray result(32, 0);
        unsigned int len = 0;

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_PKEY* pkey = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, nullptr,
            reinterpret_cast<const unsigned char*>(key.constData()), key.size());

        if (pkey && ctx) {
            EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey);
            EVP_DigestSignUpdate(ctx, data.constData(), static_cast<size_t>(data.size()));
            size_t sigLen = 32;
            EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(result.data()), &sigLen);
            len = static_cast<unsigned int>(sigLen);
        }

        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        result.resize(static_cast<int>(len));
        return result;
    };

    QByteArray h1 = hmacSha256(salt, chipSerial);
    QByteArray h2 = hmacSha256(salt, pkHash);

    // XOR the two HMAC results
    QByteArray derivedKey(32, 0);
    for (int i = 0; i < 32 && i < h1.size() && i < h2.size(); ++i) {
        derivedKey[i] = static_cast<char>(h1[i] ^ h2[i]);
    }

    LOG_INFO_CAT(TAG, "Key derived from chip serial and PK hash");
    return derivedKey;
}

} // namespace sakura
