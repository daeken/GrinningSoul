#include "gs.h"

#include <common.h>
#include <state.h>

#include "trampoliner.h"
#include <iostream>
#include <ios>
#include <stack>
#include <sys/mman.h>
#include <mutex>
#include "record.h"

//#define TRAMPOLINE_DEBUG
#define TRAMPOLINE_DEBUG_CALLS

Trampoliner TrampolinerInstance;

namespace Details {
    template<class T>
    struct iterator_range {
        T beginning, ending;
        iterator_range(T beginning, T ending) : beginning(beginning), ending(ending) {}
        T begin() const { return beginning; }
        T end() const { return ending; }
    };
}

template<class T>
auto backwards(T & collection) {
    using namespace std;
    return Details::iterator_range(rbegin(collection), rend(collection));
}

RECORD(BaseType, (int, size), (int, alignment));
DISCRIMINATED_UNION(
    AS_CLASS(Type, BaseType),
    (Void, (bool, fake), EXTERNAL_INIT),
    (Integer, (int, _size), EXTERNAL_INIT),
    (Float, (int, _size), EXTERNAL_INIT),
    (Struct, (vector<Type*>, members), (int, intCount), (int, floatCount), (int, floatSize), (bool, allFloat)),
    (Union, (vector<Type*>, members), EXTERNAL_INIT),
    (Array, (Type*, memberType), (int, count), EXTERNAL_INIT),
    (Pointer, (Type*, memberType), EXTERNAL_INIT),
    (FunctionPointer, (Type*, memberType), EXTERNAL_INIT),
    (BlockPointer, (Type*, memberType), EXTERNAL_INIT),
    (VariadicNightmare, (bool, fake), EXTERNAL_INIT),
    (Function, (Type*, returnType), (vector<Type*>, argTypes), (bool, isVariadic))
);

void Type::Void::__init() { size = alignment = 0; }
Type::Void Void;

void Type::Integer::__init() { size = alignment = _size; }
Type::Integer Int8 {1}, Int16 {2}, Int32 {4}, Int64 {8};

void Type::Float::__init() { size = alignment = _size; }
Type::Float Float {4}, Double {8}, Vector {16};

vector<Type*> combineAdjacentInts(vector<Type*> members) {
    vector<Type*> omembers;
    auto offset = 0;
    auto allFloat = true;
    for(auto member : members)
        if(!member->isFloat()) {
            allFloat = false;
            break;
        }
    auto smushed = false;
    for(auto i = 0; i < members.size(); ++i) {
        auto member = members[i];
        while(offset % member->alignment) offset++;
        if(member->isInteger() && member->size == 4 && i + 1 < members.size() && members[i + 1]->isInteger() && members[i + 1]->size == 4) {
            smushed = true;
            omembers.push_back(&Int64);
            ++i;
        } else if(member->isInteger() && member->size == 2 && i + 1 < members.size() && members[i + 1]->isInteger() && members[i + 1]->size == 2) {
            smushed = true;
            omembers.push_back(&Int32);
            ++i;
        } else if(member->isInteger() && member->size == 1 && i + 1 < members.size() && members[i + 1]->isInteger() && members[i + 1]->size == 1) {
            smushed = true;
            omembers.push_back(&Int16);
            ++i;
        } else if(!allFloat && member->isFloat() && member->size == 4 && i + 1 < members.size() && members[i + 1]->isFloat() && members[i + 1]->size == 4) {
            smushed = true;
            omembers.push_back(&Double);
            ++i;
        } else
            omembers.push_back(member);
    }
    return smushed ? combineAdjacentInts(omembers) : omembers;
}

Type::Struct* makeStructType(vector<Type*> members) {
    members = combineAdjacentInts(members);
    auto size = 0, intCount = 0, floatCount = 0, floatSize = 0, alignment = 1;
    auto allFloat = true;
    for(auto member : members) {
        while(size % member->alignment) size++;
        size += member->size;
        if(auto str = member->asStruct()) {
            intCount += str->intCount;
            floatCount += str->floatCount;
            floatSize += str->floatSize;
            allFloat = allFloat && str->allFloat;
        } else if(member->isFloat()) {
            floatCount++;
            floatSize += member->size / 4;
        } else {
            intCount++;
            allFloat = false;
        }
        alignment = max(alignment, member->alignment);
    }
    auto str = new Type::Struct(members, intCount, floatCount, floatSize, allFloat);
    str->size = size;
    str->alignment = alignment;
    return str;
}

void Type::Union::__init() {
    size = 0;
    alignment = 1;
    for(auto member : members) {
        size = max(member->size, size);
        alignment = max(member->alignment, alignment);
    }
}

void Type::Array::__init() {
    size = memberType->size * count;
    alignment = memberType->alignment;
}

void Type::Pointer::__init() { size = alignment = 8; }
Type::Pointer VoidPointer {&Void};

void Type::FunctionPointer::__init() { size = alignment = 8; }
void Type::BlockPointer::__init() { size = alignment = 8; }

const int nightmareCount = 80;
void Type::VariadicNightmare::__init() { size = alignment = 8; }
Type::VariadicNightmare VariadicNightmare;

Type::Function* parseObjCFunction(char*& signature) {
    auto skipNumber = [&]() {
        while(*signature >= '0' && *signature <= '9')
            signature++;
    };
    auto variadic = false;
    function<Type*()> recur {[&]() -> Type* {
        switch(*signature++) {
            case 'v': return &Void;
            case 'r': return recur(); // Const; skip
            case 'n': return recur(); // In; skip
            case 'N': return recur(); // Inout; skip
            case 'o': return recur(); // Out; skip
            case 'O': return recur(); // Bycopy; skip
            case 'R': return recur(); // Byref; skip
            case 'A': return recur(); // Atomic...? Only seems to be associated with atomic pointers
            case 'V': return recur(); // Oneway; skip

            case 'c': case 'C': case 'B': return &Int8;
            case 's': case 'S': return &Int16;
            case 'i': case 'I': return &Int32;

            case 'l': case 'L': return &Int64;
            case 'q': case 'Q': return &Int64;
            case 'f': return &Float;
            case 'd': case 'D': return &Double; // D is actually long double, but I don't think it matters here?
            case '@':
                if(*signature == '?')
                    signature++;
                return &VoidPointer;
            case ':': case '?': case '*': case '#': return &VoidPointer;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
                return &Vector;
            case '^': {
                auto stype = recur();
                if(stype->isFunction())
                    return new Type::FunctionPointer(stype);
                return new Type::Pointer(stype);
            }
            case '%': {
                auto stype = recur();
                assert(stype->isFunction());
                auto ftype = stype->asFunction();
                vector<Type*> args = ftype->argTypes;
                args.insert(args.begin(), &VoidPointer);
                return new Type::BlockPointer(new Type::Function(ftype->returnType, args, ftype->isVariadic));
            }
            case 'b':
                skipNumber();
                return &Int32; // TODO: Handle this properly; it's fine in a pointer type, but fucked otherwise
            case '{': {
                vector<Type*> members;
                while(*signature != '=' && *signature != '}') ++signature;
                if(*signature == '=') ++signature;
                while(*signature != '}')
                    members.push_back(recur());
                ++signature;
                return makeStructType(members);
            }
            case '(': {
                vector<Type*> members;
                while(*signature != '=' && *signature != ')') ++signature;
                if(*signature == '=') ++signature;
                while(*signature != ')')
                    members.push_back(recur());
                ++signature;
                return new Type::Union(members);
            }
            case '[': {
                auto count = atoi(signature);
                skipNumber();
                auto type = *signature == ']' ? &VoidPointer : recur();
                if(*signature != ']') {
                    log("Array has contents after type?");
                    BAILOUT();
                }
                ++signature;
                return new Type::Array(type, count);
            }
            case '~': {
                switch(*signature++) {
                    case 'f':
                        assert(*signature++ == '{');
                        return parseObjCFunction(signature);
                    case 'V': {
                        assert(*signature++ == '{');
                        auto count = atoi(signature);
                        skipNumber();
                        auto elemType = recur();
                        assert(*signature++ == '}');
                        vector<Type*> members;
                        for(auto i = 0; i < count; ++i)
                            members.push_back(elemType);
                        return makeStructType(members);
                    }
                    default:
                        log("Unknown subtype '{}'", *--signature);
                        BAILOUT();
                }
                break;
            }
            case '$':
                variadic = true;
                return nullptr;
                break;
            default:
                log("Unknown type '{}'", *--signature);
                BAILOUT();
        }
    }};
    auto rettype = recur();
    skipNumber();
    vector<Type*> argtypes;
    while(*signature != '}') {
        auto type = recur();
        if(type == nullptr) continue;
        argtypes.push_back(type);
        skipNumber();
    }
    ++signature;
    if(variadic)
        for(auto i = 0; i < nightmareCount; ++i)
            argtypes.push_back(&VariadicNightmare);

    return new Type::Function(rettype, argtypes, variadic);
}

