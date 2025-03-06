#ifndef BOT_H
#define BOT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

class Bot : public QObject {
    Q_OBJECT
public:
    explicit Bot(QObject *parent = nullptr);
    void startPolling();  // Почати отримання повідомлень
    void sendMessage(qint64 chatId, const QString &text);  // Відправити повідомлення

private slots:
    void getUpdates();  // Отримати нові повідомлення

private:
    QNetworkAccessManager *networkManager;
    QString botToken = "7997761383:AAFOlA5vXkYIOL8G2zDo6qlVQOBIxRV9-88";  // 🔹 Замініть на свій токен
    qint64 lastUpdateId;  // Останній отриманий update_id
};

#endif // BOT_H

