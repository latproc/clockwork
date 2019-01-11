#include <list>
#include <vector>
#include <fstream>
#include "State.h"
#include "StableState.h"
#include "MachineClass.h"
#include "MachineInstance.h"
#include "MachineCommandAction.h"
#include "Parameter.h"
#include "ModbusInterface.h"
#include "ExportState.h"
#include "PredicateAction.h"

std::list<MachineClass*> MachineClass::all_machine_classes;
std::map<std::string, MachineClass> MachineClass::machine_classes;

// TODO: relocate this
static bool stringEndsWith(const std::string &str, const std::string &subs) {
  size_t n1 = str.length();
  size_t n2 = subs.length();
  if (n1 >= n2 && str.substr(n1-n2) == subs)
    return true;
  return false;
}


MachineClass::MachineClass(const char *class_name) : default_state("unknown"), initial_state("INIT"),
	name(class_name), allow_auto_states(true), token_id(0), plugin(0),
	polling_delay(0), parent(0)
{
	addState("INIT");
	token_id = Tokeniser::instance()->getTokenId(class_name);
	all_machine_classes.push_back(this);
}

void MachineClass::defaultState(State state) {
	default_state = state;
}

void MachineClass::disableAutomaticStateChanges() {
	allow_auto_states = false;
}

void MachineClass::enableAutomaticStateChanges() {
	allow_auto_states = true;
}

bool MachineClass::isStableState(State &state) {
	return stable_state_xref.find(state.getName()) != stable_state_xref.end();
}

void MachineClass::addState(const char *name) {
	states.push_back(new State(name));
}

State *MachineClass::findMutableState(const char *seek) {
	std::list<State *>::const_iterator iter = states.begin();
	while (iter != states.end()) {
		State *s = *iter++;
		if (s->getName() == seek) return s;
	}
	return 0;
}

const State *MachineClass::findState(const char *seek) const {
	std::list<State *>::const_iterator iter = states.begin();
	while (iter != states.end()) {
		const State *s = *iter++;
		if (s->getName() == seek) return s;
	}
	return 0;
}

const State *MachineClass::findState(const State &seek) const {
	std::list<State*>::const_iterator iter = states.begin();
	while (iter != states.end()) {
		const State *s = *iter++;
		if (*s == seek) {
			return s;
		}
	}
	return 0;
}


MachineClass *MachineClass::find(const char *name) {
	int token = Tokeniser::instance()->getTokenId(name);
	std::list<MachineClass *>::iterator iter = all_machine_classes.begin();
	while (iter != all_machine_classes.end()) {
		MachineClass *mc = *iter++;
		if (mc->token_id == token) return mc;
	}
	return 0;
}

void MachineClass::addProperty(const char *p) {
	//NB_MSG << "Warning: ignoring OPTION " << p << " in " << name << "\n";
	property_names.insert(p);
}

void MachineClass::addPrivateProperty(const char *p) {
	//NB_MSG << "Warning: ignoring OPTION " << p << " in " << name << "\n";
	property_names.insert(p);
	local_properties.insert(p); //
}

void MachineClass::addPrivateProperty(const std::string &p) {
	//NB_MSG << "Warning: ignoring OPTION " << p << " in " << name << "\n";
	property_names.insert(p.c_str());
	local_properties.insert(p); //
}

void MachineClass::addCommand(const char *p) {
	//NB_MSG << "Warning: ignoring COMMAND/RECEIVES " << p << " in " << name << "\n";
	command_names.insert(p);
}

bool MachineClass::propertyIsLocal(const char *name) const {
	return local_properties.count(name) > 0;
}

bool MachineClass::propertyIsLocal(const std::string &name) const {
	return local_properties.count(name) > 0;
}

bool MachineClass::propertyIsLocal(const Value &name) const {
	return local_properties.count(name.asString()) > 0;
}

MachineCommandTemplate *MachineClass::findMatchingCommand(std::string cmd_name, const char *state) {
	// find the correct command to use based on the current state
	std::multimap<std::string, MachineCommandTemplate*>::iterator cmd_it = commands.find(cmd_name);
	while (cmd_it != commands.end()) {
		std::pair<std::string, MachineCommandTemplate*>curr = *cmd_it++;
		if (curr.first != cmd_name) break;
		MachineCommandTemplate *cmd = curr.second;
		if ( !state || cmd->switch_state || (!cmd->switch_state && cmd->getStateName().get() == state)  )
			return cmd;
	}
	return 0;
}


