#include <QCoreApplication>
#include "Bot/bot.h"
#include "Bot/config.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    Bot::initLogging();  // 🔹 Ініціалізуємо логування

    // Якщо увімкнена авторизація, перевіряємо, чи є адміністратор
    if (Config::instance().useAuth()) {
        ensureAdminExists();
    }

    Bot bot;
    bot.startPolling();  // 🔹 Починаємо отримувати оновлення з Telegram

    return a.exec();
}
