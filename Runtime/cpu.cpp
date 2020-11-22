#include "gs.h"
#include <mach/mach.h>
#define _XOPEN_SOURCE
#include <ucontext.h>
#include <sys/_types/_ucontext64.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <iostream>
#include <ios>
#include <thread>

#ifdef USE_UNICORN
#include <unicorn/unicorn.h>
#include <state.h>
#define UCHECKED(expr) do { if(auto _cerr = (expr)) { printf("Call " #expr " failed with error: %u (%s)\n", _cerr, uc_strerror(_cerr)); exit(1); } } while(0)
#else
#include <metacpu.h>
#include <interface.h>
#endif
#include "trampoliner.h"
#include <pthread/introspection.h>

using namespace std;

thread_local Cpu CpuInstance;
thread_local bool ThreadHooked;

bool isArmCodePointer(uint64_t addr) {
    vm_size_t vmsize;
    int _basic64[VM_REGION_BASIC_INFO_COUNT_64];
    auto info = (vm_region_basic_info_64_t) _basic64;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    memory_object_name_t object;
    auto vmaddr = PAGEBASE(addr);
    auto status = vm_region_64(mach_task_self(), (vm_address_t *) &vmaddr, &vmsize, VM_REGION_BASIC_INFO,
                               (vm_region_info_t) info, &info_count, &object);
    if(status != KERN_SUCCESS) return false;
    return (info->protection & VM_PROT_EXECUTE) != VM_PROT_EXECUTE;
}

extern "C" {
void secondtrampoline(stackcontext* context);
}
void secondtrampoline(stackcontext* context) {
    //log("Second trampoline hit");
    //log("Ensuring initialization...");
    initializeImages();
    //log("Handing off to emulator");
    CpuInstance.nativeToArm(context);
    log("Should never hit here!");
    BAILOUT();
}

__attribute__((naked)) static void jmptrampoline() {
	asm volatile(
        "push %rdi\n"
		"push %rax\n"
		"push %rbx\n"
		"push %rcx\n"
		"push %rdx\n"
		"push %rsi\n"
		"push %rbp\n"
		"push %r8\n"
		"push %r9\n"
		"push %r10\n"
		"push %r11\n"
		"push %r12\n"
		"push %r13\n"
		"push %r14\n"
		"push %r15\n"
        "mov %rsp, %rdi\n"
		"jmp _secondtrampoline\n"
	);
}

__attribute__((naked)) static void restorecontext(stackcontext* context) {
	asm volatile(
		"mov %rdi, %rsp\n"
		"pop %r15\n"
		"pop %r14\n"
		"pop %r13\n"
		"pop %r12\n"
		"pop %r11\n"
		"pop %r10\n"
		"pop %r9\n"
		"pop %r8\n"
		"pop %rbp\n"
		"pop %rsi\n"
		"pop %rdx\n"
		"pop %rcx\n"
		"pop %rbx\n"
		"pop %rax\n"
		"pop %rdi\n"
        "addq $8, %rsp\n"
		"ret\n"
	);
}