Type::Function* parseObjCSignature(char* signature) {
    string sig = signature;
    sig += "}";
    signature = (char*) sig.c_str();
    return parseObjCFunction(signature);
}

Type::Function* parseSwiftSignature(char* signature) {
    return nullptr;
}

enum Gpr {
    Rax,
    Rbx,
    Rbp,
    Rcx,
    Rdx,
    Rdi,
    Rsp,
    Rsi,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15
};
const char* X86GprNames[] = {
    "Rax", "Rbx", "Rbp", "Rcx", "Rdx", "Rdi", "Rsp", "Rsi",
    "R8" , "R9" , "R10", "R11", "R12", "R13", "R14", "R15"
};

Gpr X86ArgRegisters[] { Gpr::Rdi, Gpr::Rsi, Gpr::Rdx, Gpr::Rcx, Gpr::R8, Gpr::R9 };

DISCRIMINATED_UNION(
    Position,
    (X86Gpr, (Gpr, gpr)),
    (Xmm, (int, num), (int, offset)),
    (ArmGpr, (int, num)),
    (ArmVec, (int, num), (int, offset)),
    (Stack, (int, offset)),
    (Object, (Position*, objectPos), (int, offset))
);

RECORD(Placement, (Position*, position), (Type*, type));

class BaseSignature {
public:
    virtual string repr() = 0;
    vector<Placement*> x86PrePlacements, x86PostPlacements, armPrePlacements, armPostPlacements;
    vector<tuple<Position*, int>> x86RegisterStackOffsets, armRegisterStackOffsets;
};

class Signature : public BaseSignature {
public:
    explicit Signature(const string& name, Type::Function* functionType) : name(name), functionType(functionType) {
        //log("Generating signature object for " << functionType->repr());
        generateX86Placements();
#ifdef TRAMPOLINE_DEBUG
        log("X86 pre placements:");
        for(auto placement : x86PrePlacements)
            log("\t{}", placement->repr());
        log("X86 post placements:");
        for(auto placement : x86PostPlacements)
            log("\t{}", placement->repr());
        log("X86 register->stack offsets:");
        for(auto [reg, offset] : x86RegisterStackOffsets)
            log("\t{} <- sp + {}", reg->repr(), offset);
#endif
        generateArmPlacements();
#ifdef TRAMPOLINE_DEBUG
        log("ARM pre placements:");
        for(auto placement : armPrePlacements)
            log("\t{}", placement->repr());
        log("ARM post placements:");
        for(auto placement : armPostPlacements)
            log("\t{}", placement->repr());
        log("ARM register->stack offsets:");
        for(auto [reg, offset] : armRegisterStackOffsets)
            log("\t{} <- sp + {}", reg->repr(), offset);
#endif
        assert(x86PrePlacements.size() == armPrePlacements.size());
        assert(x86PostPlacements.size() == armPostPlacements.size());
    }

    void generateX86Placements() {
        auto intCount = 0, floatCount = 0, stackOffset = 0, objectOffset = 0;
        Position* objectPos;
        if(functionType->returnType->size > 16)
            intCount++;
        for(auto at : functionType->argTypes) {
            if(auto str = at->asStruct()) {
                auto allFloat = str->allFloat;
                if(str->size > 16 || (str->intCount != 0 && str->intCount + intCount > 6) || (str->floatCount != 0 && str->floatSize + floatCount > 16)) {
#ifdef TRAMPOLINE_DEBUG
                    log("Putting struct on the stack");
                    log("Struct size {}", str->size);
                    log("Ints in struct {} current count {}", str->intCount, intCount);
                    log("Floats in struct {} current count {}", str->floatCount, floatCount);
                    log("Float size {}", str->floatSize);
#endif
                    while(stackOffset % str->alignment) ++stackOffset;
                    function<void(Type::Struct*)> recur([&](auto str) -> void {
                        for(auto elem : str->members)
                            if(auto estr = elem->asStruct())
                                recur(estr);
                            else {
                                while(stackOffset % elem->alignment) stackOffset++;
                                x86PrePlacements.push_back(new Placement(new Position::Stack(stackOffset), elem));
                                stackOffset += elem->size;
                            }
                    });
                    recur(str);
                } else {
                    function<void(Type::Struct*)> recur([&](auto str) -> void {
                        for(auto elem : str->members)
                            if(auto estr = elem->asStruct())
                                recur(estr);
                            else if(elem->isFloat()) {
                                if(elem->size == 8 && (floatCount & 1))
                                    floatCount++;
                                x86PrePlacements.push_back(new Placement(new Position::Xmm(floatCount / 2, floatCount & 1), elem));
                                floatCount += elem->size / 4;
                            } else
                                x86PrePlacements.push_back(new Placement(new Position::X86Gpr(X86ArgRegisters[intCount++]), elem));
                    });
                    recur(str);
                }
            } else if(at->isFloat()) {
                if(floatCount & 1)
                    floatCount++;
                if(floatCount < 16) {
                    x86PrePlacements.push_back(new Placement(new Position::Xmm(floatCount / 2, floatCount & 1), at));
                    floatCount += at->size / 4;
                } else {
                    while(stackOffset % max(8, at->alignment)) ++stackOffset;
                    x86PrePlacements.push_back(new Placement(new Position::Stack(stackOffset), at));
                    stackOffset += at->size;
                }
            } else {
                if(intCount < 6)
                    x86PrePlacements.push_back(new Placement(new Position::X86Gpr(X86ArgRegisters[intCount++]), at));
                else {
                    while(stackOffset % max(8, at->alignment)) ++stackOffset;
                    x86PrePlacements.push_back(new Placement(new Position::Stack(stackOffset), at));
                    stackOffset += at->size;
                }
            }
        }
        if(functionType->returnType != &Void) {
            intCount = floatCount = 0;
            auto rt = functionType->returnType;
            if(auto str = rt->asStruct()) {
                if(str->size > 16 || str->intCount > 2 || str->floatCount > 8) {
#ifdef TRAMPOLINE_DEBUG
                    log("Putting struct on the stack");
#endif
                    while(objectOffset % 16) objectOffset++;
                    x86RegisterStackOffsets.emplace_back(objectPos = new Position::X86Gpr(Gpr::Rdi), -(objectOffset + 16));
                    function<void(Type::Struct*)> recur([&](auto str) -> void {
                        for(auto elem : str->members)
                            if(auto estr = elem->asStruct())
                                recur(estr);
                            else {
                                while(objectOffset % elem->alignment) ++objectOffset;
                                x86PostPlacements.push_back(new Placement(new Position::Object(objectPos, objectOffset), elem));
                                objectOffset += elem->size;
                            }
                    });
                    recur(str);
                } else {
                    intCount = floatCount = 0;
                    function<void(Type::Struct*)> recur([&](auto str) -> void {
                        for(auto elem : str->members)
                            if(auto estr = elem->asStruct())
                                recur(estr);
                            else if(elem->isFloat()) {
                                if(elem->size == 8 && (floatCount & 1))
                                    floatCount++;
                                x86PostPlacements.push_back(new Placement(new Position::Xmm(floatCount / 2, floatCount & 1), elem));
                                floatCount += elem->size / 4;
                            } else
                                x86PostPlacements.push_back(new Placement(new Position::X86Gpr(intCount++ == 0 ? Gpr::Rax : Gpr::Rdx), elem));
                    });
                    recur(str);
                }
            } else if(rt->isFloat())
                x86PostPlacements.push_back(new Placement(new Position::Xmm(0, 0), rt));
            else
                x86PostPlacements.push_back(new Placement(new Position::X86Gpr(Gpr::Rax), rt));
        }
    }

