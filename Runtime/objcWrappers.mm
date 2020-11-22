#include "cpu.h"
#include <state.h>
#include "objcWrappers.h"
#include "trampoliner.h"
#include <objc/message.h>
#import <Foundation/Foundation.h>
#include <boost/format.hpp>
using namespace std;

extern unordered_map<SEL, uint64_t> armSelReplacementFunctions;

Method findMethod(Class cls, SEL op) {
    while(true) {
        auto method = class_getInstanceMethod(cls, op);
        if(method != nullptr)
            return method;
        auto scls = class_getSuperclass(cls);
        if(scls == cls)
            break;
        cls = scls;
    }
    return nullptr;
}

bool forward(id object, SEL selector, CpuState* state) {
    auto cls = object_getClass(object);
    auto forwarder = findMethod(cls, @selector(forwardInvocation:));
    if(forwarder == nullptr) return false;
    auto signaturer = findMethod(cls, @selector(methodSignatureForSelector:));
    if(signaturer == nullptr) {
        //log("Weird, forwardInvocation: is supported but not methodSignatureForSelector:?");
        return false;
    }
    auto sigObject = [object methodSignatureForSelector: selector];
    if(sigObject == nil) {
        //log("Could not get signature for selector in forward");
        return false;
    }
    auto argCount = (NSUInteger) [sigObject numberOfArguments];
    if(argCount > 6) {
        log("Attempted to forward message with signature over 6 arguments!");
        BAILOUT();
    }

    auto invocation = [NSInvocation invocationWithMethodSignature:sigObject];
    [invocation setSelector: selector];
    [invocation setTarget: object];
    auto argI = 2, intI = 2, floatI = 0;
    for(; argI < argCount; ++argI) {
        auto argType = [sigObject getArgumentTypeAtIndex: argI];
        switch(argType[0]) {
            case 'f':
            case 'd':
                [invocation setArgument: &state->V[floatI++] atIndex: argI];
                break;
            default:
                [invocation setArgument: &state->X[intI++] atIndex: argI];
                break;
        }
    }
    //log("NSInvocation created; forwarding");
    [object forwardInvocation: invocation];
    //log("Done forwarding!");

    return true;
}

__attribute__((noinline)) void trapError() {
}

void wrap_recordError(id self, SEL op, id error) {
    log("Got recordError:! [{} recordError:] -- {}", class_getName(object_getClass(self)), [[error description] UTF8String]);
    trapError();
}