static void segfaultHandlerThread(mach_port_t port) {
#pragma pack(4)
    struct {
        mach_msg_header_t Head;
        NDR_record_t NDR;
        exception_type_t exception;
        mach_msg_type_number_t codeCnt;
        int64_t code[2];
        int flavor;
        mach_msg_type_number_t old_stateCnt;
        natural_t old_state[x86_THREAD_STATE64_COUNT];
        mach_msg_trailer_t trailer;
    } msgIn;

    struct {
        mach_msg_header_t Head;
        NDR_record_t NDR;
        kern_return_t RetCode;
        int flavor;
        mach_msg_type_number_t new_stateCnt;
        natural_t new_state[x86_THREAD_STATE64_COUNT];
    } msgOut;
#pragma pack()

    memset(&msgIn, 0xee, sizeof(msgIn));
    memset(&msgOut, 0xee, sizeof(msgOut));
    mach_msg_header_t* sendMsg = nullptr;
    mach_msg_size_t sendSize = 0;
    mach_msg_option_t option = MACH_RCV_MSG;

    while(true) {
        assert(!mach_msg_overwrite(sendMsg, option, sendSize, sizeof(msgIn), port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL, &msgIn.Head, 0));
        if(msgIn.Head.msgh_id == MACH_NOTIFY_NO_SENDERS) {
            mach_port_destroy(mach_task_self(), port);
            return;
        }
        assert(msgIn.Head.msgh_id == 2406);
        assert(msgIn.flavor == x86_THREAD_STATE64);

        auto state = (x86_thread_state64_t*) msgIn.old_state;
        //log("RIP at 0x{:x}", state->__rip);

        msgOut.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(msgIn.Head.msgh_bits), 0);
        msgOut.Head.msgh_remote_port = msgIn.Head.msgh_remote_port;
        msgOut.Head.msgh_local_port = MACH_PORT_NULL;
        msgOut.Head.msgh_id = msgIn.Head.msgh_id + 100;
        msgOut.NDR = msgIn.NDR;

        vm_size_t vmsize;
        int _basic64[VM_REGION_BASIC_INFO_COUNT_64];
        auto info = (vm_region_basic_info_64_t) _basic64;
        mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
        memory_object_name_t object;
        auto vmaddr = PAGEBASE(state->__rip);
        auto status = vm_region_64(mach_task_self(), (vm_address_t*) &vmaddr, &vmsize, VM_REGION_BASIC_INFO, (vm_region_info_t) info, &info_count, &object);
        if(status != KERN_SUCCESS || (info->protection & VM_PROT_EXECUTE)) {
            log("Genuine segfault at 0x{:x}; passing along.", state->__rip);
            if(status == KERN_SUCCESS)
                log("Base address for segfault is 0x{:x} with size 0x{:x} and protection 0x{:x}", vmaddr, vmsize, info->protection);
            msgOut.RetCode = KERN_FAILURE;
            msgOut.flavor = 0;
            msgOut.new_stateCnt = 0;
        } else {
            //log("Attempting to call into ARM code, I think! target 0x{:x} retaddr 0x{:x}", state->__rip, *((uint64_t*) state->__rsp));

            auto knownTramp = TrampolinerInstance.asTrampoline(state->__rip);
            if(knownTramp != nullptr) {
                //log("Found known trampoline!");
                if(knownTramp->isArmToNative()) {
                    //log("Actually A->N trampoline; cutting out middleman.");
                    state->__rip = knownTramp->target;
                } else
                    state->__rip = knownTramp->trampoline & ~NATIVE_TO_ARM;
            } else {
                //log("Using generic trampoline");
                state->__rsp -= 8;
                *(uint64_t*) state->__rsp = state->__rip;
                state->__rip = (uint64_t) jmptrampoline;
            }

            msgOut.RetCode = KERN_SUCCESS;
            msgOut.flavor = x86_THREAD_STATE64;
            msgOut.new_stateCnt = x86_THREAD_STATE64_COUNT;
            memcpy(msgOut.new_state, msgIn.old_state, x86_THREAD_STATE64_COUNT * sizeof(natural_t));
            //log("Attempting return to execution...");
        }

        msgOut.Head.msgh_size =
                offsetof(__typeof__(msgOut), new_state) + msgOut.new_stateCnt * sizeof(natural_t);

        sendMsg = &msgOut.Head;
        sendSize = msgOut.Head.msgh_size;
        option |= MACH_SEND_MSG;
    }
}

mach_port_t debugPort;
pthread_introspection_hook_t prevThreadIntrospectionHook;

void threadHook(unsigned int event, pthread_t thread, void* addr, size_t size) {
    if(event == PTHREAD_INTROSPECTION_THREAD_START) {
        log("Got new pthread! Attaching our debug hooks.");
        assert(!thread_set_exception_ports(pthread_mach_thread_np(thread), EXC_MASK_BAD_ACCESS, debugPort,
                                           EXCEPTION_STATE | MACH_EXCEPTION_CODES, x86_THREAD_STATE64));
        log("Attached. Should be good to go!");
    }
    if(prevThreadIntrospectionHook != nullptr) {
        log("Passing pthread event to next handler.");
        prevThreadIntrospectionHook(event, thread, addr, size);
    }
}

