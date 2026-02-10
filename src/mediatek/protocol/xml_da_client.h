#pragma once

#include <QByteArray>
#include <QDomDocument>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <cstdint>

#include "common/partition_info.h"
#include "mediatek/protocol/da_loader.h"

namespace sakura {

class ITransport;

// ── XML DA V6 protocol ──────────────────────────────────────────────────────
//
// Communication format:
//   Request:  <da><version>1.0</version><command>CMD:XXX</command>...params...</da>
//   Response: <da_response><version>1.0</version><result>OK</result>...data...</da_response>
//

namespace XmlDaCmd {
    constexpr const char* CMD_NOTIFY_INIT      = "CMD:NOTIFY-INIT";
    constexpr const char* CMD_SECURITY_SET_FLASH_POLICY = "CMD:SECURITY-SET-FLASH-POLICY";
    constexpr const char* CMD_READ_PARTITION   = "CMD:READ-PARTITION";
    constexpr const char* CMD_WRITE_PARTITION  = "CMD:WRITE-PARTITION";
    constexpr const char* CMD_ERASE_PARTITION  = "CMD:ERASE-PARTITION";
    constexpr const char* CMD_FORMAT_PARTITION = "CMD:FORMAT-PARTITION";
    constexpr const char* CMD_READ_FLASH       = "CMD:READ-FLASH";
    constexpr const char* CMD_WRITE_FLASH      = "CMD:WRITE-FLASH";
    constexpr const char* CMD_GET_GPT          = "CMD:GET-GPT";
    constexpr const char* CMD_READ_REGISTER    = "CMD:READ-REGISTER";
    constexpr const char* CMD_WRITE_REGISTER   = "CMD:WRITE-REGISTER";
    constexpr const char* CMD_REBOOT           = "CMD:REBOOT";
    constexpr const char* CMD_SHUTDOWN         = "CMD:SHUTDOWN";
    constexpr const char* CMD_GET_DA_INFO      = "CMD:GET-DA-INFO";
    constexpr const char* CMD_GET_HW_INFO      = "CMD:GET-HW-INFO";
    constexpr const char* CMD_GET_EMMC_INFO    = "CMD:GET-EMMC-INFO";
    constexpr const char* CMD_GET_UFS_INFO     = "CMD:GET-UFS-INFO";
    constexpr const char* CMD_GET_NAND_INFO    = "CMD:GET-NAND-INFO";
    constexpr const char* CMD_SET_HOST_INFO    = "CMD:SET-HOST-INFO";
    constexpr const char* CMD_SET_META_BOOT_MODE = "CMD:SET-META-BOOT-MODE";
    constexpr const char* CMD_GET_RPMB_STATUS  = "CMD:GET-RPMB-STATUS";
    constexpr const char* CMD_WRITE_EFUSE      = "CMD:WRITE-EFUSE";
    constexpr const char* CMD_READ_EFUSE       = "CMD:READ-EFUSE";
}

struct XmlDaInfo {
    QString daVersion;
    uint32_t flashType = 0;
    uint64_t flashSize = 0;
    QString  emmcCid;
    QString  ufsSerialNumber;
};

class XmlDaClient : public QObject {
    Q_OBJECT

public:
    explicit XmlDaClient(ITransport* transport, QObject* parent = nullptr);
    ~XmlDaClient() override;

    // Initialisation
    bool notifyInit();
    bool setFlashPolicy(const QString& policy = "FORCE");

    // Partition operations
    QList<PartitionInfo> readPartitions();
    bool writePartition(const QString& name, const QByteArray& data);
    QByteArray readPartition(const QString& name, qint64 offset = 0, qint64 length = -1);
    bool erasePartition(const QString& name);
    bool formatPartition(const QString& name);

    // DA2 upload
    bool uploadDa2(const DaEntry& da2);

    // Device info
    XmlDaInfo getDaInfo();

    // Control
    bool reboot();
    bool shutdown();

signals:
    void transferProgress(qint64 current, qint64 total);

private:
    // XML helpers
    QString buildXmlCommand(const QString& command,
                            const QMap<QString, QString>& params = {}) const;
    bool sendXml(const QString& xml);
    QDomDocument recvXmlResponse();
    bool isResponseOk(const QDomDocument& doc) const;
    QString getResponseField(const QDomDocument& doc, const QString& field) const;

    // Data transfer (binary payload follows XML handshake)
    bool sendBinaryPayload(const QByteArray& data);
    QByteArray recvBinaryPayload(qint64 expectedSize);

    ITransport* m_transport = nullptr;
    static constexpr int DEFAULT_TIMEOUT = 10000;
    static constexpr char XML_VERSION[]  = "1.0";
};

} // namespace sakura
