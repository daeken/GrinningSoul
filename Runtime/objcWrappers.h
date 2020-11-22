#pragma once

void wrap_objc_msgSend(CpuState* state);
void wrap_objc_msgSendSuper2(CpuState* state);
void wrap_objc_retainAutoreleasedReturnValue(CpuState* state);
void wrap_class_getInstanceMethod(CpuState* state);
void wrap_class_addMethod(CpuState* state);
void wrap_class_replaceMethod(CpuState* state);
void wrap_method_getImplementation(CpuState* state);
void wrap_method_setImplementation(CpuState* state);
void wrap_imp_implementationWithBlock(CpuState* state);
