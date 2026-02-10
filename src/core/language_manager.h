#pragma once

#include <QObject>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVariant>

namespace sakura {

enum class Language {
    Chinese = 0,
    English,
    Japanese,
    Korean,
    Russian,
    Spanish
};

class LanguageManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentLanguage READ currentLanguageInt WRITE setLanguageInt NOTIFY languageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)

public:
    static LanguageManager& instance();

    void initialize();
    void setLanguage(Language lang);
    void setLanguageInt(int lang) { setLanguage(static_cast<Language>(lang)); }
    Language currentLanguage() const { return m_currentLang; }
    int currentLanguageInt() const { return static_cast<int>(m_currentLang); }

    Q_INVOKABLE QString t(const QString& key) const;
    Q_INVOKABLE QString languageName(int lang) const;
    QStringList availableLanguages() const;

    void detectLanguageByIP();

signals:
    void languageChanged();

private:
    LanguageManager();
    void loadTranslations();

    Language m_currentLang = Language::Chinese;

    // key -> { lang -> translation }
    QHash<QString, QHash<Language, QString>> m_translations;

    static const QMap<QString, Language> s_countryToLang;
};

} // namespace sakura