#ifdef USE_UNICORN
bool unmpdFetchHook(uc_engine* uc, uc_mem_type type, uint64_t addr, uint32_t size, uint64_t value, void* user_data) {
    log("Unmapped fetch! 0x" << hex << addr);
    if(addr == -4ULL)
        return false;
    auto cpu = (Cpu*) user_data;
    cpu->pullUnicornState();
    if(cpu->isValidCodePointer(addr, &cpu->unicornState)) {
        log("Mapping valid code pointer for page 0x" << hex << PAGEBASE(addr));
        //uc_mem_map_ptr(uc, PAGEBASE(addr), 0x1000, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC, (void*) PAGEBASE(addr));
        vm_size_t vmsize;
        int _basic64[VM_REGION_BASIC_INFO_COUNT_64];
        auto info = (vm_region_basic_info_64_t) _basic64;
        mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
        memory_object_name_t object;
        auto vmaddr = PAGEBASE(addr);
        auto status = vm_region_64(mach_task_self(), (vm_address_t*) &vmaddr, &vmsize, VM_REGION_BASIC_INFO, (vm_region_info_t) info, &info_count, &object);
        if(status != KERN_SUCCESS) {
            log("This failed but should never be able to ... ?");
            BAILOUT();
        }
        log("Found memory region from " << hex << vmaddr << " to " << vmaddr + vmsize);
        //uc_mem_map_ptr(uc, vmaddr, vmsize, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC, (void*) vmaddr);
        auto bottom = max(vmaddr, LARGEPAGEBASE(addr));
        auto top = min(vmaddr + vmsize, LARGEPAGEBASE(addr) + 0x4000);
        log("Actually mapping " << hex << bottom << " to " << top);
        uc_mem_map_ptr(uc, bottom, top - bottom, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC, (void*) bottom);
        return true;
    } else {
        log("Fetching bad address; native call maybe?");
        return false;
    }
}
bool unmpdRWHook(uc_engine* uc, uc_mem_type type, uint64_t addr, uint32_t size, uint64_t value, void* user_data) {
    if(type == UC_MEM_READ_UNMAPPED)
        log("Reading " << dec << size << " bytes of unmapped memory at 0x" << hex << addr);
    else
        log("Writing " << dec << size << " bytes of unmapped memory (0x" << hex << value << ") at 0x" << hex << addr);
    uint64_t pc;
    uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
    log("Current PC 0x" << hex << pc);

    vm_size_t vmsize;
    int _basic64[VM_REGION_BASIC_INFO_COUNT_64];
    auto info = (vm_region_basic_info_64_t) _basic64;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    memory_object_name_t object;
    auto vmaddr = PAGEBASE(addr);
    auto status = vm_region_64(mach_task_self(), (vm_address_t*) &vmaddr, &vmsize, VM_REGION_BASIC_INFO, (vm_region_info_t) info, &info_count, &object);
    if(status != KERN_SUCCESS) {
        log("Actually unmapped page at 0x" << hex << PAGEBASE(addr));
        BAILOUT();
    }
    log("Found memory region from " << hex << vmaddr << " to " << vmaddr + vmsize);
    //uc_mem_map_ptr(uc, vmaddr, vmsize, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC, (void*) vmaddr);
    //uc_mem_map_ptr(uc, PAGEBASE(addr), 0x1000, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC, (void*) PAGEBASE(addr));
    auto bottom = max(vmaddr, LARGEPAGEBASE(addr));
    auto top = min(vmaddr + vmsize, LARGEPAGEBASE(addr) + 0x4000);
    log("Actually mapping " << hex << bottom << " to " << top);
    uc_mem_map_ptr(uc, bottom, top - bottom, UC_PROT_READ | UC_PROT_WRITE | UC_PROT_EXEC, (void*) bottom);
    return true;
}
#endif

