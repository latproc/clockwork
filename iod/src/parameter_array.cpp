#include "parameter_array.h"
#include <sstream>

std::string toLimitedString(const ParameterArray &parameters, size_t limit) {
    std::string str;
    const char *delim = "";
    const int bufsize = limit + 1;
    char buf[bufsize];
    snprintf(buf, bufsize, "[");
    size_t n = 1;
    bool truncated = false;
    for (unsigned int i = 0; i < parameters.size(); ++i) {
        std::string string_val = parameters[i].val.asString();
        if (*delim && n < bufsize - 2) {
            buf[n++] = *delim;
        }
        if (n + string_val.length() > bufsize - 2) {
            snprintf(buf + n, bufsize - n, "%s", string_val.c_str());
            n = bufsize;
            truncated = true;
            break;
        }
        snprintf(buf + n, bufsize - n, "%s", string_val.c_str());
        n += string_val.length();
        delim = ",";
    }
    if (truncated) {
        snprintf(buf + bufsize - 5, 5, "...]");
    }
    else {
        snprintf(buf + n, bufsize - n, "]");
    }
    str = buf;
    return str;
}

std::string toString(const ParameterArray &parameters) {
    std::stringstream ss;
    const char *delim = "";
    ss << "[";
    for (unsigned int i = 0; i < parameters.size(); ++i) {
        ss << delim << parameters[i].val;
        delim = ",";
    }
    ss << "]";
    return ss.str();
}
