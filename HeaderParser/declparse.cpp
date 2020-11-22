#include <clang-c/Index.h>
#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
using namespace std;
namespace fs = ::boost::filesystem;

ostream& operator<<(ostream& stream, const CXString& str) {
    stream << clang_getCString(str);
    clang_disposeString(str);
    return stream;
}

string* curRecordType = nullptr;

string BuildType(CXType type, bool noRecord = false, bool inRecord = false) {
    auto cursor = clang_getTypeDeclaration(type);
    switch(type.kind) {
        case CXType_FunctionProto: {
            //auto fptr = (boost::format("<function,%1%") % BuildType(clang_getResultType(type))).str();
            string fptr = "~f{" + BuildType(clang_getResultType(type));
            auto count = clang_getNumArgTypes(type);
            for(auto i = 0; i < count; ++i)
                fptr += BuildType(clang_getArgType(type, i));
            if(clang_isFunctionTypeVariadic(type))
                fptr += "$";
            return fptr + "}";
        }
        case CXType_FunctionNoProto:
            return "?";
        case CXType_Pointer:
            return "^" + BuildType(clang_getPointeeType(type), true);
        case CXType_Elaborated:
            return BuildType(clang_Type_getNamedType(type), noRecord, inRecord);
        case CXType_Typedef: return BuildType(clang_getCanonicalType(type), noRecord, inRecord);
        case CXType_Enum:
            return BuildType(clang_getEnumDeclIntegerType(cursor));
        case CXType_Record: {
            if(noRecord)
                return "v";
            auto ctype = clang_getCanonicalType(type);
            if(ctype.kind != CXType_Invalid)
                type = ctype;
            string str;
            clang_visitChildren(
                    clang_getTypeDeclaration(type),
                    [](CXCursor c, CXCursor parent, CXClientData client_data) {
                        auto str = (string*) client_data;
                        if(c.kind != CXCursor_FieldDecl) return CXChildVisit_Continue;
                        *str += BuildType(clang_getCursorType(c), false, true);
                        return CXChildVisit_Continue;
                    }, (CXClientData) &str
            );
            return "{=" + str + "}";
            //return string(clang_getCString(clang_Type_getObjCEncoding(type)));
        }
        case CXType_ObjCId: return "@";
        case CXType_ObjCSel: return ":";
        case CXType_Bool: return "B";
        case CXType_Char_S:
        case CXType_SChar:
            return "c";
        case CXType_Char_U:
        case CXType_UChar:
            return "C";
        case CXType_Short: return "s";
        case CXType_UShort: return "S";
        case CXType_Int: return "i";
        case CXType_UInt: return "I";
        case CXType_Long: return "l";
        case CXType_ULong: return "L";
        case CXType_LongLong: return "q";
        case CXType_ULongLong: return "Q";
        case CXType_Float: return "f";
        case CXType_Double: return "d";
        case CXType_LongDouble: return "D";
        case CXType_Complex: {
            auto et = BuildType(clang_getElementType(type));
            return "{=" + et + et + "}";
        }
        case CXType_Void: return "v";
        case CXType_IncompleteArray:
            return "^" + BuildType(clang_getArrayElementType(type), true, inRecord);
        case CXType_ConstantArray: {
            if(!inRecord)
                return "^" + BuildType(clang_getElementType(type), true, inRecord);
            auto tstr = BuildType(clang_getElementType(type), false, inRecord);
            string str = "";
            for(auto i = 0; i < clang_getNumElements(type); ++i)
                str += tstr;
            return str;
        }
        case CXType_Vector:
        case CXType_ExtVector:
            return (boost::format("~V{%1%%2%}") % clang_getNumElements(type) % BuildType(clang_getElementType(type))).str();
        case CXType_ObjCObjectPointer: return "@";
        case CXType_ObjCClass: return "@";
        case CXType_BlockPointer:
            return "%" + BuildType(clang_getPointeeType(type));
        default:
            cout << endl << "~~~Unknown CXType " << clang_getCString(clang_getTypeKindSpelling(type.kind)) << endl;
            return "?";
    }
}

int main(int argc, char** argv) {
    auto index = clang_createIndex(0, 0);
    auto unit = clang_parseTranslationUnit(index, argv[1], &argv[2], argc - 2, nullptr, 0, CXTranslationUnit_None);
    if(unit == nullptr) return 1;
    auto ds = clang_getDiagnosticSetFromTU(unit);
    auto dsc = clang_getNumDiagnosticsInSet(ds);
    for(auto i = 0; i < dsc; ++i) {
        auto diag = clang_getDiagnosticInSet(ds, i);
        auto sev = clang_getDiagnosticSeverity(diag);
        if(sev == CXDiagnostic_Error || sev == CXDiagnostic_Fatal) {
            cout << clang_getDiagnosticSpelling(diag) << endl;
            return 1;
        }
    }
    auto cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(
            cursor,
            [](CXCursor c, CXCursor parent, CXClientData client_data) {
                switch(clang_getCursorKind(c)) {
                    case CXCursor_FunctionDecl: {
                        auto loc = clang_getCursorLocation(c);
                        CXFile file;
                        clang_getExpansionLocation(loc, &file, nullptr, nullptr, nullptr);
                        string fn = clang_getCString(clang_getFileName(file));
                        string sym = clang_getCString(clang_getCursorSpelling(c));
                        auto type = BuildType(clang_getCursorType(c));
                        cout << fn << ":::" << sym << ":::" << type << endl;
                        return CXChildVisit_Continue;
                    }
                    default:
                        return CXChildVisit_Recurse;
                }
            }, nullptr);
    return 0;
}
