#pragma once

#ifdef USE_UNICORN
#include <unicorn/unicorn.h>
#endif

#include <stack>
#include <interface.h>
#include <state.h>
#include "gs.h"

class MetaCpu;

bool isArmCodePointer(uint64_t addr);

struct stackcontext {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rbp, rsi, rdx, rcx, rbx, rax, rdi, target, retaddr;
};

class Interpreter;
class Cpu : public CpuInterface {
public:
	Cpu();
	~Cpu();
	void runFrom(uint64_t addr);
    CpuState* currentState();
	void precompile(uint64_t addr);

	void trampoline(uint64_t addr);
	void dumpRegs();
	void nativeToArm(stackcontext* context);
	std::stack<uint64_t> callStack;

#ifdef USE_UNICORN
	uc_engine* uc;
	CpuState unicornState;
	void pullUnicornState();
	void pushUnicornState();
#else
	MetaCpu* metaCpu;
#endif

	uint64_t lastPageChecked[CodeSourceCount];
    bool isValidCodePointer(CodeSource source, uint64_t addr, CpuState* state) override;
    bool Svc(uint32_t svc, CpuState* state) override;
    uint64_t SR(uint32_t op0, uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2) override;
    void SR(uint32_t op0, uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2, uint64_t value) override;
    void Log(const std::string& message) override;
    void Error(const std::string& message) override;
};

extern thread_local Cpu CpuInstance;
