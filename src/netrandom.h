// .NET's seeded System.Random (Knuth subtractive, Net5CompatSeedImpl). Used by RAND (ops_misc.inc)
// and by XSound noise (audio_xsound.cpp); each keeps its own generator, as the reference does.
#pragma once
#include <cstdint>
#include <cstdlib>

namespace c93 {

struct NetRandom {
    int32_t a[56]{};
    int inext = 0, inextp = 21;
    explicit NetRandom(int32_t seed) {
        int32_t mj = 161803398 - (seed == INT32_MIN ? INT32_MAX : std::abs(seed)), mk = 1;
        a[55] = mj;
        for (int i = 1, ii = 0; i < 55; i++) {
            if ((ii += 21) >= 55) ii -= 55;
            a[ii] = mk;
            mk = mj - mk;
            if (mk < 0) mk += INT32_MAX;
            mj = a[ii];
        }
        for (int k = 1; k < 5; k++)
            for (int i = 1; i < 56; i++) {
                int n = i + 30 >= 55 ? i + 30 - 55 : i + 30;
                a[i] -= a[1 + n];
                if (a[i] < 0) a[i] += INT32_MAX;
            }
    }
    int32_t sample() {
        if (++inext >= 56) inext = 1;
        if (++inextp >= 56) inextp = 1;
        int32_t v = a[inext] - a[inextp];
        if (v == INT32_MAX) v--;
        if (v < 0) v += INT32_MAX;
        return a[inext] = v;
    }
    double nextDouble() { return sample() * (1.0 / INT32_MAX); }
    int32_t next(int32_t lo, int32_t hi) {
        int64_t range = int64_t(hi) - lo;
        if (range <= INT32_MAX) return int32_t(nextDouble() * double(range)) + lo;
        int32_t r = sample();
        if (sample() % 2 == 0) r = -r;
        double d = (double(r) + (INT32_MAX - 1)) / 4294967293.0;   // GetSampleForLargeRange
        return int32_t(int64_t(d * double(range)) + lo);
    }
};

} // namespace c93
