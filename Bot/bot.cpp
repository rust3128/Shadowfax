
#include "bot.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QDebug>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QCoreApplication>
#include <QDateTime>
#include <QRegularExpression>

static QFile logFile;


Bot::Bot(QObject *parent) : QObject(parent) {
    lastUpdateId = 0;  // Ініціалізуємо update_id
    networkManager = new QNetworkAccessManager(this);
    loadBotToken();
}

/**
 * @brief Ініціалізує логування у файл
 */
void Bot::initLogging() {
    QString logDirPath = QCoreApplication::applicationDirPath() + "/logs";
    QDir logDir(logDirPath);
    if (!logDir.exists()) {
        logDir.mkpath(".");
    }

    QString logFilePath = logDirPath + "/shadowfax.log";
    logFile.setFileName(logFilePath);

    if (!logFile.open(QIODevice::Append | QIODevice::Text)) {
        qCritical() << "Failed to open log file for writing!";
        return;
    }

    // 🔹 Оновлений обробник логів
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        QString logEntry = QString("[%1] %2")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
            .arg(msg);

        QTextStream logStream(&logFile);
        logStream << logEntry << "\n";  // 🔹 Записуємо у файл
        logStream.flush();

        QTextStream consoleStream(stdout);
        consoleStream << logEntry << Qt::endl;  // 🔹 Виводимо в консоль
    });

    qDebug() << "Logging initialized. Log file:" << logFilePath;
}


/**
 * @brief Завантажує токен бота з `config.ini` або створює файл, якщо його немає.
 */
void Bot::loadBotToken() {
    QString configPath = QCoreApplication::applicationDirPath() + "/config/config.ini";
    qDebug() << "Checking config file at:" << configPath;

    QFile configFile(configPath);

    // 🔹 Якщо `config.ini` не існує – створюємо його
    if (!configFile.exists()) {
        qWarning() << "Config file not found! Creating `config.ini`...";

        QDir configDir(QFileInfo(configPath).absolutePath());
        if (!configDir.exists()) {
            configDir.mkpath(".");
            qDebug() << "Config directory created.";
        }

        if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&configFile);
            out << "[Telegram]\n";
            out << "bot_token=\n";  // 🔹 Порожній токен
            configFile.close();
            qCritical() << "Please enter the bot token in config.ini and restart the application.";
            QCoreApplication::exit(1);
            return;
        } else {
            qCritical() << "Failed to create config.ini!";
        }
    }

    // 🔹 Читаємо токен
    QSettings settings(configPath, QSettings::IniFormat);
    botToken = settings.value("Telegram/bot_token", "").toString();
    qDebug() << "Read bot token from config.ini:" << (botToken.isEmpty() ? "EMPTY" : "LOADED");

    if (botToken.isEmpty()) {
        qCritical() << "Bot token is missing in config.ini! Please enter the token and restart the application.";
        QCoreApplication::exit(1);
    } else {
        qInfo() << "Bot token loaded successfully.";
    }
}


void Bot::startPolling() {
    qDebug() << "🤖 Бот запущений!";
    getUpdates();
}



void Bot::getUpdates() {
    QString url = QString("https://api.telegram.org/bot%1/getUpdates?offset=%2&timeout=30")
    .arg(botToken)
        .arg(lastUpdateId + 1);

    qDebug() << "🔹 Виконуємо запит до Telegram API:" << url;

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "❌ Помилка мережі:" << reply->errorString();
            reply->deleteLater();
            QTimer::singleShot(5000, this, &Bot::getUpdates);
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
        QJsonObject jsonObject = jsonResponse.object();

        qDebug() << "📩 Отримано відповідь від Telegram API:" << jsonObject;

        if (!jsonObject["ok"].toBool()) {
            qWarning() << "❌ Telegram API повернуло помилку!" << jsonObject;
            reply->deleteLater();
            return;
        }

        QJsonArray updates = jsonObject["result"].toArray();
        qDebug() << "🔹 Кількість нових повідомлень:" << updates.size();

        for (const QJsonValue &update : updates) {
            QJsonObject updateObj = update.toObject();
            qint64 updateId = updateObj["update_id"].toVariant().toLongLong();

            if (updateId > lastUpdateId) {
                lastUpdateId = updateId;
                qDebug() << "🔹 Оновлено останній updateId:" << lastUpdateId;

                QJsonObject message;

                // 🔹 Перевіряємо, яке поле є: `message` або `edited_message`
                if (updateObj.contains("message")) {
                    message = updateObj["message"].toObject();
                } else if (updateObj.contains("edited_message")) {
                    message = updateObj["edited_message"].toObject();
                }

                if (message.isEmpty()) {
                    qWarning() << "❌ Отримано оновлення без `message` або `edited_message`. Повне оновлення:" << updateObj;
                    return;
                }

                // 🔹 Отримуємо chat_id
                qint64 chatId = message["chat"].toObject()["id"].toVariant().toLongLong();

                if (chatId == 0) {
                    qWarning() << "❌ Помилка: отримано chatId = 0! Повне повідомлення:" << message;
                    return;
                }

                QString text = message["text"].toString();
                qDebug() << "🔹 Отримано текстове повідомлення:" << text;

                // 🔹 Авторизація
                if (!authorizeUser(chatId)) {
                    qWarning() << "❌ Доступ заборонено для користувача:" << chatId;
                    sendMessage(chatId, "❌ У вас немає доступу до цього бота.","");
                    return;
                }

                QString cleanText = text.simplified().trimmed();  // Видаляємо зайві пробіли
                qInfo() << "📩 Обробка повідомлення від" << chatId << ":" << cleanText;

                // 🔹 Обробка команд та кнопок
                processMessage(chatId, cleanText);
            }
        }

        reply->deleteLater();
        QTimer::singleShot(2000, this, &Bot::getUpdates);
    });
}