// TODO: Work out lifetime issues; we're leaking memory here if a thread is killed after initializing CPU
Cpu::Cpu() {
    static bool first = true;
    if(first) {
        first = false;
        log("Creating segfault handler thread!");
        mach_port_t previous;
        assert(!mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &debugPort));
        thread segfaultThread(segfaultHandlerThread, debugPort);
        segfaultThread.detach();

        assert(!mach_port_insert_right(mach_task_self(), debugPort, debugPort, MACH_MSG_TYPE_MAKE_SEND));
        //assert(!task_set_exception_ports(mach_task_self(), EXC_MASK_BAD_ACCESS, debugPort, EXCEPTION_STATE | MACH_EXCEPTION_CODES, x86_THREAD_STATE64));

        prevThreadIntrospectionHook = pthread_introspection_hook_install(threadHook);
    }
    thread_act_array_t threads;
    mach_msg_type_number_t threadCount;
    assert(task_threads(mach_task_self(), &threads, &threadCount) == KERN_SUCCESS);

    for(auto i = 0; i < threadCount; ++i) {
        //assert(!
        thread_set_exception_ports(threads[i], EXC_MASK_BAD_ACCESS, debugPort, EXCEPTION_STATE | MACH_EXCEPTION_CODES, x86_THREAD_STATE64);
        // );
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    log("Initialized CPU");
    //assert(!thread_set_exception_ports(mach_thread_self(), EXC_MASK_BAD_ACCESS, debugPort, EXCEPTION_STATE | MACH_EXCEPTION_CODES, x86_THREAD_STATE64));
    //assert(!mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, -1));
    //assert(!mach_port_request_notification(mach_task_self(), port, MACH_NOTIFY_NO_SENDERS, 0, port, MACH_MSG_TYPE_MAKE_SEND_ONCE, &previous));

#ifdef USE_UNICORN
    UCHECKED(uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &uc));
    auto fpv = 3 << 20;
    UCHECKED(uc_reg_write(uc, UC_ARM64_REG_CPACR_EL1, &fpv));
    uc_hook hookHandle;
    UCHECKED(uc_hook_add(uc, &hookHandle, UC_HOOK_MEM_FETCH_UNMAPPED, (void *) unmpdFetchHook, this, 0, -1));
    UCHECKED(uc_hook_add(uc, &hookHandle, UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED, (void *) unmpdRWHook, this, 0, -1));
#else
    metaCpu = new MetaCpu(this);
    //metaCpu->setBaseline(MetaMode::LightRecompiling);
    metaCpu->setBaseline(MetaMode::Interpreting);
    //metaCpu->enableOptimizer(MetaMode::LlvmRecompiling, 50);
    metaCpu->initialize();
#endif
	auto stacksize = 512 * 1024;
	currentState()->SP = (uint64_t) malloc(stacksize) + stacksize;
}

Cpu::~Cpu() {
    log("Terminating CPU with interface pointer 0x{:x}", (uint64_t) this);
}

void Cpu::dumpRegs() {
    auto state = currentState();
    string buf = "$STATE$\n";
    for(auto i = 0; i < 32; i += 2)
        buf += fmt::format("X{}={:x}  X{}={:x}\n", i, state->X[i], i + 1, state->X[i + 1]);
    buf += fmt::format("PC={:x}  SP={:x}", state->PC, state->SP);
    log("{}", buf);
}

void Cpu::nativeToArm(stackcontext* context) {
	auto state = currentState();
    CpuState savedState;
    memcpy(&savedState, state, sizeof(CpuState));

	auto target = context->target;
    //log("In nativeToArm; target 0x{:x} retaddr 0x{:x}", target, context->retaddr);

    auto newstack = (uint8_t*) malloc(512*1024);
	memcpy(newstack + 512*1024 - 256, (void*) context, 256);
	auto newsp = newstack + 512*1024 - 256;

    state->X0 = context->rdi;
    state->X1 = context->rsi;
    state->X2 = context->rdx;
    state->X3 = context->rcx;
    state->X4 = context->r8;
    state->X5 = context->r9;
    state->X8 = context->rax;
    state->X20 = context->r13;
    state->X21 = context->r12;
    state->SP = (uint64_t) newsp;

	runFrom(target);
	//log("Emulation done -- returning to native code!");
	free(newstack);

	auto maybeError = state->X21 != context->r12;

    context->rax = state->X0;
    context->rdx = state->X1;
    context->rcx = state->X2;
    context->r8 = state->X3;
    context->r12 = state->X21;
	//log("Native->Arm call to 0x{:x} from 0x{:x} Return values: 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x}{}", target, context->retaddr, context->rax, context->rdx, context->rcx, context->r8, context->r12, maybeError ? " !!!ERRORMAYBE!!!" : "");

    memcpy(state, &savedState, sizeof(CpuState));
	//log("Actually jumping back!");
	restorecontext(context);
}

