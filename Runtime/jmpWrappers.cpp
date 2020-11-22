#include "gs.h"
#include "jmpWrappers.h"

/*
 * _JBLEN is the number of ints required to save the following:
 * r21-r29, sp, fp, lr == 12 registers, 8 bytes each. d8-d15
 * are another 8 registers, each 8 bytes long. (aapcs64 specifies
 * that only 64-bit versions of FP registers need to be saved).
 * Finally, two 8-byte fields for signal handling purposes.
 */
#define _JBLEN		((14 + 8 + 2) * 2)

struct arm64_jmp_buf {
    uint64_t r21, r22, r23, r24, r25, r26, r27, r28, r29;
    uint64_t sp, fp, lr;
    double d8, d9, d10, d11, d12, d13, d14, d15;
    uint8_t signal_a, signal_b;
};

void wrap_setjmp(CpuState* state) {
    log("Got call to setjmp");
    auto buf = (arm64_jmp_buf*) state->X0;

    buf->r21 = state->X21;
    buf->r22 = state->X22;
    buf->r23 = state->X23;
    buf->r24 = state->X24;
    buf->r25 = state->X25;
    buf->r26 = state->X26;
    buf->r27 = state->X27;
    buf->r28 = state->X28;
    buf->r29 = state->X29;

    buf->sp = state->SP;
    buf->fp = state->X29; /// TODO: Why is this duplicated ... ?
    buf->lr = state->X30;

    buf->d8 = *(double*) &state->V8;
    buf->d9 = *(double*) &state->V9;
    buf->d10 = *(double*) &state->V10;
    buf->d11 = *(double*) &state->V11;
    buf->d12 = *(double*) &state->V12;
    buf->d13 = *(double*) &state->V13;
    buf->d14 = *(double*) &state->V14;
    buf->d15 = *(double*) &state->V15;

    buf->signal_a = buf->signal_b = 0; /// TODO: Figure this out

    state->X0 = 0;
}

void wrap_sigsetjmp(CpuState* state) {
    log("Got call to sigsetjmp");
    auto buf = (arm64_jmp_buf*) state->X0;

    buf->r21 = state->X21;
    buf->r22 = state->X22;
    buf->r23 = state->X23;
    buf->r24 = state->X24;
    buf->r25 = state->X25;
    buf->r26 = state->X26;
    buf->r27 = state->X27;
    buf->r28 = state->X28;
    buf->r29 = state->X29;

    buf->sp = state->SP;
    buf->fp = state->X29; /// TODO: Why is this duplicated ... ?
    buf->lr = state->X30;

    buf->d8 = *(double*) &state->V8;
    buf->d9 = *(double*) &state->V9;
    buf->d10 = *(double*) &state->V10;
    buf->d11 = *(double*) &state->V11;
    buf->d12 = *(double*) &state->V12;
    buf->d13 = *(double*) &state->V13;
    buf->d14 = *(double*) &state->V14;
    buf->d15 = *(double*) &state->V15;

    buf->signal_a = buf->signal_b = 0; /// TODO: Figure this out

    state->X0 = 0;
}
void wrap_longjmp(CpuState* state) {
    log("Got call to longjmp");
    auto buf = (arm64_jmp_buf*) state->X0;
    auto val = (int) state->X1;

    state->X21 = buf->r21;
    state->X22 = buf->r22;
    state->X23 = buf->r23;
    state->X24 = buf->r24;
    state->X25 = buf->r25;
    state->X26 = buf->r26;
    state->X27 = buf->r27;
    state->X28 = buf->r28;
    state->X29 = buf->r29;

    state->SP  = buf->sp;
    state->X29 = buf->fp; /// TODO: Why is this duplicated ... ?
    state->PC = state->X30 = buf->lr;

    memset(&state->V8, 0, sizeof(double) * 8 * 2);
    *(double*) &state->V8  = buf->d8;
    *(double*) &state->V9  = buf->d9;
    *(double*) &state->V10 = buf->d10;
    *(double*) &state->V11 = buf->d11;
    *(double*) &state->V12 = buf->d12;
    *(double*) &state->V13 = buf->d13;
    *(double*) &state->V14 = buf->d14;
    *(double*) &state->V15 = buf->d15;

    state->X0 = (uint64_t) val;
}
void wrap_siglongjmp(CpuState* state) {
    log("Got call to siglongjmp");
    auto buf = (arm64_jmp_buf*) state->X0;
    auto val = (int) state->X1;

    state->X21 = buf->r21;
    state->X22 = buf->r22;
    state->X23 = buf->r23;
    state->X24 = buf->r24;
    state->X25 = buf->r25;
    state->X26 = buf->r26;
    state->X27 = buf->r27;
    state->X28 = buf->r28;
    state->X29 = buf->r29;

    state->SP  = buf->sp;
    state->X29 = buf->fp; /// TODO: Why is this duplicated ... ?
    state->PC = state->X30 = buf->lr;

    memset(&state->V8, 0, sizeof(double) * 8 * 2);
    *(double*) &state->V8  = buf->d8;
    *(double*) &state->V9  = buf->d9;
    *(double*) &state->V10 = buf->d10;
    *(double*) &state->V11 = buf->d11;
    *(double*) &state->V12 = buf->d12;
    *(double*) &state->V13 = buf->d13;
    *(double*) &state->V14 = buf->d14;
    *(double*) &state->V15 = buf->d15;

    state->X0 = (uint64_t) val;
}
