# Clockwork/Latproc AI Assistant Instructions

## Project Overview
Clockwork (Latproc) is a finite state machine-based DSL for industrial process control with EtherCAT hardware integration. The system consists of multiple daemons communicating via ZeroMQ.

## Architecture Components

### Core Daemons
- **`iod`** - Main I/O daemon that interfaces with EtherCAT hardware and runs Clockwork programs
- **`iosh`** - Interactive shell for monitoring/debugging Clockwork programs 
- **`modbusd`** - Modbus/TCP bridge to I/O daemon
- **`persistd`** - State persistence daemon
- **`cw`** - Standalone Clockwork interpreter (no hardware interface)
- **`device_connector`** - Connects external devices to Clockwork via ZMQ

### Key Source Directories
- `iod/src/` - Core interpreter implementation (`MachineInstance`, `Action` classes, ZMQ messaging)
- `iod/src/cwlang.lpp/.ypp` - Clockwork language lexer/parser (Flex/Bison)
- `plugins/` - C plugin system with embedded C code in `.cw` files
- `modbus/` - Modbus interface implementation
- `tests/` - Comprehensive test suite with `.cw` examples
- `iod/tests/` - C/C++ unit tests and test drivers

## Build System Patterns

### Standard Build Commands
```bash
cd iod && make debug          # Debug build with CMake
cd iod && make release        # Release build
cd iod && make test           # Run unit test suite
```

### CMake Configuration
- Uses `LocalCMakeLists.txt` for local overrides
- EtherCAT support auto-detected via `../ethercat/` directory
- Install targets create binaries in `iod/stage/`

## Clockwork Language Patterns

### Machine Definitions
```clockwork
button Button;
controller ButtonController button;

Button MACHINE {
    on STATE;
    off INITIAL;
}

ButtonController MACHINE button {
    OPTION key ""; # declare and initialise a machine option/property
    on WHEN button IS on;
    off DEFAULT;
    COMMAND press { button := on; }
    ENTER INIT { # INIT is the default starting state unless overridden by: state INITIAL;
        LOG "ButtonController initialized";
    }
}
```

### State Transitions
- `state WHEN condition` - declares a state with automatic transition based on condition, 
     conditions are checked in sequence until one matches
- `state DEFAULT` - Default state automatic transition if no WHEN conditions match
- `state INITIAL` - Initial state on machine start (only one per machine)
- `state STATE` - declares a state without setting any automatic transitions
- `state DURING command { actions }` - Temporary state that executes actions then reevaluates WHEN conditions
- `ENTER/LEAVE state { actions }` - State entry/exit actions
- `COMMAND name { actions }` - Define commands callable externally
- `COMMAND name WITHIN state { actions }` - Command scoped to a specific state
- `TIMER` - Built-in variable for time spent in current state
- `WAITFOR condition` - Synchronous wait for condition
- `LOG "message"` - Logging
- `CALL machine.command` - Call command on another machine and wait for completion
- `SEND command TO machine` - Asynchronous command call (correct pattern)
- `SEND command TO list_variable` - Broadcast command to all machines in list
- `RECEIVE command { actions }` - Define how machine responds to received commands
- `INCLUDE item IN list` / `CLEAR list` - Dynamic list manipulation
- `SET machine TO state` - Force state change
- `DISABLE machine` / `ENABLE machine` - Stop/start machine execution
- `ANY list ARE state` - Test if any items in list are in the given state
- Use `+` to concatenate strings
- Use backticks `` ` `` for regex patterns
- States and machines use the pattern `name TYPE parameters;` for declaration/instantiation.
- Options/properties are declared with `OPTION name initial_value;`
- Commands are named after the keyword `COMMAND` and must include the action block `{ }`.
- WHEN predicates automatically update when referenced variables change but only in the
    current machine, locally instantiated machines and machines passed as parameters
    or referenced via the GLOBAL keyword.
- A common pattern is to use `SELF` to refer to the current machine in WHEN conditions to trigger
    temporary state changes based on internal variables.
- **FLAG variables** are better than complex state machines for simple on/off logic. FLAGs are also machines
- **LIST variables** can hold multiple machine references for broadcast operations. Lists are also machines.
- **WHEN conditions** are continuously evaluated - machines automatically transition when conditions become true
- **Command vs State**: Use `COMMAND name { }` for external calls, `state DURING command { }` for temporary actions
- **RECEIVE vs COMMAND**: `RECEIVE` defines response to messages, `COMMAND` defines callable operations

### Key example clockwork files and directories
- `tests/assign.cw` - demonstrates JSON integration
- `tests/unit/` - basic language feature tests and clockwork test drivers
- `tests/lists.cw` - list manipulation examples
- `tests/cycling_counter.cw` - clean example of state-driven counter with WHEN conditions
- `tests/channel_stress_test.cw` - multi-daemon communication and message broadcasting
- `tests/cw2cw/` - complete inter-daemon machine shadowing example with INTERFACE and UPDATES
- `tests/stdchannels.cw` - standard channel definitions for inter-daemon communication

### Communication Patterns
- **Channels** - ZMQ-based inter-daemon communication (see `Channel.cpp`)
- **MQTT** - Pub/sub messaging (`MQTTInterface.h`)
- **Modbus** - Industrial protocol support

### Inter-Daemon Communication (Machine Shadowing)
```clockwork
# Define interface that will be shared between daemons
FlasherInterface INTERFACE {
    inactive INITIAL;
    on STATE;
    off STATE;
}

