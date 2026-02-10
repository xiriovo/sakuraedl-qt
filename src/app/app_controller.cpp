#include "app_controller.h"
#include "core/logger.h"
#include "core/language_manager.h"

namespace sakura {

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    connect(&Logger::instance(), &Logger::messageLogged,
            this, &AppController::onLogMessage);
}

void AppController::setCurrentPage(int page)
{
    if (m_currentPage != page) {
        m_currentPage = page;
        emit currentPageChanged();
    }
}

void AppController::clearLog()
{
    m_logMessages.clear();
    emit logUpdated();
}

QString AppController::translate(const QString& key)
{
    return LanguageManager::instance().t(key);
}

void AppController::setLanguage(int lang)
{
    LanguageManager::instance().setLanguage(static_cast<Language>(lang));
}

int AppController::currentLanguage()
{
    return static_cast<int>(LanguageManager::instance().currentLanguage());
}

QStringList AppController::availableLanguages()
{
    return LanguageManager::instance().availableLanguages();
}

void AppController::onLogMessage(const QString& message, int level)
{
    m_logMessages.append(message);
    while (m_logMessages.size() > MAX_LOG_LINES)
        m_logMessages.removeFirst();
    emit logUpdated();
    emit newLogMessage(message, level);
}

} // namespace sakura
