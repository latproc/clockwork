#include <iostream>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include <inttypes.h>
#include <list>
#include <string>
#include <unistd.h>
#include <sys/param.h>

#include <boost/utility.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/dir.h>
#endif

#include "MachineInstance.h"
#include "clockwork.h"
#include "PersistentStore.h"

#include "Channel.h"
#include "startup.h"

#ifndef EC_SIMULATOR
#include "ECInterface.h"
#ifdef USE_SDO
#include "SDOEntry.h"
#endif //USE_SDO
#endif

void predefine_special_machines() {
  char host_name[100];
  int err = gethostname(host_name, 100);
  if (err == -1) strcpy(host_name, "localhost");
  MachineClass *settings_class = new MachineClass("SYSTEMSETTINGS");
  settings_class->addState("ready");
  settings_class->default_state = State("ready");
  settings_class->initial_state = State("ready");
  settings_class->disableAutomaticStateChanges();
  settings_class->properties.add("INFO", "Clockwork host", SymbolTable::ST_REPLACE);
  settings_class->properties.add("HOST", host_name, SymbolTable::ST_REPLACE);
  settings_class->properties.add("VERSION", "0.9", SymbolTable::ST_REPLACE);
  settings_class->properties.add("CYCLE_DELAY", 2000, SymbolTable::ST_REPLACE);
  settings_class->properties.add("POLLING_DELAY", 2000, SymbolTable::ST_REPLACE);

  MachineClass *cw_class = new MachineClass("CLOCKWORK");
  cw_class->addState("ready");
  cw_class->default_state = State("ready");
  cw_class->initial_state = State("ready");
  cw_class->disableAutomaticStateChanges();
  cw_class->properties.add("PROTOCOL", "CLOCKWORK", SymbolTable::ST_REPLACE);
  cw_class->properties.add("HOST", "localhost", SymbolTable::ST_REPLACE);
  cw_class->properties.add("PORT", 5600, SymbolTable::ST_REPLACE);

  MachineClass *point_class = new MachineClass("POINT");
  point_class->parameters.push_back(Parameter("module"));
  point_class->parameters.push_back(Parameter("offset"));
  point_class->addState("on");
  point_class->addState("off");
  point_class->default_state = State("off");
  point_class->initial_state = State("off");
  point_class->disableAutomaticStateChanges();

  point_class = new MachineClass("STATUS_FLAG");
  point_class->parameters.push_back(Parameter("module"));
  point_class->parameters.push_back(Parameter("offset"));
  point_class->parameters.push_back(Parameter("entry"));
  point_class->addState("on");
  point_class->addState("off");
  point_class->default_state = State("off");
  point_class->initial_state = State("off");
  point_class->disableAutomaticStateChanges();

  MachineClass *ain_class = new MachineClass("ANALOGINPUT");
  ain_class->parameters.push_back(Parameter("module"));
  ain_class->parameters.push_back(Parameter("offset"));
  ain_class->parameters.push_back(Parameter("filter_settings"));
  ain_class->addState("stable");
  ain_class->addState("unstable");
  ain_class->default_state = State("stable");
  ain_class->initial_state = State("stable");
  ain_class->disableAutomaticStateChanges();
  ain_class->properties.add("IOTIME", Value(0), SymbolTable::ST_REPLACE);
  ain_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);
  ain_class->properties.add("Position", Value(0), SymbolTable::ST_REPLACE);
  ain_class->properties.add("Velocity", Value(0), SymbolTable::ST_REPLACE);

	MachineClass *din_class = new MachineClass("DIGITALINPUT");
	din_class->parameters.push_back(Parameter("module"));
	din_class->parameters.push_back(Parameter("offset"));
	din_class->addState("stable");
	din_class->addState("unstable");
	din_class->default_state = State("stable");
	din_class->initial_state = State("stable");
	din_class->disableAutomaticStateChanges();
	din_class->properties.add("IOTIME", Value(0), SymbolTable::ST_REPLACE);
	din_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);

  MachineClass *cnt_class = new MachineClass("COUNTER");
  cnt_class->parameters.push_back(Parameter("module"));
  cnt_class->parameters.push_back(Parameter("offset"));
  cnt_class->addState("stable");
  cnt_class->addState("unstable");
  cnt_class->addState("off");
  cnt_class->default_state = State("off");
  cnt_class->initial_state = State("off");
  cnt_class->disableAutomaticStateChanges();
  cnt_class->properties.add("IOTIME", Value(0), SymbolTable::ST_REPLACE);
  cnt_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);
  cnt_class->properties.add("Position", Value(0), SymbolTable::ST_REPLACE);
  cnt_class->properties.add("Velocity", Value(0), SymbolTable::ST_REPLACE);

  MachineClass *re_class = new MachineClass("RATEESTIMATOR");
  re_class->parameters.push_back(Parameter("position_input"));
  re_class->parameters.push_back(Parameter("settings"));
  re_class->addState("stable");
  re_class->addState("unstable");
  re_class->addState("off");
  re_class->default_state = State("off");
  re_class->initial_state = State("off");
  re_class->disableAutomaticStateChanges();
  re_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);
  re_class->properties.add("position", Value(0), SymbolTable::ST_REPLACE);

  MachineClass *cr_class = new MachineClass("COUNTERRATE");
  cr_class->parameters.push_back(Parameter("position_output"));
  cr_class->parameters.push_back(Parameter("module"));
  cr_class->parameters.push_back(Parameter("offset"));
  cr_class->addState("stable");
  cr_class->addState("unstable");
  cr_class->addState("off");
  cr_class->default_state = State("off");
  cr_class->initial_state = State("off");
  cr_class->disableAutomaticStateChanges();
  cr_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);
  cr_class->properties.add("position", Value(0), SymbolTable::ST_REPLACE);

  MachineClass *aout_class = new MachineClass("ANALOGOUTPUT");
  aout_class->parameters.push_back(Parameter("module"));
  aout_class->parameters.push_back(Parameter("offset"));
  aout_class->addState("stable");
  aout_class->addState("unstable");
  aout_class->default_state = State("stable");
  aout_class->initial_state = State("stable");
  aout_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);

	MachineClass *dout_class = new MachineClass("DIGITALOUTPUT");
	dout_class->parameters.push_back(Parameter("module"));
	dout_class->parameters.push_back(Parameter("offset"));
	dout_class->addState("stable");
	dout_class->addState("unstable");
	dout_class->default_state = State("stable");
	dout_class->initial_state = State("stable");
	dout_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);

  MachineClass *list_class = new MachineClass("LIST");
  list_class->addState("empty");
  list_class->addState("nonempty");
  list_class->default_state = State("empty");
  list_class->initial_state = State("empty");
  //list_class->disableAutomaticStateChanges();
  list_class->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);

  MachineClass *ref_class = new MachineClass("REFERENCE");
  ref_class->addState("ASSIGNED");
  ref_class->addState("EMPTY");
  ref_class->default_state = State("EMPTY");
  ref_class->initial_state = State("EMPTY");
  //ref_class->disableAutomaticStateChanges();

  MachineClass *module_class = new MachineClass("MODULE");
  module_class->disableAutomaticStateChanges();
  module_class->addState("PREOP");
  module_class->addState("BOOT");
  module_class->addState("SAFEOP");
  module_class->addState("OP");
