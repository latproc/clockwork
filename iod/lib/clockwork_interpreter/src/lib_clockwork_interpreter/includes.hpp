#include <iostream>
#include <lib_clockwork_client/includes.hpp>

#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "AbortAction.h"
#include "Action.h"
#include "ActionList.h"
#include "arraystr.h"
#include "AutoStats.h"
#include "buffering.h"
#include "CallMethodAction.h"
#include "Channel.h"
#include "ClearListAction.h"
#include "ClientInterface.h"
#include "clockwork.h"
#include "ConditionHandler.h"
#include "ControlSystemMachine.h"
#include "CopyPropertiesAction.h"
#include "CounterRateFilterSettings.h"
#include "CounterRateInstance.h"
#include "cwlang.h"
#include "DisableAction.h"
#include "Dispatcher.h"
#include "domain.h"
#include "dynamic_value.h"
// #if USE_ETHERCAT
//     #include "ecat_thread.h"
//     #include "ECInterface.h"
// #elif EC_SIMULATOR
//     #include "ecat_thread.h"
//     #include "ECInterface.h"
// #endif
#include "EnableAction.h"
#include "EtherCATSetup.h"
#include "ExecuteMessageAction.h"
#include "ExportState.h"
#include "ExpressionAction.h"
#include "Expression.h"
#include "filtering.h"
#include "FireTriggerAction.h"
#include "HandleMessageAction.h"
#include "HandleRequestAction.h"
#include "hw_config.h"
#include "IfCommandAction.h"
#include "IncludeAction.h"
#include "IOComponent.h"
#include "IODCommand.h"
#include "IODCommands.h"
#include "LockAction.h"
#include "LogAction.h"
#include "MachineClass.h"
#include "MachineCommandAction.h"
#include "MachineCommandList.h"
#include "MachineDetails.h"
#include "MachineInstance.h"
#include "MachineInterface.h"
#include "MachineShadowInstance.h"
#include "ModbusInterface.h"
#include "MQTTDCommand.h"
#include "MQTTDCommands.h"
#include "MQTTInterface.h"
#include "options.h"
#include "Parameter.h"
#include "PatternAction.h"
#include "PersistentStore.h"
#include "Plugin.h"
#include "PolledMessageHandler.h"
#include "PopListAction.h"
#include "PredicateAction.h"
#include "ProcessingThread.h"
#include "RateEstimatorInstance.h"
#include "ResumeAction.h"
#include "Scheduler.h"
#include "SDOEntry.h"
#include "SendMessageAction.h"
#include "SetListEntriesAction.h"
#include "SetOperationAction.h"
#include "SetStateAction.h"
#include "SharedWorkSet.h"
#include "ShutdownAction.h"
#include "SortListAction.h"
#include "StableState.h"
#include "State.h"
#include "Statistic.h"
#include "Statistics.h"
#include "SyncRemoteStatesAction.h"
#include "Transition.h"
#include "UnlockAction.h"
#include "WaitAction.h"