    void generateArmPlacements() {
        auto intCount = 0, floatCount = 0, stackOffset = 0, objectOffset = 0;
        Position* objectPos;
        for(auto at : functionType->argTypes) {
            if(auto str = at->asStruct()) {
                auto allFloat = str->allFloat;
                if(
                    (allFloat && str->floatCount > 4) ||
                    (!allFloat && str->size > 16) ||
                    str->intCount + intCount + (allFloat ? 0 : str->floatCount) > 8 ||
                    (allFloat && str->floatCount + floatCount > 8)
                ) {
#ifdef TRAMPOLINE_DEBUG
                    log("Putting struct on the stack");
#endif
                    while(objectOffset % str->alignment) objectOffset++;
                    auto curObjectOffset = 0;
                    if(intCount < 8 && (str->size != 16 || intCount < 6)) { // This feels wrong.
                        armRegisterStackOffsets.emplace_back(objectPos = new Position::ArmGpr(intCount++),
                                                             -(objectOffset + 16));
                        function<void(Type::Struct*)> recur([&](auto str) -> void {
                            for(auto elem : str->members)
                                if(auto estr = elem->asStruct())
                                    recur(estr);
                                else {
                                    while(curObjectOffset % elem->alignment) curObjectOffset++;
                                    armPrePlacements.push_back(
                                            new Placement(new Position::Object(objectPos, curObjectOffset), elem));
                                    curObjectOffset += elem->size;
                                }
                        });
                        recur(str);
                        objectOffset += curObjectOffset;
                    } else {
                        intCount = 8;
                        while(stackOffset % str->alignment) ++stackOffset;
                        function<void(Type::Struct*)> recur([&](auto str) -> void {
                            for(auto elem : str->members)
                                if(auto estr = elem->asStruct())
                                    recur(estr);
                                else {
                                    while(stackOffset % elem->alignment) stackOffset++;
                                    armPrePlacements.push_back(new Placement(new Position::Stack(stackOffset), elem));
                                    stackOffset += elem->size;
                                }
                        });
                        recur(str);

                    }
                } else {
                    function<void(Type::Struct*)> recur([&](auto str) -> void {
                        for(auto elem : str->members)
                            if(auto estr = elem->asStruct())
                                recur(estr);
                            else if(allFloat)
                                armPrePlacements.push_back(new Placement(new Position::ArmVec(floatCount++, 0), elem));
                            else
                                armPrePlacements.push_back(new Placement(new Position::ArmGpr(intCount++), elem));
                    });
                    recur(str);
                }
            } else if(at->isFloat()) {
                if(floatCount < 8)
                    armPrePlacements.push_back(new Placement(new Position::ArmVec(floatCount++, 0), at));
                else {
                    while((stackOffset % at->alignment) != 0) ++stackOffset;
                    armPrePlacements.push_back(new Placement(new Position::Stack(stackOffset), at));
                    stackOffset += at->size;
                }
            } else if(at->isVariadicNightmare()) {
                while((stackOffset % at->alignment) != 0) ++stackOffset;
                armPrePlacements.push_back(new Placement(new Position::Stack(stackOffset), at));
                stackOffset += at->size;
            } else {
                if(intCount < 8)
                    armPrePlacements.push_back(new Placement(new Position::ArmGpr(intCount++), at));
                else {
                    while((stackOffset % at->alignment) != 0) ++stackOffset;
                    armPrePlacements.push_back(new Placement(new Position::Stack(stackOffset), at));
                    stackOffset += at->size;
                }
            }
        }
        if(functionType->returnType != &Void) {
            auto rt = functionType->returnType;
            if(auto str = rt->asStruct()) {
                auto allFloat = str->allFloat;
                intCount = floatCount = 0;
                if(
                    (allFloat && str->floatCount > 4) ||
                    (!allFloat && str->size > 16) ||
                    str->intCount + intCount + (allFloat ? 0 : str->floatCount) > 8 ||
                    (allFloat && str->floatCount + floatCount > 8)
                ) {
#ifdef TRAMPOLINE_DEBUG
                    log("Putting struct on the stack");
#endif
                    while(objectOffset % str->alignment) objectOffset++;
                    armRegisterStackOffsets.emplace_back(objectPos = new Position::ArmGpr(8), -(objectOffset + 16));
                    auto curObjectOffset = 0;
                    function<void(Type::Struct*)> recur([&](auto str) -> void {
                        for(auto elem : str->members)
                            if(auto estr = elem->asStruct())
                                recur(estr);
                            else {
                                while((curObjectOffset % elem->alignment) != 0) ++curObjectOffset;
                                armPostPlacements.push_back(new Placement(new Position::Object(objectPos, curObjectOffset), elem));
                                curObjectOffset += elem->size;
                            }
                    });
                    recur(str);
                } else {
                    function<void(Type::Struct*)> recur([&](auto str) -> void {
                        for(auto elem : str->members)
                            if(auto estr = elem->asStruct())
                                recur(estr);
                            else if(allFloat)
                                armPostPlacements.push_back(new Placement(new Position::ArmVec(floatCount++, 0), elem));
                            else
                                armPostPlacements.push_back(new Placement(new Position::ArmGpr(intCount++), elem));
                    });
                    recur(str);
                }
            } else if(rt->isFloat())
                armPostPlacements.push_back(new Placement(new Position::ArmVec(0, 0), rt));
            else
                armPostPlacements.push_back(new Placement(new Position::ArmGpr(0), rt));
        }
    }

    string repr() override { return (boost::format("<Signature name=\"%1%\" functionType=\"%2%\">") % (name.empty() ? "{{unknown}}" : name) % functionType->repr()).str(); }
    string name;
    Type::Function* functionType;
};

class SwiftSignature : public BaseSignature {
public:
    SwiftSignature(Type::Function* functionType) : functionType(functionType) {
    }

    string repr() override { return functionType->repr(); }
    Type::Function* functionType;
};

class MCBuilder {
public:
    uint8_t *code;
    uint64_t start;
    MCBuilder() {
        // TODO: Share code pages; keep everything RWX
        code = (uint8_t*) mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
        assert(code != MAP_FAILED);
        start = (uint64_t) code;
    }

    void regLiteral(uint64_t value) {
        *((uint64_t*) code) = value;
        code += 8;
    }

