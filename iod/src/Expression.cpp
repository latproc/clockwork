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

#include "Expression.h"
#include "DebugExtra.h"
#include "FireTriggerAction.h"
#include "Logger.h"
#include "MachineInstance.h"
#include "MessageLog.h"
#include "ProcessingThread.h"
#include "Scheduler.h"
#include "dynamic_value.h"
#include "regular_expressions.h"
#include <iomanip>

static int count_instances = 0;
static int max_count = 0;
static int last_max = 0;

std::ostream &operator<<(std::ostream &out, const Predicate &p) { return p.operator<<(out); }
std::ostream &operator<<(std::ostream &out, const PredicateOperator op) {
    const char *opstr;
    switch (op) {
    case opNone:
        opstr = "None";
        break;
    case opGE:
        opstr = ">=";
        break;
    case opGT:
        opstr = ">";
        break;
    case opLE:
        opstr = "<=";
        break;
    case opLT:
        opstr = "<";
        break;
    case opEQ:
        opstr = "==";
        break;
    case opNE:
        opstr = "!=";
        break;
    case opAND:
        opstr = "AND";
        break;
    case opOR:
        opstr = "OR";
        break;
    case opNOT:
        opstr = "NOT";
        break;
    case opUnaryMinus:
        opstr = "-";
        break;
    case opPlus:
        opstr = "+";
        break;
    case opMinus:
        opstr = "-";
        break;
    case opTimes:
        opstr = "*";
        break;
    case opDivide:
        opstr = "/";
        break;
    case opAbsoluteValue:
        opstr = "ABS";
        break;
    case opMod:
        opstr = "%";
        break;
    case opAssign:
        opstr = ":=";
        break;
    case opMatch:
        opstr = "~";
        break;
    case opBitAnd:
        opstr = "&";
        break;
    case opBitOr:
        opstr = "|";
        break;
    case opNegate:
        opstr = "~";
        break;
    case opBitXOr:
        opstr = "^";
        break;
    case opInteger:
        opstr = "AS INTEGER";
        break;
    case opFloat:
        opstr = "AS FLOAT";
        break;
    case opString:
        opstr = "AS STRING";
    case opAny:
        opstr = "ANY";
        break;
    case opAll:
        opstr = "ALL";
        break;
    case opCount:
        opstr = "COUNT";
        break;
    case opIncludes:
        opstr = "INCLUDES";
        break;
    }
    return out << opstr;
}

static bool stringEndsWith(const std::string &str, const std::string &subs) {
    size_t n1 = str.length();
    size_t n2 = subs.length();
    if (n1 >= n2 && str.substr(n1 - n2) == subs) {
        return true;
    }
    return false;
}

Condition::~Condition() { delete predicate; }

Predicate::Predicate(Value *v)
    : left_p(0), op(opNone), right_p(0), entry(*v), mi(0), dyn_value(0), cached_entry(0),
      last_calculation(0), priority(0), lookup_error(false), needs_reevaluation(true),
      last_evaluation_time(0) {
    if (entry.kind == Value::t_symbol && entry.sValue == "DEFAULT") {
        priority = 1;
    }
}

Predicate::Predicate(Value &v)
    : left_p(0), op(opNone), right_p(0), entry(v), mi(0), dyn_value(0), cached_entry(0),
      last_calculation(0), priority(0), lookup_error(false), needs_reevaluation(true),
      last_evaluation_time(0) {
    if (entry.kind == Value::t_symbol && entry.sValue == "DEFAULT") {
        priority = 1;
    }
}

Predicate::Predicate(const char *s)
    : left_p(0), op(opNone), right_p(0), entry(s), mi(0), dyn_value(0), cached_entry(0),
      last_calculation(0), priority(0), lookup_error(false), needs_reevaluation(true),
      last_evaluation_time(0) {
    if (entry.kind == Value::t_symbol && entry.sValue == "DEFAULT") {
        priority = 1;
    }
}
Predicate::Predicate(int v)
    : left_p(0), op(opNone), right_p(0), entry(v), mi(0), dyn_value(0), cached_entry(0),
      last_calculation(0), priority(0), lookup_error(false), needs_reevaluation(true) {}

