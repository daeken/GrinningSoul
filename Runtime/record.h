#pragma once

#include <boost/preprocessor.hpp>
#include "repr.h"

#define EXTERNAL_INIT ~~external-init~~
#define IF_NOT_EXTERNAL_INIT(elem, x) BOOST_PP_IIF(BOOST_PP_IS_BEGIN_PARENS(elem), BOOST_PP_IDENTITY(x), BOOST_PP_EMPTY)()
#define CONTAINS_EXTERNAL_INIT_OP(s, state, elem) BOOST_PP_OR(state, BOOST_PP_NOT(BOOST_PP_IS_BEGIN_PARENS(elem)))
#define CONTAINS_EXTERNAL_INIT(seq) BOOST_PP_SEQ_FOLD_LEFT(CONTAINS_EXTERNAL_INIT_OP, 0, seq)

#define AS_STRUCT(name, ...) (struct, name, BOOST_PP_TUPLE_TO_SEQ((IRepr, ##__VA_ARGS__)))
#define AS_CLASS(name, ...) (class, name, BOOST_PP_TUPLE_TO_SEQ((IRepr, ##__VA_ARGS__)))

#define MAKE_ONE_VARIABLE(r, data, elem) \
    IF_NOT_EXTERNAL_INIT(elem, BOOST_PP_TUPLE_ELEM(0, elem) BOOST_PP_TUPLE_ELEM(1, elem);)
#define ARG_ONE_VARIABLE(r, data, elem) \
    IF_NOT_EXTERNAL_INIT(elem, (BOOST_PP_TUPLE_ELEM(0, elem) BOOST_PP_TUPLE_ELEM(1, elem)))
#define INIT_ONE_VARIABLE(r, data, elem) \
    IF_NOT_EXTERNAL_INIT(elem, (BOOST_PP_TUPLE_ELEM(1, elem)(std::move(BOOST_PP_TUPLE_ELEM(1, elem)))))
#define INIT_EMPTY_ONE_VARIABLE(r, data, elem) \
    IF_NOT_EXTERNAL_INIT(elem, (BOOST_PP_TUPLE_ELEM(1, elem)()))
#define REPR_ONE_VARIABLE(r, data, elem) \
    IF_NOT_EXTERNAL_INIT(elem, ret += (" " BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(1, elem)) "=") + ::repr(BOOST_PP_TUPLE_ELEM(1, elem));)

#define MAKE_PUBLIC(r, data, elem) (virtual public elem)

#define _RECORD(baseType, ...) \
    BOOST_PP_TUPLE_ELEM(0, baseType) BOOST_PP_TUPLE_ELEM(1, baseType) : BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH(MAKE_PUBLIC, , BOOST_PP_TUPLE_ELEM(2, baseType))) { \
    public: \
        BOOST_PP_SEQ_FOR_EACH(MAKE_ONE_VARIABLE, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
        inline BOOST_PP_TUPLE_ELEM(1, baseType)() : BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH(INIT_EMPTY_ONE_VARIABLE, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))) { __init(); } \
        explicit inline BOOST_PP_TUPLE_ELEM(1, baseType)(BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH(ARG_ONE_VARIABLE, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)))) : BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH(INIT_ONE_VARIABLE, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))) { __init(); } \
        inline std::string repr() const override { \
            std::string ret = "<" BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(1, baseType)); \
            BOOST_PP_SEQ_FOR_EACH(REPR_ONE_VARIABLE, , BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
            return ret + ">"; \
        } \
        void __init() BOOST_PP_IF(CONTAINS_EXTERNAL_INIT(BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)), ;, {}) \
    }

#define RECORD(baseType, ...) \
    _RECORD(BOOST_PP_IF(BOOST_PP_IS_BEGIN_PARENS(baseType), baseType, AS_STRUCT(baseType)), __VA_ARGS__)

#define MAKE_ONE_SUBVARIABLE(r, data, i, elem) \
    BOOST_PP_IF(BOOST_PP_AND(BOOST_PP_IS_BEGIN_PARENS(elem), BOOST_PP_NOT_EQUAL(i, 0)), \
        BOOST_PP_TUPLE_ELEM(0, elem) BOOST_PP_TUPLE_ELEM(1, elem); \
        , \
    )
#define ARG_ONE_SUBVARIABLE(r, data, i, elem) \
    BOOST_PP_IF(BOOST_PP_AND(BOOST_PP_IS_BEGIN_PARENS(elem), BOOST_PP_NOT_EQUAL(i, 0)), \
        (BOOST_PP_TUPLE_ELEM(0, elem) BOOST_PP_TUPLE_ELEM(1, elem)) \
        , \
    )
