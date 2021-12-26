#include "Parameter.h"

Parameter::Parameter(Value v) : val(v), machine(0) { ; }
Parameter::Parameter(const char *name, const SymbolTable &st)
    : val(name), properties(st), machine(0) {}
std::ostream &Parameter::operator<<(std::ostream &out) const {
    return out << val << "(" << properties << ")";
}
Parameter::Parameter(const Parameter &orig) {
    val = orig.val;
    machine = orig.machine;
    properties = orig.properties;
    real_name = orig.real_name;
}

std::ostream &operator<<(std::ostream &out, const Parameter &p) {
    return p.operator<<(out);
}