Predicate::Predicate(Predicate *l, PredicateOperator o, Predicate *r)
    : left_p(l), op(o), right_p(r), mi(0), dyn_value(0), cached_entry(0), last_calculation(0),
      priority(0), lookup_error(false), needs_reevaluation(true), last_evaluation_time(0) {}

Predicate::~Predicate() {
    delete left_p;
    delete right_p;
    delete dyn_value;
}

class Evaluator {
  private:
    Stack stack;

  public:
    Value evaluate(Predicate *p, MachineInstance *m);
};

bool Predicate::usesTimer(Value &timer_val) const {
    if (left_p) {
        if (!left_p->left_p) {
            if (left_p->entry.kind == Value::t_symbol &&
                (left_p->entry.token_id == ClockworkToken::TIMER ||
                 stringEndsWith(left_p->entry.sValue, ".TIMER"))) {
                //DBG_MSG << "Copying timer value " << right_p->entry << "\n";
                timer_val = right_p->entry;
                return true;
            }
            else {
                return false;
            }
        }
        return left_p->usesTimer(timer_val) || right_p->usesTimer(timer_val);
    }
    if (entry.kind == Value::t_symbol && entry.token_id == ClockworkToken::TIMER) {
        return true;
    }
    else {
        return false;
    }
}

void Predicate::findTimerClauses(std::list<Predicate *> &clauses) {
    if (left_p) {
        if (!left_p->left_p) {
            if (left_p->entry.kind == Value::t_symbol &&
                (left_p->entry.token_id == ClockworkToken::TIMER ||
                 stringEndsWith(left_p->entry.sValue, ".TIMER"))) {
                clauses.push_back(this);
            }
        }
        else {
            left_p->findTimerClauses(clauses);
        }
    }
    if (right_p) {
        if (!right_p->left_p) {
            if (right_p->entry.kind == Value::t_symbol &&
                (right_p->entry.token_id == ClockworkToken::TIMER ||
                 stringEndsWith(right_p->entry.sValue, ".TIMER"))) {
                clauses.push_back(this);
            }
        }
        else {
            right_p->findTimerClauses(clauses);
        }
    }
}

const Value &Predicate::getTimerValue() {
    if (left_p->entry.kind == Value::t_symbol && (left_p->entry.token_id == ClockworkToken::TIMER ||
                                                  stringEndsWith(left_p->entry.sValue, ".TIMER"))) {
        return right_p->entry;
    }
    if (right_p->entry.kind == Value::t_symbol &&
        (right_p->entry.token_id == ClockworkToken::TIMER ||
         stringEndsWith(right_p->entry.sValue, ".TIMER"))) {
        return left_p->entry;
    }
    return SymbolTable::Null;
}

