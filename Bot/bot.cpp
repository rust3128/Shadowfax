#include "bot.h"
#include "config.h"
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
#include <QUrlQuery>

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
 * @brief Завантажує токен бота з config.ini або створює файл, якщо його немає.
 */
void Bot::loadBotToken() {
    QString configPath = QCoreApplication::applicationDirPath() + "/config/config.ini";
    qDebug() << "Checking config file at:" << configPath;

    QFile configFile(configPath);

    // 🔹 Якщо config.ini не існує – створюємо його
    if (!configFile.exists()) {
        qWarning() << "Config file not found! Creating config.ini...";

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

//    qDebug() << "🔹 Виконуємо запит до Telegram API:" << url;

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

//        qDebug() << "📩 Отримано відповідь від Telegram API:" << jsonObject;

        if (!jsonObject.value(QStringLiteral("ok")).toBool()) {
            qWarning() << "❌ Telegram API повернуло помилку!" << jsonObject;
            reply->deleteLater();
            return;
        }

        QJsonArray updates = jsonObject.value(QStringLiteral("result")).toArray();
//        qDebug() << "🔹 Кількість нових повідомлень:" << updates.size();

        for (const QJsonValue &update : updates) {
            QJsonObject updateObj = update.toObject();
            qint64 updateId = updateObj.value(QStringLiteral("update_id")).toVariant().toLongLong();

            if (updateId > lastUpdateId) {
                lastUpdateId = updateId;
                qDebug() << "🔹 Оновлено останній updateId:" << lastUpdateId;

                QJsonObject message;

                // 🔹 Перевіряємо, яке поле є: message або edited_message
                if (updateObj.contains(QStringLiteral("message"))) {
                    message = updateObj.value(QStringLiteral("message")).toObject();
                } else if (updateObj.contains(QStringLiteral("edited_message"))) {
                    message = updateObj.value(QStringLiteral("edited_message")).toObject();
                }

                if (message.isEmpty()) {
                    qWarning() << "❌ Отримано оновлення без message або edited_message. Повне оновлення:" << updateObj;
                    return;
                }

                // 🔹 Отримуємо chat_id та user_id
                qint64 chatId = message.value(QStringLiteral("chat")).toObject()
                                    .value(QStringLiteral("id")).toVariant().toLongLong();
                qint64 userId = message.value(QStringLiteral("from")).toObject()
                                    .value(QStringLiteral("id")).toVariant().toLongLong();  // ✅ Фікс


                if (chatId == 0) {
                    qWarning() << "❌ Помилка: отримано chatId = 0! Повне повідомлення:" << message;
                    return;
                }
                QString firstName = message["from"].toObject().value("first_name").toString();
                QString lastName = message["from"].toObject().value("last_name").toString();
                QString username = message["from"].toObject().value("username").toString();
                QString text = message.value(QStringLiteral("text")).toString();
                qDebug() << "🔹 Отримано текстове повідомлення:" << text;
                qDebug() << "📩 User ID:" << userId << "| Chat ID:" << chatId;  // ✅ Додаємо в лог userId

                QString cleanText = text.simplified().trimmed();  // Видаляємо зайві пробіли
                qInfo() << "📩 Обробка повідомлення від" << userId << "(Chat ID:" << chatId << "):" << cleanText;

                // 🔹 Передаємо userId у processMessage()
                processMessage(chatId, userId, text, firstName, lastName, username);

            }
        }

        reply->deleteLater();
        QTimer::singleShot(2000, this, &Bot::getUpdates);
    });
}


