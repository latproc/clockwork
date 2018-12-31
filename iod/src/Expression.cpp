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
#include "MachineInstance.h"
#include "Logger.h"
#include "DebugExtra.h"
#include <iomanip>
#include "regular_expressions.h"
#include "Scheduler.h"
#include "FireTriggerAction.h"
#include "dynamic_value.h"
#include "MessageLog.h"
#include "ProcessingThread.h"
#include "MessageLog.h"
#include "ExportState.h"


static int count_instances = 0;
static int max_count = 0;
static int last_max = 0;

std::ostream &operator <<(std::ostream &out, const Predicate &p) { return p.operator<<(out); }
std::ostream &operator<<(std::ostream &out, const PredicateOperator op) {
  const char *opstr;
  switch (op) {
    case opNone: opstr = "None"; break;
    case opGE: opstr = ">="; break;
    case opGT: opstr = ">"; break;
    case opLE: opstr = "<="; break;
    case opLT: opstr = "<"; break;
    case opEQ: opstr = "=="; break;
    case opNE: opstr = "!="; break;
    case opAND: opstr = "AND"; break;
    case opOR: opstr = "OR"; break;
    case opNOT: opstr = "NOT"; break;
    case opUnaryMinus: opstr = "-"; break;
    case opPlus: opstr = "+"; break;
    case opMinus: opstr = "-"; break;
    case opTimes: opstr = "*"; break;
    case opDivide: opstr = "/"; break;
    case opAbsoluteValue: opstr = "ABS"; break;
    case opMod: opstr = "%"; break;
    case opAssign: opstr = ":="; break;
    case opMatch: opstr = "~"; break;
    case opBitAnd: opstr = "&"; break;
    case opBitOr: opstr = "|"; break;
    case opNegate: opstr = "~"; break;
    case opBitXOr: opstr = "^"; break;
    case opInteger: opstr = "AS INTEGER"; break;
    case opFloat: opstr = "AS FLOAT"; break;
    case opAny: opstr = "ANY"; break;
    case opAll: opstr = "ALL"; break;
    case opCount: opstr = "COUNT"; break;
    case opIncludes: opstr = "INCLUDES"; break;
  }
  return out << opstr;
}

void toC(std::ostream &out, const PredicateOperator op) {
  const char *opstr;
  switch (op) {
    case opAND: out << "&&"; break;
    case opOR: out << "||"; break;
    case opNOT: out << "!"; break;
    case opAssign: out << "="; break;
    default:
      out << op;
  }
}

static bool stringEndsWith(const std::string &str, const std::string &subs) {
  size_t n1 = str.length();
  size_t n2 = subs.length();
  if (n1 >= n2 && str.substr(n1-n2) == subs)
    return true;
  return false;
}

Condition::~Condition() {
  delete predicate;
}

Predicate::~Predicate() {
  delete left_p;
  delete right_p;
  if (dyn_value) delete dyn_value;
}

bool Predicate::usesTimer(Value &timer_val) const {
  if (left_p) {
    if (!left_p->left_p) {
      if (left_p->entry.kind == Value::t_symbol
          && (left_p->entry.token_id == ClockworkToken::TIMER
              || stringEndsWith(left_p->entry.sValue,".TIMER"))) {
            //DBG_MSG << "Copying timer value " << right_p->entry << "\n";
            timer_val = right_p->entry;
            return true;
          }
      else
        return false;
    }
    return left_p->usesTimer(timer_val) || right_p->usesTimer(timer_val);
  }
  if (entry.kind == Value::t_symbol && entry.token_id == ClockworkToken::TIMER)
    return true;
  else
    return false;
}

void Predicate::findTimerClauses(std::list<Predicate*>&clauses) {
  if (left_p) {
    if (!left_p->left_p) {
      if (left_p->entry.kind == Value::t_symbol
          && (left_p->entry.token_id == ClockworkToken::TIMER
              || stringEndsWith(left_p->entry.sValue,".TIMER"))) {
            Predicate *p = new Predicate(*this);
            p->flushCache();
            clauses.push_back(p);
          }
    }
    else
      left_p->findTimerClauses(clauses);
  }
  if (right_p) {
    if (!right_p->left_p) {
      if (right_p->entry.kind == Value::t_symbol
          && (right_p->entry.token_id == ClockworkToken::TIMER || stringEndsWith(right_p->entry.sValue,".TIMER"))) {
        Predicate *p = new Predicate(*this);
        p->flushCache();
        clauses.push_back(p);
      }
    }
    else {
      right_p->findTimerClauses(clauses);
    }
  }
}