PredicateTimerDetails *Predicate::scheduleTimerEvents(
    PredicateTimerDetails *earliest,
    MachineInstance *target) // setup timer events that trigger the supplied machine
{
    const long MIN_TIMER = -100000;
    long scheduled_time = MIN_TIMER;
    long current_time = 0;
    MachineInstance *timed_machine = 0; // the machine that the timer is on if not SELF
    // timer usage can be of the form TIMER >= value or TIMER <= value
    // in the first case, the predicate is initially false and eventually becomes true
    // in the second case, the reverse is true and in this case, we need to keep testing
    // until the predicate finally becomes false
    bool rescheduleWhenTrue = false; // the predicate is false initially

    // below, we check the clauses of this predicate and if we find a timer test
    // we set the above variables. At the end of the method, we actually set the timer

    Evaluator evaluator;
    // clauses like (machine.TIMER >= 10)
    if (left_p && left_p->entry.kind == Value::t_symbol &&
        left_p->entry.token_id == ClockworkToken::TIMER && right_p) {
        //std::cout << "checking timers on " << right_p->entry << " " << left_p->entry << "\n";
        Value rhs = evaluator.evaluate(right_p, target);
        if (rhs.asInteger(scheduled_time)) {
            current_time = target->getTimerVal()->iValue;
            if (op == opGT) {
                ++scheduled_time;
            }
            else if (op == opLE) {
                ++scheduled_time;
                rescheduleWhenTrue = true;
            }
            else if (op == opLT) {
                rescheduleWhenTrue = true;
            }
        }
        else {
            DBG_MSG << "Error: clause " << *this << " does not yield an integer comparison\n";
        }
    }
    else if (left_p && left_p->entry.kind == Value::t_symbol &&
             stringEndsWith(left_p->entry.sValue, ".TIMER")) {
        // lookup the machine
        size_t pos = left_p->entry.sValue.find('.');
        std::string machine_name(left_p->entry.sValue);
        machine_name.erase(pos);
        timed_machine = target->lookup(machine_name);
        if (timed_machine) {
            Value rhs = evaluator.evaluate(right_p, target);
            if (rhs.asInteger(scheduled_time)) {
                current_time = timed_machine->getTimerVal()->iValue;
                if (op == opGT) {
                    ++scheduled_time;
                }
                else if (op == opLE) {
                    ++scheduled_time;
                    rescheduleWhenTrue = true;
                }
                else if (op == opLT) {
                    rescheduleWhenTrue = true;
                }
            }
        }
        else {
            DBG_MSG << "Error: clause " << *this << " does not yield an integer comparison\n";
        }
    }
    // clauses like (10 <= TIMER)
    else if (right_p && right_p->entry.kind == Value::t_symbol &&
             right_p->entry.token_id == ClockworkToken::TIMER && left_p) {
        Evaluator evaluator;
        Value lhs = evaluator.evaluate(left_p, target);
        if (lhs.asInteger(scheduled_time)) {
            current_time = target->getTimerVal()->iValue;
            if (op == opGT) {
                ++scheduled_time;
                rescheduleWhenTrue = true;
            }
            else if (op == opLE) {
                ++scheduled_time;
            }
            else if (op == opGE) {
                rescheduleWhenTrue = true;
            }
        }
        else {
            DBG_MSG << "Error: clause " << *this << " does not yield an integer comparison\n";
        }
    }
    // clauses like (10 <= machine.TIMER)
    else if (right_p && left_p && right_p->entry.kind == Value::t_symbol &&
             stringEndsWith(right_p->entry.sValue, ".TIMER")) {
        // lookup and cache the machine
        size_t pos = right_p->entry.sValue.find('.');
        std::string machine_name(right_p->entry.sValue);
        machine_name.erase(pos);
        timed_machine = target->lookup(machine_name);
        Evaluator evaluator;
        if (timed_machine) {
            Value lhs = evaluator.evaluate(left_p, target);
            if (lhs.asInteger(scheduled_time)) {
                current_time = timed_machine->getTimerVal()->iValue;
                if (op == opGT) {
                    ++scheduled_time;
                    rescheduleWhenTrue = true;
                }
                else if (op == opLE) {
                    ++scheduled_time;
                }
                else if (op == opGE) {
                    rescheduleWhenTrue = true;
                }
            }
        }
        else {
            DBG_MSG << "Error: clause " << *this << " does not yield an integer comparison\n";
        }
    }
    else if (left_p) {
        PredicateTimerDetails *prev = earliest;
        earliest = left_p->scheduleTimerEvents(earliest, target);
        if (prev && earliest != prev) {
            delete prev;
        }
    }
    if (right_p) {
        PredicateTimerDetails *prev = earliest;
        earliest = right_p->scheduleTimerEvents(earliest, target);
        if (prev && earliest != prev) {
            delete prev;
        }
    }
    else {
    }
    //TBD there is an issue with testing current_time <= scheduled_time because there may have been some
    // processing delays and current time may already be a little > scheduled time. This is especially
    // true on slow clock cycles. For now we reschedule the trigger for up to 2ms past the necessary time.

    if (scheduled_time != MIN_TIMER) {
        long t = (scheduled_time - current_time) * 1000;
        if (t > 0) {
            std::string trigger_name("Timer ");
            if (target) {
                trigger_name += target->getName();
            }
            if (timed_machine) {
                trigger_name += " ";
                trigger_name += timed_machine->getName();
            }
            DBG_SCHEDULER << "Predicate scheduling item " << trigger_name << " for " << t
                          << " (=" << scheduled_time << " - " << current_time << ")\n";
            if (!earliest) {
                earliest = new PredicateTimerDetails(t, trigger_name);
            }
            else if (earliest->delay > t) {
                DBG_SCHEDULER << "found a closer event in " << t << "us\n";
                earliest->setup(t, trigger_name);
            }
            else {
                DBG_SCHEDULER << "skipping event in " << t << "us as an earlier one exists\n";
            }
        }
        // to allow for the above processing delays we keep the target runnable
        else if (t >= -2000) {
            target->setNeedsCheck();
        }
    }
    return earliest;
}

