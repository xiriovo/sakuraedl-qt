#include "language_manager.h"
#include "logger.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>

namespace sakura {

const QMap<QString, Language> LanguageManager::s_countryToLang = {
    {"CN", Language::Chinese}, {"TW", Language::Chinese}, {"HK", Language::Chinese},
    {"US", Language::English}, {"GB", Language::English}, {"AU", Language::English},
    {"CA", Language::English}, {"IN", Language::English},
    {"JP", Language::Japanese},
    {"KR", Language::Korean},
    {"RU", Language::Russian}, {"BY", Language::Russian}, {"KZ", Language::Russian},
    {"ES", Language::Spanish}, {"MX", Language::Spanish}, {"AR", Language::Spanish},
    {"CO", Language::Spanish}, {"CL", Language::Spanish},
};

LanguageManager::LanguageManager() {}

LanguageManager& LanguageManager::instance()
{
    static LanguageManager inst;
    return inst;
}

void LanguageManager::initialize()
{
    loadTranslations();

    // Try loading saved language preference
    QSettings settings("SakuraEDL", "SakuraEDL");
    int savedLang = settings.value("language", -1).toInt();
    if (savedLang >= 0 && savedLang <= static_cast<int>(Language::Spanish)) {
        m_currentLang = static_cast<Language>(savedLang);
    }

    LOG_INFO("LanguageManager initialized, current: " + languageName(static_cast<int>(m_currentLang)));
}

void LanguageManager::setLanguage(Language lang)
{
    if (m_currentLang != lang) {
        m_currentLang = lang;
        QSettings settings("SakuraEDL", "SakuraEDL");
        settings.setValue("language", static_cast<int>(lang));
        LOG_INFO("Language changed to: " + languageName(static_cast<int>(lang)));
        emit languageChanged();
    }
}

QString LanguageManager::t(const QString& key) const
{
    auto it = m_translations.find(key);
    if (it != m_translations.end()) {
        auto langIt = it->find(m_currentLang);
        if (langIt != it->end())
            return *langIt;
        // Fallback to Chinese
        langIt = it->find(Language::Chinese);
        if (langIt != it->end())
            return *langIt;
    }
    return key; // Return key itself as fallback
}

QString LanguageManager::languageName(int lang) const
{
    switch (static_cast<Language>(lang)) {
    case Language::Chinese:  return "中文";
    case Language::English:  return "English";
    case Language::Japanese: return "日本語";
    case Language::Korean:   return "한국어";
    case Language::Russian:  return "Русский";
    case Language::Spanish:  return "Español";
    }
    return "Unknown";
}

QStringList LanguageManager::availableLanguages() const
{
    return {"中文", "English", "日本語", "한국어", "Русский", "Español"};
}

void LanguageManager::detectLanguageByIP()
{
    // IP-based language detection will be done via QNetworkAccessManager
    // calling http://ip-api.com/json/?fields=countryCode
    // For now, default to Chinese
    LOG_INFO("IP-based language detection not yet implemented, defaulting to Chinese");
}

void LanguageManager::loadTranslations()
{
    // Core UI strings — add translations inline for now
    // In production, these would be loaded from .ts/.qm files

    auto addTr = [this](const QString& key, const QString& zh, const QString& en,
                        const QString& ja = {}, const QString& ko = {},
                        const QString& ru = {}, const QString& es = {}) {
        QHash<Language, QString> map;
        map[Language::Chinese] = zh;
        map[Language::English] = en;
        if (!ja.isEmpty()) map[Language::Japanese] = ja;
        if (!ko.isEmpty()) map[Language::Korean] = ko;
        if (!ru.isEmpty()) map[Language::Russian] = ru;
        if (!es.isEmpty()) map[Language::Spanish] = es;
        m_translations[key] = map;
    };

    // Navigation
    addTr("nav.qualcomm", "高通平台", "Qualcomm", "Qualcomm", "Qualcomm", "Qualcomm", "Qualcomm");
    addTr("nav.mediatek", "MTK平台", "MediaTek", "MediaTek", "MediaTek", "MediaTek", "MediaTek");
    addTr("nav.spreadtrum", "展讯平台", "Spreadtrum", "Spreadtrum", "Spreadtrum", "Spreadtrum", "Spreadtrum");
    addTr("nav.fastboot", "引导模式", "Fastboot", "Fastboot", "Fastboot", "Fastboot", "Fastboot");
    addTr("nav.autoroot", "自动Root", "Auto Root", "自動Root", "자동 Root", "Авто Root", "Auto Root");
    addTr("nav.settings", "设置", "Settings", "設定", "설정", "Настройки", "Configuración");

    // Common actions
    addTr("action.connect", "连接", "Connect", "接続", "연결", "Подключить", "Conectar");
    addTr("action.disconnect", "断开", "Disconnect", "切断", "연결 해제", "Отключить", "Desconectar");
    addTr("action.flash", "刷写", "Flash", "書き込み", "플래시", "Прошить", "Flashear");
    addTr("action.read", "读取", "Read", "読み取り", "읽기", "Чтение", "Leer");
    addTr("action.erase", "擦除", "Erase", "消去", "지우기", "Стереть", "Borrar");
    addTr("action.reboot", "重启", "Reboot", "再起動", "재부팅", "Перезагрузка", "Reiniciar");
    addTr("action.browse", "浏览", "Browse", "参照", "찾아보기", "Обзор", "Examinar");
    addTr("action.cancel", "取消", "Cancel", "キャンセル", "취소", "Отмена", "Cancelar");

    // Status
    addTr("status.disconnected", "未连接", "Disconnected", "未接続", "연결 안 됨", "Отключен", "Desconectado");
    addTr("status.connecting", "连接中...", "Connecting...", "接続中...", "연결 중...", "Подключение...", "Conectando...");
    addTr("status.connected", "已连接", "Connected", "接続済み", "연결됨", "Подключен", "Conectado");
    addTr("status.flashing", "刷写中...", "Flashing...", "書き込み中...", "플래시 중...", "Прошивка...", "Flasheando...");
    addTr("status.done", "完成", "Done", "完了", "완료", "Готово", "Hecho");
    addTr("status.error", "错误", "Error", "エラー", "오류", "Ошибка", "Error");

    // Partition table
    addTr("partition.name", "分区名", "Name", "パーティション名", "파티션명", "Раздел", "Nombre");
    addTr("partition.start", "起始扇区", "Start Sector", "開始セクタ", "시작 섹터", "Начальный сектор", "Sector inicial");
    addTr("partition.size", "大小", "Size", "サイズ", "크기", "Размер", "Tamaño");
    addTr("partition.lun", "LUN", "LUN", "LUN", "LUN", "LUN", "LUN");

    // Settings
    addTr("settings.language", "语言", "Language", "言語", "언어", "Язык", "Idioma");
    addTr("settings.performance", "性能模式", "Performance Mode", "パフォーマンスモード", "성능 모드", "Режим производительности", "Modo de rendimiento");
    addTr("settings.log_level", "日志级别", "Log Level", "ログレベル", "로그 수준", "Уровень логов", "Nivel de registro");

    // Window
    addTr("window.title", "SakuraEDL v3.0", "SakuraEDL v3.0", "SakuraEDL v3.0", "SakuraEDL v3.0", "SakuraEDL v3.0", "SakuraEDL v3.0");
    addTr("log.title", "日志", "Log", "ログ", "로그", "Журнал", "Registro");
}

} // namespace sakura
