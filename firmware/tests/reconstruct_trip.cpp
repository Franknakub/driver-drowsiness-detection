// Rebuild 150 ms frames from 1 Hz trip samples so that the OLD judge (0.2.9-0.3.1 rules, before the
// closed-evidence check) reproduces the board's logged closed_ms peaks.
// Used once to make trip411_frames.h; see make_trip411_frames.py for the full pipeline.
//
// Input (stdin), one line per sample:  <frames in interval> <eye char O/C/u/.> <closed_ms peak>
// Output (stdout): one frame char per frame, O open, C closed, u uncertain, . not visible
//
// The last frame of each interval is the sample's own eye state (the state at the sample instant).
// Intervals are cut into chunks of up to 7 frames; each chunk takes a 6-frame pattern over {. C O u}.
// Beam search (BEAM histories) minimises, over the whole trip:
//   1000 x ( |peak in interval - board peak| + how far closed_ms at the end of the interval exceeds the
//            next sample's peak, which restarts from the current value so can never be lower )
//   + 1 per visible frame equal to the sample's own state, + 2 per other visible frame
// i.e. the frames with the least visible evidence (mostly "not visible": the trip saw a face in ~24% of
// samples) that still explain the logged closed runs.
// Build against the old judge:  g++ -std=c++17 -O2 -I<old DrowsyFW> reconstruct_trip.cpp <old>/drowsiness.cpp
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "drowsiness.h"

static EyeState st(char c) {
  return c == 'O' ? EyeState::Open : c == 'C' ? EyeState::Closed : c == 'u' ? EyeState::Uncertain : EyeState::NotVisible;
}

struct Beam {
  DrowsinessJudge j;
  uint32_t t = 10000;
  uint32_t peak = 0;      // max closed_ms in the current interval so far
  long score = 0, err = 0;
  std::string frames;
};

struct Cand { long score; int parent; int pat; };

static const int BEAM = 64;
static const char ALPHA[4] = {'.', 'C', 'O', 'u'};

static std::string chunkFrames(int pat, int m, bool last, char eye) {
  std::string f;
  for (int k = 0; k < m; k++) f += (last && k == m - 1) ? eye : ALPHA[(pat >> (2 * (k % 6))) & 3];
  return f;
}

static int frameCost(const std::string& f, char eye) {
  int c = 0;
  for (char ch : f) if (ch != '.') c += ch == eye ? 1 : 2;
  return c;
}

int main() {
  std::vector<int> N; std::vector<char> EYE; std::vector<long> PEAK;
  { int n; char e; long p; while (scanf("%d %c %ld", &n, &e, &p) == 3) { N.push_back(n); EYE.push_back(e); PEAK.push_back(p); } }

  std::vector<Beam> beams(1);
  for (size_t i = 0; i < N.size(); i++) {
    char eye = EYE[i];
    long next = i + 1 < N.size() ? PEAK[i + 1] : 0;
    for (auto& b : beams) b.peak = b.j.result().closedMs;     // board peak restarts from the current value
    for (int left = N[i]; left > 0;) {
      int m = std::min(left, 7);
      left -= m;
      bool last = left == 0;
      std::vector<Cand> cands;
      for (int bi = 0; bi < (int)beams.size(); bi++) {
        for (int p = 0; p < 4096; p++) {
          if (m < 7 && p >= (1 << (2 * std::min(m, 6)))) break;   // short chunk: fewer distinct patterns
          std::string f = chunkFrames(p, m, last, eye);
          DrowsinessJudge jj = beams[bi].j; uint32_t tt = beams[bi].t, pk = beams[bi].peak, cm = jj.result().closedMs;
          for (char ch : f) { tt += 150; cm = jj.update(tt, st(ch)).closedMs; if (cm > pk) pk = cm; }
          long s = beams[bi].score + frameCost(f, eye);
          if (last) s += (labs((long)pk - PEAK[i]) + ((long)cm > next ? (long)cm - next : 0)) * 1000;
          cands.push_back({s, bi, p});
        }
      }
      std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
      std::vector<Beam> nb;
      for (const Cand& c : cands) {
        if ((int)nb.size() >= BEAM) break;
        Beam b = beams[c.parent];
        std::string f = chunkFrames(c.pat, m, last, eye);
        uint32_t cm = b.j.result().closedMs;
        for (char ch : f) { b.t += 150; cm = b.j.update(b.t, st(ch)).closedMs; if (cm > b.peak) b.peak = cm; }
        if (last) b.err += labs((long)b.peak - PEAK[i]);
        b.score = c.score;
        b.frames += f;
        DrowsyEvent e; while (b.j.popEvent(e)) {}
        bool dup = false;   // keep the beam diverse: drop histories that end in the same visible state
        for (const Beam& o : nb)
          if (o.score == b.score && o.j.result().closedMs == b.j.result().closedMs && o.frames.compare(o.frames.size() - m, m, f) == 0) { dup = true; break; }
        if (!dup) nb.push_back(b);
      }
      beams.swap(nb);
    }
  }
  const Beam& best = beams[0];
  printf("%s\n", best.frames.c_str());
  fprintf(stderr, "samples %zu frames %zu mean |peak error| %.0f ms\n", N.size(), best.frames.size(), (double)best.err / N.size());
}