void Predicate::clearTimerEvents(
    MachineInstance *target) // clear all timer events scheduled for the supplied machine
{
    if (left_p) {
        left_p->clearTimerEvents(target);
        if (entry.kind == Value::t_symbol && (left_p->entry.token_id == ClockworkToken::TIMER ||
                                              stringEndsWith(left_p->entry.sValue, ".TIMER"))) {
            DBG_SCHEDULER << "clear timer event for entry " << entry << "\n";
        }
    }
    if (right_p) {
        right_p->clearTimerEvents(target);
    }
}

std::ostream &Predicate::operator<<(std::ostream &out) const {
    if (left_p) {
        out << "(";
        if (op != opNOT && op != opInteger && op != opFloat && op != opString) {
            left_p->operator<<(out); // ignore the lhs for NOT operators
        }
        out << " " << op << " ";
        if (right_p) {
            right_p->operator<<(out);
        }
        out << ")";
    }
    else {
        if (cached_entry) {
            out << entry;
            if (entry.kind == Value::t_symbol) {
                out << " (" << *cached_entry << ")";
            }
        }
        else if (last_calculation) {
            out << entry;
            if (entry.kind == Value::t_symbol || entry.kind == Value::t_dynamic) {
                out << " (" << *last_calculation << ") ";
            }
        }
        else {
            out << " " << entry;
        }
    }
    return out;
}

// Conditions
Predicate::Predicate(const Predicate &other) : left_p(0), op(opNone), right_p(0) {
    if (other.left_p) {
        left_p = new Predicate(*(other.left_p));
    }
    op = other.op;
    if (op == opInteger) {
    }
    if (other.right_p) {
        right_p = new Predicate(*(other.right_p));
    }
    entry = other.entry;
    if (other.entry.dynamicValue()) {
        entry.setDynamicValue(other.entry.dynamicValue()->clone());
        //dyn_value = DynamicValueBase::ref(other.dyn_value); // note shared copy, should be a shared pointer
    }
    if (other.dyn_value) {
        dyn_value = new Value(DynamicValueBase::ref(other.dyn_value->dynamicValue()));
    }
    else {
        dyn_value = 0;
    }
    entry.cached_machine = 0; // do not preserve any cached values in this clone
    priority = other.priority;
    mi = 0;
    cached_entry = 0;
    lookup_error = false;
    last_calculation = 0;
    needs_reevaluation = true;
}

Predicate &Predicate::operator=(const Predicate &other) {
    delete left_p;
    delete right_p;
    delete dyn_value;
    if (other.left_p) {
        left_p = new Predicate(*(other.left_p));
    }
    op = other.op;
    if (other.right_p) {
        right_p = new Predicate(*(other.right_p));
    }
    entry = other.entry;
    if (other.entry.dynamicValue()) {
        entry.setDynamicValue(other.entry.dynamicValue()->clone());
        //dyn_value = DynamicValueBase::ref(other.dyn_value); // note shared copy, should be a shared pointer
    }
    if (other.dyn_value) {
        dyn_value = new Value(DynamicValueBase::ref(other.dyn_value->dynamicValue()));
    }
    else {
        dyn_value = 0;
    }
    entry.cached_machine = 0; // do not preserve any cached machine pointers in this clone
    priority = other.priority;
    mi = 0;
    cached_entry = 0; // do not preserve cached the value pointer
    lookup_error = false;
    last_calculation = 0;
    needs_reevaluation = true;
    return *this;
}

void Predicate::flushCache() {
    cached_entry = 0;
    last_calculation = 0;
    needs_reevaluation = true;
    stack.stack.clear();
    if (dyn_value) {
        delete dyn_value;
        dyn_value = 0;
    }
    /*  if (op == opNone) {
        if (entry.kind == Value::t_dynamic) {
            //entry.dyn_value->flushCache();
            entry = entry.sValue; // force resolution of dynamic values again
        }
        //entry.cached_machine = 0;
        //entry.cached_value = &SymbolTable::Null;
        }
        if (left_p) left_p->flushCache();
        if (right_p) right_p->flushCache();*/
}