    void raxLiteral(uint64_t value) { *code++ = 0x48; *code++ = 0xB8; regLiteral(value); }
    void rcxLiteral(uint64_t value) { *code++ = 0x48; *code++ = 0xB9; regLiteral(value); }
    void rdxLiteral(uint64_t value) { *code++ = 0x48; *code++ = 0xBA; regLiteral(value); }
    void rbxLiteral(uint64_t value) { *code++ = 0x48; *code++ = 0xBB; regLiteral(value); }
    void rspLiteral(uint64_t value) { *code++ = 0x48; *code++ = 0xBC; regLiteral(value); }
    void rbpLiteral(uint64_t value) { *code++ = 0x48; *code++ = 0xBD; regLiteral(value); }
    void rsiLiteral(uint64_t value) { *code++ = 0x48; *code++ = 0xBE; regLiteral(value); }
    void rdiLiteral(uint64_t value) { *code++ = 0x48; *code++ = 0xBF; regLiteral(value); }
    void r8Literal(uint64_t value) { *code++ = 0x49; *code++ = 0xB8; regLiteral(value); }
    void r9Literal(uint64_t value) { *code++ = 0x49; *code++ = 0xB9; regLiteral(value); }
    void r10Literal(uint64_t value) { *code++ = 0x49; *code++ = 0xBA; regLiteral(value); }
    void r11Literal(uint64_t value) { *code++ = 0x49; *code++ = 0xBB; regLiteral(value); }
    void r12Literal(uint64_t value) { *code++ = 0x49; *code++ = 0xBC; regLiteral(value); }
    void r13Literal(uint64_t value) { *code++ = 0x49; *code++ = 0xBD; regLiteral(value); }
    void r14Literal(uint64_t value) { *code++ = 0x49; *code++ = 0xBE; regLiteral(value); }
    void r15Literal(uint64_t value) { *code++ = 0x49; *code++ = 0xBF; regLiteral(value); }

    void pushLiteral(uint64_t value) {
        // sub rsp, 8
        *code++ = 0x48; *code++ = 0x83; *code++ = 0xEC; *code++ = 0x08;

        // mov dword ptr [rsp+4], high32
        *code++ = 0xC7; *code++ = 0x44; *code++ = 0x24; *code++ = 0x04;
        *((uint32_t*) code) = (uint32_t) (value >> 32U);
        code += 4;

        // mov dword ptr [rsp], low32
        *code++ = 0xC7; *code++ = 0x04; *code++ = 0x24;
        *((uint32_t*) code) = (uint32_t) (value & 0xFFFFFFFFUL);
        code += 4;
    }

    void pushRax() { *code++ = 0x50; }
    void pushRcx() { *code++ = 0x51; }
    void pushRdx() { *code++ = 0x52; }
    void pushRbx() { *code++ = 0x53; }
    void pushRsp() { *code++ = 0x54; }
    void pushRbp() { *code++ = 0x55; }
    void pushRsi() { *code++ = 0x56; }
    void pushRdi() { *code++ = 0x57; }
    void pushR8() { *code++ = 0x41; *code++ = 0x50; }
    void pushR9() { *code++ = 0x41; *code++ = 0x51; }
    void pushR10() { *code++ = 0x41; *code++ = 0x52; }
    void pushR11() { *code++ = 0x41; *code++ = 0x53; }
    void pushR12() { *code++ = 0x41; *code++ = 0x54; }
    void pushR13() { *code++ = 0x41; *code++ = 0x55; }
    void pushR14() { *code++ = 0x41; *code++ = 0x56; }
    void pushR15() { *code++ = 0x41; *code++ = 0x57; }

    void popRax() { *code++ = 0x58; }
    void popRcx() { *code++ = 0x59; }
    void popRdx() { *code++ = 0x5A; }
    void popRbx() { *code++ = 0x5B; }
    void popRsp() { *code++ = 0x5C; }
    void popRbp() { *code++ = 0x5D; }
    void popRsi() { *code++ = 0x5E; }
    void popRdi() { *code++ = 0x5F; }
    void popR8() { *code++ = 0x41; *code++ = 0x58; }
    void popR9() { *code++ = 0x41; *code++ = 0x59; }
    void popR10() { *code++ = 0x41; *code++ = 0x5A; }
    void popR11() { *code++ = 0x41; *code++ = 0x5B; }
    void popR12() { *code++ = 0x41; *code++ = 0x5C; }
    void popR13() { *code++ = 0x41; *code++ = 0x5D; }
    void popR14() { *code++ = 0x41; *code++ = 0x5E; }
    void popR15() { *code++ = 0x41; *code++ = 0x5F; }

    void movdXmm0Eax() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xC0; }
    void movdXmm1Eax() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xC8; }
    void movdXmm2Eax() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xD0; }
    void movdXmm3Eax() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xD8; }
    void movdXmm4Eax() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xE0; }
    void movdXmm5Eax() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xE8; }
    void movdXmm6Eax() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xF0; }
    void movdXmm7Eax() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xF8; }

    void movqXmm0Rax() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xC0; }
    void movqXmm1Rax() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xC8; }
    void movqXmm2Rax() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xD0; }
    void movqXmm3Rax() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xD8; }
    void movqXmm4Rax() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xE0; }
    void movqXmm5Rax() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xE8; }
    void movqXmm6Rax() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xF0; }
    void movqXmm7Rax() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x6E; *code++ = 0xF8; }

    void movdEaxXmm0() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xC0; }
    void movdEaxXmm1() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xC8; }
    void movdEaxXmm2() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xD0; }
    void movdEaxXmm3() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xD8; }
    void movdEaxXmm4() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xE0; }
    void movdEaxXmm5() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xE8; }
    void movdEaxXmm6() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xF0; }
    void movdEaxXmm7() { *code++ = 0x66; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xF8; }

    void movqRaxXmm0() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xC0; }
    void movqRaxXmm1() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xC8; }
    void movqRaxXmm2() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xD0; }
    void movqRaxXmm3() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xD8; }
    void movqRaxXmm4() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xE0; }
    void movqRaxXmm5() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xE8; }
    void movqRaxXmm6() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xF0; }
    void movqRaxXmm7() { *code++ = 0x66; *code++ = 0x48; *code++ = 0x0F; *code++ = 0x7E; *code++ = 0xF8; }

    void jmpRax() { *code++ = 0xFF; *code++ = 0xE0; }
    void jmpRcx() { *code++ = 0xFF; *code++ = 0xE1; }
    void jmpRdx() { *code++ = 0xFF; *code++ = 0xE2; }
    void jmpRbx() { *code++ = 0xFF; *code++ = 0xE3; }
    void jmpRbp() { *code++ = 0xFF; *code++ = 0xE5; }
    void jmpRsi() { *code++ = 0xFF; *code++ = 0xE6; }
    void jmpRdi() { *code++ = 0xFF; *code++ = 0xE7; }

    void callRax() { *code++ = 0xFF; *code++ = 0xD0; }
    void callRcx() { *code++ = 0xFF; *code++ = 0xD1; }
    void callRdx() { *code++ = 0xFF; *code++ = 0xD2; }
    void callRbx() { *code++ = 0xFF; *code++ = 0xD3; }
    void callRbp() { *code++ = 0xFF; *code++ = 0xD5; }
    void callRsi() { *code++ = 0xFF; *code++ = 0xD6; }
    void callRdi() { *code++ = 0xFF; *code++ = 0xD7; }

    void ret() { *code++ = 0xC3; }
    void int3() { *code++ = 0xCC; }
};

uint64_t Trampoliner::getKnownTrampoline(uint64_t target) {
    mutex.lock();
    auto iter = instances->find(target);
    auto ret = iter == instances->end() ? 0 : iter->second;
    mutex.unlock();
    return ret;
}

uint64_t Trampoliner::getKnownInverse(uint64_t target) {
    mutex.lock();
    auto iter = inverses->find(target);
    auto ret = iter == inverses->end() ? 0 : iter->second;
    mutex.unlock();
    return ret;
}

