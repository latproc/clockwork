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

#ifndef EXPRESSION_PCODE_H
#define EXPRESSION_PCODE_H

#include "value.h"
#include <cstdint>
#include <string>
#include <vector>

class Predicate;
class MachineInstance;

// Linear form of a closed arithmetic / compare / cast Predicate tree.
// tryCompile returns null if the tree uses LIST, JSON, TIMER/dynamic, or
// other operators the interpreter must keep.
class ExpressionPcode {
  public:
    // Default off. Enable: IOD_EXPR_FAST=1 or DEBUG DEBUG_EXPR_FAST on.
    // Gates pcode, deferred property notify, and fast command bodies.
    static bool fastPathEnabled();

    static ExpressionPcode *tryCompile(const Predicate *p);

    Value run(MachineInstance *scope) const;

  private:
    enum Op : uint8_t {
        OpPushI = 0,
        OpPushF,
        OpPushB,
        OpLoad,
        OpAdd,
        OpSub,
        OpMul,
        OpDiv,
        OpMod,
        OpNeg,
        OpEQ,
        OpNE,
        OpLT,
        OpLE,
        OpGT,
        OpGE,
        OpAND,
        OpOR,
        OpNOT,
        OpInt,
        OpFloat,
        OpAbs
    };

    struct Inst {
        Op op;
        int64_t i;
        double f;
        uint32_t slot;
        Inst() : op(OpPushI), i(0), f(0.0), slot(0) {}
    };

    struct Slot {
        std::string path;
    };

    std::vector<Inst> ops_;
    std::vector<Slot> slots_;

    bool emit(const Predicate *p);
    uint32_t addSlot(const std::string &path);
};

#endif