Condition::Condition(Predicate *p) : predicate(0) {
    if (p) {
        predicate = new Predicate(*p);
    }
}

Condition::Condition(const Condition &other) : predicate(0) {
    if (other.predicate) {
        predicate = new Predicate(*(other.predicate));
    }
}

Condition &Condition::operator=(const Condition &other) {
    if (other.predicate) {
        predicate = new Predicate(*(other.predicate));
    }
    return *this;
}

std::ostream &operator<<(std::ostream &out, const Stack &s) { return s.operator<<(out); }

std::ostream &Stack::operator<<(std::ostream &out) const {
#if 1
    // prefix
    BOOST_FOREACH (const ExprNode &n, stack) {
        if (n.kind == ExprNode::t_op) {
            out << n.op << " ";
        }
        else {
            if (n.node) {
                out << *n.node;
            }
            out << " (" << *n.val << ") ";
        }
    }
    return out;
#else
    // disabled due to error
    std::list<ExprNode>::const_iterator iter = stack.begin();
    std::list<ExprNode>::const_iterator end = stack.end();
    return traverse(out, iter, end);
#endif
}

std::ostream &Stack::traverse(std::ostream &out, std::list<ExprNode>::const_iterator &iter,
                              std::list<ExprNode>::const_iterator &end) const {
    // note current error with display of rhs before lhs
    if (iter == end) {
        return out;
    }
    const ExprNode &n = *iter++;
    if (n.kind == ExprNode::t_op) {
        out << "(";
        if (n.op != opNOT && n.op != opInteger && n.op != opFloat)
        // ignore lhs for the not operator
        {
            traverse(out, iter, end);
        }
        out << " " << n.op << " ";
        traverse(out, iter, end);
        out << ")";
    }
    else {
        if (n.node) {
            out << *n.node;
        }
        out << " (" << *n.val << ") ";
    }
    return out;
}

class ClearFlagOnReturn {
  public:
    ClearFlagOnReturn(bool *val) : to_clear(val) {}
    ~ClearFlagOnReturn() { *to_clear = false; }

  private:
    bool *to_clear;
};

const Value *resolveCacheMiss(Predicate *p, MachineInstance *m, bool left, bool reevaluate) {
    Value *v = &p->entry;
    p->clearError();
    if (v->kind == Value::t_symbol) {
        // lookup machine and get state. hopefully we have already cached a pointer to the machine
        MachineInstance *found = 0;
        if (p->mi) {
            found = p->mi;
        }
        else {
#if 0
            const Value *prop = 0;
#endif
            // before looking up machines, check for specific keywords
            if (left && v->sValue == "DEFAULT") {
                // default state has a low priority but always returns true
                p->cached_entry = &SymbolTable::True;
                return p->cached_entry;
            }
            else if (v->sValue == "SELF") {
                //v = m->getCurrent().getName();
                p->cached_entry = m->getCurrentStateVal();
                return p->cached_entry;
            }
            else if (v->sValue == "TRUE") {
                p->cached_entry = &SymbolTable::True;
                return p->cached_entry;
            }
            else if (v->sValue == "FALSE") {
                p->cached_entry = &SymbolTable::False;
                return p->cached_entry;
            }
            else if (m->hasState(v->sValue)) {
                p->cached_entry = v;
                return p->cached_entry;
            }
#if 1
            else {
                return m->resolve(v->sValue);
            }
#else
            else if ((prop = &m->properties.lookup(v->sValue.c_str()))) {
                return prop;
            }
            else {
                prop = &m->getValue(v->sValue); // property lookup
                if (*prop != SymbolTable::Null) {
                    // do not cache timer values. TBD subclass Value for dynamic values..
                    if (!stringEndsWith(v->sValue, ".TIMER")) {
                        p->cached_entry = prop;
                    }
                    else {
                        p->last_calculation = prop;
                    }
                    return prop;
                }
            }
            found = m->lookup(p->entry);
            p->mi = found; // cache the machine we just looked up with the predicate
            if (p->mi) {
                // found a machine, make sure we get to hear if that machine changes state or if its properties change
                if (p->mi != m) {
                    p->mi->addDependancy(
                        m); // ensure this condition will be updated when p->mi changes
                    m->listenTo(p->mi);
                }
            }
            else {
                std::stringstream ss;
                ss << "## - Warning: " << m->getName() << " couldn't find a machine called "
                   << p->entry;
                p->setErrorString(ss.str());
                //DBG_MSG << p->errorString() << "\n";
            }
#endif
        }
        // if we found a reference to a machine but that machine is a variable or constant, we
        // are actually interested in its 'VALUE' property
        if (found) {
            Value *res = found->getCurrentValue();
            if (*res == SymbolTable::Null) {
                p->cached_entry = 0;
            }
            else {
                p->cached_entry = res;
            }
            return p->cached_entry;
        }
    }
    else if (v->kind == Value::t_dynamic) {
        return v;
    }
    return v;
}

