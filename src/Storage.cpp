#include "Storage.h"
#include "Globals.h"

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
  }
}

void loadPoints() {
  numPoints = constrain(prefs.getInt("npts", 0), 0, MAX_POINTS);
  for (int i = 0; i < numPoints; i++) {
    points[i].x = prefs.getDouble(("px" + String(i)).c_str(), 0);
    points[i].y = prefs.getDouble(("py" + String(i)).c_str(), 0);
  }
}