// Метод authorizeUser() (авторизація користувача)
bool Bot::authorizeUser(qint64 chatId) {
    if (!isUserAuthorized(chatId)) {
        qWarning() << "❌ Доступ заборонено для користувача:" << chatId;
        return false;
    }
    return true;
}
//Метод processMessage() (обробка команд і кнопок)
void Bot::processMessage(qint64 chatId, const QString &text) {
    QString cleanText = text.simplified().trimmed();  // Видаляємо зайві пробіли

    // 🔹 Конвертуємо текстові кнопки в команди
    if (cleanText == "📜 Допомога") cleanText = "/help";
    if (cleanText == "📋 Список клієнтів") cleanText = "/clients";
    if (cleanText == "🚀 Почати") cleanText = "/start";
    if (cleanText == "🔙 Головне меню") cleanText = "/start";  // Додаємо обробку кнопки "🔙 Головне меню"

    qInfo() << "📩 Отримано повідомлення від" << chatId << ":" << cleanText;

    // 🔹 Якщо бот очікує номер терміналу – обробляємо введення
    if (waitingForTerminal) {
        processTerminalInput(chatId, cleanText);
        return;
    }

    // 🔹 Якщо натиснута кнопка клієнта – обробляємо вибір
    if (clientIdMap.contains(cleanText)) {
        processClientSelection(chatId, cleanText);
        return;
    }

    // 🔹 Обробка команд
    if (!cleanText.startsWith("/")) {
        sendMessage(chatId, "❌ Виберіть команду з меню!","");
    } else if (cleanText == "/start") {
        handleStartCommand(chatId);
    } else if (cleanText == "/help") {
        handleHelpCommand(chatId);
    } else if (cleanText == "/clients") {
        handleClientsCommand(chatId);
    } else {
        sendMessage(chatId, "❌ Невідома команда.","");
    }
}

//Метод processClientSelection() (обробка вибору клієнта)
void Bot::processClientSelection(qint64 chatId, const QString &clientName) {
    qint64 clientId = clientIdMap[clientName];
    lastSelectedClientId = clientId;

    qInfo() << "✅ Користувач вибрав клієнта:" << clientName << "(ID:" << clientId << ")";

    sendMessage(chatId, "🏪 Мережа АЗС - " + clientName + ".\nВкажіть номер терміналу:","");
    waitingForTerminal = true;
}
/**
 * @brief Обробляє введений номер терміналу
 * @param chatId ID чату користувача
 * @param cleanText Введений текст (номер терміналу)
 */
void Bot::processTerminalInput(qint64 chatId, const QString &cleanText) {
    bool ok;
    int terminalNumber = cleanText.toInt(&ok);

    if (ok) {
        qInfo() << "✅ Отримано номер терміналу:" << terminalNumber;
        waitingForTerminal = false;  // Завершуємо очікування

        // 🔹 Виконуємо запит у Palantír
        fetchTerminalInfo(chatId, lastSelectedClientId, terminalNumber);
    } else {
        sendMessage(chatId, "❌ Будь ласка, введіть **числовий номер терміналу**.","");
    }
}

/**
 * @brief Виконує запит у Palantír для отримання інформації про термінал
 * @param chatId ID чату користувача
 * @param clientId ID вибраного клієнта
 * @param terminalId Номер терміналу
 */