void unicallNA(uint64_t target, Signature* signature, uint64_t* cstack) {
#ifdef TRAMPOLINE_DEBUG_CALLS
    log("UnicallNA to 0x{:x} {}", target, signature->repr());
#endif
    auto state = CpuInstance.currentState();
    CpuState savestate;
    // TODO: Optimize this; we don't need to save EVERYTHING
    memcpy(&savestate, (void*) state, sizeof(CpuState));
    while(*cstack++ != 0x9090909040404040);
    auto returnRegion = *cstack++ + 8;
    auto saveRegion = *(uint64_t*) (returnRegion - 8);
    auto origSp = saveRegion + 6 * 8 + 8; // 6 saved registers + callee address

    auto stackReq = 0, objectReq = 0;
    auto allArmPlacements = signature->armPrePlacements;
    allArmPlacements.insert(allArmPlacements.end(), signature->armPostPlacements.begin(), signature->armPostPlacements.end());
    for(auto placement : allArmPlacements)
        if(auto sp = placement->position->asStack())
            stackReq = max(stackReq, sp->offset + placement->type->size);
        else if(auto op = placement->position->asObject())
            objectReq = max(objectReq, op->offset + placement->type->size);

    auto context = (uint8_t*) malloc(512*1024);
    auto contextTop = (uint64_t) (context + 512*1024 - 128);

    auto objectSpace = contextTop - objectReq;
    while(objectSpace % 16) objectSpace--;
    auto stackArgSpace = objectSpace - stackReq;
    while(stackArgSpace % 16) stackArgSpace--;
    auto newStack = (uint64_t*) stackArgSpace;

    unordered_map<Gpr, uint64_t> xregOffs;
    for(auto [position, _] : backwards(signature->x86RegisterStackOffsets))
        if(auto xp = position->asX86Gpr()) {
            auto addr = *cstack++;
#ifdef TRAMPOLINE_DEBUG_CALLS
            log("Got {}: 0x{:x}", position->repr(), addr);
#endif
            xregOffs[xp->gpr] = addr;
        }

    state->SP = (uint64_t) newStack;

    uint64_t saveX[9];
    for(auto i = (int) signature->armRegisterStackOffsets.size() - 1; i >= 0; --i) {
        auto [position, offset] = signature->armRegisterStackOffsets[i];
        auto addr = offset >= 0 ? stackArgSpace + offset : objectSpace + -offset - 16;
        if(auto ap = position->asArmGpr())
            saveX[ap->num] = state->X[ap->num] = addr;
        else if(auto sp = position->asStack())
            *(uint64_t*) (state->SP + sp->offset) = addr;
        else {
            log("Unknown position for ARM register/stack offsets: {}", position->repr());
            BAILOUT();
        }
    }

    for(auto i = (int) signature->x86PrePlacements.size() - 1; i >= 0; --i) {
        auto xplace = signature->x86PrePlacements[i], aplace = signature->armPrePlacements[i];
#ifdef TRAMPOLINE_DEBUG_CALLS
        log("Dealing with {} {} {}", i, xplace->repr(), aplace->repr());
#endif
        uint64_t value;
        auto xmmIsOffset = -1;
        if(xplace->position->isX86Gpr())
            value = *cstack++;
        else if(auto xmm = xplace->position->asXmm()) {
            if(xmm->offset == 1) {
                value = *(uint32_t*)((uint64_t) cstack + 4);
                xmmIsOffset = xmm->num;
            } else if(xmm->offset == 0 && xplace->type->size == 4) {
                value = (uint32_t) *cstack++;
                xmmIsOffset = -1;
            } else
                value = *cstack++;
        } else if(auto sp = xplace->position->asStack())
            memcpy(&value, (void*) (origSp + sp->offset), xplace->type->size);
        else if(auto opos = xplace->position->asObject()) {
            uint64_t aoff;
            if(auto gpos = opos->objectPos->asX86Gpr()) {
                assert(xregOffs.find(gpos->gpr) != xregOffs.end());
                aoff = xregOffs[gpos->gpr] + opos->offset;
            } else if(auto spos = opos->objectPos->asStack())
                aoff = *(uint64_t*) (origSp + spos->offset) + opos->offset;
            else
                BAILOUT();
            memcpy(&value, (void*) aoff, xplace->type->size);
        } else {
            log("Unknown position for X86: {}", aplace->position->repr());
            BAILOUT();
        }

#ifdef TRAMPOLINE_DEBUG_CALLS
        log("Input 0x{:x}", value);
#endif

        if(auto ap = aplace->position->asArmGpr())
            state->X[ap->num] = value;
        else if(auto vp = aplace->position->asArmVec())
            *(uint64_t*)(&state->V[vp->num]) = value;
        else if(auto sp = aplace->position->asStack())
            memcpy((void*) (state->SP + sp->offset), &value, aplace->type->size);
        else if(auto opos = aplace->position->asObject()) {
            if(auto gpos = opos->objectPos->asArmGpr())
                memcpy((void*) (state->X[gpos->num] + opos->offset), &value, aplace->type->size);
            else if(auto spos = opos->objectPos->asStack())
                memcpy((void*) (*(uint64_t*) (state->SP + spos->offset) + opos->offset), &value, aplace->type->size);
            else
                BAILOUT();
        } else {
            log("Unknown position for ARM: {}", aplace->position->repr());
            BAILOUT();
        }
    }

#ifdef TRAMPOLINE_DEBUG_CALLS
    log("Kicking off emulation for 0x{:x}", target);
#endif
    CpuInstance.runFrom(target);
#ifdef TRAMPOLINE_DEBUG_CALLS
    log("Emulation done for 0x{:x}", target);
#endif

    auto retStack = (uint64_t*) returnRegion;
    auto xmmIsOffset = -1;
    for(auto i = 0; i < signature->x86PostPlacements.size(); ++i) {
        auto xplace = signature->x86PostPlacements[i], aplace = signature->armPostPlacements[i];
#ifdef TRAMPOLINE_DEBUG_CALLS
        log("Dealing with {} {} {}", i, xplace->repr(), aplace->repr());
#endif
        unsigned char value[16];
        if(auto ap = aplace->position->asArmGpr())
            memcpy(&value, &state->X[ap->num], aplace->type->size);
        else if(auto vp = aplace->position->asArmVec())
            memcpy(&value, &state->V[vp->num], aplace->type->size);
        else if(auto sp = aplace->position->asStack())
            memcpy(&value, (void*) (state->SP + sp->offset), aplace->type->size);
        else if(auto opos = aplace->position->asObject()) {
            if(auto gpos = opos->objectPos->asArmGpr()) {
                assert(gpos->num < 9);
                memcpy(&value, (uint64_t*) (saveX[gpos->num] + opos->offset), aplace->type->size);
            } else if(auto spos = opos->objectPos->asStack())
                memcpy(&value, (uint64_t*) (*(uint64_t*) (newStack + spos->offset) + opos->offset), aplace->type->size);
            else
                BAILOUT();
        } else {
            log("Unknown position for ARM: {}", aplace->position->repr());
            BAILOUT();
        }
#ifdef TRAMPOLINE_DEBUG_CALLS
        log("Incoming 0x{:x}", *((uint64_t*) value));
#endif
        if(xplace->position->isX86Gpr()) {
            if(xplace->type->size != 8)
                memset(retStack, 0, 8); // Erase existing contents
            memcpy(retStack++, value, xplace->type->size);
        } else if(auto xmm = xplace->position->asXmm()) {
            if(xmm->offset == 4) {
                assert(xmmIsOffset == xmm->num);
                memcpy(retStack, value, xplace->type->size);
                retStack = (uint64_t*) ((uint64_t) retStack + 4);
                xmmIsOffset = -1;
            } else if(xmm->offset == 0 && xplace->type->size == 4) {
                if(xmmIsOffset != -1) {
                    retStack = (uint64_t*) ((uint64_t) retStack + 4);
                }
                memcpy(retStack, value, xplace->type->size);
                retStack = (uint64_t*) ((uint64_t) retStack + 4);
                xmmIsOffset = xmm->num;
            } else
                memcpy(retStack++, value, xplace->type->size);
        } else if(auto opos = xplace->position->asObject()) {
            uint64_t aoff;
            if(auto gpos = opos->objectPos->asX86Gpr()) {
                assert(xregOffs.find(gpos->gpr) != xregOffs.end());
                aoff = xregOffs[gpos->gpr] + opos->offset;
            } else if(auto spos = opos->objectPos->asStack())
                aoff = *(uint64_t*) (origSp + spos->offset) + opos->offset;
            else
                BAILOUT();
            memcpy((void*) aoff, value, xplace->type->size);
        } else {
            log("Unknown position for X86: {}", xplace->position->repr());
            BAILOUT();
        }
    }

#ifdef TRAMPOLINE_DEBUG_CALLS
    log("Returning to native code");
#endif

    free(context);
    memcpy((void*) state, &savestate, sizeof(CpuState));
}