bool Cpu::isValidCodePointer(CodeSource source, uint64_t addr, CpuState* state) {
    //if(state != nullptr && !fromOptimizer)
    //    dumpRegs();
    if(TrampolinerInstance.asTrampoline(addr) != nullptr)
        return false;
    auto page = addr & ~0xFFFULL;
    if(lastPageChecked[source] == page)
        return true;
    if(addr >> 63) return false;
    //log("Checking pointer validity: " << hex << addr);
    vm_size_t vmsize;
    int _basic64[VM_REGION_BASIC_INFO_COUNT_64];
    auto info = (vm_region_basic_info_64_t) _basic64;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    memory_object_name_t object;
    auto vmaddr = page;
    auto status = vm_region_64(mach_task_self(), (vm_address_t*) &vmaddr, &vmsize, VM_REGION_BASIC_INFO, (vm_region_info_t) info, &info_count, &object);
    if(status != KERN_SUCCESS) {
        log("Actually unmapped page at 0x{:x}", page);
        BAILOUT();
    }
    if(info->reserved) {
        log("Reserved memory at 0x{:x} to 0x{:x}", vmaddr, vmaddr + vmsize);
        BAILOUT();
    }
    if(!(info->protection & VM_PROT_READ)) {
        //log("Checking bad page?? " << hex << page);
        return false;
    }
    if(info->protection & VM_PROT_EXECUTE) {
        //log("Trying to call into native code!");
        return false;
    }
    lastPageChecked[source] = page;
    return true;
}
bool Cpu::Svc(uint32_t svc, CpuState* state) {
    BAILOUT();
    return false;
}
uint64_t Cpu::SR(uint32_t op0, uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2) {
    BAILOUT();
    return 0UL;
}
void Cpu::SR(uint32_t op0, uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2, uint64_t value) {
    BAILOUT();
}

void Cpu::Log(const std::string& message) {
    log("{}", message);
}

void Cpu::Error(const std::string& message) {
    log("{}", message);
    BAILOUT();
}

static bool wasFirst = true;
thread_local bool isFirst = false;