void Bot::processMessage(qint64 chatId, qint64 userId, const QString &text, const QString &firstName, const QString &lastName, const QString &username) {

    QString blacklistFilePath = QCoreApplication::applicationDirPath() + "/Config/blacklist.txt";

    QFile blacklistFile(blacklistFilePath);
    if (blacklistFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&blacklistFile);
        while (!in.atEnd()) {
            if (in.readLine().trimmed() == QString::number(userId)) {
                qDebug() << "❌ Користувач " << userId << " у чорному списку! Ігноруємо повідомлення.";
                blacklistFile.close();
                return;  // 🔹 Бот просто ігнорує цього користувача
            }
        }
        blacklistFile.close();
    }

    // 🔹 Перевіряємо авторизацію
    if (Config::instance().useAuth()) {
        if (!isUserAuthorized(userId)) {
            qDebug() << "❌ Unauthorized user" << userId << "attempted to use the bot.";
            sendMessage(chatId, "❌ У вас немає доступу до цього бота. Зверніться до адміністратора.");

            requestAdminApproval(userId, chatId, firstName, lastName, username);
            return;
        }
    }
    QString cleanText = text.simplified().trimmed();  // Видаляємо зайві пробіли

    // 🔹 Конвертуємо текстові кнопки в команди
    if (cleanText == "📜 Допомога") cleanText = "/help";
    if (cleanText == "📋 Список клієнтів") cleanText = "/clients";
    if (cleanText == "🚀 Почати") cleanText = "/start";
    if (cleanText == "🔙 Головне меню") cleanText = "/start";
    if (cleanText == "🏪 Оберіть термінал") cleanText = "/get_terminal_id";
    if (cleanText == "📋 Список АЗС") cleanText = "/get_azs_list";
    if (cleanText == "💳 РРО") cleanText = "/get_rro_info";
    if (cleanText == "🛢 Резервуари") cleanText = "/get_reservoir_info";
    if (cleanText == "⛽ ПРК") cleanText = "/get_prk_info";

    qInfo() << "📩 Отримано повідомлення від" << userId << "(Chat ID:" << chatId << "):" << cleanText;

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
        sendMessage(chatId, "❌ Виберіть команду з меню!");
    } else if (cleanText == "/start") {
        handleStartCommand(chatId);
    } else if (cleanText == "/help") {
        handleHelpCommand(chatId);
    } else if (cleanText == "/clients") {
        handleClientsCommand(chatId);
    } else if (cleanText.startsWith("/approve ")) {
        handleApproveCommand(chatId, userId, cleanText);
    } else if (cleanText.startsWith("/reject ")) {
        handleRejectCommand(chatId, userId, cleanText);
    } else if (cleanText == "/get_terminal_id") {
        qDebug() << "✅ Викликано handleTerminalSelection()!";
        handleTerminalSelection(chatId);
    } else if (cleanText == "/get_azs_list") {
        qDebug() << "✅ Викликано handleAzsList()!";
        handleAzsList(chatId);
    } else if (cleanText == "/get_rro_info") {
        qDebug() << "✅ Викликано handleRroInfo()!";
        handleRroInfo(chatId);
    } else if (cleanText == "/get_reservoir_info") {
        qDebug() << "✅ Викликано handleReservoirInfo()!";
        handleReservoirInfo(chatId);
    } else if (cleanText == "/get_prk_info") {
        qDebug() << "✅ Викликано handlePrkInfo()!";
        handlePrkInfo(chatId);
    } else {
        sendMessage(chatId, "❌ Невідома команда.");
    }

}


//Метод processClientSelection() (обробка вибору клієнта)
void Bot::processClientSelection(qint64 chatId, const QString &clientName) {
    // qint64 clientId = clientIdMap[clientName];
    // lastSelectedClientId = clientId;

    // qInfo() << "✅ Користувач вибрав клієнта:" << clientName << "(ID:" << clientId << ")";

    // sendMessage(chatId, "🏪 Мережа АЗС - " + clientName + ".\nВкажіть номер терміналу:");
    // waitingForTerminal = true;
    qint64 clientId = clientIdMap[clientName];
    lastSelectedClientId = clientId;

    qInfo() << "✅ Користувач вибрав клієнта:" << clientName << "(ID:" << clientId << ")";

    sendMessage(chatId, "📌 Ви вибрали клієнта: " + clientName + ".\nОберіть дію:", true);

    // Відправляємо три кнопки
    QJsonObject keyboard;
    QJsonArray keyboardArray;

    QJsonArray row1;
    row1.append("🏪 Оберіть термінал");
    row1.append("📋 Список АЗС");

    QJsonArray row2;
    row2.append("🔙 Головне меню");  // Використовуємо вже існуючу логіку

    keyboardArray.append(row1);
    keyboardArray.append(row2);

    keyboard["keyboard"] = keyboardArray;
    keyboard["resize_keyboard"] = true;
    keyboard["one_time_keyboard"] = false;

    QJsonObject payload;
    payload["chat_id"] = chatId;
    payload["text"] = "Оберіть дію:";
    payload["reply_markup"] = keyboard;

    sendMessageWithKeyboard(payload);
}
void Bot::handleTerminalSelection(qint64 chatId) {
    sendMessage(chatId, "🏪 Ви обрали термінал. Введіть номер терміналу:");
    waitingForTerminal = true;  // ✅ Тепер бот чекає введення номера терміналу
}