uint64_t Trampoliner::getNATrampoline(uint64_t target, BaseSignature* signature) {
    auto trampoline = getKnownTrampoline(target);
    if(trampoline != 0) return trampoline;

    mutex.lock();

    auto srepr = signature->repr();
    //log("Generating generic Native->Arm trampoline for " << srepr);
    MCBuilder builder;
    //log("Precompiling target");
    //CpuInstance->precompile(target);
    //log("Done precompiling! Continuing with trampoline setup");
    //log("Trampoline located at 0x" << hex << builder.start);
    auto tt = allocateTrampolineTrampoline(NATIVE_TO_ARM | builder.start, target, signature);
    (*instances)[target] = (uint64_t) tt;
    (*inverses)[(uint64_t) tt] = target;
    auto iter = nativeToArm.find(srepr);
    builder.pushLiteral(target);
    if(iter != nativeToArm.end()) {
        builder.raxLiteral(iter->second);
        builder.jmpRax();
        mutex.unlock();
        return (uint64_t) tt;
    }
    //log("Unknown signature; building new base trampoline too");
    nativeToArm[srepr] = (uint64_t) builder.code;

    builder.popR11();
    builder.pushRbx();
    builder.pushRbp();
    builder.pushR12();
    builder.pushR13();
    builder.pushR14();
    builder.pushR15();
    builder.pushRsp();
    builder.popRbx();

    auto stackCount = 0;
    for(auto placement : signature->x86PostPlacements)
        if(placement->position->isX86Gpr() || (placement->position->isXmm() && placement->position->asXmm()->offset == 0)) {
            builder.pushLiteral(0);
            stackCount++;
        }

    builder.pushRbx();
    stackCount++;
    builder.pushRsp();
    builder.popRbx();

    for(auto placement : signature->x86PrePlacements) {
        auto position = placement->position;
        auto type = placement->type;
        if(auto xp = position->asX86Gpr()) {
            switch(xp->gpr) {
                case Gpr::Rdi:
                    builder.pushRdi();
                    break;
                case Gpr::Rsi:
                    builder.pushRsi();
                    break;
                case Gpr::Rdx:
                    builder.pushRdx();
                    break;
                case Gpr::Rcx:
                    builder.pushRcx();
                    break;
                case Gpr::R8:
                    builder.pushR8();
                    break;
                case Gpr::R9:
                    builder.pushR9();
                    break;
                default:
                    BAILOUT();
            }
            stackCount++;
        } else if(auto xmm = position->asXmm()) {
            if(xmm->offset != 0) continue;
            switch(xmm->num) {
                case 0:
                    builder.movqRaxXmm0();
                    break;
                case 1:
                    builder.movqRaxXmm1();
                    break;
                case 2:
                    builder.movqRaxXmm2();
                    break;
                case 3:
                    builder.movqRaxXmm3();
                    break;
                case 4:
                    builder.movqRaxXmm4();
                    break;
                case 5:
                    builder.movqRaxXmm5();
                    break;
                case 6:
                    builder.movqRaxXmm6();
                    break;
                case 7:
                    builder.movqRaxXmm7();
                    break;
            }
            builder.pushRax();
            stackCount++;
        }
    }

    for(auto [position, _] : signature->x86RegisterStackOffsets) {
        if(auto xp = position->asX86Gpr()) {
            switch(xp->gpr) {
                case Gpr::Rdi:
                    builder.pushRdi();
                    break;
                case Gpr::Rsi:
                    builder.pushRsi();
                    break;
                case Gpr::Rdx:
                    builder.pushRdx();
                    break;
                case Gpr::Rcx:
                    builder.pushRcx();
                    break;
                case Gpr::R8:
                    builder.pushR8();
                    break;
                case Gpr::R9:
                    builder.pushR9();
                    break;
                default:
                    BAILOUT();
            }
            stackCount++;
        }
    }

    builder.pushRbx();
    builder.pushLiteral(0x9090909040404040);
    if(!(stackCount & 1))
        builder.pushLiteral(0);

    builder.pushRsp();
    builder.popRdx();
    builder.pushR11();
    builder.popRdi();
    builder.rsiLiteral((uint64_t) signature);
    builder.raxLiteral((uint64_t) unicallNA);
    builder.callRax();

    builder.pushRbx();
    builder.popRsp();
    builder.popRbx();

    for(auto placement : signature->x86PostPlacements) {
        auto position = placement->position;
        auto type = placement->type;

        if(auto gpr = position->asX86Gpr()) {
            switch(gpr->gpr) {
                case Gpr::Rax:
                    builder.popRax();
                    break;
                case Gpr::Rdx:
                    builder.popRdx();
                    break;
                default:
                    BAILOUT();
            }
        } else if(auto xmm = position->asXmm()) {
            if(xmm->offset != 0) continue;
            builder.pushRax();
            builder.popR11();
            builder.popRax();
            switch(xmm->num) {
                case 0:
                    builder.movqXmm0Rax();
                    break;
                case 1:
                    builder.movqXmm1Rax();
                    break;
                default:
                    BAILOUT();
            }
            builder.pushR11();
            builder.popRax();
        }
    }

    // In theory, we should already be at this stack address; let's just ... be sure.
    builder.pushRbx();
    builder.popRsp();

    builder.popR15();
    builder.popR14();
    builder.popR13();
    builder.popR12();
    builder.popRbp();
    builder.popRbx();
    builder.ret();

    mutex.unlock();
    return (uint64_t) tt;
}

