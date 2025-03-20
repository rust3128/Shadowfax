#ifndef BOT_H
#define BOT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <tuple>
#include <QMap>

class Bot : public QObject {
    Q_OBJECT
public:
    explicit Bot(QObject *parent = nullptr);
    void startPolling();  // Почати отримання повідомлень
    void sendMessage(qint64 chatId, const QString &text, bool isHtml = true); // Відправити повідомлення

    static void initLogging();  // 🔹 Метод ініціалізації логування

private slots:
    void getUpdates();  // Отримати нові повідомлення

private:
    void loadBotToken();                                                 // Завантажує токен бота з `config.ini`
    void handleStartCommand(qint64 chatId);  // 🔹 Метод для обробки команди `/start`
    void handleHelpCommand(qint64 chatId);   // 🔹 Обробка `/help`
    void handleClientsCommand(qint64 chatId);// 🔹 Обробка `/clients`

    void fetchClientsList();                // Відправляємо GET /clients.
    void processClientsResponse(const QByteArray &data); //Обробляємо GET /clients.
    void sendMessageWithKeyboard(const QJsonObject &payload);
    bool isUserAuthorized(qint64 chatId);           // Перевірка авторізації
    void processClientsList(qint64 chatId, const QByteArray &data);
    bool authorizeUser(qint64 chatId);               // авторизація користувача
    void processMessage(qint64 chatId, qint64 userId, const QString &text, const QString &firstName, const QString &lastName, const QString &username); //обробка команд і кнопок
    void requestAdminApproval(qint64 userId, qint64 chatId, const QString &firstName, const QString &lastName, const QString &username);
    void handleApproveCommand(qint64 chatId, qint64 userId, const QString &text);
    void handleRejectCommand(qint64 chatId, qint64 userId, const QString &text);
    void handleTerminalSelection(qint64 chatId);
    void handleAzsList(qint64 chatId);
    void handleRroInfo(qint64 chatId);
    void handlePrkInfo(qint64 chatId);
    void handleReservoirInfo(qint64 chatId);


    void processClientSelection(qint64 chatId, const QString &clientName); //обробка вибору клієнта
    void processTerminalInput(qint64 chatId, const QString &cleanText);     //обробка номера терміналу
    void fetchTerminalInfo(qint64 chatId, qint64 clientId, int terminalId); // * @brief Виконує запит у Palantír для отримання інформації про термінал
    void processTerminalInfo(qint64 chatId, const QByteArray &data);        //@brief Обробляє відповідь Palantír із інформацією про термінал

private:
    QNetworkAccessManager *networkManager;
    QString botToken;
    qint64 lastUpdateId;  // Останній отриманий update_id
    qint64 lastChatId = 0;  // Зберігаємо останній Chat ID для відповідей
    QMap<QString, qint64> clientIdMap;  // Збереження відповідності "Назва клієнта" -> ID
    qint64 lastSelectedClientId = 0;  // ID вибраного клієнта
    qint64 lastSelectedTerminalId;  // ✅ Додаємо збереження вибраного терміналу
    bool waitingForTerminal = false;  // Чи очікуємо введення номера терміналу?
    QMap<qint64, std::tuple<QString, QString, QString>> lastApprovalRequest;
};

#endif // BOT_H
