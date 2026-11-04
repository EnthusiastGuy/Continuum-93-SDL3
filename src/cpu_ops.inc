// The instruction tables and families, included inside a namespace: cpu.cpp builds them in c93, and
// service/cpu_tracked.cpp a second time in c93::tracked, where Machine is TrackedMachine (D-26).

static Op gTop[256];        // first instruction byte (byte-wise dispatch: the tracked loop)
// [first byte][second byte], Machine::run's one-branch dispatch. Family rows hold the sub-op handlers; the row
// of a one-byte opcode holds it (through one<>) for every second byte, which belongs to what follows.
static Op gSub[256][256];
static Op gOne[256];        // one-byte opcodes as gSub entries; nullptr for families

// Second-level dispatch, one instance per family so the row is a constant address.
template <uint8_t P> static uint32_t sub(Machine& m, uint32_t ip) { uint8_t s = m.fetch8(ip); C93_MUSTTAIL return gSub[P][s](m, ip); }
template <Op H> static uint32_t one(Machine& m, uint32_t ip) { C93_MUSTTAIL return H(m, ip - 1); }   // gSub read one byte more
template <Op H> static void top(uint8_t code) { gTop[code] = H; gOne[code] = one<H>; }
template <uint8_t P> static void fam() { gTop[P] = sub<P>; gOne[P] = nullptr; }

// Halting on a malformed instruction mirrors the original, where a missing sub-op handler threw
// out of the run loop and stopped the machine.
static uint32_t illegal(Machine& m, uint32_t ip) {
    LOGE("CPU: illegal instruction at 0x%06X; machine halted", (ip - 2) & kRamMask);
    m.running.store(false, std::memory_order_relaxed);
    C93_NEXT(ip);
}
static uint32_t nop(Machine& m, uint32_t ip) { C93_NEXT(ip); }
// INT n, r: "oooBBBBB AAAAAAAA" — 3-bit sub-op (only 0 exists), register, then the group.
static uint32_t intOp(Machine& m, uint32_t ip) {
    uint8_t b = m.fetch8(ip);
    if (b >> 5) C93_NEXT(ip);
    uint8_t r = b & 31, group = m.fetch8(ip);
    if (group >= kIntGroups) C93_NEXT(ip);
    m.ip = ip;   // interrupts see (and may move) Machine::ip, as before
    gInt[group][m.get<8>(r)](m, r);
    C93_NEXT(m.ip);
}
static uint32_t brk(Machine& m, uint32_t ip) { m.running.store(false, std::memory_order_relaxed); C93_NEXT(ip); }   // BREAK: stop the machine
// ExDEBUG: service mode opens (main thread) and the CPU stops before the next instruction.
static uint32_t debugOp(Machine& m, uint32_t ip) {
    m.debugSet(true, false);
    m.debugRequest.store(true);
    C93_NEXT(ip);
}

#include "ops_addsub.inc"
#include "ops_logic.inc"
#include "ops_shift.inc"
#include "ops_math.inc"
#include "ops_flow.inc"
#include "ops_ld.inc"
#include "ops_misc.inc"
#include "ops_audio.inc"

