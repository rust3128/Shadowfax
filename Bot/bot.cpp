#include "bot.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QDebug>

Bot::Bot(QObject *parent) : QObject(parent) {
    lastUpdateId = 0;  // Ініціалізуємо update_id
    networkManager = new QNetworkAccessManager(this);
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
            qWarning() << "❌ Помилка мережі: " << reply->errorString();

            if (reply->errorString().contains("Conflict")) {
                qWarning() << "🔴 Виявлено конфлікт! Перевір Webhook!";
                // Автоматично вимикаємо Webhook
                QNetworkRequest webhookRequest(QUrl(QString("https://api.telegram.org/bot%1/deleteWebhook?drop_pending_updates=true")
                                                        .arg(botToken)));
                networkManager->get(webhookRequest);
            }

            reply->deleteLater();
            QTimer::singleShot(5000, this, &Bot::getUpdates);  // Повтор через 5 сек
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
        QJsonObject jsonObject = jsonResponse.object();

        if (!jsonObject["ok"].toBool()) {
            qWarning() << "⚠️ Telegram API повернуло помилку!";
            reply->deleteLater();
            return;
        }

        QJsonArray updates = jsonObject["result"].toArray();
        for (const QJsonValue &update : updates) {
            QJsonObject updateObj = update.toObject();
            qint64 updateId = updateObj["update_id"].toVariant().toLongLong();

            if (updateId > lastUpdateId) {
                lastUpdateId = updateId;  // ✅ Оновлюємо, щоб уникнути повторів

                QJsonObject message = updateObj["message"].toObject();
                QString text = message["text"].toString();
                qint64 chatId = message["chat"].toObject()["id"].toVariant().toLongLong();

                qDebug() << "📩 Отримано повідомлення від" << chatId << ":" << text;

                sendMessage(chatId, "Ви написали: " + text);
            }
        }

        reply->deleteLater();
        QTimer::singleShot(2000, this, &Bot::getUpdates);  // Запускаємо знову через 2 секунди
    });
}

void Bot::sendMessage(qint64 chatId, const QString &text) {
    QString url = QString("https://api.telegram.org/bot%1/sendMessage")
    .arg(botToken);

    QJsonObject payload;
    payload["chat_id"] = chatId;
    payload["text"] = text;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->post(request, QJsonDocument(payload).toJson());

    connect(reply, &QNetworkReply::finished, this, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "⚠️ Помилка при відправці повідомлення: " << reply->errorString();
        } else {
            qDebug() << "✅ Повідомлення відправлено!";
        }
        reply->deleteLater();
    });
}
