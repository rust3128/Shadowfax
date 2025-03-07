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

    static void initLogging();  // 🔹 Метод ініціалізації логування

private slots:
    void getUpdates();  // Отримати нові повідомлення

private:
    void loadBotToken();                                                 // Завантажує токен бота з `config.ini`
    void handleStartCommand(qint64 chatId);  // 🔹 Метод для обробки команди `/start`
    void handleHelpCommand(qint64 chatId);   // 🔹 Обробка `/help`
private:
    QNetworkAccessManager *networkManager;
    QString botToken;
    qint64 lastUpdateId;  // Останній отриманий update_id



};

#endif // BOT_H