void Bot::handleAzsList(qint64 chatId) {
    qDebug() << "✅ Виконано handleAzsList() для чату" << chatId;

    QUrl url(QString("http://localhost:8181/azs_list?client_id=%1").arg(lastSelectedClientId));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "❌ Помилка отримання списку АЗС:" << reply->errorString();
            sendMessage(chatId, "❌ Не вдалося отримати список АЗС.");
            reply->deleteLater();
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        reply->deleteLater();

        if (!jsonDoc.isObject()) {
            qWarning() << "❌ Отримано некоректний JSON!";
            sendMessage(chatId, "❌ Сталася помилка при обробці відповіді сервера.");
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        // 🔹 Перевіряємо, чи є помилка у відповіді
        if (jsonObj.contains("error")) {
            QString errorMessage = jsonObj["error"].toString();
            qWarning() << "❌ Сервер повернув помилку:" << errorMessage;
            sendMessage(chatId, "❌ " + errorMessage);
            return;
        }

        QJsonArray azsList = jsonObj["azs_list"].toArray();

        if (azsList.isEmpty()) {
            sendMessage(chatId, "ℹ️ Немає доступних АЗС для цього клієнта.");
            return;
        }

        // ✅ Формуємо список АЗС
        QString responseText = "⛽ <b>Список АЗС</b>\n";
        for (const QJsonValue &val : azsList) {
            QJsonObject obj = val.toObject();
            responseText += QString("🔹 %1 - %2\n")
                                .arg(obj["terminal_id"].toInt())
                                .arg(obj["name"].toString());
        }

        qDebug() << "📩 Відправляється повідомлення:\n" << responseText;
        sendMessage(chatId, responseText);
    });
}


void Bot::handleReservoirInfo(qint64 chatId) {
    qDebug() << "✅ Виконано handleReservoirsInfo() для чату" << chatId;

    QUrl url(QString("http://localhost:8181/reservoirs_info?client_id=%1&terminal_id=%2")
                 .arg(lastSelectedClientId)
                 .arg(lastSelectedTerminalId));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "❌ Помилка отримання даних про резервуари:" << reply->errorString();
            sendMessage(chatId, "❌ Не вдалося отримати інформацію про резервуари.");
            reply->deleteLater();
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        reply->deleteLater();

        if (!jsonDoc.isObject()) {
            qWarning() << "❌ Отримано некоректний JSON!";
            sendMessage(chatId, "❌ Сталася помилка при обробці відповіді сервера.");
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        // 🔹 Перевіряємо, чи є помилка у відповіді
        if (jsonObj.contains("error")) {
            QString errorMessage = jsonObj["error"].toString();
            qWarning() << "❌ Сервер повернув помилку:" << errorMessage;
            sendMessage(chatId, "❌ " + errorMessage);
            return;
        }

        QJsonArray reservoirs = jsonObj["reservoirs_info"].toArray();

        if (reservoirs.isEmpty()) {
            sendMessage(chatId, "ℹ️ Немає доступних резервуарів для цього терміналу.");
            return;
        }

        // ✅ Формуємо повідомлення
        QString responseText = "🛢 <b>Інформація про резервуари</b>\n";
        for (const QJsonValue &val : reservoirs) {
            QJsonObject obj = val.toObject();
            responseText += QString("🔹 <b>Резервуар %1</b> – %2, %3:\n")
                                .arg(obj["tank_id"].toInt())
                                .arg(obj["name"].toString())
                                .arg(obj["shortname"].toString());
            responseText += QString("   🔽 <b>Min:</b> %1  |  🔼 <b>Max:</b> %2\n")
                                .arg(obj["minvalue"].toInt())
                                .arg(obj["maxvalue"].toInt());
            responseText += QString("   📏 <b>Рівномір:</b> %1 - %2\n")
                                .arg(obj["deadmin"].toInt())
                                .arg(obj["deadmax"].toInt());
            responseText += QString("   🏭 <b>Трубопровід:</b> %1\n\n")
                                .arg(obj["tubeamount"].toInt());
        }

        qDebug() << "📩 Відправляється повідомлення:\n" << responseText;
        sendMessage(chatId, responseText);
    });
}



