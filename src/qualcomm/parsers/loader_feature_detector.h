#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace sakura {

// ─── Detected loader features ────────────────────────────────────────
struct LoaderFeatures {
    bool     valid = false;

    // Protocol capabilities
    bool     supportsRead = false;
    bool     supportsProgram = false;
    bool     supportsErase = false;
    bool     supportsPeek = false;
    bool     supportsPoke = false;
    bool     supportsGetGpt = false;
    bool     supportsSetActiveSlot = false;
    bool     supportsPatch = false;
    bool     supportsNop = false;
    bool     supportsProvision = false;

    // Storage
    bool     supportsUfs = false;
    bool     supportsEmmc = false;
    bool     supportsNand = false;

    // Auth / security
    bool     requiresAuth = false;
    bool     hasHashChecking = false;
    QString  authVendor;             // e.g. "oneplus", "xiaomi", "samsung"

    // Metadata
    uint32_t maxPayloadSize = 0;
    uint32_t maxXmlSize = 0;
    QString  buildDate;
    QString  buildVersion;
    QStringList strings;             // All extracted printable strings
};

// ─── Loader feature detection ────────────────────────────────────────
// Analyzes a Firehose programmer (ELF) binary to detect its capabilities,
// required authentication, and supported features.
class LoaderFeatureDetector {
public:
    // Analyze a loader binary and detect features
    static LoaderFeatures detect(const QByteArray& loaderBinary);

    // Quick check: does this look like a valid Firehose programmer?
    static bool isFirehoseLoader(const QByteArray& data);

    // Extract the max payload size from the loader binary
    static uint32_t detectMaxPayload(const QByteArray& data);

    // Detect vendor from embedded strings
    static QString detectVendor(const QByteArray& data);

private:
    // Extract printable ASCII strings from binary (like `strings` command)
    static QStringList extractStrings(const QByteArray& data, int minLength = 6);

    // Check for specific string patterns
    static bool containsString(const QStringList& strings, const QString& pattern);

    // ELF parsing helpers
    static bool isElf(const QByteArray& data);
    static QByteArray extractElfSegments(const QByteArray& data);
};

} // namespace sakura