const Value *resolve(Predicate *p, MachineInstance *m, bool left, bool reevaluate) {
    if (reevaluate) {
        p->flushCache();
    }
    ClearFlagOnReturn cfor(&p->needs_reevaluation);
    // return the cached pointer to the value if we have one
    if (p->cached_entry) {
        return p->cached_entry;
    }
    return resolveCacheMiss(p, m, left, reevaluate);
}

ExprNode eval_stack();
void prep(Predicate *p, MachineInstance *m, bool left);

std::ostream &operator<<(std::ostream &out, const ExprNode &o) {
    if (o.kind == ExprNode::t_int) {
        if (o.node) {
            out << "val: " << *o.node;
        }
        else {
            out << "null";
        }
    }
    else {
        out << "op: " << o.op;
    }
    return out;
}

ExprNode eval_stack(MachineInstance *m, std::list<ExprNode>::const_iterator &stack_iter) {
    ExprNode o(*stack_iter++);
    if (o.kind != ExprNode::t_op) {
        if (o.val && o.val->kind == Value::t_dynamic) {
            o.val->dynamicValue()->operator()(m);
            return o.val->dynamicValue()->lastResult();
        }
        return o;
    }
    Value lhs, rhs;
    ExprNode b(eval_stack(m, stack_iter));
#if 0
    if (b.node) {
        std::cout << " eval stack (b): " << *b.node << "\n";
    }
    else {
        std::cout << " evaluated b: null\n";
    }
#endif
    assert(b.kind != ExprNode::t_op);
    if (b.val && b.val->kind == Value::t_dynamic) {
        rhs = b.val->dynamicValue()->operator()(m);
        if (o.op == opAND && rhs.kind == Value::t_bool && rhs.bValue == false) {
            return rhs;
        }
        if (o.op == opOR && rhs.kind == Value::t_bool && rhs.bValue == true) {
            return rhs;
        }
    }
    else if (b.val) {
        rhs = *b.val;
    }
    ExprNode a(eval_stack(m, stack_iter));
#if 0
    if (a.node) {
        std::cout << " eval stack (a): " << *a.node << "\n";
    }
    else {
        std::cout << " evaluated a: null\n";
    }
#endif
    assert(a.kind != ExprNode::t_op);
    if (a.val && a.val->kind == Value::t_dynamic) {
        lhs = a.val->dynamicValue()->operator()(m);
    }
    else if (a.val) {
        lhs = *a.val;
    }
    switch (o.op) {
    case opGE:
        return lhs >= rhs;
    case opGT:
        return lhs > rhs;
    case opLE:
        return lhs <= rhs;
    case opLT:
        return lhs < rhs;
    case opEQ:
        return lhs == rhs;
    case opNE:
        return lhs != rhs;
    case opAND:
        return lhs && rhs;
    case opOR:
        return lhs || rhs;
    case opNOT:
        return !(rhs);
    case opUnaryMinus:
        return -rhs;
    case opPlus:
        return lhs + rhs;
    case opMinus:
        return lhs - rhs;
    case opTimes:
        return lhs * rhs;
    case opDivide:
        return lhs / rhs;
    case opAbsoluteValue:
        if (rhs < 0) {
            return -rhs;
        }
        else {
            return rhs;
        }
    case opMod:
        return lhs % rhs;
    case opBitAnd:
        return lhs & rhs;
    case opBitOr:
        return lhs | rhs;
    case opNegate:
        return ~rhs;
    case opBitXOr:
        return lhs ^ rhs;
    case opInteger:
        return rhs.trunc();
    case opFloat:
        return rhs.toFloat();
    case opString:
        return Value(rhs.asString(), Value::t_string);
    case opAssign:
        return rhs;
    case opMatch: {
        assert(a.val);
        assert(b.val);
        return (bool)matches(a.val->asString().c_str(), b.val->asString().c_str());
    }
    case opAny:
    case opCount:
    case opAll:
    case opIncludes: {
        //DynamicValue *dv = b.val->dynamicValue();
        //if (dv) return dv->operator()(m);
        //return SymbolTable::False;
        return a.val;
    } break;
    case opNone:
        return Value(0);
    }
    return o;
}

