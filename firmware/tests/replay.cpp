// เล่นลำดับ P(ตาปิด) ผ่าน DrowsinessJudge ตัวเดียวกับบอร์ด · ใช้กับ ml/incar/replay.py
//
//   g++ -std=c++17 -Wall -I../DrowsyFW replay.cpp ../DrowsyFW/drowsiness.cpp -o replay
//   replay <P_CLOSED_ON> <P_CLOSED_OFF> < frames.csv > levels.csv
//
// เข้า: ทีละบรรทัด "ms,pL,pR" (P < 0 = ตานั้นไม่ได้จำแนกในเฟรมนี้ · ทั้งคู่ < 0 = มองไม่เห็นตา)
// ออก: "ms,eye_state,level,closed_ms,distraction,unmonitored" ทีละเฟรม
// รวมสองตาและแปลง P เป็น Eye State แบบเดียวกับ combineEyes / eyeStateFrom ใน vision.cpp (แก้ที่นั่นต้องแก้ที่นี่)
#include <cstdio>
#include <cstdlib>
#include "drowsiness.h"

static float combineEyes(float a, float b) {
  if (a >= 0 && b >= 0) return (a + b) / 2;
  return a >= 0 ? a : b;
}

static EyeState eyeStateFrom(float p, float on, float off) {
  if (p < 0) return EyeState::NotVisible;
  if (p >= on) return EyeState::Closed;
  if (p < off) return EyeState::Open;
  return EyeState::Uncertain;
}

int main(int argc, char** argv) {
  if (argc != 3) { fprintf(stderr, "usage: replay <P_CLOSED_ON> <P_CLOSED_OFF> < frames.csv\n"); return 2; }
  const float on = (float)atof(argv[1]), off = (float)atof(argv[2]);
  static const char* NAME[] = {"unknown", "open", "closed", "uncertain", "not_visible"};
  DrowsinessJudge j;
  unsigned long ms;
  float pL, pR;
  printf("ms,eye_state,level,closed_ms,distraction,unmonitored\n");
  while (scanf(" %lu,%f,%f", &ms, &pL, &pR) == 3) {
    EyeState es = eyeStateFrom(combineEyes(pL, pR), on, off);
    const DrowsyResult& r = j.update((uint32_t)ms, es);
    printf("%lu,%s,%u,%lu,%d,%d\n", ms, NAME[(int)es], (unsigned)r.level, (unsigned long)r.closedMs, r.distraction, r.unmonitored);
    DrowsyEvent e;
    while (j.popEvent(e)) {}
  }
  return 0;
}
