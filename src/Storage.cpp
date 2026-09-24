#include "Storage.h"
#include "Globals.h"
#include "MathEngine.h"

bool sdLockBegin(uint32_t timeoutMs) {
  if (!audioMutex) return true;                 // audio never started
  return xSemaphoreTake(audioMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void sdLockEnd() {
  if (audioMutex) xSemaphoreGive(audioMutex);
}

void saveFunctions() {
  for (int i = 0; i < NUM_FUNCS; i++) {
    prefs.putString(("eq" + String(i)).c_str(), funcs[i].input);
    prefs.putBool(("vis" + String(i)).c_str(), funcs[i].visible);
  }
}

void loadFunctions() {
  for (int i = 0; i < NUM_FUNCS; i++) {
    funcs[i].input = prefs.getString(("eq" + String(i)).c_str(), funcs[i].input);
    funcs[i].visible = prefs.getBool(("vis" + String(i)).c_str(), funcs[i].visible);
  }
}

void savePoints() {
  prefs.putInt("npts", numPoints);
  for (int i = 0; i < numPoints; i++) {
    prefs.putDouble(("px" + String(i)).c_str(), points[i].x);
    prefs.putDouble(("py" + String(i)).c_str(), points[i].y);
    // Keep the typed halves so a point built from parameters (2m, c+1) is
    // still parameter driven after a reboot.
    prefs.putString(("pex" + String(i)).c_str(), points[i].exprX);
    prefs.putString(("pey" + String(i)).c_str(), points[i].exprY);
  }
}

void loadPoints() {
  numPoints = constrain(prefs.getInt("npts", 0), 0, MAX_POINTS);
  for (int i = 0; i < numPoints; i++) {
    points[i].x = prefs.getDouble(("px" + String(i)).c_str(), 0);
    points[i].y = prefs.getDouble(("py" + String(i)).c_str(), 0);
    points[i].exprX = prefs.getString(("pex" + String(i)).c_str(), "");
    points[i].exprY = prefs.getString(("pey" + String(i)).c_str(), "");
    rebuildPointExprs(i);
  }
}

void saveVariables() {
  for (int i = 0; i < NUM_CUSTOM_VARS; i++) {
    prefs.putDouble(("v" + String(i)).c_str(), sliders[i].value);
  }
}

void loadVariables() {
  for (int i = 0; i < NUM_CUSTOM_VARS; i++) {
    sliders[i].value = constrain(
      prefs.getDouble(("v" + String(i)).c_str(), sliders[i].value),
      sliders[i].min_val,
      sliders[i].max_val);
  }
}