void Bot::handlePrkInfo(qint64 chatId) {
    qDebug() << "✅ Виконано handlePrkInfo() для чату" << chatId;

    QUrl url(QString("http://localhost:8181/terminal_info?client_id=%1&terminal_id=%2")
                 .arg(lastSelectedClientId).arg(lastSelectedTerminalId));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "❌ Помилка отримання даних про ПРК:" << reply->errorString();
            sendMessage(chatId, "❌ Не вдалося отримати інформацію про ПРК.");
            reply->deleteLater();
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        reply->deleteLater();

        if (!jsonDoc.isObject()) {
            qWarning() << "❌ Отримано некоректний JSON!";
            sendMessage(chatId, "❌ Сталася помилка при обробці відповіді сервера.");
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        // 🔹 Перевіряємо, чи є помилка у відповіді
        if (jsonObj.contains("error")) {
            QString errorMessage = jsonObj["error"].toString();
            qWarning() << "❌ Сервер повернув помилку:" << errorMessage;
            sendMessage(chatId, "❌ " + errorMessage);
            return;
        }

        // ✅ Отримуємо `dispensers_info`
        QJsonArray dispensers = jsonObj["dispensers_info"].toArray();
        QString responseText = "<b>Конфігурація ПРК</b>\n";

        if (dispensers.isEmpty()) {
            responseText += "ℹ️ Дані про ПРК відсутні.";
        } else {
            for (const QJsonValue &dispenserVal : dispensers) {
                QJsonObject dispenserObj = dispenserVal.toObject();
                int dispenserId = dispenserObj["dispenser_id"].toInt();
                QString protocol = dispenserObj["protocol"].toString();
                int port = dispenserObj["port"].toInt();
                int speed = dispenserObj["speed"].toInt();
                int address = dispenserObj["address"].toInt();

                responseText += QString("🔹 <b>ПРК %1:</b> %2, порт %3, швидкість %4, адреса %5\n")
                                    .arg(dispenserId).arg(protocol).arg(port).arg(speed).arg(address);

                if (dispenserObj.contains("pumps_info")) {
                    QJsonArray pumps = dispenserObj["pumps_info"].toArray();
                    for (int i = 0; i < pumps.size(); ++i) {
                        QJsonObject pumpObj = pumps[i].toObject();
                        int pumpId = pumpObj["pump_id"].toInt();
                        int tankId = pumpObj["tank_id"].toInt();
                        QString fuel = pumpObj["fuel_shortname"].toString();

                        if (i == pumps.size() - 1) {
                            responseText += QString("  └ 🛠 Пістолет %1 (резервуар %2) – %3\n")
                                                .arg(pumpId).arg(tankId).arg(fuel);
                        } else {
                            responseText += QString("  ├ 🛠 Пістолет %1 (резервуар %2) – %3\n")
                                                .arg(pumpId).arg(tankId).arg(fuel);
                        }
                    }
                }
            }
        }

        sendMessage(chatId, responseText);
    });
}



void Bot::handleRroInfo(qint64 chatId) {
    qDebug() << "✅ Виконано handleRroInfo() для чату" << chatId;

    QUrl posUrl(QString("http://localhost:8181/pos_info?client_id=%1&terminal_id=%2")
                    .arg(lastSelectedClientId).arg(lastSelectedTerminalId));  // ✅ Використовуємо збережений terminalId

    QNetworkRequest posRequest(posUrl);
    posRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *posReply = networkManager->get(posRequest);

    connect(posReply, &QNetworkReply::finished, this, [this, posReply, chatId]() mutable {
        if (posReply->error() == QNetworkReply::NoError) {
            QByteArray posData = posReply->readAll();
            QJsonDocument posJsonDoc = QJsonDocument::fromJson(posData);

            if (posJsonDoc.isObject()) {
                QJsonObject posJsonObj = posJsonDoc.object();
                QJsonArray posInfoArray = posJsonObj["pos_info"].toArray();

                // 🔹 Формуємо блок з інформацією про POS
                QString responseText;
                if (!posInfoArray.isEmpty()) {
                    responseText += "<b>Інформація про POS</b>\n";
                    for (const QJsonValue &posVal : posInfoArray) {
                        QJsonObject posObj = posVal.toObject();
                        responseText += QString("💳 <b>Каса %1</b>\n").arg(posObj["pos_id"].toInt());

                        // 🔹 Виводимо ЗН та ФН в одному рядку
                        responseText += QString("   🔹 ЗН: %1  |  ФН: %2\n")
                                            .arg(posObj["factorynumber"].toString())
                                            .arg(posObj["regnumber"].toString());

                        if (posObj.contains("pos_version") && !posObj["pos_version"].toString().isEmpty()) {
                            responseText += QString("   🔹 Версія ПО: %1\n").arg(posObj["pos_version"].toString());
                        }

                        if (posObj.contains("db_version") && !posObj["db_version"].toString().isEmpty()) {
                            responseText += QString("   🔹 Версія FB: %1\n").arg(posObj["db_version"].toString());
                        }

                        if (posObj.contains("posterm_version") && !posObj["posterm_version"].toString().isEmpty()) {
                            responseText += QString("   🔹 Bank DLL ver: %1\n").arg(posObj["posterm_version"].toString());
                        }

                        responseText += "\n";
                    }
                } else {
                    responseText = "ℹ️ Дані про POS відсутні.";
                }

                sendMessage(chatId, responseText);
            }
        } else {
            qWarning() << "❌ Не вдалося отримати інформацію про POS:" << posReply->errorString();
            sendMessage(chatId, "❌ Не вдалося отримати інформацію про POS.");
        }

        posReply->deleteLater();
    });
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

        lastSelectedTerminalId = terminalNumber;  // ✅ Зберігаємо вибраний термінал

        // 🔹 Виконуємо запит у Palantír
        fetchTerminalInfo(chatId, lastSelectedClientId, lastSelectedTerminalId);
    } else {
        sendMessage(chatId, "❌ Будь ласка, введіть **числовий номер терміналу**.");
    }
}


