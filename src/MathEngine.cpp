#include "MathEngine.h"
#include <math.h>
#include "Globals.h"

bool isDigitChar(char c) {
  return c >= '0' && c <= '9';
}

bool isAlphaChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isVarChar(char c) {
  return c == 'x' || c == 'y' || c == 't' || c == 'a' || c == 'b' || c == 'c' || c == 'k' || c == 'm' || c == 'n' || c == 'p' || c == 'q' || c == 'e';
}

String niceNum(double v) {
  if (fabs(v - round(v)) < 1e-9) return String((long)round(v));
  String s = String(v, 3);
  while (s.endsWith("0")) s.remove(s.length() - 1);
  if (s.endsWith(".")) s.remove(s.length() - 1);
  return s;
}

// True when one of the slider letters appears in `eq` as a whole token, so the
// `a` of tan() or the `c` of cos() is not mistaken for a parameter.
bool hasParamToken(const String& eq) {
  for (int v = 0; v < NUM_CUSTOM_VARS; v++) {
    const String& name = sliders[v].name;
    for (int at = eq.indexOf(name); at >= 0; at = eq.indexOf(name, at + name.length())) {
      bool leftIsWord = at > 0 && (isAlphaChar(eq[at - 1]) || isDigitChar(eq[at - 1]) || eq[at - 1] == '_');
      int after = at + name.length();
      bool rightIsWord = after < (int)eq.length() && (isAlphaChar(eq[after]) || isDigitChar(eq[after]) || eq[after] == '_');
      if (!leftIsWord && !rightIsWord) return true;
    }
  }
  return false;
}

void flagActiveVariables(String eq) {
  if (!hasParamToken(eq)) return;
  for (int v = 0; v < NUM_CUSTOM_VARS; v++) {
    const String& name = sliders[v].name;
    for (int at = eq.indexOf(name); at >= 0; at = eq.indexOf(name, at + name.length())) {
      bool leftIsWord = at > 0 && (isAlphaChar(eq[at - 1]) || isDigitChar(eq[at - 1]) || eq[at - 1] == '_');
      int after = at + name.length();
      bool rightIsWord = after < (int)eq.length() && (isAlphaChar(eq[after]) || isDigitChar(eq[after]) || eq[after] == '_');
      if (!leftIsWord && !rightIsWord) {
        sliders[v].in_use = true;
        break;
      }
    }
  }
}

void refreshActiveVariables() {
  for (int v = 0; v < NUM_CUSTOM_VARS; v++) sliders[v].in_use = false;
  for (int i = 0; i < NUM_FUNCS; i++) {
    if (funcs[i].input.length()) flagActiveVariables(funcs[i].input);
  }
  // Points typed with parameters light up the same sliders, so a dot added as
  // (2m, c) can be driven from the VAR panel (or the play button).
  for (int i = 0; i < numPoints; i++) {
    if (!points[i].live) continue;
    flagActiveVariables(points[i].exprX);
    flagActiveVariables(points[i].exprY);
  }
}

static bool isFunctionMarker(char c) {
  return c >= '\x01' && c <= '\x0C';
}

String fixEquation(String eq) {
  String res = eq;
  // Convert standard inverse
  res.replace("sin-1", "asin");
  res.replace("cos-1", "acos");
  res.replace("tan-1", "atan");
  res.replace("cosec", "1/sin");
  res.replace("sec", "1/cos");
  res.replace("cot", "1/tan");

  // Protect math words from being split into variables by substituting them temporarily
  res.replace("asin", "\x01");
  res.replace("acos", "\x02");
  res.replace("atan", "\x03");
  res.replace("sin", "\x04");
  res.replace("cos", "\x05");
  res.replace("tan", "\x06");
  res.replace("sqrt", "\x07");
  res.replace("ln", "\x08");
  res.replace("abs", "\x09");
  res.replace("pi", "\x0A");
  res.replace("log", "\x0B");
  res.replace("exp", "\x0C");

  String final_res = "";
  for (int i = 0; i < res.length(); i++) {
    char c1 = res[i];
    final_res += c1;
    if (i + 1 < res.length()) {
      char c2 = res[i + 1];
      bool addStar = false;
      // Note: the control characters are temporary function markers.  A
      // marker followed by '(' is a call (sin(x), sqrt(x)), not a product.
      if (isDigitChar(c1) && (isVarChar(c2) || c2 == '(' || isFunctionMarker(c2))) addStar = true;
      if (isVarChar(c1) && (isDigitChar(c2) || isVarChar(c2) || c2 == '(' || isFunctionMarker(c2))) addStar = true;
      if (isFunctionMarker(c1) && c2 != '(' && (isDigitChar(c2) || isVarChar(c2) || isFunctionMarker(c2))) addStar = true;
      if (c1 == ')' && (isDigitChar(c2) || isVarChar(c2) || c2 == '(' || isFunctionMarker(c2))) addStar = true;
      if (addStar) final_res += '*';
    }
  }

  // Restore math words
  final_res.replace("\x01", "asin");
  final_res.replace("\x02", "acos");
  final_res.replace("\x03", "atan");
  final_res.replace("\x04", "sin");
  final_res.replace("\x05", "cos");
  final_res.replace("\x06", "tan");
  final_res.replace("\x07", "sqrt");
  final_res.replace("\x08", "ln");
  final_res.replace("\x09", "abs");
  final_res.replace("\x0A", "pi");
  final_res.replace("\x0B", "log");
  final_res.replace("\x0C", "exp");
  return final_res;
}