const Value &Predicate::getTimerValue() {
  if (left_p->entry.kind == Value::t_symbol
      && (left_p->entry.token_id == ClockworkToken::TIMER || stringEndsWith(left_p->entry.sValue,".TIMER"))) {
    return right_p->entry;
  }
  if (right_p->entry.kind == Value::t_symbol
      && (right_p->entry.token_id == ClockworkToken::TIMER || stringEndsWith(right_p->entry.sValue,".TIMER"))) {
    return left_p->entry;
  }
  return SymbolTable::Null;
}

void Predicate::clearTimerEvents(MachineInstance *target) // clear all timer events scheduled for the supplid machine
{
  if (left_p) {
    left_p->clearTimerEvents(target);
    if (entry.kind == Value::t_symbol
        && (left_p->entry.token_id == ClockworkToken::TIMER
            || stringEndsWith(left_p->entry.sValue,".TIMER"))) {
          DBG_SCHEDULER << "clear timer event for entry " << entry << "\n";
        }
  }
  if (right_p) right_p->clearTimerEvents(target);
}


std::ostream &Predicate::operator <<(std::ostream &out) const {
  if (left_p) {
    out << "(";
    if (op != opNOT && op != opInteger && op != opFloat)
      left_p->operator<<(out); // ignore the lhs for NOT operators
    out << " " << op << " ";
    if (right_p) right_p->operator<<(out);
    out << ")";
  }
  else {
    if (cached_entry) {
      out << entry;
      if (entry.kind == Value::t_symbol) out << " (" << *cached_entry << ")";
    }
    else if (last_calculation) {
      out << entry;
      if (entry.kind == Value::t_symbol || entry.kind == Value::t_dynamic)
        out <<  " (" << *last_calculation << ") ";
    }
    else out << " " << entry;
  }
  return out;
}

// Conditions
Predicate::Predicate(const Predicate &other) : left_p(0), op(opNone), right_p(0) {
  if (other.left_p) left_p = new Predicate( *(other.left_p) );
  op = other.op;
  if (op == opInteger) {

  }
  if (other.right_p) right_p = new Predicate( *(other.right_p) );
  entry = other.entry;
  if (other.entry.dyn_value) {
    entry.dyn_value = DynamicValue::ref(other.entry.dyn_value->clone());
    //dyn_value = DynamicValue::ref(other.dyn_value); // note shared copy, should be a shared pointer
  }
  if (other.dyn_value)
    dyn_value = new Value(DynamicValue::ref(other.dyn_value->dyn_value));
  else
    dyn_value = 0;
  entry.cached_machine = 0; // do not preserve any cached values in this clone
  priority = other.priority;
  mi = 0;
  cached_entry = 0;
  lookup_error = false;
  last_calculation = 0;
  needs_reevaluation = true;
}

Predicate &Predicate::operator=(const Predicate &other) {
  if (other.left_p) left_p = new Predicate( *(other.left_p) );
  op = other.op;
  if (other.right_p) right_p = new Predicate( *(other.right_p) );
  entry = other.entry;
  if (other.entry.dyn_value) {
    entry.dyn_value = DynamicValue::ref(other.entry.dyn_value->clone());
    //dyn_value = DynamicValue::ref(other.dyn_value); // note shared copy, should be a shared pointer
  }
  if (other.dyn_value)
    dyn_value = new Value(DynamicValue::ref(other.dyn_value->dyn_value));
  else
    dyn_value = 0;
  entry.cached_machine = 0; // do not preserve any cached machine pointers in this clone
  priority = other.priority;
  mi = 0;
  cached_entry = 0; // do not preserve cached the value pointer
  lookup_error = false;
  last_calculation = 0;
  needs_reevaluation = true;
  return *this;
}

PredicateSymbolDetails::PredicateSymbolDetails(std::string n, std::string t, std::string e): name(n), type(t), export_name(e) {

}

PredicateSymbolDetails::PredicateSymbolDetails() { }

bool PredicateSymbolDetails::operator<(const PredicateSymbolDetails &other) const {
  return name < other.name;
}

static std::string interpret_name(const std::string &name) {
  std::string type("unknown");
  if (name == "TIMER" || stringEndsWith(name, ".TIMER"))
    type = "timer";
  else if (name == "SELF")
    type = "machine";
  else if (name == "VALUE")
    type = "property";
  else if (name == "DEFAULT")
    type = "ignored";
  else
    int x = 1;
  return type;
}

