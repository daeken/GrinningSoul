#pragma once

#include <iomanip>
#include <string>
#include <type_traits>
#include <sstream>
#include <boost/format.hpp>
#include <boost/core/demangle.hpp>

class IRepr {
public:
    virtual std::string repr() const = 0;
};

inline std::string repr(IRepr& reprable) {
    return reprable.repr();
}

template<typename T, typename = std::enable_if_t<std::is_base_of_v<IRepr, T>>>
inline std::string repr(std::shared_ptr<T>& reprable) {
    return std::static_pointer_cast<IRepr>(reprable)->repr();
}

template<typename T>
inline std::string repr(const T* pointer) {
    if constexpr(std::is_base_of<IRepr, std::remove_pointer_t<T>>())
        return dynamic_cast<const IRepr*>(pointer)->repr();
    else
        return (boost::format("(%1% *) 0x%2$x") % boost::core::demangle(typeid(T).name()) % (uint64_t) pointer).str();
}

template<typename T>
inline std::string repr(const std::vector<T>& vec) {
    std::string ret = (boost::format("vector<%1%>{") % boost::core::demangle(typeid(T).name())).str();
    for(auto i = 0; i < vec.size(); ++i) {
        if(i != 0)
            ret += ", ";
        ret += repr(vec[i]);
    }
    return ret + "}";
}

inline std::string repr(const std::string& str) {
    std::ostringstream ss;
    ss << std::quoted(str);
    return ss.str();
}

inline std::string repr(int i) {
    return (boost::format("%1%") % i).str();
}