void wrap_objc_msgSend(CpuState* state) {
    //log("In msgSend wrapper!");
    //CpuInstance.dumpRegs();
    id self = (id) state->X0;
    auto op = (SEL) state->X1;
    if(self == nil) {
        auto selName = op != nullptr ? sel_getName(op) : nullptr;
        if(selName != nullptr)
            log("Message to nil! Selector {}", selName);
        else
            log("Message to nil! BAD SELECTOR");
        memset(&state->V0, 0, sizeof(vector128_float) * 4);
        return;
    }
    //log("Object at 0x" << hex << state->X0);
    auto cls = object_getClass(self);
    //log("Message to object of type " << class_getName(cls));
    //log("Selector " << sel_getName(op));
    log("###ARM->ObjC Message: [{} {}] to object 0x{:x} from 0x{:x}", class_getName(cls), sel_getName(op), (uint64_t) self, state->X30);
    auto csel = @selector(class);
    if(class_respondsToSelector(cls, csel)) {
        ((void (*)(id, SEL)) objc_msgSend)(self, csel); // Ensure initialization!
        //log("Called initializer, just in case");
    }
    // else
    //    log("Class seems to not respond to selector ... ?");
    if(sel_isEqual(op, @selector(setAssertsEnabled:))) {
        log("HACK: FORCING ASSERTS IN TWITTER");
        state->X2 = 1;
    } else if(sel_isEqual(op, @selector(recordError:))) {
        wrap_recordError(self, op, (id) state->X2);
        state->X0 = 0;
        return;
    } else if(sel_isEqual(op, @selector(network_URLSessionTaskOperation:didReceiveURLResponse:))) {
        id operation = (id) state->X2;
        auto desc = [operation description];
        log("GOT CALL TO network_URLSessionTaskOperation:didReceiveURLResponse: with operation description {}", desc != nil ? [desc UTF8String] : "(nil)");
        id resp = (id) state->X3;
        desc = [resp description];
        log("GOT CALL TO network_URLSessionTaskOperation:didReceiveURLResponse: with response description {}", desc != nil ? [desc UTF8String] : "(nil)");
    } else if(sel_isEqual(op, @selector(network_URLSessionTaskOperation:didStartSessionTaskWithRequest:))) {
        id operation = (id) state->X2;
        auto desc = [operation description];
        log("GOT CALL TO network_URLSessionTaskOperation:didStartSessionTaskWithRequest: with operation description {}", desc != nil ? [desc UTF8String] : "(nil)");
        id resp = (id) state->X3;
        desc = [resp description];
        log("GOT CALL TO network_URLSessionTaskOperation:didStartSessionTaskWithRequest: with request description {}", desc != nil ? [desc UTF8String] : "(nil)");
        desc = [[resp allHTTPHeaderFields] description];
        log("GOT CALL TO network_URLSessionTaskOperation:didStartSessionTaskWithRequest: with request header fields description {}", desc != nil ? [desc UTF8String] : "(nil)");
    }
    auto iter = armSelReplacementFunctions.find(op);
    if(iter != armSelReplacementFunctions.end()) {
        //log("This is a message to a function we've replaced!");
        state->PC = iter->second;
        return;
    }
    auto method = findMethod(cls, op);
    if(method == nullptr) {
        //log("Unsupported selector. Attempting to find forwarding target");

        if(class_respondsToSelector(cls, @selector(forwardingTargetForSelector:))) {
            //log("forwardingTargetForSelector: supported; trying to get target");
            id newTarget = [self forwardingTargetForSelector: op];
            if(newTarget != nil && newTarget != self) {
                //log("Got new target; restarting process.");
                state->X0 = (uint64_t) newTarget;
                wrap_objc_msgSend(state);
                return;
            }
        }

        //log("Falling back to an attempt to slow forward");

        if(!forward(self, op, state)) {
            log("Got message to unsupported selector and could not find forward!");
            state->X0 = 0;
        }
        return;
    }
    auto impl = (uint64_t) method_getImplementation(method);
    if(CpuInstance.isValidCodePointer(CodeSource::Execution, impl, state)) {
        //log("This is actually a message to our own ARM code!");
        state->PC = impl;
        return;
    }
    auto inv = TrampolinerInstance.getKnownInverse(impl);
    if(inv != 0) {
        //log("This is actually a message to our own ARM code (via trampoline 0x{:x} to 0x{:x})!", impl, inv);
        state->PC = inv;
    } else {
        auto encoding = method_getTypeEncoding(method);
        //log("Method encoding {}", encoding);
        auto tramp = (TrampolineTrampoline*) TrampolinerInstance.getANTrampoline(impl, (boost::format("[%1% %2%]") % class_getName(cls) % sel_getName(op)).str(), encoding);
        //log("Got trampoline; calling");
        ((void(*)())(tramp->trampoline & ~ARM_TO_NATIVE))();
        //log("Returned from trampoline (to {:x})", state->X30);
    }
}