std::string method_name(const std::string msg) {
  char buf[msg.length()+1];
  const char *p = msg.c_str();
  char *q = buf;
  while (*p) {
    if (*p == '.') *q++ = '_';
    else *q++ = *p;
    ++p;
  }
  *q = 0;
  return buf;
}

// Messages that appear in clockwork programs as strings are converted to
// functions in the source
const Value &ExportState::symbol(const char *name) { return messages.find(name); }
const Value &ExportState::create_symbol(const char *name) {
  messages.add(name, Value(method_name(name)));
  return messages.find(name);
}
void ExportState::add_state(const std::string name) {
  std::map<std::string, int>::iterator found = string_ids.find(name);
  if (found == string_ids.end())
    string_ids[name] =  string_ids.size() + 1;
}
int ExportState::lookup(const std::string name) {
  std::map<std::string, int>::iterator found = string_ids.find(name);
  if (found == string_ids.end()) return -1;
  else return (*found).second;
}
// states and messages share the same id range; messages also have their own
// hash to make it easy to generate a global message id header file
void ExportState::add_message(const std::string name, int value) {
  std::map<std::string, int>::iterator found = string_ids.find(name);
  if (found == string_ids.end())
    string_ids[name] =  (value == -1) ? string_ids.size() + 1 : value;
  message_ids[name] = string_ids[name];
}

int ExportState::lookup_symbol(const std::string name) {
  std::map<std::string, int>::iterator found = symbols.find(name);
  if (found == symbols.end()) return -1;
  else return (*found).second;
}

void ExportState::add_symbol(const std::string name, int value) {
  std::map<std::string, int>::iterator found = symbols.find(name);
  if (found == symbols.end())
    symbols[name] =  (value == -1) ? symbols.size() + 1 : value;
}

void MachineClass::exportHandlers(std::ostream &ofs)
{
  std::stringstream received_message_handlers;
  {
    bool init_seen = false;
    std::multimap<Message, MachineCommandTemplate*>::const_iterator recv_iter = receives.begin();
    while (recv_iter != receives.end()) {
      const std::pair<Message, MachineCommandTemplate*> item = *recv_iter++;
      if (method_name(item.first.getText()) == "INIT_enter")
        init_seen = true;

      bool can_block = false;
      for (unsigned int i = 0; i<(item.second)->action_templates.size(); ++i) {
        ActionTemplate*at = (item.second)->action_templates.at(i);
        if (at && at->canBlock()) can_block = true;
      }
      ofs
      << "int cw_" << name << "_" << method_name(item.first.getText())
      << "(struct cw_" << name << " *m, ccrContParam) {\n";
      std::string saved_prefix = ExportState::instance()->prefix();
      if (can_block) {
        ofs << "\tccrBeginContext;\n";
        ExportState::instance()->set_prefix(std::string("ctx->") + saved_prefix);
      }
      ofs << "\tstruct cw_" << name << "_Vars *v;\n";
      std::stringstream actions;
      for (unsigned int i = 0; i<(item.second)->action_templates.size(); ++i) {
        ActionTemplate*at = (item.second)->action_templates.at(i);
        if (at) { at->toC(actions, ofs); }
      }
      if (can_block) {
        ofs << "\tccrEndContext(ctx);\n\tccrBegin(ctx);\n";
        ofs << "\tctx->v = m->vars;\n";
        ExportState::instance()->set_prefix(saved_prefix);
      }
      else {
        ofs << "\tv = m->vars;\n";
      }
      ofs << actions.str();
      ofs << "\tm->machine.execute = 0;\n";
      if (can_block) {
        ofs << "\tccrFinish(1);\n}\n";
      }
      else {
        ofs << "\treturn 1;\n}\n";
      }
      // if this method is not an enter handler it needs to be inserted into the handle_message
      bool handled = item.first.isEnter();
      if (item.first.isSimple()) {
        std::string message_name = item.first.getText();
        size_t name_pos = message_name.find(".");
        if (name_pos != std::string::npos) {
          message_name.erase(name_pos);
          std::string msg_name(item.first.getText().substr(name_pos+1));
          size_t enter_pos = msg_name.find("_enter");
          if (enter_pos != std::string::npos && enter_pos == msg_name.length() - 6) {
            msg_name.erase(enter_pos);
            received_message_handlers << "\t if (source == m->_"
              << message_name
              << " && state == " << ExportState::lookup(msg_name)
              << ")\n"
              << "\t\tMachineActions_add(obj, (enter_func)"
              << "cw_" << name << "_"
              << method_name(item.first.getText())
              << ");\n";
            handled = true;
          }
        }
        else {
          ExportState::add_message(message_name);
          received_message_handlers << "\tif (state == cw_message_" << message_name << ")\n"
          << "\t\tMachineActions_add(obj, (enter_func)"
          << "cw_" << name << "_"
          << method_name(item.first.getText())
          << ");\n";
          handled = true;
        }
      }
      if (!handled)
        std::cout << "Warning: Not handling " << item.first.getText() << "\n";
    }
    if (!init_seen) {
      ofs << "int cw_" << name << "_INIT_enter(struct cw_" << name << " *m, ccrContParam) {\n"
      << "\tm->machine.execute = 0;\n"
      << "\treturn 1;\n"
      << "};\n";
    }
  }

  std::string external_handlers = received_message_handlers.str();
  ofs
   << "int cw_" << name << "_handle_message(struct MachineBase *obj, struct MachineBase *source, int state) {\n"
   << "\tstruct cw_" << name << " *m = (struct cw_" << name << " *)obj;\n"
   << external_handlers
   << "\tmarkPending(obj);\n"
   << "\treturn 1;\n"
   << "}\n";
}