/*
    TBD : add a preliminary check for a state test, of one of the
    two forms:   machine IS state  or   state IS machine

    If this is the situation, we identify the machine and state and resolve
    the state within the scope of that machine.

*/

bool prep(Stack &stack, Predicate *p, MachineInstance *m, bool left, bool reevaluate) {
    // check for state comparison
    if (p->left_p && p->left_p->op == opNone && p->right_p && p->right_p->op == opNone &&
        (p->op == opEQ || p->op == opNE)) {
        if (p->left_p->entry.kind == Value::t_symbol && p->right_p->entry.kind == Value::t_symbol) {
            MachineInstance *lhm = m->lookup(p->left_p->entry);
            MachineInstance *rhm = m->lookup(p->right_p->entry);
            if (lhm && !rhm) {
                if (lhm->hasState(p->right_p->entry.sValue)) {
                    p->right_p->entry.kind = Value::t_string;
                    assert(p->left_p->dyn_value == nullptr);
                    p->left_p->dyn_value =
                        new Value(new MachineValue(lhm, p->left_p->entry.sValue));
                }
            }
            else if (rhm && !lhm) {
                if (rhm->hasState(p->left_p->entry.sValue)) {
                    p->left_p->entry.kind = Value::t_string;
                    assert(p->left_p->dyn_value == nullptr);
                    p->right_p->dyn_value =
                        new Value(new MachineValue(rhm, p->right_p->entry.sValue));
                }
            }
        }
    }

    if (p->left_p) {
        // binary operator: push left, right and op
        //std::cout << " resolving left tree\n";
        if (!prep(stack, p->left_p, m, true, reevaluate)) {
            return false;
        }

        //std::cout << " resolving right tree\n";
        if (!prep(stack, p->right_p, m, false, reevaluate)) {
            return false;
        }
        //std::cout << " pushing operator " << p->op << "\n";
        stack.push(p->op);
    }
    else if (p->op == opNOT) {
        //std::cout << " pushing 'true'\n";
        stack.push(ExprNode(SymbolTable::True));
        //std::cout << " resolving right tree\n";
        if (!prep(stack, p->right_p, m, false, reevaluate)) {
            return false;
        }
        //std::cout << " pushing operator " << p->op << "\n";
        stack.push(p->op);
    }
    else if (p->op == opInteger || p->op == opFloat || p->op == opString) {
        stack.push(ExprNode(SymbolTable::True));
        if (!prep(stack, p->right_p, m, false, reevaluate)) {
            return false;
        }
        //std::cout << " pushing operator " << p->op << "\n";
        stack.push(p->op);
    }
    else {
        const Value *result = resolve(p, m, left, reevaluate);
        //std::cout << "prep: resolved " << *result << "\n";
        if (*result == SymbolTable::Null) {
            return false; //result = &p->entry;
        }
        p->last_calculation = result;
        p->cached_entry = result;
        //std::cout << "pushing result: " << *result << "\n";
        if (p->dyn_value) {
            stack.push(ExprNode(result, p->dyn_value));
        }
        else if (p->entry.kind == Value::t_dynamic) {
            stack.push(ExprNode(result, &p->entry));
        }
        else {
            stack.push(ExprNode(result, &p->entry));
        }
    }
    return true;
}