#ifdef EC_SIMULATOR
  module_class->transitions.push_back(Transition(State("INIT"), State("OP"), Message("turnOn")));
  module_class->transitions.push_back(Transition(State("INIT"), State("PREOP"), Message("powerUp")));
  module_class->transitions.push_back(Transition(State("PREOP"), State("OP"), Message("turnOn")));
  module_class->transitions.push_back(Transition(State("OP"), State("PREOP"), Message("turnOff")));
#endif

  MachineClass *publisher_class = new MachineClass("MQTTPUBLISHER");
  publisher_class->parameters.push_back(Parameter("broker"));
  publisher_class->parameters.push_back(Parameter("topic"));
  publisher_class->parameters.push_back(Parameter("message"));

  MachineClass *subscriber_class = new MachineClass("MQTTSUBSCRIBER");
  subscriber_class->parameters.push_back(Parameter("broker"));
  subscriber_class->parameters.push_back(Parameter("topic"));
  subscriber_class->options["message"] = 0; // Value("", Value::t_string); //TODO: exported code cannot handle string messages

  MachineClass *broker_class = new MachineClass("MQTTBROKER");
  broker_class->parameters.push_back(Parameter("host"));
  broker_class->parameters.push_back(Parameter("port"));
  broker_class->disableAutomaticStateChanges();

  MachineClass *cond = new MachineClass("CONDITION");
  cond->addState("true");
  cond->addState("false");
  cond->default_state = State("false");

  MachineClass *flag = new MachineClass("FLAG");
  flag->addState("on");
  flag->addState("off");
  flag->default_state = State("off");
  flag->initial_state = State("off");
  flag->disableAutomaticStateChanges();
  flag->transitions.push_back(Transition(State("off"), State("on"), Message("turnOn")));
  flag->transitions.push_back(Transition(State("on"), State("off"), Message("turnOff")));

  MachineClass *mc_variable = new MachineClass("VARIABLE");
  mc_variable->addState("ready");
  mc_variable->initial_state = State("ready");
  mc_variable->disableAutomaticStateChanges();
  mc_variable->parameters.push_back(Parameter("VAL_PARAM1"));
  mc_variable->options["VALUE"] = "VAL_PARAM1";

  MachineClass *mc_constant = new MachineClass("CONSTANT");
  mc_constant->addState("ready");
  mc_constant->initial_state = State("ready");
  mc_constant->disableAutomaticStateChanges();
  mc_constant->parameters.push_back(Parameter("VAL_PARAM1"));
  mc_constant->options["VALUE"] = "VAL_PARAM1";

