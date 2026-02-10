#include "fastboot_protocol.h"

namespace sakura {

// ---------------------------------------------------------------------------
// FastbootResponse
// ---------------------------------------------------------------------------

uint32_t FastbootResponse::dataLength() const
{
    if (type != FastbootResponseType::Data)
        return 0;
    bool ok = false;
    uint32_t len = data.trimmed().toUInt(&ok, 16);
    return ok ? len : 0;
}

QString FastbootResponse::toString() const
{
    QString prefix = FastbootProtocol::responseTypeName(type);
    if (data.isEmpty())
        return prefix;
    return prefix + QStringLiteral(": ") + QString::fromUtf8(data);
}

// ---------------------------------------------------------------------------
// FastbootProtocol – command building
// ---------------------------------------------------------------------------

QByteArray FastbootProtocol::buildCommand(const QString& cmd,
                                          const QStringList& args)
{
    QString full = cmd;
    if (!args.isEmpty()) {
        // Fastboot convention: "command:arg1:arg2"
        full += QLatin1Char(':') + args.join(QLatin1Char(':'));
    }
    return full.toUtf8().left(MAX_COMMAND_LENGTH);
}

QByteArray FastbootProtocol::buildDownloadCommand(uint32_t size)
{
    // "download:<hex-size>" – 8-character zero-padded hex
    return QStringLiteral("download:%1")
        .arg(size, 8, 16, QLatin1Char('0'))
        .toUtf8();
}

// ---------------------------------------------------------------------------
// FastbootProtocol – response parsing
// ---------------------------------------------------------------------------

FastbootResponse FastbootProtocol::parseResponse(const QByteArray& data)
{
    FastbootResponse resp;
    if (data.size() < 4) {
        resp.type = FastbootResponseType::Unknown;
        resp.data = data;
        return resp;
    }

    QByteArray prefix = data.left(4);

    if (prefix == PREFIX_OKAY)
        resp.type = FastbootResponseType::Okay;
    else if (prefix == PREFIX_FAIL)
        resp.type = FastbootResponseType::Fail;
    else if (prefix == PREFIX_DATA)
        resp.type = FastbootResponseType::Data;
    else if (prefix == PREFIX_INFO)
        resp.type = FastbootResponseType::Info;
    else
        resp.type = FastbootResponseType::Unknown;

    resp.data = data.mid(4);
    return resp;
}

QString FastbootProtocol::responseTypeName(FastbootResponseType type)
{
    switch (type) {
    case FastbootResponseType::Okay:    return QStringLiteral("OKAY");
    case FastbootResponseType::Fail:    return QStringLiteral("FAIL");
    case FastbootResponseType::Data:    return QStringLiteral("DATA");
    case FastbootResponseType::Info:    return QStringLiteral("INFO");
    case FastbootResponseType::Unknown: return QStringLiteral("UNKNOWN");
    }
    return QStringLiteral("UNKNOWN");
}

// ---------------------------------------------------------------------------
// FastbootProtocol – VID helpers
// ---------------------------------------------------------------------------

QList<uint16_t> FastbootProtocol::knownVids()
{
    return {
        VID_GOOGLE, VID_SAMSUNG, VID_XIAOMI, VID_HUAWEI,
        VID_ONEPLUS, VID_MOTOROLA, VID_SONY, VID_LG,
        VID_OPPO, VID_QUALCOMM
    };
}

bool FastbootProtocol::isKnownFastbootVid(uint16_t vid)
{
    const auto vids = knownVids();
    return vids.contains(vid);
}

} // namespace sakura
