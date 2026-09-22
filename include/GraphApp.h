#pragma once
#include <Arduino.h>

void drawHoles(int i, int topY);
void drawPoints(int topY);
void drawTopBar();
void drawFunctionTabs();
void drawBottomControls();
void drawGraphScreen(bool fullWipe);

void toggleVarAnimation();
void stopVarAnimation();
void cycleAnimSpeed();
void tickVarAnimation();

void handleGraphTouch(bool touched, int sx, int sy);
void handleTabTouch(int sx, int sy);
void holdZoom(double factor, int rx, int ry, int rw, int rh);
