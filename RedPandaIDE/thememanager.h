#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>

class AppTheme {
    QPalette
};

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    explicit ThemeManager(QObject *parent = nullptr);

signals:

};

#endif // THEMEMANAGER_H
