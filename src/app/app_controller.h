#pragma once

#include <QObject>
#include <QStringList>

namespace sakura {

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QStringList logMessages READ logMessages NOTIFY logUpdated)

public:
    explicit AppController(QObject* parent = nullptr);

    int currentPage() const { return m_currentPage; }
    void setCurrentPage(int page);

    QString statusText() const { return m_statusText; }
    QStringList logMessages() const { return m_logMessages; }

    Q_INVOKABLE void clearLog();
    Q_INVOKABLE QString translate(const QString& key);
    Q_INVOKABLE void setLanguage(int lang);
    Q_INVOKABLE int currentLanguage();
    Q_INVOKABLE QStringList availableLanguages();

signals:
    void currentPageChanged();
    void statusChanged();
    void logUpdated();
    void newLogMessage(const QString& message, int level);

private slots:
    void onLogMessage(const QString& message, int level);

private:
    int m_currentPage = 0;
    QString m_statusText;
    QStringList m_logMessages;
    static constexpr int MAX_LOG_LINES = 5000;
};

} // namespace sakura