void Bot::fetchTerminalInfo(qint64 chatId, qint64 clientId, int terminalId) {
    // 🔹 Формуємо URL для запиту до API Palantír
    QUrl url(QString("http://localhost:8181/terminal_info?client_id=%1&terminal_id=%2")
                 .arg(clientId).arg(terminalId));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            processTerminalInfo(chatId, responseData);
        } else {
            qWarning() << "❌ Помилка отримання даних про термінал:" << reply->errorString();
            sendMessage(chatId, "❌ Не вдалося отримати інформацію про термінал.","");
        }
        reply->deleteLater();
    });
}

/**
 * @brief Обробляє відповідь Palantír із інформацією про термінал
 * @param chatId ID чату користувача
 * @param data Отримана JSON-відповідь
 */
void Bot::processTerminalInfo(qint64 chatId, const QByteArray &data) {
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    if (!jsonDoc.isObject()) {
        qWarning() << "❌ Невірний формат JSON!";
        sendMessage(chatId, "❌ Сталася помилка при отриманні інформації.","");
        return;
    }

    QJsonObject jsonObj = jsonDoc.object();

    // 🔹 Перевіряємо, чи є помилка у відповіді
    if (jsonObj.contains("error")) {
        sendMessage(chatId, "❌ " + jsonObj["error"].toString(),"");
        return;
    }

    // 🔹 Формуємо відповідь
    QString clientName = jsonObj["client_name"].toString();
    int terminalId = jsonObj["terminal_id"].toInt();
    QString address = jsonObj["adress"].toString();
    QString phone = jsonObj["phone"].toString();

    // 🔹 Робимо номер клікабельним через HTML
    QString responseMessage = QString(
                                  "🏪 <b>Мережа АЗС:</b> %1\n"
                                  "⛽ <b>Номер терміналу:</b> %2\n"
                                  "📍 <b>Адреса:</b> %3\n"
                                  "📞 <b>Телефон:</b> <a href=\"tel:%4\">%4</a>"
                                  ).arg(clientName).arg(terminalId).arg(address).arg(phone);

    // 🔹 Відправляємо повідомлення з HTML-форматуванням
    sendMessage(chatId, responseMessage, "HTML");

    // 🔹 Після виводу інформації повертаємо головне меню
    handleStartCommand(chatId);
}




void Bot::handleStartCommand(qint64 chatId) {
    qInfo() << "✅ Виконання команди /start для користувача" << chatId;

    QJsonObject keyboard;
    QJsonArray keyboardArray;
    QJsonArray row1;
    QJsonArray row2;

    // Додаємо кнопки з описами замість команд
    row1.append("📜 Допомога");
    row1.append("📋 Список клієнтів");
    row2.append("🚀 Почати");

    keyboardArray.append(row1);
    keyboardArray.append(row2);

    keyboard["keyboard"] = keyboardArray;
    keyboard["resize_keyboard"] = true;
    keyboard["one_time_keyboard"] = false;

    QJsonObject payload;
    payload["chat_id"] = chatId;
    payload["text"] = "Привіт! Виберіть дію:";
    payload["reply_markup"] = keyboard;

    sendMessageWithKeyboard(payload);
}


void Bot::handleHelpCommand(qint64 chatId) {
    qInfo() << "✅ Виконання команди /help для користувача" << chatId;

    QString helpText = "❓ Доступні команди:\n"
                       "/start - Почати взаємодію з ботом\n"
                       "/help - Показати список команд\n"
                       "/clients - показати список клієнтів\n";

    sendMessage(chatId, helpText,"");
}

// void Bot::handleClientsCommand(qint64 chatId) {
//     lastChatId = chatId;  // Зберігаємо правильний Chat ID
//     fetchClientsList();
//     sendMessage(chatId, "🔄 Отримую список клієнтів...");
// }

void Bot::handleClientsCommand(qint64 chatId) {
    qInfo() << "? Виконання команди /clients для користувача" << chatId;

    // Запит до API Palantir для отримання списку клієнтів
    QUrl url("http://localhost:8181/clients");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            processClientsList(chatId, responseData);
        } else {
            qWarning() << "? Помилка отримання списку клієнтів:" << reply->errorString();
            sendMessage(chatId, "? Помилка отримання даних.","");
        }
        reply->deleteLater();
    });
}