/**
 * @brief Виконує запит у Palantír для отримання інформації про термінал
 * @param chatId ID чату користувача
 * @param clientId ID вибраного клієнта
 * @param terminalId Номер терміналу
 **/
void Bot::fetchTerminalInfo(qint64 chatId, qint64 clientId, int terminalId) {
    QUrl url(QString("http://localhost:8181/terminal_info?client_id=%1&terminal_id=%2")
                 .arg(clientId).arg(terminalId));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, chatId, clientId, terminalId]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "❌ Помилка отримання даних про термінал:" << reply->errorString();
            sendMessage(chatId, "❌ Не вдалося отримати інформацію про термінал.");
            reply->deleteLater();
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
        reply->deleteLater();

        if (!jsonDoc.isObject()) {
            qWarning() << "❌ Отримано некоректний JSON!";
            sendMessage(chatId, "❌ Сталася помилка при обробці відповіді сервера.");
            return;
        }

        QJsonObject jsonObj = jsonDoc.object();

        // 🔹 Перевіряємо, чи є помилка у відповіді
        if (jsonObj.contains("error")) {
            QString errorMessage = jsonObj["error"].toString();
            qWarning() << "❌ Сервер повернув помилку:" << errorMessage;
            sendMessage(chatId, "❌ " + errorMessage);
            return;
        }

        // ✅ Отримуємо основну інформацію
        QString clientName = jsonObj["client_name"].toString();
        QString address = jsonObj["adress"].toString();
        QString phone = jsonObj["phone"].toString();
        int terminalNumber = jsonObj["terminal_id"].toInt();

        // 📌 Формуємо текстове повідомлення
        QString responseText;
        responseText += QString("🏪 <b>АЗС:</b> %1\n").arg(clientName);
        responseText += QString("⛽ <b>Термінал:</b> %1\n").arg(terminalNumber);
        responseText += QString("📍 <b>Адреса:</b> %1\n").arg(address);
        responseText += QString("📞 <b>Телефон:</b> <code>%1</code>\n\n").arg(phone);

        sendMessage(chatId, responseText);

        // 📌 Додаємо кнопки
        QJsonObject keyboard;
        QJsonArray keyboardArray;

        QJsonArray row1;
        row1.append("💳 РРО");
        row1.append("🛢 Резервуари");
        row1.append("⛽ ПРК");

        QJsonArray row2;
        row2.append("🔙 Головне меню");

        keyboardArray.append(row1);
        keyboardArray.append(row2);

        keyboard["keyboard"] = keyboardArray;
        keyboard["resize_keyboard"] = true;
        keyboard["one_time_keyboard"] = false;

        QJsonObject payload;
        payload["chat_id"] = chatId;
        payload["text"] = "Оберіть опцію:";
        payload["reply_markup"] = keyboard;

        sendMessageWithKeyboard(payload);

    });
}



// void Bot::fetchTerminalInfo(qint64 chatId, qint64 clientId, int terminalId) {
//     QUrl url(QString("http://localhost:8181/terminal_info?client_id=%1&terminal_id=%2")
//                  .arg(clientId).arg(terminalId));

//     QNetworkRequest request(url);
//     request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

//     QNetworkReply *reply = networkManager->get(request);

//     connect(reply, &QNetworkReply::finished, this, [this, reply, chatId, clientId, terminalId]() {
//         if (reply->error() != QNetworkReply::NoError) {
//             qWarning() << "❌ Помилка отримання даних про термінал:" << reply->errorString();
//             sendMessage(chatId, "❌ Не вдалося отримати інформацію про термінал.");
//             handleStartCommand(chatId);  // ⬅️ Повертаємо головне меню
//             reply->deleteLater();
//             return;
//         }

//         QByteArray responseData = reply->readAll();
//         QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData);
//         reply->deleteLater();

//         if (!jsonDoc.isObject()) {
//             qWarning() << "❌ Отримано некоректний JSON!";
//             sendMessage(chatId, "❌ Сталася помилка при обробці відповіді сервера.");
//             handleStartCommand(chatId);
//             return;
//         }