#ifndef EC_SIMULATOR
  MachineClass *mcwc = new MachineClass("ETHERCAT_WORKINGCOUNTER");
  mcwc->addState("ZERO");
  mcwc->addState("INCOMPLETE");
  mcwc->addState("COMPLETE");
  mcwc->initial_state = State("ZERO");
  mcwc->disableAutomaticStateChanges();
  mcwc->properties.add("VALUE", Value(0), SymbolTable::ST_REPLACE);

  MachineClass *mcls = new MachineClass("ETHERCAT_LINKSTATUS");
  mcls->addState("DOWN");
  mcls->addState("UP");
  mcls->initial_state = State("DOWN");
  mcls->disableAutomaticStateChanges();

  MachineClass *mcec = new MachineClass("ETHERCAT_BUS");
  {
    State *init = mcec->findMutableState("INIT");
    if (!init) {
      init = new State("INIT");
      mcec->states.push_back(init);
    }
    init->setEnterFunction(ECInterface::setup);
  }
  mcec->addState("DISCONNECTED");
  mcec->addState("CONNECTED");
  mcec->addState("CONFIG");
  mcec->addState("ACTIVE");
  mcec->addState("ERROR");
  mcec->initial_state = State("INIT");
  mcec->properties.add("tolerance", Value(10), SymbolTable::ST_REPLACE);
  mcec->disableAutomaticStateChanges();
  MachineCommandTemplate *mc = new MachineCommandTemplate("activate", "");
  mcec->receives.insert(std::make_pair(Message("activate"), mc));
  mc = new MachineCommandTemplate("deactivate", "");
  mcec->receives.insert(std::make_pair(Message("deactivate"), mc));
  //	mcec->transitions.push_back(Transition(State("CONFIG"), State("ACTIVE"), Message("activate")));
  //	mcec->transitions.push_back(Transition(State("ACTIVE"), State("CONFIG"), Message("deactivate")));

  MachineInstance *miwc = MachineInstanceFactory::create("ETHERCAT_WC", "ETHERCAT_WORKINGCOUNTER");
  miwc->setProperties(mcwc->properties);
  miwc->setStateMachine(mcwc);
  miwc->setDefinitionLocation("Internal", 0);
  machines["ETHERCAT_WC"] = miwc;

  MachineInstance *mils = MachineInstanceFactory::create("ETHERCAT_LS", "ETHERCAT_LINKSTATUS");
  mils->setProperties(mcls->properties);
  mils->setStateMachine(mcls);
  mils->setDefinitionLocation("Internal", 0);
  machines["ETHERCAT_LS"] = mils;

  MachineInstance *miec = MachineInstanceFactory::create("ETHERCAT", "ETHERCAT_BUS");
  miec->setProperties(mcec->properties);
  miec->setStateMachine(mcec);
  miec->addLocal("counter", miwc);
  miec->addLocal("link", mils);
  miec->setDefinitionLocation("Internal", 0);
  machines["ETHERCAT"] = miec;
#endif

#ifdef USE_SDO
  MachineClass *mc_sdo = new MachineClass("SDOENTRY");
  mc_sdo->addState("ready");
  mc_sdo->initial_state = State("ready");
  mc_sdo->disableAutomaticStateChanges();
  mc_sdo->parameters.push_back(Parameter("MODULE"));
  mc_sdo->parameters.push_back(Parameter("INDEX"));
  mc_sdo->parameters.push_back(Parameter("SUBINDEX"));
  mc_sdo->parameters.push_back(Parameter("SIZE"));
  mc_sdo->parameters.push_back(Parameter("OFFSET"));
  mc_sdo->options["VALUE"] = 0;
#endif //USE_SDO

  MachineClass *mc_external = new MachineClass("EXTERNAL");
  mc_external->options["HOST"] = "localhost";
  mc_external->options["PORT"] = 5600;
  mc_external->options["PROTOCOL"] = "ZMQ";

  MachineInstance*settings = MachineInstanceFactory::create("SYSTEM", "SYSTEMSETTINGS");
  machines["SYSTEM"] = settings;
  settings->setProperties(settings_class->properties);
  settings->setStateMachine(settings_class);
  settings->setDefinitionLocation("Internal", 0);

  MachineInstance *channels = MachineInstanceFactory::create("CHANNELS", "LIST");
  machines["CHANNELS"] = channels;
  channels->setProperties(list_class->properties);
  channels->setStateMachine(list_class);

  ClockworkInterpreter::instance()->setup(settings);
  settings->setValue("NAME", Value(device_name(), Value::t_string));
}
