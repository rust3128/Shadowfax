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

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Помилка мережі:" << reply->errorString();
            reply->deleteLater();
            QTimer::singleShot(5000, this, &Bot::getUpdates);
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
        QJsonObject jsonObject = jsonResponse.object();

        if (!jsonObject["ok"].toBool()) {
            qWarning() << "Telegram API повернуло помилку!";
            reply->deleteLater();
            return;
        }

        QJsonArray updates = jsonObject["result"].toArray();
        for (const QJsonValue &update : updates) {
            QJsonObject updateObj = update.toObject();
            qint64 updateId = updateObj["update_id"].toVariant().toLongLong();

            if (updateId > lastUpdateId) {
                lastUpdateId = updateId;

                QJsonObject message = updateObj["message"].toObject();
                QString text = message["text"].toString();
                qint64 chatId = message["chat"].toObject()["id"].toVariant().toLongLong();

                qInfo() << "📩 Отримано повідомлення від" << chatId << ":" << text;

                if (text == "/start") {
                    handleStartCommand(chatId);
                } else if (text == "/help") {
                    handleHelpCommand(chatId);
                } else {
                    sendMessage(chatId, "Ви написали: " + text);
                }
            }
        }

        reply->deleteLater();
        QTimer::singleShot(2000, this, &Bot::getUpdates);
    });
}


void Bot::handleStartCommand(qint64 chatId) {
    qInfo() << "✅ Виконання команди /start для користувача" << chatId;
    sendMessage(chatId, "Привіт! Я бот Shadowfax. Чим можу допомогти?");
}

void Bot::handleHelpCommand(qint64 chatId) {
    qInfo() << "✅ Виконання команди /help для користувача" << chatId;

    QString helpText = "❓ Доступні команди:\n"
                       "/start - Почати взаємодію з ботом\n"
                       "/help - Показати список команд\n";

    sendMessage(chatId, helpText);
}


void Bot::sendMessage(qint64 chatId, const QString &text) {
    QString url = QString("https://api.telegram.org/bot%1/sendMessage")
    .arg(botToken);

    QJsonObject payload;
    payload["chat_id"] = chatId;
    payload["text"] = text;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qDebug() << "📤 Відправка повідомлення...";
    qDebug() << "🔹 Chat ID:" << chatId;
    qDebug() << "🔹 Текст:" << text;

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
