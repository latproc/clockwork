/*
 * Copyright (c) 2026 OEG / Latproc contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#include "ExpressionPcode.h"
#include "DebugExtra.h"
#include "Expression.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "symboltable.h"
#include <cstdlib>
#include <strings.h>
#include <vector>

bool ExpressionPcode::fastPathEnabled() {
    const char *env = std::getenv("IOD_EXPR_FAST");
    if (env && (env[0] == '1' || strcasecmp(env, "on") == 0 || strcasecmp(env, "true") == 0)) {
        return true;
    }
    DebugExtra *dbg = DebugExtra::instance();
    return dbg && LogState::instance()->includes(dbg->DEBUG_EXPR_FAST);
}

uint32_t ExpressionPcode::addSlot(const std::string &path) {
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].path == path) {
            return i;
        }
    }
    Slot s;
    s.path = path;
    slots_.push_back(s);
    return static_cast<uint32_t>(slots_.size() - 1);
}

bool ExpressionPcode::emit(const Predicate *p) {
    if (!p) {
        return false;
    }
    if (p->json_expression || p->dyn_value) {
        return false;
    }
    if (p->op == opNone) {
        const Value &v = p->entry;
        if (v.kind == Value::t_integer) {
            Inst in;
            in.op = OpPushI;
            in.i = v.iValue;
            ops_.push_back(in);
            return true;
        }
        if (v.kind == Value::t_float) {
            Inst in;
            in.op = OpPushF;
            in.f = v.fValue;
            ops_.push_back(in);
            return true;
        }
        if (v.kind == Value::t_bool) {
            Inst in;
            in.op = OpPushB;
            in.i = v.bValue ? 1 : 0;
            ops_.push_back(in);
            return true;
        }
        if (v.kind == Value::t_symbol) {
            if (v.sValue.empty() || v.sValue.find("TIMER") != std::string::npos) {
                return false;
            }
            // Bare tokens like on/off/OP/PREOP are states, not properties.
            // Refuse so the caller falls back to the interpreter.
            if (v.sValue.find('.') == std::string::npos) {
                const char *s = v.sValue.c_str();
                if (strcasecmp(s, "on") == 0 || strcasecmp(s, "off") == 0 ||
                    strcasecmp(s, "true") == 0 || strcasecmp(s, "false") == 0 ||
                    strcasecmp(s, "OP") == 0 || strcasecmp(s, "PREOP") == 0 ||
                    strcasecmp(s, "SAFEOP") == 0 || strcasecmp(s, "INIT") == 0 ||
                    strcasecmp(s, "COMPLETE") == 0 || strcasecmp(s, "INCOMPLETE") == 0 ||
                    strcasecmp(s, "INVALID") == 0 || strcasecmp(s, "SELF") == 0 ||
                    strcasecmp(s, "DEFAULT") == 0) {
                    return false;
                }
            }
            Inst in;
            in.op = OpLoad;
            in.slot = addSlot(v.sValue);
            ops_.push_back(in);
            return true;
        }
        return false;
    }

    Op mapped = OpPushI;
    bool unary = false;
    switch (p->op) {
    case opPlus:
        mapped = OpAdd;
        break;
    case opMinus:
        mapped = OpSub;
        break;
    case opTimes:
        mapped = OpMul;
        break;
    case opDivide:
        mapped = OpDiv;
        break;
    case opMod:
        mapped = OpMod;
        break;
    case opUnaryMinus:
        mapped = OpNeg;
        unary = true;
        break;
    case opEQ:
        mapped = OpEQ;
        break;
    case opNE:
        mapped = OpNE;
        break;
    case opLT:
        mapped = OpLT;
        break;
    case opLE:
        mapped = OpLE;
        break;
    case opGT:
        mapped = OpGT;
        break;
    case opGE:
        mapped = OpGE;
        break;
    case opAND:
        mapped = OpAND;
        break;
    case opOR:
        mapped = OpOR;
        break;
    case opNOT:
        mapped = OpNOT;
        unary = true;
        break;
    case opInteger:
        mapped = OpInt;
        unary = true;
        break;
    case opFloat:
        mapped = OpFloat;
        unary = true;
        break;
    case opAbsoluteValue:
        mapped = OpAbs;
        unary = true;
        break;
    default:
        return false;
    }

    if (unary) {
        if (!emit(p->right_p)) {
            return false;
        }
    }
    else {
        if (!emit(p->left_p) || !emit(p->right_p)) {
            return false;
        }
    }
    Inst in;
    in.op = mapped;
    ops_.push_back(in);
    return true;
}

ExpressionPcode *ExpressionPcode::tryCompile(const Predicate *p) {
    if (!p) {
        return 0;
    }
    ExpressionPcode *code = new ExpressionPcode();
    if (!code->emit(p) || code->ops_.empty()) {
        delete code;
        return 0;
    }
    return code;
}

Value ExpressionPcode::run(MachineInstance *scope) const {
    std::vector<Value> st;
    st.reserve(ops_.size());
    for (size_t n = 0; n < ops_.size(); ++n) {
        const Inst &in = ops_[n];
        switch (in.op) {
        case OpPushI:
            st.push_back(Value(in.i));
            break;
        case OpPushF:
            st.push_back(Value(in.f));
            break;
        case OpPushB:
            st.push_back(Value(in.i != 0));
            break;
        case OpLoad: {
            const Value &v = scope->getValue(slots_[in.slot].path);
            if (v.kind == Value::t_dynamic) {
                st.push_back(v.dynamicValue()->operator()(scope));
            }
            else {
                st.push_back(v);
            }
            break;
        }
        case OpNeg: {
            Value a = st.back();
            st.pop_back();
            st.push_back(-a);
            break;
        }
        case OpNOT: {
            Value a = st.back();
            st.pop_back();
            st.push_back(!a);
            break;
        }
        case OpInt: {
            Value a = st.back();
            st.pop_back();
            int64_t iv = 0;
            if (a.asInteger(iv)) {
                st.push_back(Value(iv));
            }
            else {
                st.push_back(Value(static_cast<int64_t>(0)));
            }
            break;
        }
        case OpFloat: {
            Value a = st.back();
            st.pop_back();
            st.push_back(a.toFloat());
            break;
        }
        case OpAbs: {
            Value a = st.back();
            st.pop_back();
            if (a < 0) {
                st.push_back(-a);
            }
            else {
                st.push_back(a);
            }
            break;
        }
        case OpAdd:
        case OpSub:
        case OpMul:
        case OpDiv:
        case OpMod:
        case OpEQ:
        case OpNE:
        case OpLT:
        case OpLE:
        case OpGT:
        case OpGE:
        case OpAND:
        case OpOR: {
            Value b = st.back();
            st.pop_back();
            Value a = st.back();
            st.pop_back();
            switch (in.op) {
            case OpAdd:
                st.push_back(a + b);
                break;
            case OpSub:
                st.push_back(a - b);
                break;
            case OpMul:
                st.push_back(a * b);
                break;
            case OpDiv:
                st.push_back(a / b);
                break;
            case OpMod:
                st.push_back(a % b);
                break;
            case OpEQ:
                st.push_back(a == b);
                break;
            case OpNE:
                st.push_back(a != b);
                break;
            case OpLT:
                st.push_back(a < b);
                break;
            case OpLE:
                st.push_back(a <= b);
                break;
            case OpGT:
                st.push_back(a > b);
                break;
            case OpGE:
                st.push_back(a >= b);
                break;
            case OpAND:
                st.push_back(a && b);
                break;
            case OpOR:
                st.push_back(a || b);
                break;
            default:
                st.push_back(SymbolTable::Null);
                break;
            }
            break;
        }
        }
    }
    if (st.empty()) {
        return SymbolTable::Null;
    }
    return st.back();
}
