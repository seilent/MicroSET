#pragma once

class QWindow;

namespace microset {

void claimGamescopeFocus(QWindow *window);
bool markOverlay(QWindow *window);
bool setOverlayInput(QWindow *window, bool input);
void exitCleanlyOnDisplayLoss();
bool displayGone();

}
