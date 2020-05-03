/*
  Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

  This file is part of Latproc

  Latproc is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  Latproc is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Latproc; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __Expression_h__
#define __Expression_h__ 1

#include <iostream>
#include "symboltable.h"
#include <list>


class MachineInstance;

enum PredicateOperator { opNone, opGE, opGT, opLE, opLT, opEQ, opNE, opAND, opOR, opNOT,
	opUnaryMinus, opPlus, opMinus, opTimes, opDivide, opMod, opAssign, opMatch,
    opBitAnd, opBitOr, opBitXOr, opNegate, opAny, opAll, opCount, opIncludes,
	opInteger, opFloat, opAbsoluteValue
};
std::ostream &operator<<(std::ostream &out, const PredicateOperator op);

class ExprNode {
public:
	ExprNode(const Value *a, const Value *name = NULL);
	ExprNode(const Value &a, const Value *name = NULL);
	ExprNode(Value *a, const Value *name = NULL);
	ExprNode(Value &a, const Value *name = NULL);
	ExprNode(long a, const Value *name = NULL);
	ExprNode(float a, const Value *name = NULL);
	ExprNode(double a, const Value *name = NULL);
	ExprNode(bool a, const Value *name = NULL);
	ExprNode(PredicateOperator o);
	ExprNode(const ExprNode &other);
  ~ExprNode();

  const Value *getValue() const { return val; }
protected:
    Value tmpval;
    const Value *val;
    const Value *node;
    PredicateOperator op;
    enum { t_int, t_op } kind;
private:
    ExprNode &operator=(const ExprNode &other);

  friend class Stack;
  friend ExprNode eval_stack(MachineInstance *m, std::list<ExprNode>::const_iterator &stack_iter);
};

class Stack {
public:
    ExprNode pop() { ExprNode v = stack.front(); stack.pop_front(); return v; }
    //void push(ExprNode v) { stack.push_front(v); }
    void push(const ExprNode &v) { stack.push_front(v); }
    // void push(ExprNode &&v) { stack.push_front(std::move(v)); }
    void clear();
    std::ostream &operator<<(std::ostream &out)const;
    std::ostream &traverse(std::ostream &out, std::list<ExprNode>::const_iterator &iter, std::list<ExprNode>::const_iterator &end) const;
    Stack(const Stack&other) { std::copy(other.stack.begin(), other.stack.end(), std::back_inserter(stack)); }
    virtual ~Stack(){}
    Stack() { }


    std::list<ExprNode> stack;
private:
    Stack &operator=(const Stack&other);
};

struct PredicateTimerDetails {
	int64_t delay;
	std::string label;
	PredicateTimerDetails(int64_t dly, const std::string &lbl) :  delay(dly), label(lbl) { }
	void setup(int64_t dly, const std::string &lbl) { delay = dly; label = lbl; }
};

class Predicate {
public:
    Predicate *left_p;
    PredicateOperator op;
    Predicate *right_p;
    Value entry;
	MachineInstance *mi;
    Value *dyn_value;
	const Value *cached_entry;
    const Value *last_calculation; // for dynamic values, retains the last value for display only
	bool error() { return lookup_error; }
	const std::string &errorString() { return error_str; }
	void clearError() { lookup_error = false; }
	void setErrorString(const std::string &err) { error_str = err; lookup_error = true; }

    Predicate(Value *v) : left_p(0), op(opNone), right_p(0), entry(*v), mi(0), dyn_value(0), cached_entry(0),
            last_calculation(0), priority(0), lookup_error(false), needs_reevaluation(true), last_evaluation_time(0) {
        if (entry.kind == Value::t_symbol && entry.sValue == "DEFAULT") priority = 1;
    }

    Predicate(Value &v) : left_p(0), op(opNone), right_p(0), entry(v), mi(0), dyn_value(0), cached_entry(0),
            last_calculation(0), priority(0), lookup_error(false), needs_reevaluation(true), last_evaluation_time(0) {
        if (entry.kind == Value::t_symbol && entry.sValue == "DEFAULT") priority = 1;
    }

    Predicate(const char *s) : left_p(0), op(opNone), right_p(0), entry(s), mi(0), dyn_value(0), cached_entry(0),
            last_calculation(0), priority(0), lookup_error(false), needs_reevaluation(true), last_evaluation_time(0) {
        if (entry.kind == Value::t_symbol && entry.sValue == "DEFAULT") priority = 1;
    }
    Predicate(int v) : left_p(0), op(opNone), right_p(0), entry(v), mi(0), dyn_value(0), cached_entry(0),
            last_calculation(0), priority(0), lookup_error(false), needs_reevaluation(true) {}

	Predicate(Predicate *l, PredicateOperator o, Predicate *r) : left_p(l), op(o), right_p(r),
	mi(0), dyn_value(0), cached_entry(0), last_calculation(0), priority(0),
	lookup_error(false), needs_reevaluation(true), last_evaluation_time(0) {}
    ~Predicate();
	Predicate(const Predicate &other);
	Predicate &operator=(const Predicate &other);
    const Value &getTimerValue();
    std::ostream &operator <<(std::ostream &out) const;
    Value evaluate(MachineInstance *m);
    void flushCache();
    /* predicate clauses may involve timers that need to be scheduled or cleared
       whenever a machine changes state.
     */
	bool usesTimer(Value &val) const; // recursively search for use of TIMER
    void findTimerClauses(std::list<Predicate*>&clauses);
    PredicateTimerDetails *scheduleTimerEvents(PredicateTimerDetails *earliest, MachineInstance *target); // setup timer events that trigger the supplied machine
    void clearTimerEvents(MachineInstance *target); // clear all timer events scheduled for the supplid machine

    int priority; // used for the default predicate
	bool lookup_error;
	std::string error_str;
    bool needs_reevaluation;
    Stack stack;
    uint64_t last_evaluation_time;
};

std::ostream &operator <<(std::ostream &out, const Predicate &p);

class Condition {
public:
    Predicate *predicate;
    std::string last_evaluation;
    Value last_result;
    bool operator()(MachineInstance *m);
	Condition() : predicate(0) {}
	Condition(Predicate*p);
	Condition(const Condition &other);
	Condition &operator=(const Condition &other);
    ~Condition();
};

#endif
