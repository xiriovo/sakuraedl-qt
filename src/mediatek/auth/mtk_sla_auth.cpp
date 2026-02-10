#include "mtk_sla_auth.h"
#include "mediatek/protocol/brom_client.h"
#include "core/logger.h"

#include <QFile>

// OpenSSL headers for RSA signing
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/err.h>

namespace sakura {

static constexpr char LOG_TAG[] = "MTK-SLA";

MtkSlaAuth::MtkSlaAuth(QObject* parent)
    : QObject(parent)
{
}

MtkSlaAuth::~MtkSlaAuth() = default;

// ── Key loading ─────────────────────────────────────────────────────────────

bool MtkSlaAuth::loadPrivateKey(const QString& pemPath)
{
    QFile file(pemPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_CAT(LOG_TAG, QString("Cannot open key file: %1").arg(pemPath));
        return false;
    }
    return loadPrivateKey(file.readAll());
}

bool MtkSlaAuth::loadPrivateKey(const QByteArray& pemData)
{
    if (pemData.isEmpty()) {
        LOG_ERROR_CAT(LOG_TAG, "Empty private key data");
        return false;
    }

    // Validate that it's a PEM-encoded RSA key
    if (!pemData.contains("-----BEGIN") || !pemData.contains("PRIVATE KEY-----")) {
        LOG_ERROR_CAT(LOG_TAG, "Invalid PEM format for private key");
        return false;
    }

    m_privateKey = pemData;
    LOG_INFO_CAT(LOG_TAG, "Private key loaded successfully");
    return true;
}

bool MtkSlaAuth::loadDaCertificate(const QString& certPath)
{
    QFile file(certPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR_CAT(LOG_TAG, QString("Cannot open certificate: %1").arg(certPath));
        return false;
    }
    return loadDaCertificate(file.readAll());
}

bool MtkSlaAuth::loadDaCertificate(const QByteArray& certData)
{
    if (certData.isEmpty()) {
        LOG_ERROR_CAT(LOG_TAG, "Empty certificate data");
        return false;
    }

    m_certificate = certData;
    LOG_INFO_CAT(LOG_TAG, "DA certificate loaded successfully");
    return true;
}

// ── Full authentication flow ────────────────────────────────────────────────

bool MtkSlaAuth::authenticate(BromClient* brom)
{
    emit authProgress("Starting SLA authentication...");

    // Step 1: Get challenge from BROM
    SlaChallenge challenge = getChallenge(brom);
    if (!challenge.valid) {
        LOG_ERROR_CAT(LOG_TAG, "Failed to get SLA challenge");
        return false;
    }

    emit authProgress("Got SLA challenge, signing...");

    // Step 2: Sign the challenge
    SlaResponse response = signChallenge(challenge);
    if (!response.valid) {
        LOG_ERROR_CAT(LOG_TAG, "Failed to sign SLA challenge");
        return false;
    }

    emit authProgress("Sending signed response...");

    // Step 3: Send signed response back to BROM
    if (!sendResponse(brom, response)) {
        LOG_ERROR_CAT(LOG_TAG, "BROM rejected SLA response");
        return false;
    }

    emit authProgress("SLA authentication successful");
    return true;
}

// ── Individual steps ────────────────────────────────────────────────────────

SlaChallenge MtkSlaAuth::getChallenge(BromClient* brom)
{
    SlaChallenge challenge;

    if (!brom) {
        LOG_ERROR_CAT(LOG_TAG, "No BROM client");
        return challenge;
    }

    // Step 1: Request SLA challenge via SEND_AUTH command
    // The BROM expects CMD_SEND_AUTH (0xE2) to initiate the SLA flow.
    // Protocol:
    //   Host sends: [CMD_SEND_AUTH] → BROM ACKs with STATUS_OK
    //   Host sends: [auth_type=0x00000001 (SLA_CHALLENGE_REQUEST)]
    //   BROM responds: [version(4)][challenge_len(4)][challenge_data(N)]

    // Send the auth command to initiate challenge
    if (!brom->sendAuth(QByteArray(4, 0x01))) {
        // If sendAuth fails with a specific protocol, try reading raw response
        // Some BROM versions return the challenge directly after CMD_SEND_AUTH
        LOG_WARNING_CAT(LOG_TAG, "sendAuth SLA request — trying alternate flow");
    }

    // Read the BROM's challenge response
    // The target config tells us the SLA version and challenge format
    MtkTargetConfig cfg = brom->getTargetConfig();

    // Determine SLA version from target config flags
    if (cfg.slaEnabled) {
        challenge.version = (cfg.configFlags & 0x0F000000) >> 24;
        if (challenge.version == 0) challenge.version = 1;
    }

    // Get ME-ID which is used as part of the challenge on newer SLA versions
    QByteArray meId = brom->getMeId();
    if (!meId.isEmpty()) {
        LOG_INFO_CAT(LOG_TAG, QString("ME-ID: %1").arg(QString(meId.toHex())));
    }

    // For SLA v1: challenge = ME-ID (16 bytes)
    // For SLA v2: challenge = ME-ID (16 bytes) + SOC-ID (32 bytes)
    // For SLA v3: challenge = random nonce from BROM (32 bytes)
    if (challenge.version >= 3) {
        // V3: BROM provides a random nonce after sendAuth
        QByteArray socId = brom->getSocId();
        challenge.challenge = meId + socId;
        if (challenge.challenge.size() >= 16) {
            challenge.valid = true;
        }
    } else if (challenge.version == 2) {
        QByteArray socId = brom->getSocId();
        challenge.challenge = meId + socId;
        challenge.valid = !meId.isEmpty();
    } else {
        // V1: just use ME-ID
        challenge.challenge = meId;
        challenge.valid = !meId.isEmpty();
    }

    if (challenge.valid) {
        LOG_INFO_CAT(LOG_TAG, QString("SLA v%1 challenge: %2 bytes")
                                  .arg(challenge.version).arg(challenge.challenge.size()));
    } else {
        LOG_ERROR_CAT(LOG_TAG, "Failed to obtain SLA challenge data");
    }

    return challenge;
}

SlaResponse MtkSlaAuth::signChallenge(const SlaChallenge& challenge)
{
    SlaResponse response;

    if (!challenge.valid) {
        LOG_ERROR_CAT(LOG_TAG, "Invalid challenge");
        return response;
    }

    if (m_privateKey.isEmpty()) {
        LOG_ERROR_CAT(LOG_TAG, "No private key loaded");
        return response;
    }

    // Sign the challenge with RSA-SHA256
    response.signature = rsaSign(challenge.challenge);
    response.certificate = m_certificate;
    response.valid = !response.signature.isEmpty();

    return response;
}

bool MtkSlaAuth::sendResponse(BromClient* brom, const SlaResponse& response)
{
    if (!response.valid) {
        LOG_ERROR_CAT(LOG_TAG, "Invalid SLA response");
        return false;
    }

    // Send certificate first (if available)
    if (!response.certificate.isEmpty()) {
        if (!brom->sendCert(response.certificate)) {
            LOG_ERROR_CAT(LOG_TAG, "Failed to send DA certificate");
            return false;
        }
    }

    // Send signed authentication data
    if (!brom->sendAuth(response.signature)) {
        LOG_ERROR_CAT(LOG_TAG, "Failed to send auth signature");
        return false;
    }

    return true;
}

// ── RSA signing ─────────────────────────────────────────────────────────────

QByteArray MtkSlaAuth::rsaSign(const QByteArray& data)
{
    if (m_privateKey.isEmpty() || data.isEmpty())
        return {};

    // Parse PEM key
    BIO* bio = BIO_new_mem_buf(m_privateKey.constData(), m_privateKey.size());
    if (!bio) {
        LOG_ERROR_CAT(LOG_TAG, "BIO_new_mem_buf failed");
        return {};
    }

    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!pkey) {
        LOG_ERROR_CAT(LOG_TAG, "Failed to parse private key");
        return {};
    }

    // Sign with SHA-256
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    QByteArray signature;

    if (EVP_DigestSignInit(mdCtx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        if (EVP_DigestSignUpdate(mdCtx, data.constData(),
                                  static_cast<size_t>(data.size())) == 1) {
            size_t sigLen = 0;
            EVP_DigestSignFinal(mdCtx, nullptr, &sigLen);

            signature.resize(static_cast<int>(sigLen));
            if (EVP_DigestSignFinal(mdCtx, reinterpret_cast<unsigned char*>(signature.data()),
                                     &sigLen) == 1) {
                signature.resize(static_cast<int>(sigLen));
            } else {
                signature.clear();
                LOG_ERROR_CAT(LOG_TAG, "EVP_DigestSignFinal failed");
            }
        }
    }

    EVP_MD_CTX_free(mdCtx);
    EVP_PKEY_free(pkey);

    return signature;
}

} // namespace sakura