void Cpu::runFrom(uint64_t addr) {
    if(wasFirst) {
        isFirst = true;
        wasFirst = false;
    }
    auto state = currentState();
    state->X30 = -4UL;
	while(true) {
	    //TrampolinerInstance.checkCanaries();
	    /*if(isFirst) {
            log("Starting emulation at 0x{:x}", addr);
            dumpRegs();
        }*/
        /*log("PC 0x{:x}\n\tVector regs: D0 {} 0x{:x}  D1 {} 0x{:x}  D2 {} 0x{:x}  D3 {} 0x{:x}\n\tVector regs: D4 {} 0x{:x}  D5 {} 0x{:x}  D6 {} 0x{:x}  D7 {} 0x{:x}\n\tVector regs: D8 {} 0x{:x}  D9 {} 0x{:x}  D10 {} 0x{:x}  D11 {} 0x{:x}",
                addr,
                *(double*) &state->V0, *(uint64_t*) &state->V0, *(double*) &state->V1, *(uint64_t*) &state->V1, *(double*) &state->V2, *(uint64_t*) &state->V2, *(double*) &state->V3, *(uint64_t*) &state->V3,
                *(double*) &state->V4, *(uint64_t*) &state->V4, *(double*) &state->V5, *(uint64_t*) &state->V5, *(double*) &state->V6, *(uint64_t*) &state->V6, *(double*) &state->V7, *(uint64_t*) &state->V7,
                *(double*) &state->V8, 000*(uint64_t*) &state->V8, *(double*) &state->V9, *(uint64_t*) &state->V9, *(double*) &state->V10, *(uint64_t*) &state->V10, *(double*) &state->V11, *(uint64_t*) &state->V11);*/
#ifdef USE_UNICORN
		unicornState.PC = addr;
		pushUnicornState();
		auto ret = uc_emu_start(uc, addr, -0x1000ULL, 0, 0);
        pullUnicornState();
		if(ret != UC_ERR_FETCH_UNMAPPED) {
		    //log("Unicorn error? " << uc_strerror(ret));
		    //dumpRegs();
		}
#else
        metaCpu->run(addr, state->SP);
#endif
		//log("Emulator returned from execution at " << hex << state->PC);
		if(state->PC == -4UL) {
		    //log("Finished subemulation?");
		    break;
		}
        addr = state->PC;
        /*if(isFirst) {
            log("Completed emulation at 0x{:x}", addr);
            dumpRegs();
        }*/
		if(addr == 0xDEADBEEFCAFEBAB0) { // Backdoor Log
		    log("Got backdoor log message: {}", (const char*) state->X0);
		    addr = state->PC = state->X30;
		    continue;
        }
		auto tt = TrampolinerInstance.asTrampoline(addr);
		if(tt != nullptr) {
		    if(tt->isNativeToArm()) {
		        //log("Attempting to call ARM->ARM via trampoline");
		        addr = state->PC = tt->target;
		    } else {
                //log("Attempting to call into native code; got trampoline instead!");
                auto func = (void (*)()) (tt->trampoline & ~ARM_TO_NATIVE);
                state->PC = state->X30;
                //log("Going to return to " << hex << state->X30);
                func();
                //log("Returned from trampoline");
                addr = state->PC;
            }
        } else if(isValidCodePointer(CodeSource::Execution, addr, state)) {
		    //log("For some reason, we bailed out of ARM code but shouldn't have. 0x" << hex << addr);
		    continue;
		} else if((addr >> 62) == 0x2) {
            //log("Attempting to call into native code; got wrapper instead!");
            auto func = (void(*)(CpuState*)) (addr & ~(2ULL << 62));
            state->PC = state->X30;
            func(state);
            //log("Returned from wrapper");
            addr = state->PC;
        } else {
            auto lr = state->X30;
            //log("Going to native code then returning to " << hex << lr);
            auto ctramp = TrampolinerInstance.getKnownTrampoline(addr);
            if(ctramp == 0) {
                //log("Using generic ARM->Native trampoline for call to 0x{:x} from 0x{:x}", addr, lr);
                trampoline(addr);
            } else {
                tt = TrampolinerInstance.asTrampoline(ctramp);
                if(tt == nullptr) {
                    log("Got trampoline but it isn't a valid trampoline??");
                    BAILOUT();
                }
                ((void (*)()) (tt->trampoline & ~ARM_TO_NATIVE))();
            }
            //log("Returning to " << hex << lr);
            addr = lr;
            if(addr == -4UL) {
                //log("Skipping next subemulation?");
                state->PC = addr;
                break;
            }
        }
	}
}

CpuState* Cpu::currentState() {
#ifdef USE_UNICORN
    return &unicornState;
#else
    return metaCpu->state;
#endif
}

#ifdef USE_UNICORN
void Cpu::pullUnicornState() {
    for(auto i = 0; i < 29; ++i)
        uc_reg_read(uc, UC_ARM64_REG_X0 + i, &unicornState.X[i]);
    uc_reg_read(uc, UC_ARM64_REG_X29, &unicornState.X29);
    uc_reg_read(uc, UC_ARM64_REG_X30, &unicornState.X30);
    for(auto i = 0; i < 32; ++i)
        uc_reg_read(uc, UC_ARM64_REG_V0 + i, &unicornState.V[i]);
    uc_reg_read(uc, UC_ARM64_REG_SP, &unicornState.SP);
    uc_reg_read(uc, UC_ARM64_REG_PC, &unicornState.PC);
}
void Cpu::pushUnicornState() {
    for(auto i = 0; i < 29; ++i)
        uc_reg_write(uc, UC_ARM64_REG_X0 + i, &unicornState.X[i]);
    uc_reg_write(uc, UC_ARM64_REG_X29, &unicornState.X29);
    uc_reg_write(uc, UC_ARM64_REG_X30, &unicornState.X30);
    for(auto i = 0; i < 32; ++i)
        uc_reg_write(uc, UC_ARM64_REG_V0 + i, &unicornState.V[i]);
    uc_reg_write(uc, UC_ARM64_REG_SP, &unicornState.SP);
    uc_reg_write(uc, UC_ARM64_REG_PC, &unicornState.PC);
}
#endif

void Cpu::precompile(uint64_t addr) {
}