void unicallAN(uint64_t target, Signature* signature) {
    //logLock.lock();
    auto state = CpuInstance.currentState();
#ifdef TRAMPOLINE_DEBUG_CALLS
    log("In unicallAN to 0x{:x} from 0x{:x} {}", target, state->X30, signature->repr());
#endif
    int64_t* sp = 0;
    asm("movq %%rbp, %0" : "=r"(sp));
    auto tsp = sp;
    for(; *sp != 0x7090709040404040; ++sp);
    auto secondStage = *--sp + 3; // Skip pop r11 and ret before second stage
    auto context = (uint8_t*) malloc(512*1024);
    memset(context, 0, 512*1024);
    auto contextTop = (uint64_t) (context + 512*1024 - 128);
    uint64_t bottomsp;
    asm("movq %%rsp, %0" : "=r"(bottomsp));

    auto stackReq = 0, objectReq = 0;
    auto allX86Placements = signature->x86PrePlacements;
    allX86Placements.insert(allX86Placements.end(), signature->x86PostPlacements.begin(), signature->x86PostPlacements.end());
    for(auto placement : allX86Placements)
        if(auto stp = placement->position->asStack())
            stackReq = max(stackReq, stp->offset + placement->type->size);
        else if(auto op = placement->position->asObject())
            objectReq = max(objectReq, op->offset + placement->type->size);

    auto objectSpace = contextTop - objectReq;
    while(objectSpace % 16) objectSpace--;
    auto stackArgSpace = objectSpace - stackReq;
    while(stackArgSpace % 16) stackArgSpace--;
    auto newStack = (uint64_t*) stackArgSpace;

    *--newStack = bottomsp - 16;
    *--newStack = target;
    auto xmmIsOffset = -1;

    for(auto i = (int) signature->x86PrePlacements.size() - 1; i >= 0; --i) {
        auto xplace = signature->x86PrePlacements[i], aplace = signature->armPrePlacements[i];
#ifdef TRAMPOLINE_DEBUG_CALLS
        log("Dealing with {} {} {}", i, xplace->repr(), aplace->repr());
#endif
        uint64_t value;
        if(auto ap = aplace->position->asArmGpr())
            value = state->X[ap->num];
        else if(auto vp = aplace->position->asArmVec())
            value = *(uint64_t*)(&state->V[vp->num]);
        else if(auto stp = aplace->position->asStack())
            value = *(uint64_t *) (state->SP + stp->offset);
        else if(auto opos = aplace->position->asObject()) {
            if(auto gpos = opos->objectPos->asArmGpr())
                value = *(uint64_t *) (state->X[gpos->num] + opos->offset);
            else if(auto spos = opos->objectPos->asStack())
                value = *(uint64_t *) (*(uint64_t*) (state->SP + spos->offset) + opos->offset);
            else
                BAILOUT();
        } else {
            log("Unknown position for ARM: {}", aplace->position->repr());
            BAILOUT();
        }
#ifdef TRAMPOLINE_DEBUG_CALLS
        log("Incoming 0x{:x}", value);
#endif
        if(aplace->type->isFunctionPointer()) {
            log("Got function pointer!");
            if(!CpuInstance.isValidCodePointer(CodeSource::Speculation, value, state))
                log("Bad function addr");
            else {
                auto ftype = aplace->type->asFunctionPointer()->memberType->asFunction();
                assert(ftype != nullptr);
                auto fptr = TrampolinerInstance.getKnownTrampoline(value);
                if(fptr != 0)
                    value = fptr;
                else if(!TrampolinerInstance.getKnownInverse(value))
                    value = TrampolinerInstance.getNATrampoline(value, new Signature("~function-pointer~", ftype));
                log("Final function pointer at 0x{:x}", value);
            }
        } else if(aplace->type->isBlockPointer()) {
            log("Got block pointer!");
            if(value == 0)
                log("... But it's null");
            else {
                auto bi = (BlockInternal*) value;
                log("Block function pointer is 0x{:x}", bi->function);
                if(!CpuInstance.isValidCodePointer(CodeSource::Speculation, bi->function, state))
                    log("Bad function addr");
                else {
                    auto ftype = aplace->type->asBlockPointer()->memberType->asFunction();
                    assert(ftype != nullptr);
                    auto fptr = TrampolinerInstance.getKnownTrampoline(bi->function);
                    if(fptr != 0)
                        bi->function = fptr;
                    else if(!TrampolinerInstance.getKnownInverse(bi->function))
                        bi->function = TrampolinerInstance.getNATrampoline(bi->function,
                                                                           new Signature("~block-pointer~", ftype));
                    log("Final block function pointer at 0x{:x}", bi->function);
                }
            }
        }

        if(xplace->position->isX86Gpr())
            *--newStack = value;
        else if(auto xmm = xplace->position->asXmm()) {
            if(xmm->offset == 1) {
                *--newStack = value << 32;
                xmmIsOffset = xmm->num;
            } else if(xmm->offset == 0 && xplace->type->size == 4) {
                if(xmmIsOffset == xmm->num)
                    *((uint32_t *) newStack) = (uint32_t) value;
                else
                    *--newStack = (uint32_t) value;
                xmmIsOffset = -1;
            } else {
                *--newStack = value;
                xmmIsOffset = -1;
            }
        } else if(auto sttp = xplace->position->asStack())
            memcpy((void*) (stackArgSpace + sttp->offset), &value, xplace->type->size);
        else if(auto opos = xplace->position->asObject()) {
            uint64_t aoff;
            if(auto gpos = opos->objectPos->asX86Gpr()) {
                auto found = false;
                for(auto [tgt, offset] : signature->x86RegisterStackOffsets)
                    if(tgt->isX86Gpr() && tgt->asX86Gpr()->gpr == gpos->gpr) {
                        found = true;
                        if(offset >= 0)
                            BAILOUT();
                        else
                            aoff = -offset - 16 + opos->offset;
                        break;
                    }
                assert(found);
            } else if(auto spos = opos->objectPos->asStack()) {
                auto found = false;
                for(auto [tgt, offset] : signature->x86RegisterStackOffsets)
                    if(tgt->isStack() && tgt->asStack()->offset == spos->offset) {
                        found = true;
                        if(offset >= 0)
                            BAILOUT();
                        else
                            aoff = -offset - 16 + opos->offset;
                        break;
                    }
                assert(found);
            } else
                BAILOUT();
            memcpy((void*) (objectSpace + aoff), &value, xplace->type->size);
        } else {
            log("Unknown position for X86: {}", xplace->position->repr());
            BAILOUT();
        }
    }

    unordered_map<Gpr, uint64_t> objectRegOffsets;
    for(auto i = (int) signature->x86RegisterStackOffsets.size() - 1; i >= 0; --i) {
        auto [position, offset] = signature->x86RegisterStackOffsets[i];
        uint64_t addr;
        if(offset >= 0)
            addr = stackArgSpace + offset;
        else
            addr = objectSpace + -offset - 16;
        if(auto xp = position->asX86Gpr()) {
            objectRegOffsets[xp->gpr] = *--newStack = addr;
        } else if(auto stp = position->asStack())
            *(uint64_t*) (stackArgSpace + stp->offset) = addr;
        else {
            log("Unhandled position for register/stack offset: {}", position->repr());
            BAILOUT();
        }
    }

#ifdef TRAMPOLINE_DEBUG_CALLS
    //log("Calling second stage... New stack at 0x" << hex << (uint64_t) newStack);
    //for(auto dsp = newStack; (uint64_t) dsp < contextTop; dsp++)
    //    log("\t0x" << hex << (uint64_t) dsp << ": 0x" << *dsp);
#endif
    auto ret = ((uint64_t(*)(uint64_t*)) secondStage)(newStack);
#ifdef TRAMPOLINE_DEBUG_CALLS
    log("Returned from second stage successfully!");
#endif
    asm("movq %%rsp, %0" : "=r"(bottomsp));

    auto retStack = stackArgSpace;
    for(auto i = 0; i < signature->x86PostPlacements.size(); ++i) {
        auto xplace = signature->x86PostPlacements[i], aplace = signature->armPostPlacements[i];
#ifdef TRAMPOLINE_DEBUG_CALLS
        log("Dealing with {} {} {}", i, xplace->repr(), aplace->repr());
#endif
        uint64_t value;
        if(xplace->position->isX86Gpr()) {
            while(retStack % 8) retStack--;
            retStack -= 8;
            value = *(uint64_t*) retStack;
        } else if(auto xmm = xplace->position->asXmm()) {
            if(xplace->type->size == 8 || xmm->offset == 0) {
                while(retStack % 8) retStack--;
                retStack -= 8;
            }
            if(xplace->type->size == 4)
                value = ((uint32_t*) retStack)[xmm->offset];
            else
                value = *(uint64_t*) retStack;
        } else if(auto opos = xplace->position->asObject()) {
            if(auto xp = opos->objectPos->asX86Gpr())
                value = *(uint64_t*) (objectRegOffsets[xp->gpr] + opos->offset);
            else {
                log("Unhandled position for register/stack offset: {}", opos->objectPos->repr());
                BAILOUT();
            }
        } else {
            log("Unknown position for x86: {}", xplace->position->repr());
            BAILOUT();
        }
#ifdef TRAMPOLINE_DEBUG_CALLS
        log("Incoming 0x{:x}", value);
#endif
        if(auto ap = aplace->position->asArmGpr()) {
            auto xp = &state->X[ap->num];
            *xp = 0;
            memcpy(xp, &value, aplace->type->size);
        } else if(auto vp = aplace->position->asArmVec())
            *(uint64_t*)(&state->V[vp->num]) = value;
        else if(auto opos = aplace->position->asObject()) {
            void* ptr;
            if(auto gpos = opos->objectPos->asArmGpr())
                ptr = (void*) (state->X[gpos->num] + opos->offset);
            else if(auto spos = opos->objectPos->asStack())
                ptr = (void*) (*(uint64_t*) (state->SP + spos->offset) + opos->offset);
            else
                BAILOUT();
            memcpy(ptr, &value, aplace->type->size);
        } else {
            log("Unknown position for ARM: {}", aplace->position->repr());
            BAILOUT();
        }
    }

    free(context);
    //log("Going back to ARM code at 0x{:x}", state->X30);
    //logLock.unlock();
}

