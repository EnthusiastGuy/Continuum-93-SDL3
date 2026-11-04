/*
 * Halcyon OS - processor speed.
 *
 * The same measurement as the CPU benchmark program: a loop whose cost on real hardware is known
 * (an in-order i486-class chip of the machine's era: 6 cycles per run), timed with the 100 ns
 * clock. The MHz shown is the clock such a chip would need to keep up with this machine, so a
 * slower host shows proportionally less. To keep the desktop live it runs in slices, one per
 * frame, between frames: 32 x 500,000 runs, the program's 16 million.
 */

#define BENCH_SLICES 32

unsigned int bench_t0[2], bench_t1[2];  /* 100 ns clock: high word, low word */
unsigned int bench_ticks;               /* summed over the slices run so far */
int bench_slice = -1;                   /* slices done; -1 when not measuring */
int cpu_mhz;                            /* 0 until the first slice */
char cpu_name[24];

/* 500,000 runs of the benchmark program's loop, timed (XYZ is free: the C code never uses it). */
void bench_run_slice(void)
{
    asm("LD A, 0x03\nLD B, 0x01\nLD CDE, .c_bench_t0\nINT 0x00, A\n"
        "LD XYZ, 500000\n"
        ".HalBenchLoop\n"
        "LD XYZ, XYZ\n"         /* 1 cycle */
        "DEC XYZ\n"             /* 1 cycle */
        "CP XYZ, 0\n"           /* 1 cycle */
        "JR GT, .HalBenchLoop\n"/* 3 cycles, taken */
        "LD A, 0x03\nLD B, 0x01\nLD CDE, .c_bench_t1\nINT 0x00, A");
    bench_ticks += bench_t1[1] - bench_t0[1];   /* the low words: a slice is far shorter than their 7-minute wrap */
}

/* The marketing name the machine gives a speed (INT 0 0xF0), e.g. "Odin I". */
void cpu_designation(int mhz, char *out)
{
    asm("LD BC, (QRS + 2)\nLD DEF, (QRS + 5)\nLD A, 0xF0\nINT 0x00, A");
}

void bench_start(void)
{
    bench_slice = 0;
    bench_ticks = 0;
}

/* One slice per frame; the figure so far is kept current, so the top bar counts up to it. */
void bench_tick(void)
{
    if (bench_slice < 0) return;
    bench_run_slice();
    bench_slice++;
    /* MHz = cycles / (ticks x 100 ns) = 6 x 500,000 x slices x 10 / ticks */
    if (bench_ticks)
        cpu_mhz = (int)((30000000u * (unsigned int)bench_slice + bench_ticks / 2) / bench_ticks);
    if (bench_slice == BENCH_SLICES)
    {
        bench_slice = -1;
        cpu_designation(cpu_mhz, cpu_name);
        win_dirty = 1;                  /* About shows it */
    }
}

/* "802 MHz" */
void fmt_mhz(char *buf, int mhz)
{
    fmt_int(buf, mhz, 1);
    strcat(buf, " MHz");
}