#define INIT_ONE_SUBVARIABLE(r, data, i, elem) \
    BOOST_PP_IF(BOOST_PP_AND(BOOST_PP_IS_BEGIN_PARENS(elem), BOOST_PP_NOT_EQUAL(i, 0)), \
        (BOOST_PP_TUPLE_ELEM(1, elem)(std::move(BOOST_PP_TUPLE_ELEM(1, elem)))) \
        , \
    )
#define INIT_EMPTY_ONE_SUBVARIABLE(r, data, i, elem) \
    BOOST_PP_IF(BOOST_PP_AND(BOOST_PP_IS_BEGIN_PARENS(elem), BOOST_PP_NOT_EQUAL(i, 0)), \
        (BOOST_PP_TUPLE_ELEM(1, elem)()) \
        , \
    )
#define REPR_ONE_SUBVARIABLE(r, data, i, elem) \
    BOOST_PP_IF(BOOST_PP_AND(BOOST_PP_IS_BEGIN_PARENS(elem), BOOST_PP_NOT_EQUAL(i, 0)), \
        ret += (" " BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(1, elem)) "=") + ::repr(BOOST_PP_TUPLE_ELEM(1, elem)); \
        , \
    )

#define MAKE_ONE_SUBRECORD(r, baseType, elem) \
    BOOST_PP_TUPLE_ELEM(0, baseType) BOOST_PP_TUPLE_ELEM(1, baseType)::BOOST_PP_TUPLE_ELEM(0, elem) : public BOOST_PP_TUPLE_ELEM(1, baseType) { \
    public: \
        BOOST_PP_SEQ_FOR_EACH_I(MAKE_ONE_SUBVARIABLE, , BOOST_PP_TUPLE_TO_SEQ(elem)) \
        inline BOOST_PP_TUPLE_ELEM(0, elem)() : BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH_I(INIT_EMPTY_ONE_SUBVARIABLE, , BOOST_PP_TUPLE_TO_SEQ(elem))) { __init(); } \
        explicit inline BOOST_PP_TUPLE_ELEM(0, elem)(BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH_I(ARG_ONE_SUBVARIABLE, , BOOST_PP_TUPLE_TO_SEQ(elem)))) : BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH_I(INIT_ONE_SUBVARIABLE, , BOOST_PP_TUPLE_TO_SEQ(elem))) { __init(); } \
        inline std::string repr() const override { \
            std::string ret = "<" BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(1, baseType)) "::" BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(0, elem)); \
            BOOST_PP_SEQ_FOR_EACH_I(REPR_ONE_SUBVARIABLE, , BOOST_PP_TUPLE_TO_SEQ(elem)) \
            return ret + ">"; \
        } \
        inline bool BOOST_PP_CAT(is, BOOST_PP_TUPLE_ELEM(0, elem))() override { return true; } \
        inline BOOST_PP_TUPLE_ELEM(0, elem)* BOOST_PP_CAT(as, BOOST_PP_TUPLE_ELEM(0, elem))() override { return this; } \
        void __init() BOOST_PP_IF(CONTAINS_EXTERNAL_INIT(BOOST_PP_TUPLE_TO_SEQ(BOOST_PP_TUPLE_POP_FRONT(elem))), ;, {}) \
    };
#define PREDEFINE_ONE_SUBRECORD(r, baseType, elem) \
    BOOST_PP_TUPLE_ELEM(0, baseType) BOOST_PP_TUPLE_ELEM(0, elem); \
    inline virtual bool BOOST_PP_CAT(is, BOOST_PP_TUPLE_ELEM(0, elem))() { return false; } \
    inline virtual BOOST_PP_TUPLE_ELEM(0, elem)* BOOST_PP_CAT(as, BOOST_PP_TUPLE_ELEM(0, elem))() { return nullptr; }

#define _DISCRIMINATED_UNION(baseType, ...) \
    BOOST_PP_TUPLE_ELEM(0, baseType) BOOST_PP_TUPLE_ELEM(1, baseType) : BOOST_PP_SEQ_ENUM(BOOST_PP_SEQ_FOR_EACH(MAKE_PUBLIC, , BOOST_PP_TUPLE_ELEM(2, baseType))) { \
    public: \
        BOOST_PP_SEQ_FOR_EACH(PREDEFINE_ONE_SUBRECORD, baseType, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
    }; \
    BOOST_PP_SEQ_FOR_EACH(MAKE_ONE_SUBRECORD, baseType, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \

#define DISCRIMINATED_UNION(baseType, ...) \
    _DISCRIMINATED_UNION(BOOST_PP_IF(BOOST_PP_IS_BEGIN_PARENS(baseType), baseType, AS_STRUCT(baseType)), __VA_ARGS__) \
    struct __fake_struct_unlikely_to_ever_be_used____why_would_anyone_use_this