uint64_t Trampoliner::getANTrampoline(uint64_t target, BaseSignature* signature) {
    auto trampoline = getKnownTrampoline(target);
    if(trampoline != 0) return trampoline;

    mutex.lock();

    auto srepr = signature->repr();
    //log("Generating generic ARM->Native trampoline for " << hex << target << " -- " << srepr);
    MCBuilder builder;
    auto tt = allocateTrampolineTrampoline(ARM_TO_NATIVE | builder.start, target, signature);
    (*instances)[target] = (uint64_t) tt;
    //log("Trampoline located at 0x" << hex << builder.start);
    (*inverses)[(uint64_t) tt] = target;
    auto iter = armToNative.find(srepr);
    builder.pushLiteral(target);
    if(iter != armToNative.end()) {
        builder.raxLiteral(iter->second);
        builder.jmpRax();
        mutex.unlock();
        return (uint64_t) tt;
    }
    //log("Unknown signature; building new base trampoline too");
    armToNative[srepr] = (uint64_t) builder.code;

    // First stage
    builder.popRdi();
    builder.rsiLiteral((uint64_t) signature);
    builder.raxLiteral((uint64_t) unicallAN);
    builder.pushLiteral(0x7090709040404040);
    builder.callRax();
    // These will get skipped for the second stage
    builder.popR11();
    builder.ret();

    // Second stage
    builder.pushRbp();

    builder.pushRdi();
    builder.popRsp();

    for(auto [position, _] : signature->x86RegisterStackOffsets)
        if(auto xp = position->asX86Gpr()) {
            switch(xp->gpr) {
                case Gpr::Rdi:
                    builder.popRdi();
                    break;
                case Gpr::Rsi:
                    builder.popRsi();
                    break;
                case Gpr::Rdx:
                    builder.popRdx();
                    break;
                case Gpr::Rcx:
                    builder.popRcx();
                    break;
                case Gpr::R8:
                    builder.popR8();
                    break;
                case Gpr::R9:
                    builder.popR9();
                    break;
                default:
                    BAILOUT();
            }
        }

    for(auto placement : signature->x86PrePlacements) {
        auto position = placement->position;
        auto type = placement->type;
        if(auto xp = position->asX86Gpr()) {
            switch(xp->gpr) {
                case Gpr::Rdi:
                    builder.popRdi();
                    break;
                case Gpr::Rsi:
                    builder.popRsi();
                    break;
                case Gpr::Rdx:
                    builder.popRdx();
                    break;
                case Gpr::Rcx:
                    builder.popRcx();
                    break;
                case Gpr::R8:
                    builder.popR8();
                    break;
                case Gpr::R9:
                    builder.popR9();
                    break;
                default:
                    BAILOUT();
            }
        } else if(auto xmm = position->asXmm()) {
            if(xmm->offset != 0) continue;
            builder.popRax();
            switch(xmm->num) {
                case 0:
                    builder.movqXmm0Rax();
                    break;
                case 1:
                    builder.movqXmm1Rax();
                    break;
                case 2:
                    builder.movqXmm2Rax();
                    break;
                case 3:
                    builder.movqXmm3Rax();
                    break;
                case 4:
                    builder.movqXmm4Rax();
                    break;
                case 5:
                    builder.movqXmm5Rax();
                    break;
                case 6:
                    builder.movqXmm6Rax();
                    break;
                case 7:
                    builder.movqXmm7Rax();
                    break;
            }
        }
    }

    builder.popRax();
    builder.popRbp();
    //builder.int3();
    builder.callRax();

    for(auto placement : signature->x86PostPlacements) {
        auto position = placement->position;
        auto type = placement->type;
        if(auto gpr = position->asX86Gpr())
            switch(gpr->gpr) {
                case Gpr::Rax:
                    builder.pushRax();
                    break;
                case Gpr::Rdx:
                    builder.pushRdx();
                    break;
                default:
                    BAILOUT();
            }
        else if(auto xmm = position->asXmm()) {
            if(xmm->offset != 0) continue;
            switch(xmm->num) {
                case 0:
                    if(placement->type->size == 8)
                        builder.movqRaxXmm0();
                    else
                        builder.movdEaxXmm0();
                    break;
                case 1:
                    if(placement->type->size == 8)
                        builder.movqRaxXmm1();
                    else
                        builder.movdEaxXmm1();
                    break;
                default:
                    BAILOUT();
            }
            builder.pushRax();
        }
    }

    builder.pushRbp();
    builder.popRsp();
    builder.popRbp();

    builder.ret();

    mutex.unlock();

    return (uint64_t) tt;
}

uint64_t Trampoliner::getNATrampoline(uint64_t target, const string& name, const string& signature) {
    auto trampoline = getKnownTrampoline(target);
    if(trampoline != 0) return trampoline;

    //log("Generating Native->ARM trampoline for " << signature << " at 0x" << hex << target);
    auto ftype = parseObjCSignature((char*) signature.c_str());
    return getNATrampoline(target, new Signature(name, ftype));
}

uint64_t Trampoliner::getANTrampoline(uint64_t target, const string& name, const string& signature) {
    auto trampoline = getKnownTrampoline(target);
    if(trampoline != 0) return trampoline;

    //log("Generating ARM->Native trampoline for " << signature << " at 0x" << hex << target);
    auto ftype = parseObjCSignature((char*) signature.c_str());
    return getANTrampoline(target, new Signature(name, ftype));
}

Trampoliner::Trampoliner() {
    instances = new unordered_map<uint64_t, uint64_t>;
    inverses = new unordered_map<uint64_t, uint64_t>;
    auto region = (uint64_t) mmap(nullptr, 0x2002000, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if(region == -1ULL) {
        log("Failed to allocate trampoliner region");
        BAILOUT();
    }
    trampolineBase = (TrampolineTrampoline*) (region + 0x1000);
    mprotect(trampolineBase, 0x2000000, PROT_READ | PROT_WRITE);
}

TrampolineTrampoline* Trampoliner::allocateTrampolineTrampoline(uint64_t trampoline, uint64_t target, BaseSignature* signature) {
    // ALWAYS LOCK THE MUTEX FIRST!
    if(trampolineCounter >= trampolineMax) {
        log("Ran out of trampoline-trampoline slots");
        BAILOUT();
    }
    auto tt = &trampolineBase[trampolineCounter];
    tt->trampoline = trampoline;
    tt->target = target;
    tt->canary = ~target;
    tt->signature = signature;
    trampolineCounter++;
    return tt;
}
