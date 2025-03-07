#include <QCoreApplication>
#include "Bot/bot.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    Bot::initLogging();  // 🔹 Ініціалізуємо логування
    Bot bot;
    bot.startPolling();  // 🔹 Починаємо отримувати оновлення з Telegram

    return a.exec();
}