// Fills the tables; once per copy, at startup, before any machine runs.
static bool buildTables() {
    for (int a = 0; a < 256; ++a) top<nop>(uint8_t(a));   // unassigned first bytes are no-ops in the original too
    for (auto& row : gSub) for (Op& op : row) op = illegal;
    top<brk>(BREAK);
    top<intOp>(INT);
    top<debugOp>(DEBUG);

    fillAddSub<false>(gSub[ADD]); fam<ADD>();
    fillAddSub<true>(gSub[SUB]);  fam<SUB>();
    fillIncDec<1>(gSub[INC]);     fam<INC>();
    fillIncDec<-1>(gSub[DEC]);    fam<DEC>();
    fillLogic<Logic<opAnd>, false>(gSub[AND]);     fam<AND>();
    fillLogic<Logic<opOr>, true>(gSub[OR]);       fam<OR>();
    fillLogic<Logic<opXor>, true>(gSub[XOR]);     fam<XOR>();
    fillLogic<Logic<opNand>, true>(gSub[NAND]);   fam<NAND>();
    fillLogic<Logic<opNor>, true>(gSub[NOR]);     fam<NOR>();
    fillLogic<Logic<opXnor>, true>(gSub[XNOR]);   fam<XNOR>();
    fillLogic<Logic<opImply>, true>(gSub[IMPLY]); fam<IMPLY>();
    fillInv(gSub[INV]);                     fam<INV>();
    fillShift<ShL>(gSub[SL]);   fam<SL>();
    fillShift<ShR>(gSub[SR]);   fam<SR>();
    fillShift<RoL>(gSub[RL]);   fam<RL>();
    fillShift<RoR>(gSub[RR]);   fam<RR>();
    fillShift<BSet>(gSub[SET]); fam<SET>();
    fillShift<BRes>(gSub[RES]); fam<RES>();
    fillShift<BTst>(gSub[BIT]); fam<BIT>();
    fillEx(gSub[EX]);           fam<EX>();
    fillMinMax<false>(gSub[MIN]); fam<MIN>();
    fillMinMax<true>(gSub[MAX]);  fam<MAX>();
    fillLdf(gSub[LDF]);         fam<LDF>();
    top<setfOp<true>>(SETF);
    top<setfOp<false>>(RESF);
    top<invfOp>(INVF);
    fillMul(gSub[MUL]);           fam<MUL>();
    fillDiv(gSub[DIV]);           fam<DIV>();
    fillSMul(gSub[SMUL]);         fam<SMUL>();
    fillSDiv(gSub[SDIV]);         fam<SDIV>();
    fillPow(gSub[POW]);           fam<POW>();
    fillRoot<Sqr>(gSub[SQR]);     fam<SQR>();
    fillRoot<Cbr>(gSub[CBR]);     fam<CBR>();
    fillRoot<ISqr>(gSub[ISQR]);   fam<ISQR>();
    fillUnaryIn<fNeg>(gSub[ISGN]);  fam<ISGN>();
    fillUnaryIn<fSin>(gSub[SIN]);   fam<SIN>();
    fillUnaryIn<fCos>(gSub[COS]);   fam<COS>();
    fillUnaryIn<fTan>(gSub[TAN]);   fam<TAN>();
    fillUnary<fAbs>(gSub[ABS]);     fam<ABS>();
    fillUnary<fRound>(gSub[ROUND]); fam<ROUND>();
    fillUnary<fFloor>(gSub[FLOOR]); fam<FLOOR>();
    fillUnary<fCeil>(gSub[CEIL]);   fam<CEIL>();
    fillColor<rgbToHsx<false>, 24, 32>(gSub[RGB2HSL]); fam<RGB2HSL>();
    fillColor<hslToRgb, 32, 24>(gSub[HSL2RGB]);        fam<HSL2RGB>();
    fillColor<rgbToHsx<true>, 24, 32>(gSub[RGB2HSB]);  fam<RGB2HSB>();
    fillColor<hsbToRgb, 32, 24>(gSub[HSB2RGB]);        fam<HSB2RGB>();
    fillJump<false>(gSub[JP], false);   fam<JP>();
    fillJump<false>(gSub[JR], true);    fam<JR>();
    fillJump<true>(gSub[CALL], false);  fam<CALL>();
    fillJump<true>(gSub[CALLR], true);  fam<CALLR>();
    top<retOp>(RET);
    top<retifOp>(RETIF);
    fillDjnz(gSub[DJNZ]);               fam<DJNZ>();
    fillStack<false>(gSub[PUSH]);       fam<PUSH>();
    fillStack<true>(gSub[POP]);         fam<POP>();
    fillBank<false>(gSub[REGS]);        fam<REGS>();
    fillBank<true>(gSub[FREGS]);        fam<FREGS>();
    fillRegsMem<false>(gSub[LDREGS]);   fam<LDREGS>();
    fillRegsMem<true>(gSub[STREGS]);    fam<STREGS>();
    fillCp<false>(gSub[CP]);            fam<CP>();
    fillCp<true>(gSub[SCP]);            fam<SCP>();
    fillLd(gSub[LD]);                       fam<LD>();
    fillMem(gSub[MEMF], gSub[MEMC]);        fam<MEMF>(); fam<MEMC>();
    fillFind(gSub[FIND]);                   fam<FIND>();
    fillBits(gSub[GETBITS], gSub[SETBITS]); fam<GETBITS>(); fam<SETBITS>();
    fillVars(gSub[SETVAR], gSub[GETVAR]);   fam<SETVAR>(); fam<GETVAR>();
    fillRand(gSub[RAND]);                   fam<RAND>();
    fillWait(gSub[WAIT]);                   fam<WAIT>();
    fillPlay(gSub[PLAY]);                   fam<PLAY>();
    fillVdl(gSub[VDL]); fillVdl(gSub[VCL]); fam<VDL>(); fam<VCL>();   // VCL runs ExVDL too
    for (int a = 0; a < 256; ++a) if (gOne[a]) for (Op& op : gSub[a]) op = gOne[a];
    return true;
}