//         QJsonObject jsonObj = jsonDoc.object();

//         // 🔹 Перевіряємо, чи є помилка у відповіді
//         if (jsonObj.contains("error")) {
//             QString errorMessage = jsonObj["error"].toString();
//             qWarning() << "❌ Сервер повернув помилку:" << errorMessage;
//             sendMessage(chatId, "❌ " + errorMessage);
//             handleStartCommand(chatId);
//             return;
//         }

//         // ✅ Продовжуємо обробку
//         QString clientName = jsonObj["client_name"].toString();
//         QString adress = jsonObj["adress"].toString();
//         QString phone = jsonObj["phone"].toString();
//         QJsonArray dispensers = jsonObj["dispensers_info"].toArray();

//         // 📌 Формуємо текстове повідомлення
//         QString responseText;
//         responseText += QString("🏪 <b>АЗС:</b> %1\n").arg(clientName);
//         responseText += QString("⛽ <b>Термінал:</b> %1\n").arg(jsonObj["terminal_id"].toInt());
//         responseText += QString("📍 <b>Адреса:</b> %1\n").arg(adress);
//         responseText += QString("📞 <b>Телефон:</b> <code>%1</code>\n\n").arg(phone);



        // // 🔹 Тепер виконуємо запит до `/pos_info`
        // QUrl posUrl(QString("http://localhost:8181/pos_info?client_id=%1&terminal_id=%2")
        //                 .arg(clientId).arg(terminalId));

        // QNetworkRequest posRequest(posUrl);
        // posRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        // QNetworkReply *posReply = networkManager->get(posRequest);

        // connect(posReply, &QNetworkReply::finished, this, [this, posReply, chatId, responseText, dispensers]() mutable {
        //     if (posReply->error() == QNetworkReply::NoError) {
        //         QByteArray posData = posReply->readAll();
        //         QJsonDocument posJsonDoc = QJsonDocument::fromJson(posData);

        //         if (posJsonDoc.isObject()) {
        //             QJsonObject posJsonObj = posJsonDoc.object();
        //             QJsonArray posInfoArray = posJsonObj["pos_info"].toArray();

        //             // 🔹 Формуємо блок з інформацією про POS
        //             if (!posInfoArray.isEmpty()) {
        //                 responseText += "<b>Інформація про POS</b>\n";
        //                 for (const QJsonValue &posVal : posInfoArray) {
        //                     QJsonObject posObj = posVal.toObject();
        //                     responseText += QString("💳 <b>Каса %1</b>\n").arg(posObj["pos_id"].toInt());

        //                     // 🔹 Виводимо ЗН та ФН в одному рядку
        //                     responseText += QString("   🔹 ЗН: %1  |  ФН: %2\n")
        //                                         .arg(posObj["factorynumber"].toString())
        //                                         .arg(posObj["regnumber"].toString());

        //                     if (posObj.contains("pos_version") && !posObj["pos_version"].toString().isEmpty()) {
        //                         responseText += QString("   🔹 Версія ПО: %1\n").arg(posObj["pos_version"].toString());
        //                     }

        //                     if (posObj.contains("db_version") && !posObj["db_version"].toString().isEmpty()) {
        //                         responseText += QString("   🔹 Версія FB: %1\n").arg(posObj["db_version"].toString());
        //                     }

        //                     if (posObj.contains("posterm_version") && !posObj["posterm_version"].toString().isEmpty()) {
        //                         responseText += QString("   🔹 Bank DLL ver: %1\n").arg(posObj["posterm_version"].toString());
        //                     }

        //                     responseText += "\n";
        //                 }
        //             }

        //         }
        //     } else {
        //         qWarning() << "❌ Не вдалося отримати інформацію про POS:" << posReply->errorString();
        //     }

        //     posReply->deleteLater();

//             // 🔹 Додаємо інформацію про ТРК
//             responseText += "<b>Конфігурація ПРК</b>\n";

//             for (const QJsonValue &dispenserVal : dispensers) {
//                 QJsonObject dispenserObj = dispenserVal.toObject();
//                 int dispenserId = dispenserObj["dispenser_id"].toInt();
//                 QString protocol = dispenserObj["protocol"].toString();
//                 int port = dispenserObj["port"].toInt();
//                 int speed = dispenserObj["speed"].toInt();
//                 int address = dispenserObj["address"].toInt();

//                 responseText += QString("🔹 <b>ПРК %1:</b> %2, порт %3, швидкість %4, адреса %5\n")
//                                     .arg(dispenserId).arg(protocol).arg(port).arg(speed).arg(address);

