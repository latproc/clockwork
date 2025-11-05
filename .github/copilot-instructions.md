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
- `new_state DURING command { actions }` - Command definition with an associated state change
- `ENTER/LEAVE state { actions }` - State entry/exit actions
- `COMMAND name { actions }` - Define commands callable externally
- `COMMAND name WITHIN state { actions }` - Command scoped to a specific state
- `TIMER` - Built-in variable for time spent in current state
- `WAITFOR condition` - Synchronous wait for condition
- `LOG "message"` - Logging
- `CALL machine.command` - Call command on another machine and wait for completion
- `SEND machine.command` - Asynchronous command call
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

### Key example clockwork files and directories
- `tests/assign.cw` - demonstrates JSON integration
- `tests/unit` - basic language feature tests and clockwork test drivers
- `tests/lists.cw` - list manipulation examples
- `tests/stdchannels.cw` - standard channel definitions for inter-daemon communication

### Communication Patterns
- **Channels** - ZMQ-based inter-daemon communication (see `Channel.cpp`)
- **MQTT** - Pub/sub messaging (`MQTTInterface.h`)
- **Modbus** - Industrial protocol support

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
- **Multi-daemon Testing**: Use `tests/channel_stress_test.cw` and `test_channel_memory_leaks.sh` for channel leak detection
- **Load Testing**: Channel memory leaks manifest under high-frequency state changes between daemons