void Cpu::trampoline(uint64_t addr) {
    auto state = currentState();
	auto sp = (uint64_t*) state->SP;
	sp -= 128;
	//dumpRegs();
    assert(!((uint64_t) sp & 0xF));
    memcpy(sp, (void*) state->SP, 128);
    *--sp = state->X7;
    *--sp = state->X6;
    *--sp = state->X0;
    *--sp = state->X1;
    *--sp = state->X2;
    *--sp = state->X3;
    *--sp = state->X4;
    *--sp = state->X5;
    *--sp = state->X20;
    *--sp = state->X21;
    *--sp = state->X8;
    /**--sp = ((uint64_t) *(uint32_t*) &state->V0) | (((uint64_t) *(uint32_t*) &state->V1) << 32);
    *--sp = ((uint64_t) *(uint32_t*) &state->V2) | (((uint64_t) *(uint32_t*) &state->V3) << 32);
    *--sp = ((uint64_t) *(uint32_t*) &state->V4) | (((uint64_t) *(uint32_t*) &state->V5) << 32);
    *--sp = ((uint64_t) *(uint32_t*) &state->V6) | (((uint64_t) *(uint32_t*) &state->V7) << 32);
    *--sp = ((uint64_t) *(uint32_t*) &state->V8) | (((uint64_t) *(uint32_t*) &state->V9) << 32);
    *--sp = ((uint64_t) *(uint32_t*) &state->V10) | (((uint64_t) *(uint32_t*) &state->V11) << 32);*/
    *--sp = *(uint64_t*) &state->V0;
    *--sp = *(uint64_t*) &state->V1;
    *--sp = *(uint64_t*) &state->V2;
    *--sp = *(uint64_t*) &state->V3;
    *--sp = *(uint64_t*) &state->V4;
    *--sp = *(uint64_t*) &state->V5;
	uint64_t ret0, ret1, ret2, ret3, eret;
	log("Generic Arm->Native to 0x{:x} from 0x{:x}", addr, state->X30);
    //dumpRegs();
    asm volatile(
		"mov %1, %%r10\n"
		"mov %%rsp, %%rbx\n"
		"mov %%rcx, %%rsp\n"
        "pop %%rdx\n"
        "movq %%rdx, %%xmm5\n"
        "pop %%rdx\n"
        "movq %%rdx, %%xmm4\n"
        "pop %%rdx\n"
        "movq %%rdx, %%xmm3\n"
        "pop %%rdx\n"
        "movq %%rdx, %%xmm2\n"
        "pop %%rdx\n"
        "movq %%rdx, %%xmm1\n"
        "pop %%rdx\n"
        "movq %%rdx, %%xmm0\n"
        "pop %%rax\n"
        "pop %%r12\n"
        "pop %%r13\n"
		"pop %%r9\n"
		"pop %%r8\n"
		"pop %%rcx\n"
		"pop %%rdx\n"
		"pop %%rsi\n"
		"pop %%rdi\n"
		"call *%%r10\n"
		"mov %%rsp, %%r13\n"
        "push %%rax\n"
        "push %%rdx\n"
        "push %%rcx\n"
        "push %%r8\n"
        "push %%r12\n"
        "mov %%r13, %%rcx\n"
		"mov %%rbx, %%rsp\n"
		: "=c" (sp)
		: "r" (addr), "c" (sp)
		: "%rax", "%rbx", "%r9", "%r8", "%r10", "%r12", "%r13", "%rdx", "%rsi", "%rdi"
	);
	//log("Returned from native code -- stack at 0x" << hex << sp);
	ret0 = *--sp;
	ret1 = *--sp;
	ret2 = *--sp;
	ret3 = *--sp;
	eret = *--sp;
	auto maybeError = eret != state->X21;
    log("Generic Arm->Native to 0x{:x} from 0x{:x}  Return values 0x{:x} 0x{:x} 0x{:x} 0x{:x} 0x{:x}{}", addr, state->X30, ret0, ret1, ret2, ret3, eret, maybeError ? " !!!ERRORMAYBE!!!" : "");
    state->X0 = ret0;
    state->X1 = ret1;
    state->X2 = ret2;
    state->X3 = ret3;
    state->X21 = eret;
}