void MachineClass::collectTimerPredicates() {
  for (size_t i=0; i<stable_states.size(); ++i) {
    stable_states[i].condition.predicate->findTimerClauses(timer_clauses);
    stable_states[i].collectTimerPredicates();
  }
  std::list<Predicate*>::const_iterator ti = timer_clauses.begin();
  while (ti != timer_clauses.end()) {
    std::cout << "MACHINE " << name << " timer clause: " << *(*ti++) << "\n";
  }
}

void MachineClass::exportCommands(std::ostream &ofs)
{
#if 0
	std::list<State*>::iterator iter = states.begin();
	while (iter != states.end()) {
		const State *s = *iter++;
		ofs
		<< "\tint " << name << "_enter_" << s->getName()
		<< "(struct cw_" << name << " *m, ccrContParam) {";
		std::string fn_name(s->getName());
		std::multimap<std::string, MachineCommandTemplate*>::iterator found = commands.find(fn_name);
		if (found != commands.end()) {
			const std::pair<std::string, MachineCommandTemplate*> &item = *found;
			ofs << "// " <<(*item.second) << "\n";
			for (unsigned int i = 0; i<(item.second)->action_templates.size(); ++i) {
				ActionTemplate*at = (item.second)->action_templates.at(i);
				if (at) ofs << "\t// " << (*at) << "\n";
			}
		}
		ofs << "\tm->machine.execute = 0; return 1; }\n";
	}
#else
  std::multimap<Message, MachineCommandTemplate*>::const_iterator recv_iter = receives.begin();
  while (recv_iter != receives.end()) {
    const std::pair<Message, MachineCommandTemplate*> item = *recv_iter++;
  }
#endif
}

static std::string generate_name_lookup_decls(std::string var, std::string to_lookup, const std::string type) {
  std::stringstream res;

  if (type == "machine")
    res << "\tunsigned int *l_" << var << ";\n";
  else if (type == "state")
    res << "\tunsigned int l_" << var << ";\n";
  else if (type == "timer" || stringEndsWith(to_lookup, ".TIMER"))
    res << "\tunsigned long *l_" << var << ";\n";
  else {
    if (to_lookup.find('.') != std::string::npos) {
      res << "\tMachineBase *l_m_" << var << ";\n";
      ExportState::instance()->remotes().insert(var); // used to access the machine owning the variable
    }
    res << "\tValue *l_" << var << ";\n";
  }
  return res.str();
}


static std::string generate_name_backups(std::string var, std::string to_lookup, const std::string type) {
  std::stringstream res;

  if (type == "machine")
    res << "\tb->l_" << var << " = " << "*v->l_" << var << ";\n";
  else if (type == "state")
    res << "\tb->l_" << var << " = " << "v->l_" << var << ";\n";
  else if (type == "timer" || stringEndsWith(to_lookup, ".TIMER"))
    res << "\tb->l_" << var << " = " << "*v->l_" << var << ";\n";
  else
    res << "\tb->l_" << var << " = " << "*v->l_" << var << ";\n";
  return res.str();
}


