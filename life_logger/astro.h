// astro.h  Life Logger: sun and moon for the SUN & MOON screen, worked out on the box from a position and the clock.
// Sun: the Astronomical Almanac's low-precision formulas (about 0.01 deg). Moon: Paul Schlyter's orbital
// elements with the main perturbations (about 2 arcmin). Rise and set are found by stepping the altitude in
// 10-minute steps and halving the step where it crosses the line, down to a few seconds, so the times shown
// (to the minute) come out within a minute or so. No Arduino calls: the same file builds on a computer for testing.
// The ESP32-S2 has no floating-point unit, so the sun's day is sampled once for all its events (about 0.05 s)
// and the moon's rise and set are worked out only when needed (about 0.2 s); the caller keeps the answers.
#pragma once
#include <math.h>
#include <time.h>

namespace astro {

const double D2R = M_PI / 180.0, R2D = 180.0 / M_PI;
inline double rev(double x) { x = fmod(x, 360.0); return x < 0 ? x + 360.0 : x; }
inline double sind(double x) { return sin(x * D2R); }
inline double cosd(double x) { return cos(x * D2R); }
inline double jdOf(time_t t) { return (double)t / 86400.0 + 2440587.5; }

struct Pos { double ra, dec, lon, lat, hp; };   // degrees; lon/lat ecliptic of date; hp: horizontal parallax (moon)

inline void eqOf(double lon, double lat, double eps, Pos& p) {
  double x = cosd(lat) * cosd(lon), y = cosd(lat) * sind(lon), z = sind(lat);
  double ye = y * cosd(eps) - z * sind(eps), ze = y * sind(eps) + z * cosd(eps);
  p.ra = rev(atan2(ye, x) * R2D); p.dec = atan2(ze, sqrt(x * x + ye * ye)) * R2D;
}

inline Pos sunPos(time_t t) {
  double n = jdOf(t) - 2451545.0;
  double L = rev(280.460 + 0.9856474 * n), g = rev(357.528 + 0.9856003 * n);
  Pos p; p.lon = rev(L + 1.915 * sind(g) + 0.020 * sind(2 * g)); p.lat = 0; p.hp = 0;
  eqOf(p.lon, 0, 23.439 - 0.0000004 * n, p);
  return p;
}

inline Pos moonPos(time_t t) {
  double d = jdOf(t) - 2451543.5;
  double N = rev(125.1228 - 0.0529538083 * d), i = 5.1454, w = rev(318.0634 + 0.1643573223 * d);
  double a = 60.2666, e = 0.054900, M = rev(115.3654 + 13.0649929509 * d);
  double E = M + R2D * e * sind(M) * (1.0 + e * cosd(M));
  for (int k = 0; k < 5; k++) E = E - (E - R2D * e * sind(E) - M) / (1 - e * cosd(E));
  double xv = a * (cosd(E) - e), yv = a * sqrt(1 - e * e) * sind(E);
  double v = atan2(yv, xv) * R2D, r = sqrt(xv * xv + yv * yv);
  double xh = r * (cosd(N) * cosd(v + w) - sind(N) * sind(v + w) * cosd(i));
  double yh = r * (sind(N) * cosd(v + w) + cosd(N) * sind(v + w) * cosd(i));
  double zh = r * sind(v + w) * sind(i);
  double lon = atan2(yh, xh) * R2D, lat = atan2(zh, sqrt(xh * xh + yh * yh)) * R2D;
  double Ms = rev(356.0470 + 0.9856002585 * d), ws = rev(282.9404 + 4.70935e-5 * d);
  double Ls = Ms + ws, Lm = M + w + N, D = Lm - Ls, F = Lm - N;
  lon += -1.274 * sind(M - 2 * D) + 0.658 * sind(2 * D) - 0.186 * sind(Ms) - 0.059 * sind(2 * M - 2 * D)
         - 0.057 * sind(M - 2 * D + Ms) + 0.053 * sind(M + 2 * D) + 0.046 * sind(2 * D - Ms) + 0.041 * sind(M - Ms)
         - 0.035 * sind(D) - 0.031 * sind(M + Ms) - 0.015 * sind(2 * F - 2 * D) + 0.011 * sind(M - 4 * D);
  lat += -0.173 * sind(F - 2 * D) - 0.055 * sind(M - F - 2 * D) - 0.046 * sind(M + F - 2 * D)
         + 0.033 * sind(F + 2 * D) + 0.017 * sind(2 * M + F);
  r += -0.58 * cosd(M - 2 * D) - 0.46 * cosd(2 * D);
  Pos p; p.lon = rev(lon); p.lat = lat; p.hp = asin(1.0 / r) * R2D;
  eqOf(p.lon, p.lat, 23.4393 - 3.563e-7 * d, p);
  return p;
}

inline double altOf(const Pos& p, double lat, double lon, time_t t) {
  double gmst = rev(280.46061837 + 360.98564736629 * (jdOf(t) - 2451545.0));
  double ha = gmst + lon - p.ra;
  return asin(sind(lat) * sind(p.dec) + cosd(lat) * cosd(p.dec) * cosd(ha)) * R2D;
}
inline double sunAlt(double lat, double lon, time_t t) { return altOf(sunPos(t), lat, lon, t); }
// the moon's altitude above the height at which its top edge is on the horizon (refraction, size, parallax)
inline double moonRel(double lat, double lon, time_t t) { Pos p = moonPos(t); return altOf(p, lat, lon, t) - (0.7275 * p.hp - 0.5667); }

// first crossing of f = 0 between t0 and t1 (either direction in time); dir +1 rising, -1 setting; 0 if none
template <class Fn> time_t cross(Fn f, time_t t0, time_t t1, int dir, long STEP = 600) {
  long s = t1 >= t0 ? STEP : -STEP;
  double a = f(t0);
  for (time_t t = t0; s > 0 ? t < t1 : t > t1; t += s) {
    time_t u = s > 0 ? (t + s > t1 ? t1 : t + s) : (t + s < t1 ? t1 : t + s);
    double b = f(u);
    time_t lo = s > 0 ? t : u, hi = s > 0 ? u : t; double flo = s > 0 ? a : b, fhi = s > 0 ? b : a;
    if ((dir > 0 && flo < 0 && fhi >= 0) || (dir < 0 && flo >= 0 && fhi < 0)) {
      while (hi - lo > 4) { time_t m = lo + (hi - lo) / 2; double fm = f(m); if ((fm >= 0) == (fhi >= 0)) { hi = m; fhi = fm; } else lo = m; }
      return lo + (hi - lo) / 2;
    }
    a = b;
  }
  return 0;
}

struct SunDay { time_t rise = 0, set = 0, cDawn = 0, cDusk = 0, nDawn = 0, nDusk = 0, noon = 0; float noonAlt = NAN; int8_t all = 0; };   // 0 = doesn't happen that day; all: +1 up all day, -1 down all day
// t0, t1: the start and end of the local day
inline SunDay sunDay(double lat, double lon, time_t t0, time_t t1) {
  SunDay o;
  const int NS = 152; float a[NS]; int n0 = 0;             // the altitude every 10 minutes through the day (23 to 25 h)
  for (time_t t = t0; t <= t1 + 599 && n0 < NS; t += 600) a[n0++] = (float)sunAlt(lat, lon, t < t1 ? t : t1);
  auto find = [&](double h, int dir) -> time_t {
    for (int k = 0; k + 1 < n0; k++) {
      bool up = a[k] - h < 0 && a[k + 1] - h >= 0, dn = a[k] - h >= 0 && a[k + 1] - h < 0;
      if (!(dir > 0 ? up : dn)) continue;
      time_t lo = t0 + k * 600L, hi = lo + 600 < t1 ? lo + 600 : t1;
      return cross([=](time_t t) { return sunAlt(lat, lon, t) - h; }, lo, hi, dir, 600);
    }
    return 0;
  };
  o.rise = find(-0.833, +1); o.set = find(-0.833, -1);
  o.cDawn = find(-6, +1); o.cDusk = find(-6, -1);
  o.nDawn = find(-12, +1); o.nDusk = find(-12, -1);
  // noon: the hour angle is zero; start from the middle of the day and correct it a few times
  time_t n = t0 + (t1 - t0) / 2;
  for (int k = 0; k < 4; k++) {
    Pos p = sunPos(n); double gmst = rev(280.46061837 + 360.98564736629 * (jdOf(n) - 2451545.0));
    double ha = rev(gmst + lon - p.ra + 180.0) - 180.0;
    n -= (time_t)lround(ha / 360.98564736629 * 86400.0);
  }
  if (n < t0) n += 86164; else if (n >= t1) n -= 86164;
  if (n >= t0 && n < t1) { o.noon = n; o.noonAlt = (float)sunAlt(lat, lon, n); }
  if (!o.rise && !o.set) o.all = sunAlt(lat, lon, t0 + (t1 - t0) / 2) > -0.833 ? 1 : -1;
  return o;
}

struct MoonNow { bool up = false; time_t prev = 0, next = 0; double frac = 0, elong = 0; time_t full = 0, nu = 0; };   // prev: it rose (up) or set (down); next: it sets (up) or rises (down); full/nu: the next full and new moon
inline double elongOf(time_t t) { return rev(moonPos(t).lon - sunPos(t).lon); }   // 0 new, 90 first quarter, 180 full
inline time_t nextElong(time_t t, double target) {
  time_t x = t + (time_t)(rev(target - elongOf(t)) / 12.19 * 86400.0);
  for (int k = 0; k < 4; k++) { double dE = rev(target - elongOf(x) + 180.0) - 180.0; x += (time_t)(dE / 12.19 * 86400.0); }
  return x;
}
inline MoonNow moonNow(double lat, double lon, time_t now) {
  MoonNow o;
  auto f = [=](time_t t) { return moonRel(lat, lon, t); };
  o.up = f(now) >= 0;
  const long W = 30L * 3600L;                            // the moon never goes more than about 26 h between risings
  if (o.up) { o.prev = cross(f, now, now - W, +1, 1200); o.next = cross(f, now, now + W, -1, 1200); }
  else { o.prev = cross(f, now, now - W, -1, 1200); o.next = cross(f, now, now + W, +1, 1200); }
  Pos m = moonPos(now), s = sunPos(now);
  double cpsi = cosd(m.lat) * cosd(m.lon - s.lon);
  o.frac = (1.0 - cpsi) / 2.0; o.elong = rev(m.lon - s.lon);
  o.full = nextElong(now, 180.0); o.nu = nextElong(now, 0.0);
  return o;
}
inline const char* phaseName(double e) {
  if (e < 12 || e >= 348) return "New moon";
  if (e < 78) return "Waxing crescent";
  if (e < 102) return "First quarter";
  if (e < 168) return "Waxing gibbous";
  if (e < 192) return "Full moon";
  if (e < 258) return "Waning gibbous";
  if (e < 282) return "Last quarter";
  return "Waning crescent";
}

}   // namespace astro
