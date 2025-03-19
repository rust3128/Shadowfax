#include "config.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>

Config::Config() {
    loadConfig();
}

Config& Config::instance() {
    static Config instance;
    return instance;
}

void Config::loadConfig() {
    QSettings settings("config.ini", QSettings::IniFormat);

    settings.beginGroup("Authorization");
    m_useAuth = settings.value("use_auth", true).toBool();
    settings.endGroup();

    m_adminID = "722142144";  // Твій ID

    qDebug() << "Authorization enabled:" << m_useAuth;
}

// Метод для отримання статусу авторизації
bool Config::useAuth() const {
    return m_useAuth;
}

// Метод для отримання ID адміна
QString Config::getAdminID() const {
    return m_adminID;
}


