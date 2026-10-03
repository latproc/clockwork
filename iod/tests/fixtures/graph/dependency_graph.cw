# Fixture for the dependency graph export (cw / iod / iod_sdo / iod-elc -g FILE).
#
# The shapes here are deliberately generic. Between them they cover the detail
# the graph has to keep so that rule order, guards and configuration stay
# answerable from the export:
#
#   * class OPTIONs                  -> an option node per class option
#   * a class value overridden at    -> a property node on that instance only,
#     construction                      not on the instance that keeps the default
#   * parameter binding              -> a parameter edge (and a depends edge)
#   * WHEN rules in evaluation       -> rule index, guard text, and the
#     order, one with ENTER + LEAVE     enter / leave flags
#     and one with neither
#   * a TRANSITION with REQUIRES     -> a guarded transition edge

Gate MACHINE (TravelTime: 150) {
    OPTION TravelTime 150;
    demand FLAG;
    stopped INITIAL;
    open WHEN demand IS on;
    closed WHEN demand IS off;
    parked DEFAULT;
    ENTER open { LOG "gate open"; }
    LEAVE open { LOG "gate leaving open"; }
}

GateController MACHINE gate {
    OPTION GateDelay 40;
    OPTION RampRate 100;
    idle INITIAL;
    opening WHEN gate IS open;
    closing WHEN gate IS closed;
    fault WHEN gate IS stopped;
    ENTER opening { LOG "opening"; }
    TRANSITION opening TO fault REQUIRES gate IS stopped;
}

gate_one Gate;
gate_two Gate(TravelTime:40);
ctrl GateController gate_one;