void Predicate::findSymbols(std::set<PredicateSymbolDetails> &symbols, const Predicate *parent) const {
  if (left_p) left_p->findSymbols(symbols, this);
  if (right_p) right_p->findSymbols(symbols, this);
  if (entry.kind == Value::t_symbol) {
    std::string var(entry.sValue);
    size_t pos = var.rfind('.');
    if (pos != std::string::npos)
      var = var.substr(pos+1);

    std::string export_name(entry.sValue);
    pos = export_name.find('.');
    while (pos != std::string::npos) {
      export_name[pos] = '_';
      pos = export_name.find('.', pos+1);
    }
    std::string type(interpret_name(export_name));
    if (type == "unknown" && parent) { // look deeper into the current context
      Predicate *other = parent->left_p;
      if (other == this) other = parent->right_p;
      if (other && other->entry != SymbolTable::Null) {
        Value val(other->entry);
        if (val.kind == Value::t_string || val.kind == Value::t_symbol) {
          std::string other_type(interpret_name(val.sValue));
          if (other_type != "unknown")
            type = other_type;
          else
            int x = 1;
        }
      }
    }
    else if (!parent)
      int x = 1;
    symbols.insert(PredicateSymbolDetails(entry.sValue, type, export_name));
  }
}

void Predicate::toC(std::ostream &out) const {
  std::map<std::string, PredicateSymbolDetails> &symbols(ExportState::all_symbol_names());
  if (left_p) {
    if (op != opAssign) out << "(";
    if (op != opNOT && op != opInteger && op != opFloat)
      left_p->toC(out); // ignore the lhs for NOT operators
    out << " "; ::toC(out, op); out << " ";
    if (right_p) right_p->toC(out);
    if (op != opAssign) out << ")";
  }
  else {
    if (entry.kind == Value::t_symbol) {
      if (entry.sValue == "TIMER")
        out << "m->machine.TIMER";
      else {
        std::map<std::string, PredicateSymbolDetails>::iterator item = symbols.find(entry.sValue);
        if (item != symbols.end()) {
          const PredicateSymbolDetails &psd = (*item).second;
          if (psd.export_name == "off")
            int x = 1;
          if (psd.type == "state")
            out << ExportState::instance()->prefix() << psd.export_name;
          else
            out << "*" << ExportState::instance()->prefix() << psd.export_name;
        }
        else
          out << ExportState::instance()->prefix() << entry.sValue;
      }
    }
    else
      out << entry;
  }
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
  /*if (op == opNone) {
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

Condition::Condition(Predicate*p) : predicate(0) {
  if (p) predicate = new Predicate(*p);
}

Condition::Condition(const Condition &other) : predicate(0) {
  if (other.predicate)
    predicate = new Predicate( *(other.predicate) );
}

Condition &Condition::operator=(const Condition &other) {
  if (other.predicate)
    predicate = new Predicate( *(other.predicate) );
  return *this;
}

std::ostream &operator<<(std::ostream&out, const Stack &s) {
  return s.operator<<(out);
}

std::ostream &Stack::operator<<(std::ostream&out) const {
#if 1
  // prefix
  BOOST_FOREACH(const ExprNode &n, stack) {
    if (n.kind == ExprNode::t_op) {
      out << n.op << " ";
    }
    else {
      if (n.node) out << *n.node;
      out << " (" << *n.val<< ") ";
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

std::ostream &Stack::traverse(std::ostream &out, std::list<ExprNode>::const_iterator &iter, std::list<ExprNode>::const_iterator &end) const
{
  // note current error with display of rhs before lhs
  if (iter == end) return out;
  const ExprNode &n = *iter++;
  if (n.kind == ExprNode::t_op) {
    out << "(";
    if (n.op != opNOT && n.op != opInteger && n.op != opFloat)
      // ignore lhs for the not operator
      traverse(out, iter, end);
    out <<" " << n.op << " " ;
    traverse(out, iter, end);
    out << ")";
  }
  else {
    if (n.node) out << *n.node;
    out << " ("<< *n.val << ") ";
  }
  return out;
}

class ClearFlagOnReturn {
public:
  ClearFlagOnReturn(bool *val) : to_clear(val) { }
  ~ClearFlagOnReturn() {
    *to_clear = false;
  }
private:
  bool *to_clear;
};

const Value *resolveCacheMiss(Predicate *p, MachineInstance *m, bool left, bool reevaluate) {
  Value *v = &p->entry;
  p->clearError();
  if (v->kind == Value::t_symbol) {
    // lookup machine and get state. hopefully we have already cached a pointer to the machine
    MachineInstance *found = 0;
    if (p->mi)
      found = p->mi;
    else {
      // before looking up machines, check for specific keywords
      if (left && v->sValue == "DEFAULT") { // default state has a low priority but always returns true
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
      else {
        const Value *res = m->resolve(v->sValue);
        while (res->kind == Value::t_symbol)
          res = m->resolve(res->sValue);
        return res;
      }
    }
    // if we found a reference to a machine but that machine is a variable or constant, we
    // are actually interested in its 'VALUE' property
    if (found) {
      Value *res = found->getCurrentValue();
      if (*res == SymbolTable::Null)
        p->cached_entry = 0;
      else
        p->cached_entry = res;
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
  if (p->cached_entry)
    return p->cached_entry;
  return resolveCacheMiss(p, m, left, reevaluate);
}

ExprNode eval_stack();
void prep(Predicate *p, MachineInstance *m, bool left);

ExprNode eval_stack(MachineInstance *m, std::list<ExprNode>::const_iterator &stack_iter){
  ExprNode o(*stack_iter++);
#if 0
  std::cout << "popped node: ";
  if (o.kind == ExprNode::t_int) {
    if (o.node)
      std::cout << "val: " << *o.node;
    else
      std::cout << "null";

  }
  else
    std::cout << "op: " << o.op;
  std::cout << "\n";
#endif
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
  if (b.node)
    std::cout << " eval stack (b): " << *b.node << "\n";
  else
    std::cout << " evaluated b: null\n";
#endif
  assert(b.kind != ExprNode::t_op);
  if (b.val && b.val->kind == Value::t_dynamic) {
    rhs = b.val->dynamicValue()->operator()(m);
    if (o.op == opAND && rhs.kind == Value::t_bool && rhs.bValue == false) return rhs;
    if (o.op == opOR && rhs.kind == Value::t_bool && rhs.bValue == true) return rhs;
  }
  else if (b.val) rhs = *b.val;
  ExprNode a(eval_stack(m, stack_iter));
#if 0
  if (a.node)
    std::cout << " eval stack (a): " << *a.node << "\n";
  else
    std::cout << " evaluated a: null\n";
#endif
  assert(a.kind != ExprNode::t_op);
  if (a.val && a.val->kind == Value::t_dynamic) {
    lhs = a.val->dynamicValue()->operator()(m);
  }
  else if (a.val) lhs = *a.val;
  switch (o.op) {
    case opGE: return lhs >= rhs;
    case opGT: return lhs > rhs;
    case opLE: return lhs <= rhs;
    case opLT: return lhs < rhs;
    case opEQ: return lhs == rhs;
    case opNE: return lhs != rhs;
    case opAND: return lhs && rhs;
    case opOR: return lhs || rhs;
    case opNOT: return !(rhs);
    case opUnaryMinus: return - rhs;
    case opPlus:  return lhs + rhs;
    case opMinus: return lhs - rhs;
    case opTimes: return lhs * rhs;
    case opDivide:return lhs / rhs;
    case opAbsoluteValue: if (rhs < 0) return - rhs; else return rhs;
    case opMod:   return lhs % rhs;
    case opBitAnd: return lhs & rhs;
    case opBitOr: return lhs | rhs;
    case opNegate: return ~ rhs;
    case opBitXOr: return lhs ^ rhs;
    case opInteger: return rhs.trunc();
    case opFloat: return rhs.toFloat();
    case opAssign:return rhs;
    case opMatch: {
      assert(a.val); assert(b.val);
      return (bool)matches(a.val->asString().c_str(), b.val->asString().c_str());
    }
    case opAny:
    case opCount:
    case opAll:
    case opIncludes:
    {
      //DynamicValue *dv = b.val->dynamicValue();
      //if (dv) return dv->operator()(m);
      //return SymbolTable::False;
      return a.val;
    }
      break;
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
  if (p->left_p && p->left_p->op == opNone
      && p->right_p && p->right_p->op == opNone
      && (p->op == opEQ || p->op == opNE)) {
    if (p->left_p->entry.kind == Value::t_symbol && p->right_p->entry.kind == Value::t_symbol) {
      MachineInstance *lhm = m->lookup(p->left_p->entry);
      MachineInstance *rhm = m->lookup(p->right_p->entry);
      if (lhm && !rhm) {
        if (lhm->hasState(p->right_p->entry.sValue)) {
          p->right_p->entry.kind = Value::t_string;
          p->left_p->dyn_value = new Value(new MachineValue(lhm, p->left_p->entry.sValue));
        }
      }
      else if (rhm && !lhm) {
        if (rhm->hasState(p->left_p->entry.sValue)) {
          p->left_p->entry.kind = Value::t_string;
          p->right_p->dyn_value = new Value(new MachineValue(rhm, p->right_p->entry.sValue));
        }
      }
    }
  }

  if (p->left_p) {
    // binary operator: push left, right and op
    //std::cout << " resolving left tree\n";
    if (!prep(stack, p->left_p, m, true, reevaluate)) return false;

    //std::cout << " resolving right tree\n";
    if (!prep(stack, p->right_p, m, false, reevaluate)) return false;
    //std::cout << " pushing operator " << p->op << "\n";
    stack.push(p->op);
  }
  else if (p->op == opNOT) {
    //std::cout << " pushing 'true'\n";
    stack.push(ExprNode(SymbolTable::True));
    //std::cout << " resolving right tree\n";
    if (!prep(stack, p->right_p, m, false, reevaluate)) return false;
    //std::cout << " pushing operator " << p->op << "\n";
    stack.push(p->op);
  }
  else if (p->op == opInteger || p->op == opFloat) {
    //std::cout << " pushing 'true'\n";
    stack.push(ExprNode(SymbolTable::True));
    //std::cout << " resolving right tree\n";
    if (!prep(stack, p->right_p, m, false, reevaluate)) return false;
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
    else
      stack.push(ExprNode(result, &p->entry));
  }
  return true;
}

ExprNode::ExprNode(const Value *a, const Value *name)
: val(a), node(name), kind(t_int) {
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(const Value &a, const Value *name)
:tmpval(a), val(0), node(name), kind(t_int){
  val = &tmpval;
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(Value *a, const Value *name)
: val(a), node(name), kind(t_int) {
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(Value &a, const Value *name) :tmpval(a), val(0), node(name), kind(t_int) {
  val = &tmpval;
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(long a, const Value *name)
: tmpval(a), val(0), node(name), kind(t_int) {
  val = &tmpval;
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(float a, const Value *name)
: tmpval(a), val(0), node(name), kind(t_int) {
  val = &tmpval;
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(double a, const Value *name)
: tmpval(a), val(0), node(name), kind(t_int) {
  val = &tmpval;
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(bool a, const Value *name)
: tmpval(a), val(0), node(name), kind(t_int) {
  val = &tmpval;
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(PredicateOperator o) : val(0), node(0), op(o), kind(t_op) {
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}
ExprNode::ExprNode(const ExprNode &other) : tmpval(other.tmpval), val(other.val), node(other.node), op(other.op), kind(other.kind) {
  if (other.val == &other.tmpval) val = &tmpval;
  ++count_instances;
  if (count_instances>max_count) max_count = count_instances;
}

ExprNode &ExprNode::operator=(const ExprNode &other) {
  assert(false);
}

ExprNode::~ExprNode() {
  --count_instances;
  /*
   if (last_max != max_count) {
   DBG_MSG << "Max ExprNodes: " << max_count<<" current " << count_instances << "\n"; last_max = max_count;
   }
   */
}

