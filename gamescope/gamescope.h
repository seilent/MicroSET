#pragma once

class QWindow;

namespace microset {

void claimGamescopeFocus(QWindow *window);
bool markOverlay(QWindow *window);

}