// for the exporter; turn a namestring into a runtime lookup
static std::string generate_name_lookup(std::string var, std::string to_lookup, std::string &type) {
  std::stringstream res;

  //res << ((is_machine) ? "MachineBase *" :  "long *") << "l_" << var;
  bool is_machine = type == "machine";
  size_t start = 0;
  size_t pos = to_lookup.find('.', start);
  if (pos == std::string::npos) { // local variable
    res << "\tv->l_" << var;
    if (to_lookup == "SELF") {
      res << " = &m->machine.state;\n";
      type = "machine";
    }
    else if (to_lookup == "TIMER") {
      res << " = &m->machine.TIMER;\n";
      type = "timer";
    }
    else if (type == "state") {
      res << " = state_cw_" << var <<";\n";
    }
    else if (is_machine)
      res << " = &m->_" << var << "->state;\n";
    else
      res << " = &m->" << var << ";\n";
  }
  else {
    res << "\t{\n\tMachineBase *mm = &m->machine;\n";
    while (pos != std::string::npos) {
      std::string nm( to_lookup.substr(start, pos-start) );
      res << "\tmm = mm->lookup_machine(mm, sym_" << nm << ");\n";
      start = pos + 1;
      pos = to_lookup.find('.', start);
    }
    std::string short_name = to_lookup.substr(start);
    if (is_machine)
      res << "\tv->l_" << var << " = mm->lookup_machine(mm, sym_" << short_name << ");\n";
    else if (short_name == "TIMER") // implemented in MachineBase
      res << "\tv->l_" << var << " = &mm->" << short_name << ";\n";
    else if (short_name == "STATE")
      res << "\tv->l_" << var << " = &mm->state;\n";
    else {
      res << "\tv->l_m_" << var << " = mm;\n";
      res << "\tv->l_" << var << " = mm->lookup(mm, sym_" << short_name << ");\n";
    }
    res << "\t}\n";
  }
  return res.str();
}

static void subst_all(std::string &s, char old, char replacement) {
  size_t start = 0;
  size_t pos;
  while ( (pos = s.find(old, start)) != std::string::npos) {
    s[pos] = replacement;
  }
}

#if 0
static void subst_all(std::string &s, std::string old, std::string replacement) {
  size_t start = 0;
  size_t pos;
  while ( (pos = s.find(old, start)) != std::string::npos) {
    s.replace(pos, old.length(), replacement);
  }
}
#endif
std::map<std::string, int> ExportState::string_ids;
std::map<std::string, int> ExportState::message_ids;
std::map<std::string, int> ExportState::symbols;
std::map<std::string, PredicateSymbolDetails> ExportState::symbol_names;