//                 if (dispenserObj.contains("pumps_info")) {
//                     QJsonArray pumps = dispenserObj["pumps_info"].toArray();
//                     for (int i = 0; i < pumps.size(); ++i) {
//                         QJsonObject pumpObj = pumps[i].toObject();
//                         int pumpId = pumpObj["pump_id"].toInt();
//                         int tankId = pumpObj["tank_id"].toInt();
//                         QString fuel = pumpObj["fuel_shortname"].toString();

//                         if (i == pumps.size() - 1) {
//                             responseText += QString("  └ 🛠 Пістолет %1 (резервуар %2) – %3\n")
//                                                 .arg(pumpId).arg(tankId).arg(fuel);
//                         } else {
//                             responseText += QString("  ├ 🛠 Пістолет %1 (резервуар %2) – %3\n")
//                                                 .arg(pumpId).arg(tankId).arg(fuel);
//                         }
//                     }
//                 }
//             }

//             qDebug() << "📩 Відправляється повідомлення:\n" << responseText;

//             // 🔹 Відправляємо все одне повідомлення в Telegram
//             sendMessage(chatId, responseText);
//         });
//     });
// }




void Bot::handleStartCommand(qint64 chatId) {
    qInfo() << "✅ Виконання команди /start для користувача" << chatId;

    QJsonObject keyboard;
    QJsonArray keyboardArray;
    QJsonArray row1;
    QJsonArray row2;

    // Додаємо кнопки з описами замість команд
    row1.append("📋 Список клієнтів");
    row1.append("📜 Допомога");

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

    sendMessage(chatId, helpText);
}


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
            sendMessage(chatId, "? Помилка отримання даних.");
        }
        reply->deleteLater();
    });
}

void Bot::processClientsList(qint64 chatId, const QByteArray &data) {
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    if (!jsonDoc.isObject()) {
        qWarning() << "❌ Невірний формат JSON!";
        sendMessage(chatId, "❌ Сталася помилка під час отримання клієнтів.");
        return;
    }

    QJsonObject jsonObj = jsonDoc.object();
    if (!jsonObj.contains("data") || !jsonObj["data"].isArray()) {
        qWarning() << "❌ Відсутній масив data у відповіді!";
        sendMessage(chatId, "❌ Дані про клієнтів недоступні.");
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
    sendMessage(lastChatId, responseMessage);
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


void Bot::sendMessage(qint64 chatId, const QString &text, bool isHtml) {
    QUrl url(QString("https://api.telegram.org/bot%1/sendMessage").arg(botToken));

    QUrlQuery query;
    query.addQueryItem("chat_id", QString::number(chatId));
    query.addQueryItem("text", text);

    if (isHtml) {
        query.addQueryItem("parse_mode", "HTML");  // Додаємо підтримку HTML
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    manager->post(request, query.toString(QUrl::FullyEncoded).toUtf8());
}

void Bot::requestAdminApproval(qint64 userId, qint64 chatId, const QString &firstName, const QString &lastName, const QString &username) {
    QString adminsFilePath = QCoreApplication::applicationDirPath() + "/Config/admins.txt";

    QFile file(adminsFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "❌ Не вдалося відкрити admins.txt за шляхом:" << adminsFilePath;
        return;
    }

    // 🔹 Зберігаємо дані користувача для подальшого запису у `users.txt`
    lastApprovalRequest[userId] = std::make_tuple(firstName, lastName, username);

    // 🔹 Формуємо текст повідомлення
    QString userInfo = QString("🔹 Користувач %1 (Chat ID: %2)").arg(userId).arg(chatId);

    if (!firstName.isEmpty()) userInfo += QString("\n   👤 Ім'я: %1").arg(firstName);
    if (!lastName.isEmpty()) userInfo += QString("\n   🏷 Прізвище: %1").arg(lastName);
    if (!username.isEmpty()) userInfo += QString("\n   💬 Telegram: @%1").arg(username);

    userInfo += QString("\nВикористовуйте \n/approve %1 для підтвердження або \n/reject %1 для відмови.").arg(userId);

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString adminId = in.readLine().trimmed();
        if (!adminId.isEmpty()) {
            sendMessage(adminId.toLongLong(), userInfo);
        }
    }

    file.close();
}



bool Bot::isUserAuthorized(qint64 userId) {
    QString usersFilePath = QCoreApplication::applicationDirPath() + "/Config/users.txt";
    QString blacklistFilePath = QCoreApplication::applicationDirPath() + "/Config/blacklist.txt";

    // 🔹 Перевіряємо, чи userId у чорному списку
    QFile blacklistFile(blacklistFilePath);
    if (blacklistFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&blacklistFile);
        while (!in.atEnd()) {
            if (in.readLine().trimmed() == QString::number(userId)) {
                qDebug() << "❌ Користувач " << userId << " у чорному списку!";
                blacklistFile.close();
                return false;
            }
        }
        blacklistFile.close();
    }

    // 🔹 Адміністратор завжди має доступ
    if (userId == Config::instance().getAdminID().toLongLong()) {
        return true;
    }

    // 🔹 Перевіряємо `users.txt`
    QFile userFile(usersFilePath);
    if (!userFile.exists()) {
        qDebug() << "users.txt не існує, створюємо...";
        return false;
    }

    if (!userFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "❌ Не вдалося відкрити users.txt";
        return false;
    }

    QTextStream userIn(&userFile);
    while (!userIn.atEnd()) {
        QString line = userIn.readLine().trimmed();
        QStringList parts = line.split("#");
        QString storedUserId = parts[0].trimmed();  // 🔹 Видаляємо пробіли перед порівнянням

        if (storedUserId == QString::number(userId)) {
            userFile.close();
            return true;
        }
    }

    userFile.close();
    return false;
}


void Bot::handleApproveCommand(qint64 chatId, qint64 userId, const QString &text) {
    QString usersFilePath = QCoreApplication::applicationDirPath() + "/Config/users.txt";

    QStringList parts = text.split(" ");
    if (parts.size() < 2) {
        sendMessage(chatId, "❌ Невірний формат. Використовуйте: /approve <user_id>");
        return;
    }

    qint64 approvedUserId = parts[1].toLongLong();

    if (isUserAuthorized(approvedUserId)) {
        sendMessage(chatId, "✅ Користувач " + parts[1] + " вже має доступ.");
        return;
    }

    QFile file(usersFilePath);
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        sendMessage(chatId, "❌ Помилка: не вдалося оновити users.txt");
        return;
    }

    QTextStream out(&file);
    out << approvedUserId;

    // 🔹 Додаємо ім'я, прізвище та Telegram username
    QString userInfo;
    if (lastApprovalRequest.contains(approvedUserId)) {
        auto [firstName, lastName, username] = lastApprovalRequest[approvedUserId];
        if (!firstName.isEmpty()) userInfo += " " + firstName;
        if (!lastName.isEmpty()) userInfo += " " + lastName;
        if (!username.isEmpty()) userInfo += " (@" + username + ")";
    }

    if (!userInfo.isEmpty()) {
        out << " #" << userInfo.trimmed();
    }

    out << "\n";
    file.close();

    sendMessage(chatId, "✅ Користувач " + parts[1] + " успішно авторизований!");
    sendMessage(approvedUserId, "✅ Адміністратор надав вам доступ до бота.");
}