void wrap_objc_msgSendSuper2(CpuState* state) {
    //log("In msgSendSuper2 wrapper!");
    //CpuInstance.dumpRegs();
    auto super = (objc_super*) state->X0;
    //log("Super at 0x{:x}", state->X0);
    id self = super->receiver;
    if(self == nil) {
        log("Message to nil!");
        state->PC = state->X30;
        return;
    }
    auto op = (SEL) state->X1;
    //log("Message to (superclass of) object of type " << class_getName(object_getClass(self)));
    auto cls = super->super_class;
    //log("First superclass " << class_getName(cls));
    ((void(*)(id, SEL)) objc_msgSend)(self, sel_registerName("class")); // Ensure initialization!
    //log("Called initializer, just in case");
    cls = class_getSuperclass(cls);
    //log("Second superclass " << class_getName(cls));
    //log("###ARM->ObjC Super2 Message: [{} {}] to object 0x{:x} from 0x{:x}", class_getName(cls), sel_getName(op), (uint64_t) self, state->X30);
    state->X0 = (uint64_t) self; // Method expects self in x0, not objc_super*
    auto iter = armSelReplacementFunctions.find(op);
    if(iter != armSelReplacementFunctions.end()) {
        //log("This is a message to a function we've replaced!");
        state->PC = iter->second;
        return;
    }
    auto method = findMethod(cls, op);
    if(method == nullptr) {
        //log("Unsupported selector. Attempting to find forwarding target");

        if(class_respondsToSelector(cls, @selector(forwardingTargetForSelector:))) {
            //log("forwardingTargetForSelector: supported; trying to get target");
            id newTarget = [self forwardingTargetForSelector: op];
            if(newTarget != nil && newTarget != self) {
                //log("Got new target; restarting process.");
                state->X0 = (uint64_t) newTarget;
                wrap_objc_msgSend(state);
                return;
            }
        }

        //log("Falling back to an attempt to slow forward");

        if(!forward(self, op, state)) {
            log("Got message to unsupported selector and could not find forward!");
            state->X0 = 0;
        }
        return;
    }
    auto impl = (uint64_t) method_getImplementation(method);
    auto inv = TrampolinerInstance.getKnownInverse(impl);
    if(inv != 0) {
        //log("This is actually a message to our own ARM code via inverse trampoline!");
        state->PC = inv;
    } else if(CpuInstance.isValidCodePointer(CodeSource::Execution, impl, state)) {
        //log("This is actually a message to our own ARM code!");
        state->PC = impl;
    } else {
        auto encoding = method_getTypeEncoding(method);
        //log("Method encoding {}", encoding);
        auto tramp = (TrampolineTrampoline*) TrampolinerInstance.getANTrampoline(impl, (boost::format("[%1% %2%]") % class_getName(cls) % sel_getName(op)).str(), encoding);
        //log("Got trampoline; calling");
        ((void(*)())(tramp->trampoline & ~ARM_TO_NATIVE))();
        //log("Returned from trampoline");
    }
}

void wrap_class_getInstanceMethod(CpuState* state) {
    auto cls = (Class) state->X0;
    auto name = (SEL) state->X1;
    log("Got class_getInstanceMethod -- requesting [{} {}]", class_getName(cls), sel_getName(name));
    state->X0 = (uint64_t) class_getInstanceMethod(cls, name);
    log("class_getInstanceMethod returning {:x}", state->X0);
}

void wrap_class_addMethod(CpuState* state) {
    auto cls = (Class) state->X0;
    auto name = (SEL) state->X1;
    auto imp = (IMP) state->X2;
    auto types = (const char*) state->X3;
    log("Got class_addMethod -- adding [{} {}] with encoding {}", class_getName(cls), sel_getName(name), types);
    state->X0 = (uint64_t) class_addMethod(cls, name, (IMP) TrampolinerInstance.getNATrampoline((uint64_t) imp, fmt::format("[{} {}]", class_getName(cls), sel_getName(name)), types), types);
    log("class_addMethod returning {}", state->X0);
}

void wrap_class_replaceMethod(CpuState* state) {
    auto cls = (Class) state->X0;
    auto name = (SEL) state->X1;
    auto mname = sel_getName(name);
    auto imp = (IMP) state->X2;
    auto types = (const char*) state->X3;
    log("Got class_replaceMethod -- replacing [{} {}] with encoding {}", class_getName(cls), mname == nullptr ? "<null>" : mname, types == nullptr ? "<null>" : types);
    if(mname == nullptr || types == nullptr) {
        log("Couldn't get selector name or encoding; dropping.");
        state->X0 = 0;
        return;
    }
    state->X0 = (uint64_t) class_replaceMethod(cls, name, (IMP) TrampolinerInstance.getNATrampoline((uint64_t) imp, fmt::format("[{} {}]", class_getName(cls), mname == nullptr ? "<null>" : mname), types), types);
}

