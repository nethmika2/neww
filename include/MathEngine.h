#pragma once
#include <Arduino.h>
#include "Types.h"

bool isDigitChar(char c);
bool isAlphaChar(char c);
bool isVarChar(char c);

String niceNum(double v);
bool hasParamToken(const String& eq);
void flagActiveVariables(String eq);
void refreshActiveVariables();
String fixEquation(String eq);
void compileSlot(int i);
bool parsePoint(String s, double& x, double& y);

// Point entry (numbers, functions and parameter letters)
bool compilePointInput(String s, te_expr*& cx, te_expr*& cy, String& left, String& right, bool& live);
void freePointExprs(int idx);
void rebuildPointExprs(int idx);
bool evalPoint(int idx, double& x, double& y);

int worldXToScreen(double wx);
int worldYToScreen(double wy);
double screenXToWorld(int sx);
double screenYToWorld(int sy);
void zoomAt(double factor, int sx, int sy);

double getNiceStep(double range);
double getPiStep(double range);
String formatTick(double val, double step, bool isPi);