void ensureAdminExists() {
    QString adminsFilePath = QCoreApplication::applicationDirPath() + "/Config/admins.txt";

    QDir dir(QCoreApplication::applicationDirPath() + "/Config");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile file(adminsFilePath);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        qWarning() << "Failed to open admins.txt";
        return;
    }

    QTextStream in(&file);
    bool found = false;
    QString adminID = Config::instance().getAdminID();

    while (!in.atEnd()) {
        if (in.readLine().trimmed() == adminID) {
            found = true;
            break;
        }
    }

    if (!found) {
        QTextStream out(&file);
        out << adminID << "\n";
        qDebug() << "Added default admin in Config/admins.txt:" << adminID;
    }

    file.close();
}

void Bot::handleRejectCommand(qint64 chatId, qint64 userId, const QString &text) {
    QString blacklistFilePath = QCoreApplication::applicationDirPath() + "/Config/blacklist.txt";

    QStringList parts = text.split(" ");
    if (parts.size() < 2) {
        sendMessage(chatId, "❌ Невірний формат. Використовуйте: /reject <user_id>");
        return;
    }

    qint64 rejectedUserId = parts[1].toLongLong();

    // Перевіряємо, чи користувач уже в blacklist.txt
    QFile file(blacklistFilePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            if (in.readLine().trimmed() == QString::number(rejectedUserId)) {
                sendMessage(chatId, "❌ Користувач " + parts[1] + " уже заблокований.");
                file.close();
                return;
            }
        }
        file.close();
    }

    // Додаємо userId у blacklist.txt
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        sendMessage(chatId, "❌ Помилка: не вдалося оновити blacklist.txt");
        return;
    }

    QTextStream out(&file);
    out << rejectedUserId << "\n";
    file.close();

    sendMessage(chatId, "🚫 Користувач " + parts[1] + " заблокований.");
}