void Stack::clear() {
  stack.clear();
}

Value Predicate::evaluate(MachineInstance *m) {
  if (stack.stack.size() != 0)
    stack.stack.clear();
  if (stack.stack.size() == 0)
    if (!prep(stack, this, m, true, needs_reevaluation)) {
      std::ostream &out = MessageLog::instance()->get_stream();
      out << m->getName() << " Predicate failed to resolve: " << *this << "\n";
      MessageLog::instance()->release_stream();
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
    if (predicate->stack.stack.size() == 0 ) {
      if (!prep(predicate->stack, predicate, m, true, predicate->needs_reevaluation)) {
        std::ostream &out = MessageLog::instance()->get_stream();
        out << m->getName() << " condition failed: predicate failed to resolve: " << *this->predicate << "\n";
        MessageLog::instance()->release_stream();
        return false;
      }
    }
    std::list<ExprNode>::const_iterator work = predicate->stack.stack.begin();
    ExprNode res(eval_stack(m, work));
    last_result = *res.val;
    std::ostream &out = MessageLog::instance()->get_stream();
    out << last_result << " " << *predicate;
    long t = microsecs();
    predicate->last_evaluation_time = t;
    last_evaluation = MessageLog::instance()->access_stream_message();
    MessageLog::instance()->close_stream();
    if (last_result.kind == Value::t_bool)
      return last_result.bValue;
    else {
      std::ostream &out = MessageLog::instance()->get_stream();
      out << "warning:  last result of " << *predicate << " is not boolean: " << last_result << "\n";
      MessageLog::instance()->release_stream();
    }
  }
  return false;
}