void Bot::processClientsList(qint64 chatId, const QByteArray &data) {
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    if (!jsonDoc.isObject()) {
        qWarning() << "❌ Невірний формат JSON!";
        sendMessage(chatId, "❌ Сталася помилка під час отримання клієнтів.","");
        return;
    }

    QJsonObject jsonObj = jsonDoc.object();
    if (!jsonObj.contains("data") || !jsonObj["data"].isArray()) {
        qWarning() << "❌ Відсутній масив `data` у відповіді!";
        sendMessage(chatId, "❌ Дані про клієнтів недоступні.","");
        return;
    }

    QJsonArray clientsArray = jsonObj["data"].toArray();
    QJsonArray keyboardArray;
    QJsonArray row;
    int count = 0;

    clientIdMap.clear();  // Очистити перед записом нових значень

    for (const QJsonValue &client : clientsArray) {
        QJsonObject obj = client.toObject();
        QString clientName = obj["name"].toString();
        qint64 clientId = obj["id"].toInt();

        clientIdMap[clientName] = clientId;  // Зберігаємо ID клієнта

        row.append(clientName);
        count++;

        if (count == 3) {
            keyboardArray.append(row);
            row = QJsonArray();
            count = 0;
        }
    }

    if (!row.isEmpty()) {
        keyboardArray.append(row);
    }

    QJsonArray backRow;
    backRow.append("🔙 Головне меню");
    keyboardArray.append(backRow);

    QJsonObject keyboard;
    keyboard["keyboard"] = keyboardArray;
    keyboard["resize_keyboard"] = true;
    keyboard["one_time_keyboard"] = false;

    QJsonObject payload;
    payload["chat_id"] = chatId;
    payload["text"] = "📋 Виберіть клієнта:";
    payload["reply_markup"] = keyboard;

    sendMessageWithKeyboard(payload);
}






void Bot::fetchClientsList()
{
    QUrl url("http://localhost:8181/clients");  // Адреса API
    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->get(request);

    // Обробляємо відповідь
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            processClientsResponse(responseData);
        } else {
            qWarning() << "HTTP Error:" << reply->errorString();
        }
        reply->deleteLater();
    });
}

void Bot::processClientsResponse(const QByteArray &data) {
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    if (!jsonDoc.isObject()) {
        qWarning() << "Invalid JSON response";
        return;
    }

    QJsonObject jsonObj = jsonDoc.object();
    if (!jsonObj.contains("data") || !jsonObj["data"].isArray()) {
        qWarning() << "Invalid JSON format";
        return;
    }

    QJsonArray clientsArray = jsonObj["data"].toArray();
    QStringList clientsList;

    for (const QJsonValue &client : clientsArray) {
        QJsonObject obj = client.toObject();
        clientsList.append(QString("%1: %2").arg(obj["id"].toInt()).arg(obj["name"].toString()));
    }

    QString responseMessage = "📋 Список клієнтів:\n" + clientsList.join("\n");
    qDebug() << "Clients list fetched successfully";

    // ✅ Використовуємо правильний Chat ID для відповіді
    sendMessage(lastChatId, responseMessage,"");
}

void Bot::sendMessageWithKeyboard(const QJsonObject &payload) {
    QString url = QString("https://api.telegram.org/bot%1/sendMessage").arg(botToken);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->post(request, QJsonDocument(payload).toJson());

    connect(reply, &QNetworkReply::finished, this, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Помилка при відправці повідомлення:" << reply->errorString();
        } else {
            qInfo() << "✅ Меню команд успішно відправлено.";
        }
        reply->deleteLater();
    });
}

bool Bot::isUserAuthorized(qint64 chatId) {
    QSettings settings(QCoreApplication::applicationDirPath() + "/config/config.ini", QSettings::IniFormat);
    QStringList whitelist = settings.value("Auth/whitelist").toString().split(",", Qt::SkipEmptyParts);

    // Якщо список порожній – усі мають доступ
    if (whitelist.isEmpty()) {
        return true;
    }

    return whitelist.contains(QString::number(chatId));
}

void Bot::sendMessage(qint64 chatId, const QString &text, const QString &parseMode = "") {
    QString url = QString("https://api.telegram.org/bot%1/sendMessage")
    .arg(botToken);

    QJsonObject payload;
    payload["chat_id"] = chatId;
    payload["text"] = text;
    if (!parseMode.isEmpty()) {
        payload["parse_mode"] = parseMode;  // 🔹 Важливо для форматування!
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->post(request, QJsonDocument(payload).toJson());

    connect(reply, &QNetworkReply::finished, this, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Помилка при відправці повідомлення:" << reply->errorString();
        } else {
            qInfo() << "✅ Повідомлення успішно відправлено.";
        }
        reply->deleteLater();
    });
}