void compileSlot(int i) {
  if (funcs[i].exprX) {
    te_free(funcs[i].exprX);
    funcs[i].exprX = nullptr;
  }
  if (funcs[i].exprY) {
    te_free(funcs[i].exprY);
    funcs[i].exprY = nullptr;
  }
  String raw = funcs[i].input;
  raw.trim();
  if (raw.length() == 0) {
    funcs[i].type = EQ_EMPTY;
    return;
  }
  String fixed = fixEquation(raw);
  int err;

  // 1. Parametric or Point: (t, t^2) or (2, 3)
  if (fixed.startsWith("(") && fixed.endsWith(")") && fixed.indexOf(',') > 0) {
    String inner = fixed.substring(1, fixed.length() - 1);
    int comma = inner.indexOf(',');
    funcs[i].exprX = te_compile(inner.substring(0, comma).c_str(), vars, NUM_TE_VARS, &err);
    funcs[i].exprY = te_compile(inner.substring(comma + 1).c_str(), vars, NUM_TE_VARS, &err);
    funcs[i].type = (fixed.indexOf('t') >= 0) ? EQ_PARAMETRIC : EQ_POINT;
    if (!funcs[i].exprX || !funcs[i].exprY) {
      te_free(funcs[i].exprX);
      te_free(funcs[i].exprY);
      funcs[i].exprX = nullptr;
      funcs[i].exprY = nullptr;
    }
    flagActiveVariables(fixed);
    return;
  }

  // 2. Implicit or Explicit y=/x=
  int eqPos = fixed.indexOf('=');
  if (eqPos >= 0) {
    String lhs = fixed.substring(0, eqPos);
    lhs.trim();
    String rhs = fixed.substring(eqPos + 1);
    if (lhs == "y") {
      funcs[i].type = EQ_EXPLICIT;
      funcs[i].exprX = te_compile(rhs.c_str(), vars, NUM_TE_VARS, &err);
    } else {
      funcs[i].type = EQ_IMPLICIT;
      String impl = "(" + lhs + ")-(" + rhs + ")";
      funcs[i].exprX = te_compile(impl.c_str(), vars, NUM_TE_VARS, &err);
    }
    flagActiveVariables(fixed);
    return;
  }

  // 3. Fallback: standard f(x)
  funcs[i].type = EQ_EXPLICIT;
  funcs[i].exprX = te_compile(fixed.c_str(), vars, NUM_TE_VARS, &err);
  flagActiveVariables(fixed);
}

// ==========================================
// POINT ENTRY (numbers, functions and parameters)
// ==========================================
// Points are evaluated against the parameter sliders only.  x/y/t are left out
// on purpose: they describe the plot cursor, and a dot bound to them would
// wander off on its own.
static te_variable point_vars[NUM_CUSTOM_VARS] = {
  { "a", &sliders[0].value }, { "b", &sliders[1].value }, { "c", &sliders[2].value }, { "k", &sliders[3].value },
  { "m", &sliders[4].value }, { "n", &sliders[5].value }, { "p", &sliders[6].value }, { "q", &sliders[7].value }
};

// First comma that is not inside brackets, so function calls such as
// "(max(a,b), 3)" still split in the right place.  -1 when there is none.
static int topLevelComma(const String& s) {
  int depth = 0;
  for (int i = 0; i < (int)s.length(); i++) {
    char c = s[i];
    if (c == '(') depth++;
    else if (c == ')') depth--;
    else if (c == ',' && depth <= 0) return i;
  }
  return -1;
}

bool compilePointInput(String s, te_expr*& cx, te_expr*& cy, String& left, String& right, bool& live) {
  cx = nullptr;
  cy = nullptr;
  s.trim();
  // The input box already draws the surrounding parentheses; accept them typed
  // as well so "(2m, 3)" and "2m, 3" behave the same.
  while (s.length() >= 2 && s[0] == '(' && s[s.length() - 1] == ')') {
    int depth = 0;
    bool whole = true;
    for (int i = 0; i < (int)s.length(); i++) {
      if (s[i] == '(') depth++;
      else if (s[i] == ')') {
        depth--;
        if (depth == 0 && i != (int)s.length() - 1) {
          whole = false;
          break;
        }
      }
    }
    if (!whole || depth != 0) break;
    s = s.substring(1, s.length() - 1);
    s.trim();
  }
  int c = topLevelComma(s);
  if (c < 0) return false;
  left = s.substring(0, c);
  right = s.substring(c + 1);
  left.trim();
  right.trim();
  if (left.length() == 0 || right.length() == 0) return false;

  // Implicit products (2m, 3c) are expanded first, which is also what makes the
  // parameter letters detectable as tokens.
  String fixedX = fixEquation(left);
  String fixedY = fixEquation(right);
  int err;
  cx = te_compile(fixedX.c_str(), point_vars, NUM_CUSTOM_VARS, &err);
  if (!cx) return false;
  cy = te_compile(fixedY.c_str(), point_vars, NUM_CUSTOM_VARS, &err);
  if (!cy) {
    te_free(cx);
    cx = nullptr;
    return false;
  }
  live = hasParamToken(fixedX) || hasParamToken(fixedY);
  return true;
}

