#pragma once
#include <Arduino.h>
#include "Types.h"

bool isDigitChar(char c);
bool isAlphaChar(char c);
bool isVarChar(char c);

String niceNum(double v);
void flagActiveVariables(String eq);
void refreshActiveVariables();
String fixEquation(String eq);
void compileSlot(int i);
bool parsePoint(String s, double& x, double& y);

int worldXToScreen(double wx);
int worldYToScreen(double wy);
double screenXToWorld(int sx);
double screenYToWorld(int sy);
void zoomAt(double factor, int sx, int sy);

double getNiceStep(double range);
double getPiStep(double range);
String formatTick(double val, double step, bool isPi);
