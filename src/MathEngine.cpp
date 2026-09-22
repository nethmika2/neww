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

void flagActiveVariables(String eq) {
  // Only match a variable as a token.  A plain indexOf() marks the `a` in
  // tan() and the `c` in cos() as sliders, which makes the variable panel
  // noisy and is very unlike a graphing calculator.
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

bool parsePoint(String s, double& x, double& y) {
  int c = s.indexOf(',');
  if (c < 0) return false;
  String a = s.substring(0, c), b = s.substring(c + 1);
  a.trim();
  b.trim();
  if (a.length() == 0 || b.length() == 0 || b.indexOf(',') >= 0) return false;
  int err;
  x = te_interp(a.c_str(), &err);
  if (err) return false;
  y = te_interp(b.c_str(), &err);
  if (err) return false;
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