bool MachineClass::cExport(const std::string &filename) {
	// TODO: redo this with some kind of templating system

	std::stringstream params;
	std::stringstream values;
	std::stringstream refs;
	std::stringstream setup;
  std::stringstream handlers;
  std::stringstream lookup;
  std::stringstream lookup_machine;

  ExportState::all_symbol_names().clear();
  std::map<std::string, PredicateSymbolDetails> &symbols(ExportState::all_symbol_names());

  {
    std::vector<Parameter>::iterator p_iter = parameters.begin();
    while (p_iter != parameters.end()) {
      const Parameter &p = *p_iter++;
      params << ", MachineBase *" <<p.val;
      values << ", " << p.val;
      MachineInstance *p_machine = MachineInstance::find(p.val.asString().c_str());
      refs << "\tMachineBase *_" << p.val << ";\n";
      symbols[p.val.sValue] = PredicateSymbolDetails(p.val.sValue, "machine", p.val.sValue);
      setup << "\tm->_" << p.val << " = " << p.val << ";\n";
      setup << "\tif (" << p.val<< ") MachineDependencies_add(" << p.val << ", cw_" << name << "_To_MachineBase(m));\n";
      lookup_machine << "\tif (symbol == sym_" << p.val <<") return m->_" << p.val << ";\n";
      ExportState::add_symbol(std::string("sym_") + p.val.sValue);
    }
  }

	{
		std::string header(filename);
		header += ".h";
		std::ofstream ofh(header);
		ofh
		<< "#ifndef __cw_" << name << "_h__\n"
		<< "#define __cw_" << name << "_h__\n"
		<< "\n#include \"runtime.h\"\n";

    ofh
    << "#define Value int\n"
    << "struct cw_" << name << "_Vars;\n"
    << "struct cw_" << name << "_Vars_backup;\n"
    << "struct cw_" << name << " {\n"
    << "\tMachineBase machine;\n"
    << "\tint gpio_pin;\n"
    << "\tstruct IOAddress addr;\n"
    << refs.str();
    SymbolTableConstIterator pnames_iter = properties.begin();
    while (pnames_iter != properties.end()) {
      const std::pair<std::string, Value> prop = *pnames_iter++;
      ofh<< "\tValue " << prop.first << "; // " << prop.second << "\n";
      symbols[prop.first] = PredicateSymbolDetails(prop.first, "property", prop.first);
      lookup << "\tif (symbol == sym_" << prop.first <<") return &m->" << prop.first << ";\n";
      ExportState::add_symbol(std::string("sym_") + prop.first);
    }
    std::map<std::string, Value>::iterator opts_iter = options.begin();
    while (opts_iter != options.end()) {
      const std::pair<std::string, Value> opt = *opts_iter++;
      ofh << "\tValue " << opt.first << "; // " << opt.second << "\n";
      symbols[opt.first] = PredicateSymbolDetails(opt.first, "property", opt.first);
      lookup << "\tif (symbol == sym_" << opt.first <<") return &m->" << opt.first << ";\n";
      ExportState::add_symbol(std::string("sym_") + opt.first);
    }
    ofh << "\tstruct cw_" << name << "_Vars *vars;\n";
    ofh << "\tstruct cw_" << name << "_Vars_backup *backup;\n";
    ofh << "};\n"
		<< "struct IOAddress *cw_" << name << "_getAddress(struct cw_" << name << " *p);\n"
		<< "struct cw_" << name << " *create_cw_" << name << "(const char *name";
    if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
      ofh << ", int pin";
    }
    ofh << params.str() << ");\n"
    << "void Init_cw_" << name << "(struct cw_" << name << " * " << ", const char *name";
    if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
      ofh << ", int pin";
    }
    ofh << params.str() << ");\n"
    << "MachineBase *cw_" << name << "_To_MachineBase(struct cw_" << name << " *);\n"
    << "#endif\n";
	}
	{
		std::string source(filename);
		source += ".c";
		std::ofstream ofs(source);

		{
			ofs
			<< "\n#include \"base_includes.h\"\n"
      << "#include \"cw_message_ids.h\"\n"
      << "#include \"cw_symbol_ids.h\"\n"
			<< "#include \"cw_" << name << ".h\"\n"
      << "#define DEBUG_LOG 0\n"
      << "static const char* TAG = \"" << name << "\";\n"
      << "\n";
		}

    // prepare variables for symbols
    std::list<State*>::iterator iter = states.begin();
    while (iter != states.end()) {
      const State *s = *iter++;
      symbols[s->getName()] = PredicateSymbolDetails(s->getName(), "state", s->getName());
    }
    for (unsigned int i=0; i<stable_states.size(); ++i) {
      std::set<PredicateSymbolDetails> state_symbols;
      const StableState &s = stable_states.at(i);
      s.condition.predicate->findSymbols(state_symbols, 0);
      std::set<PredicateSymbolDetails>::iterator iter = state_symbols.begin();
      while (iter != state_symbols.end()) {
        const PredicateSymbolDetails &psd = (*iter++);
        std::map<std::string, PredicateSymbolDetails>::iterator found = symbols.find(psd.name);
        if (found != symbols.end()) {
          const PredicateSymbolDetails &other = (*found).second;
          if (psd.type == "unknown" && other.type != "unknown") {
            PredicateSymbolDetails update(psd);
            update.type = other.type;
            symbols[psd.name] = update;
          }
          else if (other.type != psd.type) {
            int x = 1;
          }
        }
        else
          symbols[psd.name] = psd;
      }
    }
    // also look through actions
    std::multimap<Message, MachineCommandTemplate*>::const_iterator recv_iter = receives.begin();
    while (recv_iter != receives.end()) {
      const std::pair<Message, MachineCommandTemplate*> item = *recv_iter++;
      for (unsigned int i = 0; i<(item.second)->action_templates.size(); ++i) {
        ActionTemplate*at = (item.second)->action_templates.at(i);
        PredicateActionTemplate *pat = dynamic_cast<PredicateActionTemplate*>(at);
        if (pat) {
          std::set<PredicateSymbolDetails> pat_symbols;
          pat->predicate->findSymbols(pat_symbols, 0);
          std::set<PredicateSymbolDetails>::iterator iter = pat_symbols.begin();
          while (iter != pat_symbols.end()) {
            const PredicateSymbolDetails &psd = (*iter++);
            std::map<std::string, PredicateSymbolDetails>::iterator found = symbols.find(psd.name);
            if (found != symbols.end()) {
              const PredicateSymbolDetails &other = (*found).second;
              if (psd.type == "unknown" && other.type != "unknown") {
                PredicateSymbolDetails update(psd);
                update.type = other.type;
                symbols[psd.name] = update;
              }
              else if (other.type != psd.type) {
                int x = 1;
              }
            }
            else
              symbols[psd.name] = psd;
          }
        }
      }
    }


    std::string lookup_vars(std::string("\tstruct cw_") + name + " *m;\n");
    std::string lookup_vars_init;
    std::set<std::string>used_states;
    std::string backup_vars;
    {
      {
        std::map<std::string, PredicateSymbolDetails>::iterator iter = symbols.begin();
        while (iter != symbols.end()) {
          const PredicateSymbolDetails &psd = (*iter).second;
          if (psd.type != "ignored") {
            std::string type(psd.type);
            if (type == "unknown") {
              if (psd.name.find('.') != std::string::npos) {
                type = "remote";
              }
              else if (this->findState(psd.name.c_str()))
                type = "state";
              else if (this->options.find(psd.name) != this->options.end()) {
                type = "property";
              }
              else {
                for (int i=0; i<parameters.size(); ++i) {
                  if (parameters[i].val.kind == Value::t_symbol && parameters[i].val.sValue == psd.name) {
                    type = "machine";
                  }
                }
                if (type == "unknown")
                  int x = 1;
              }
            }
            if (type == "unknown") {
              std::cout << "Unknown type for " << psd.name << " in " << name << "; assuming state\n";
              type = "state";
              used_states.insert(psd.name);
              symbols[psd.name] = PredicateSymbolDetails(psd.name, type, psd.export_name);
            }
            lookup_vars_init += generate_name_lookup(psd.export_name, psd.name, type);
            lookup_vars += generate_name_lookup_decls(psd.export_name, psd.name, type);
            backup_vars += generate_name_backups(psd.export_name, psd.name, type);
          }
          iter++;
        }
      }
    }

    // export statenumbers
    {
      std::list<State*>::iterator iter = states.begin();
      while (iter != states.end()) {
        const State *s = *iter++;
        used_states.insert(s->getName());
      }
      std::set<std::string>::iterator us_iter = used_states.begin();
      while (us_iter != used_states.end()) {
        const std::string &s = *us_iter++;
        ofs << "#define state_cw_" << s << " " << ExportState::lookup(s) << "\n";
      }
    }

    ofs << "struct cw_" << name << "_Vars {\n" << lookup_vars << "};\n";
    std::string vars(lookup_vars);
    subst_all(vars, '*', ' ');
    ofs << "struct cw_" << name << "_Vars_backup {\n" << vars << "};\n";
    ofs << "static void init_Vars(struct cw_" << name << " *m, struct cw_" << name << "_Vars *v) {\n"
    << "\tv->m = m;\n"
    << lookup_vars_init << "}\n";

    ofs << "static void backup_Vars(struct cw_" << name << " *m) {\n"
    << "\tstruct cw_" << name << "_Vars *v = m->vars;\n"
    << "\tstruct cw_" << name << "_Vars_backup *b = m->backup;\n"
    << backup_vars << "}\n";

    // generate the lookup function
    {
      ofs << "Value *cw_" << name << "_lookup(struct cw_" << name <<" *m, int symbol) {\n"
      << lookup.str() << "\treturn 0;\n}\n";
      ofs << "MachineBase *cw_" << name << "_lookup_machine(struct cw_" << name <<" *m, int symbol) {\n"
      << lookup_machine.str() << "\treturn 0;\n}\n";
    }
    ofs << "void cw_" << name << "_describe(struct cw_" << name << " *m);\n";
    ofs << "int cw_" << name << "_handle_message(struct MachineBase *ramp, struct MachineBase *machine, int state);\n";
    ofs << "int cw_" << name << "_check_state(struct cw_" << name << " *m);\n";
    if (timer_clauses.size()) {
      ofs << "uint64_t cw_" << name << "_next_trigger_time(struct cw_" << name << " *m, struct cw_" << name << "_Vars *v);\n";
    }

		ofs
		<< "struct cw_" << name << " *create_cw_" << name << "(const char *name";
		if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
			ofs << ", int pin";
		}
		ofs << params.str() << ") {\n"
		<< "\tstruct cw_" << name << " *p = (struct cw_" << name << " *)malloc(sizeof(struct cw_" << name << "));\n"
		<< "\tInit_cw_" << name << "(p, name";
		if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
			ofs << ", pin";
		}

		ofs << values.str() << ");\n"
		<< "\treturn p;\n"
		<< "}\n";


    // export enter functions for states
    exportHandlers(ofs);

    // generate a function to find the earliest time that the next timer might trigger
    // TODO: update to take into account the source timer being tested.
    if (timer_clauses.size()) {
      ofs
      << "uint64_t cw_" << name << "_next_trigger_time(struct cw_" << name << " *m, struct cw_" << name << "_Vars *v) {\n"
      << "\tint64_t res = 1000000000;\n"
      << "\tint64_t val = 0;\n"
      << "\t//TODO: remove possible duplicates here\n";
      std::list<Predicate*>::const_iterator iter = timer_clauses.begin();
      while (iter != timer_clauses.end()) {
        Predicate *pp = *iter++;
        Predicate *l = pp->left_p;
        Predicate *r = pp->right_p;
        // identify which side of the expression is the value and which is the source timer being tested
        Predicate *sub_p = (l && (l->entry.kind == Value::t_symbol ||  stringEndsWith(l->entry.sValue,".TIMER"))) ? r : l;
        Predicate *source = (sub_p == r) ? l : r;
        Value clause_time;
        std::string source_name = source->entry.sValue;
        std::map<std::string, PredicateSymbolDetails>::iterator found = ExportState::all_symbol_names().find(source_name);
        if (found != ExportState::all_symbol_names().end()) {
          const PredicateSymbolDetails &psd = (*found).second;
          ofs << "\tval = "; sub_p->toC(ofs);
          ofs << " - *v->l_" << psd.export_name << ";\n";
        }
        else {
          ofs << "\tval = "; sub_p->toC(ofs);
          ofs << " - m->machine.TIMER;\n";
        }
        ofs << "\tif (val>0 && val < res) res = val;\n";
      }
      ofs << "\tif (res == 1000000000) res = 0;\n";
      ofs << "\treturn res;\n}\n";
    }


    handlers << "\tMachineActions_add(cw_" << name << "_To_MachineBase(m), (enter_func)cw_"
      << name << "_" << initial_state.getName() << "_enter);\n";

		ofs
		<< "void Init_cw_" << name << "(struct cw_" << name << " *m, const char *name";
		if (name == "ANALOGINPUT" || name == "ANALOGOUTPUT") {
			ofs << ", int pin";
		}
		ofs << params.str() << ") {\n"
		<< "\tinitMachineBase(&m->machine, name);\n"
		<< "\tinit_io_address(&m->addr, 0, 0, 0, 0, iot_none, IO_STABLE);\n"
		<< setup.str();
		SymbolTableConstIterator pnames_iter = properties.begin();
		while (pnames_iter != properties.end()) {
			const std::pair<std::string, Value> prop = *pnames_iter++;
			ofs << "\tm->" << prop.first << " = " << prop.second << ";\n";
		}
		std::map<std::string, Value>::iterator opts_iter = options.begin();
		while (opts_iter != options.end()) {
			const std::pair<std::string, Value> opt = *opts_iter++;
			ofs<< "\tm->" << opt.first << " = " << opt.second << ";\n";
		}
		ofs
		<< "\tm->machine.state = state_cw_" << initial_state << ";\n"
    << "\tm->machine.check_state = ( int(*)(MachineBase*) )cw_" << name << "_check_state;\n"
    << "\tm->machine.handle = (message_func)cw_" << name << "_handle_message; // handle message from other machines\n"
    << "\tm->machine.lookup = (lookup_func)cw_" << name << "_lookup; // lookup symbols within this machine\n"
    << "\tm->machine.lookup_machine = (lookup_machine_func)cw_" << name << "_lookup_machine; // lookup symbols within this machine\n"
    << "\tm->machine.describe = (describe_func)cw_" << name << "_describe;\n"
    << "\tm->vars = (struct cw_" << name << "_Vars *)malloc(sizeof(struct cw_" << name << "_Vars));\n"
    << "\tm->backup = (struct cw_" << name << "_Vars_backup *)malloc(sizeof(struct cw_" << name << "_Vars_backup));\n"
    << "\tinit_Vars(m, m->vars);\n"
    << handlers.str()
		<< "\tmarkPending(&m->machine);\n"
		<< "}\n";

		ofs
		<< "struct IOAddress *cw_" << name << "_getAddress(struct cw_" << name << " *p) {\n"
		<< "\treturn (p->addr.io_type == iot_none) ? 0 : &p->addr;\n"
		<< "}\n";

		ofs
		<< "MachineBase *cw_" << name << "_To_MachineBase(struct cw_" << name << " *p) { return &p->machine; }\n\n";

		//exportCommands(ofs);

		ofs
		<< "int cw_" << name << "_check_state(struct cw_" << name << " *m) {\n"
    << "\tstruct cw_" << name << "_Vars *v = m->vars;\n"
    << "\tint res = 0;\n\tint new_state = 0; enter_func new_state_enter = 0;\n"
    << "\tbackup_Vars(m);\n";

    std::stringstream when_clauses;
    when_clauses << "\tchar buf[200];\n";

		for (unsigned int i=0; i<stable_states.size(); ++i) {
      std::stringstream desc;
      std::stringstream desc_vars;
			const StableState &s = stable_states.at(i);
			if (i>0) ofs << "\telse\n";
      Value timer_val;
      if (s.condition.predicate->priority != 1) { // not the DEFAULT state
        s.condition.predicate->toCstring(desc, desc_vars);
        when_clauses << "\tsnprintf(buf, 200, \"" << s.state_name << " [%d]: "
          << desc.str() << "\"" << ",state_cw_" << s.state_name << desc_vars.str() << ");\n";
        when_clauses << "\tsendMQTT(0, \"/response\", buf);\n";
        ofs << "\tif (";
        s.condition.predicate->toC(ofs);
        ofs << ") /* " << s.state_name << " */ {\n";
      }
      else {
        ofs << "\t{\n"; // no condition on the default case
      }
      ofs << "\t\tnew_state = state_cw_" << s.state_name << ";\n";
      std::string enter_msg_name(s.state_name);
      enter_msg_name += "_enter";
      if (receives.find(Message(enter_msg_name.c_str())) != receives.end())
        ofs << "\t\tnew_state_enter = (enter_func)cw_" << name << "_" << enter_msg_name << ";\n";
			ofs << "\t}\n";
		}

    ofs << "\tif (new_state && new_state != m->machine.state) {\n"
    << "\t\tchangeMachineState(cw_" << name << "_To_MachineBase(m), new_state, new_state_enter); // TODO: fix me\n"
    << "\t\tmarkPending(&m->machine);\n"
    << "\t\tres = 1;\n"
    << "\t}\n";

    if (timer_clauses.size()) {
      ofs
      << "\tuint64_t delay = cw_" << name << "_next_trigger_time(m, v);\n"
      << "\tif (delay > 0) {\n"
      << "\t\tstruct RTScheduler *scheduler = RTScheduler_get();\n"
      << "\t\twhile (!scheduler) {\n"
      << "\t\t\ttaskYIELD();\n"
      << "\t\t\tscheduler = RTScheduler_get();\n"
      << "\t\t}\n"
      << "\t\tRTScheduler_add(scheduler, ScheduleItem_create(delay, &m->machine));\n"
      << "\t\tRTScheduler_release();\n"
      << "\t}\n";
    }
    ofs << "\treturn res;\n}\n";

    ofs
    << "void cw_" << name << "_describe(struct cw_" << name << " *m) {\n"
    << "\tstruct cw_" << name << "_Vars_backup *v = m->backup;\n"
    << "{\n\tchar buf[100];\n"
    << "\tsnprintf(buf, 100, \"%s: %s  Class: " << name << "\", m->machine.name, name_from_id(m->machine.state));\n"
    << "\tsendMQTT(0, \"/response\", buf);\n"
    << "\tsnprintf(buf, 100, \"Timer: %ld\", m->machine.TIMER);\n"
    << "\tsendMQTT(0,\"/response\", buf);\n"
    << "}\n"
    << when_clauses.str()
    << "}\n";

	}

	return false;
}