void freePointExprs(int idx) {
  if (idx < 0 || idx >= MAX_POINTS) return;
  if (points[idx].compX) te_free(points[idx].compX);
  if (points[idx].compY) te_free(points[idx].compY);
  points[idx].compX = nullptr;
  points[idx].compY = nullptr;
  points[idx].live = false;
}

// Rebuild the compiled form of a stored point (used after loading from NVS).
void rebuildPointExprs(int idx) {
  if (idx < 0 || idx >= MAX_POINTS) return;
  freePointExprs(idx);
  if (points[idx].exprX.length() == 0 || points[idx].exprY.length() == 0) return;
  String whole = points[idx].exprX + "," + points[idx].exprY;
  te_expr* cx = nullptr;
  te_expr* cy = nullptr;
  String left, right;
  bool live = false;
  if (!compilePointInput(whole, cx, cy, left, right, live)) return;
  points[idx].compX = cx;
  points[idx].compY = cy;
  points[idx].live = live;
}

// Current position of a point.  Parameter points are re-evaluated on every
// redraw, which is what makes them follow the sliders (and the play button).
bool evalPoint(int idx, double& x, double& y) {
  if (idx < 0 || idx >= MAX_POINTS) return false;
  if (points[idx].live && points[idx].compX && points[idx].compY) {
    double px = te_eval(points[idx].compX);
    double py = te_eval(points[idx].compY);
    if (isnan(px) || isinf(px) || isnan(py) || isinf(py)) return false;
    points[idx].x = px;
    points[idx].y = py;
  }
  x = points[idx].x;
  y = points[idx].y;
  return !isnan(x) && !isinf(x) && !isnan(y) && !isinf(y);
}

bool parsePoint(String s, double& x, double& y) {
  te_expr* cx = nullptr;
  te_expr* cy = nullptr;
  String left, right;
  bool live = false;
  if (!compilePointInput(s, cx, cy, left, right, live)) return false;
  x = te_eval(cx);
  y = te_eval(cy);
  te_free(cx);
  te_free(cy);
  return !isnan(x) && !isinf(x) && !isnan(y) && !isinf(y);
}

int worldXToScreen(double wx) {
  return (int)constrain(160.0 + (wx - centerWorldX) * zoom, -30000, 30000);
}

int worldYToScreen(double wy) {
  return (int)constrain(120.0 - (wy - centerWorldY) * zoom, -30000, 30000);
}

double screenXToWorld(int sx) {
  return centerWorldX + (sx - 160) / zoom;
}

double screenYToWorld(int sy) {
  return centerWorldY - (sy - 120) / zoom;
}

void zoomAt(double factor, int sx, int sy) {
  factor = constrain(factor, 0.01, 100.0);
  double keepX = screenXToWorld(sx);
  double keepY = screenYToWorld(sy);
  zoom = constrain(zoom * factor, 0.5, 4000.0);
  // Preserve the world coordinate under the cursor.  This small detail makes
  // the +/- controls feel like a real graphing calculator instead of simply
  // stretching the graph around an unrelated origin.
  centerWorldX = keepX - (sx - 160) / zoom;
  centerWorldY = keepY + (sy - 120) / zoom;
}

double getNiceStep(double range) {
  double p = pow(10.0, floor(log10(range)));
  double f = range / p;
  if (f < 1.5) return 1.0 * p;
  if (f < 3.0) return 2.0 * p;
  if (f < 7.0) return 5.0 * p;
  return 10.0 * p;
}

double getPiStep(double range) {
  double pi_range = range / PI;
  if (pi_range < 0.3) return 0.25 * PI;
  if (pi_range < 0.7) return 0.5 * PI;
  if (pi_range < 1.5) return 1.0 * PI;
  if (pi_range < 3.0) return 2.0 * PI;
  if (pi_range < 7.0) return 5.0 * PI;
  return 10.0 * PI;
}

String formatTick(double val, double step, bool isPi) {
  if (abs(val) < 0.001) return "0";
  if (!isPi) {
    if (step >= 1.0) return String((int)round(val));
    else if (step >= 0.1) return String(val, 1);
    else return String(val, 2);
  } else {
    double pval = val / PI;
    if (abs(pval - 1.0) < 0.05) return "pi";
    if (abs(pval + 1.0) < 0.05) return "-pi";
    if (abs(pval - 0.5) < 0.05) return "pi/2";
    if (abs(pval + 0.5) < 0.05) return "-pi/2";
    if (abs(pval - 0.25) < 0.05) return "pi/4";
    if (abs(pval + 0.25) < 0.05) return "-pi/4";
    if (step >= PI) return String((int)round(pval)) + "pi";
    return String(pval, 1) + "pi";
  }
}