# Channel configuration with UPDATES for bidirectional shadowing
Link CHANNEL {
    OPTION PORT 9000;
    UPDATES machine_a FlasherInterface;
    UPDATES machine_b FlasherInterface;
}

# Daemon A: Creates channel instance with connection details
link Link(host: "127.0.0.1", port: 5555);
machine_a FlasherMachine;

# Daemon B: Simple machine definition - will be shadowed in A
machine_b FlasherMachine;
```

**Key Inter-Daemon Patterns:**
- **INTERFACE** - Defines the state contract for machine shadowing across daemons
- **UPDATES machine Interface** - Creates bidirectional shadow where local commands on shadows are sent to owner
- **Shadow machines** - Remote machines appear locally and can be controlled as if local
- **Authority system** - Each daemon has authority over its own machines, shadows relay commands
- **Channel connection states**: `DISCONNECTED` → `WAITSTART` → `UPLOADING` → `DOWNLOADING` → `CONNECTED` → `ACTIVE`
- **Automatic shadow synchronization** - State changes propagate automatically between daemons
- **Command routing** - Commands sent to shadows are automatically routed to owning daemon

## Development Workflow

### C/C++ Testing
- Unit tests in `iod/tests/` using Google Test framework
- Build and run with `make test`
- Mock hardware interfaces for isolated testing

### Plugin Development
- C plugins embed in `.cw` files between `%BEGIN_PLUGIN`/`%END_PLUGIN`
- Build with `plugins/build.sh` or individual compilation
- Use `Plugin.h` API: `getIntValue()`, `changeState()`, `setInstanceData()`

### clockwork language Testing Patterns
- Test machines implement `TestDriver` interface with states: `ok`, `error`, `waiting`, `idle`
- Use `WAITFOR` for synchronous testing
- Run tests: `cw run_tests.cw test1.cw test2.cw`
- Only one instance of `iod` can be running on a system due to EtherCAT constraints

### Debugging
- Use `iosh` shell to inspect running machines: `LIST`, `DESCRIBE machine_name`
- Enable debug: `cw -c debug_config_file`
- ZMQ monitoring: `zmq_monitor` tool

## Hardware Integration

### EtherCAT Setup
- Requires IgH EtherCAT Master installation in `../ethercat/`
- XML configuration parsing for hardware modules
- Real-time I/O through process data objects (PDOs)
- Automatic discovery of modules and mapping to Clockwork points

### I/O Point Binding
```clockwork
EK1814 MODULE (position=0);
EL2008 MODULE (position=1);
input_01 POINT EK1814, 0, 0;  # Module position, bit offset
output_02 POINT EL2008, 1, 3;
```

## Code Organization Principles
- `MachineInstance` - Core state machine runtime
- `ProcessingThread` - Main execution loop for I/O daemon
- `Action` subclasses - All Clockwork commands/operations  
- `Value` class for multi-type data handling
- `Channel` class to provide a channel abstraction to connect separate iod/cw daemons
- Thread-safe messaging via `MessagingInterface`
- Boost threading with careful mutex usage
- Plugin system allows custom C logic in Clockwork programs

## Common Gotchas
- ZMQ socket management requires careful lifecycle handling
- EtherCAT real-time constraints affect threading
- Parser generates `cwlang.tab.cpp` - don't edit directly
- CMake regenerates build files - use `LocalCMakeLists.txt` for local changes
- **Memory Management**: Channel filters must properly free input buffers; many filters use `delete[] buf` pattern
- **Multi-daemon Testing**: Channel connections under load can reveal memory leaks not visible in single-daemon tests

## Memory Leak Debugging
- Use Valgrind for heap analysis: `valgrind --tool=memcheck --leak-check=full ./iod`
- Channel-related code (`Channel.cpp`) requires careful buffer management
- Message filters must free input buffers before setting `*buf = nullptr`
- ZMQ message buffers need explicit cleanup in high-throughput scenarios
- **Multi-daemon Testing**: Use `tests/channel_stress_test.cw` and `scripts/test_channel_memory_leaks.sh` for channel leak detection
- **Load Testing**: Channel memory leaks manifest under high-frequency state changes between daemons