void wrap_method_getImplementation(CpuState* state) {
    auto method = (Method) state->X0;
    if(method == nullptr) {
        state->X0 = 0;
        log("method_getImplementation got null method");
        return;
    }
    auto mname = method_getName(method);
    if(mname == nullptr) {
        log("Could not get selector for method to method_getImplementation??");
        state->X0 = 0;
        return;
    }
    auto name = sel_getName(mname);
    if(name == nullptr) {
        log("Got method name but not selector name in method_getImplementation??");
        state->X0 = 0;
        return;
    }
    log("Got method_getImplementation -- {}", name);
    auto types = method_getTypeEncoding(method);
    auto addr = (uint64_t) method_getImplementation(method);
    auto tramp = TrampolinerInstance.asTrampoline(addr);
    if(tramp != nullptr || CpuInstance.isValidCodePointer(CodeSource::Speculation, addr, state)) {
        if(tramp == nullptr || tramp->isNativeToArm()) {
            log("ARM method -- returning direct address");
            state->X0 = tramp != nullptr ? tramp->target : addr;
        } else {
            log("Native method via trampoline -- returning trampoline");
            state->X0 = addr;
        }
    } else {
        log("Native method -- returning trampoline");
        state->X0 = TrampolinerInstance.getANTrampoline(addr, name, types);
    }
}

void wrap_method_setImplementation(CpuState* state) {
    auto method = (Method) state->X0;
    auto newImp = (IMP) state->X1;
    auto name = sel_getName(method_getName(method));
    log("Got method_setImplementation -- replacing {}", name);
    auto types = method_getTypeEncoding(method);
    auto oldImp = method_setImplementation(method, (IMP) TrampolinerInstance.getNATrampoline((uint64_t) newImp, fmt::format("replaced!!!{}", name), types));
    if(TrampolinerInstance.asTrampoline((uint64_t) oldImp) != nullptr)
        state->X0 = (uint64_t) oldImp;
    else
        state->X0 = (uint64_t) TrampolinerInstance.getANTrampoline((uint64_t) oldImp, fmt::format("replaced!!!{}", name), types);
}

extern "C" {
id objc_retainBlock(id value);
}

void wrap_imp_implementationWithBlock(CpuState* state) {
    log("Got imp_implementationWithBlock; returning AARch64 trampoline");
    auto blockAddr = (uint64_t) objc_retainBlock((id) state->X0);
    auto block = (BlockInternal*) blockAddr;
    auto funcAddr = block->function;
    auto inv = TrampolinerInstance.getKnownInverse(funcAddr);
    if(inv != 0) funcAddr = inv;

    auto code = new uint8_t[10 * 4 + 3];
    while((uint64_t) code & 3) code++;

    auto insts = (uint32_t*) code;
    insts[0] = 0xAA0003e1; // mov X1, X0

    insts[1] = 0xd2800000 | ((uint32_t) (blockAddr & 0xFFFF) << 5); // movz X0, block bits 0-15
    insts[2] = 0xf2a00000 | ((uint32_t) ((blockAddr >> 16) & 0xFFFF) << 5); // movk X0, block bits 16-31, LSL 16
    insts[3] = 0xf2c00000 | ((uint32_t) ((blockAddr >> 32) & 0xFFFF) << 5); // movk X0, block bits 32-47, LSL 32
    insts[4] = 0xf2e00000 | ((uint32_t) ((blockAddr >> 48) & 0xFFFF) << 5); // movk X0, block bits 48-63, LSL 48

    insts[5] = 0xd2800009 | ((uint32_t) (funcAddr & 0xFFFF) << 5); // movz X9, func bits 0-15
    insts[6] = 0xf2a00009 | ((uint32_t) ((funcAddr >> 16) & 0xFFFF) << 5); // movk X9, func bits 16-31, LSL 16
    insts[7] = 0xf2c00009 | ((uint32_t) ((funcAddr >> 32) & 0xFFFF) << 5); // movk X9, func bits 32-47, LSL 32
    insts[8] = 0xf2e00009 | ((uint32_t) ((funcAddr >> 48) & 0xFFFF) << 5); // movk X9, func bits 48-63, LSL 48

    insts[9] = 0xd61f0120; // br X9

    state->X0 = (uint64_t) code;
}
