#include "env.h"

Env::Env(QObject *parent)
    : QObject(parent)
{
}

QString Env::fontDir() const
{
    const QString dir = qEnvironmentVariable("MICROSET_FONT_DIR");
    return dir.isEmpty() ? QStringLiteral(MICROSET_UI_FONT_DIR) : dir;
}

int Env::landscapeTurn() const
{
    return 1;
}
