#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QSettings>

void ensureAdminExists();

class Config {
public:
    static Config& instance();  // Синглтон
    void loadConfig();          // Завантаження конфігурації

    bool useAuth() const;       // Чи включена авторизація
    QString getAdminID() const; // ID адміністратора за замовчуванням

private:
    Config();  // Приватний конструктор для синглтона
    ~Config() = default;

    bool isUserAuthorized(const QString& userId);


    bool m_useAuth;   // Флаг авторизації
    QString m_adminID; // ID адміна за замовчуванням


};

#endif // CONFIG_H