ExprNode::ExprNode(const Value *a, const Value *name) : val(a), node(name), kind(t_int) {
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}
ExprNode::ExprNode(const Value &a, const Value *name) : tmpval(a), val(0), node(name), kind(t_int) {
    val = &tmpval;
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}
ExprNode::ExprNode(Value *a, const Value *name) : val(a), node(name), kind(t_int) {
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}
ExprNode::ExprNode(Value &a, const Value *name) : tmpval(a), val(0), node(name), kind(t_int) {
    val = &tmpval;
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}
ExprNode::ExprNode(long a, const Value *name) : tmpval(a), val(0), node(name), kind(t_int) {
    val = &tmpval;
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}
ExprNode::ExprNode(float a, const Value *name) : tmpval(a), val(0), node(name), kind(t_int) {
    val = &tmpval;
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}
ExprNode::ExprNode(double a, const Value *name) : tmpval(a), val(0), node(name), kind(t_int) {
    val = &tmpval;
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}
ExprNode::ExprNode(bool a, const Value *name) : tmpval(a), val(0), node(name), kind(t_int) {
    val = &tmpval;
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}
ExprNode::ExprNode(PredicateOperator o) : val(0), node(0), op(o), kind(t_op) {
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}

ExprNode::ExprNode(const ExprNode &other)
    : tmpval(other.tmpval), val(other.val), node(other.node), op(other.op), kind(other.kind) {
    if (other.val == &other.tmpval) {
        val = &tmpval;
    }
    ++count_instances;
    if (count_instances > max_count) {
        max_count = count_instances;
    }
}

ExprNode &ExprNode::operator=(const ExprNode &other) { assert(false); }

ExprNode::~ExprNode() {
    --count_instances;
    /*
        if (last_max != max_count) {
        DBG_MSG << "Max ExprNodes: " << max_count<<" current " << count_instances << "\n"; last_max = max_count;
        }
    */
}

void Stack::clear() { stack.clear(); }

Value Evaluator::evaluate(Predicate *predicate, MachineInstance *m) {
    if (!predicate || !m) {
        return SymbolTable::Null;
    }
    if (!stack.stack.empty()) {
        stack.stack.clear();
    }
    if (stack.stack.empty())
        if (!prep(stack, predicate, m, true, true)) {
            std::stringstream ss;
            ss << m->getName() << " Predicate failed to resolve: " << *predicate << "\n";
            MessageLog::instance()->add(ss.str().c_str());
            return false;
        }
    std::list<ExprNode>::const_iterator work = stack.stack.begin();
    ExprNode evaluated(eval_stack(m, work));
    return *(evaluated.val);
}

Value Predicate::evaluate(MachineInstance *m) {
    if (!stack.stack.empty()) {
        stack.stack.clear();
    }
    if (stack.stack.empty())
        if (!prep(stack, this, m, true, needs_reevaluation)) {
            std::stringstream ss;
            ss << m->getName() << " Predicate failed to resolve: " << *this << "\n";
            MessageLog::instance()->add(ss.str().c_str());
            return false;
        }
    //Stack work(stack);
    std::list<ExprNode>::const_iterator work = stack.stack.begin();
    ExprNode evaluated(eval_stack(m, work));
    Value res = *(evaluated.val);
    long t = microsecs();
    last_evaluation_time = t;
    return res;
}

bool Condition::operator()(MachineInstance *m) {
    if (predicate) {
        predicate->stack.stack.clear();
        if (predicate->stack.stack.empty()) {
            if (!prep(predicate->stack, predicate, m, true, predicate->needs_reevaluation)) {
                std::stringstream ss;
                ss << m->getName()
                   << " condition failed: predicate failed to resolve: " << *this->predicate
                   << "\n";
                MessageLog::instance()->add(ss.str().c_str());
                return false;
            }
        }
        std::list<ExprNode>::const_iterator work = predicate->stack.stack.begin();
        ExprNode res(eval_stack(m, work));
        last_result = *res.val;
        std::stringstream ss;
        ss << last_result << " " << *predicate;
        predicate->last_evaluation_time = microsecs();
        last_evaluation = ss.str();
        if (last_result.kind == Value::t_bool) {
            return last_result.bValue;
        }
        else {
            std::stringstream ss;
            ss << "warning:  last result of " << *predicate << " is not boolean: " << last_result
               << "\n";
            MessageLog::instance()->add(ss.str().c_str());
        }
    }
    return false;
}
