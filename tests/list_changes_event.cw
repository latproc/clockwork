# list_changes_event.cw
#
# Covers two LIST event rules used by a supervisor that watches a list:
#
#  1. `CHANGES OF <list>` changes for EVERY membership edit, including a
#     remove+add in one pass that leaves the size unchanged. The BySize watcher
#     is the old shape and proves the difference by never noticing the swap.
#
#  2. `X LOCAL STATE;` marks a transient state local: the machine still enters
#     it, but no `X_enter` message is sent to dependents and no state change is
#     published. The PlainProbe control proves the receive path works.
#
# Asserted by CMake with separate bounded runs (run_cw_runtime.sh accepts one
# --must-contain per invocation):
#   list_changes_same_size_swap  must-contain "BYCHANGES token=B"
#                                must-not-contain "BYSIZE token=B"
#   list_changes_local_state     must-contain "OBSERVED plain loud_enter"
#                                must-not-contain "LEAK local quiet_enter"

A FLAG(name: "A");
B FLAG(name: "B");
POOL LIST;

# --- 1. membership token vs size --------------------------------------------

ByChanges MACHINE {
	GLOBAL POOL;
	LOCAL OPTION last "<init>";
	idle DEFAULT;
	changed WHEN last != CHANGES OF POOL;
	ENTER changed {
		last := CHANGES OF POOL;
		LOG "BYCHANGES token=" + (CHANGES OF POOL);
	}
}
bychanges ByChanges;

# The old signature: notices the first add, but the same-size swap is invisible.
BySize MACHINE {
	GLOBAL POOL;
	LOCAL OPTION last -1;
	idle DEFAULT;
	changed WHEN last != SIZE OF POOL;
	ENTER changed {
		last := SIZE OF POOL;
		LOG "BYSIZE token=" + (CHANGES OF POOL);
	}
}
bysize BySize;

SwapDriver MACHINE {
	GLOBAL A, B;
	OPTION n 0;
	LOCAL OPTION taken 0;
	ticking WHEN SELF IS wait AND TIMER >= 300;
	wait DEFAULT;
	ENTER ticking {
		n := n + 1;
		IF (n == 1) { ADD A AFTER LAST OF POOL; }
		IF (n == 2) {
			taken := TAKE ITEM 0 FROM POOL;   # A leaves ...
			ADD B AFTER LAST OF POOL;         # ... B arrives: size still 1
			LOG "SWAP A->B";
		}
	}
}
swap_driver SwapDriver;

# --- 2. LOCAL STATE is not broadcast ----------------------------------------

PULSE FLAG;

# quiet is transient and local: entering it must not reach dependents.
LocalProbe MACHINE {
	GLOBAL PULSE;
	quiet LOCAL STATE;
	quiet_idle DEFAULT;
	quiet WHEN PULSE IS on;
	ENTER quiet { LOG "LOCAL entered quiet"; }
}
lp LocalProbe;

# loud is the same shape but not local, as a control.
PlainProbe MACHINE {
	GLOBAL PULSE;
	loud STATE;
	loud_idle DEFAULT;
	loud WHEN PULSE IS on;
	ENTER loud { LOG "PLAIN entered loud"; }
}
pp PlainProbe;

ObsLocal MACHINE target {
	RECEIVE target.quiet_enter { LOG "LEAK local quiet_enter"; }
}
obsl ObsLocal lp;

ObsPlain MACHINE target {
	RECEIVE target.loud_enter { LOG "OBSERVED plain loud_enter"; }
}
obsp ObsPlain pp;

PulseDriver MACHINE {
	GLOBAL PULSE;
	OPTION n 0;
	ticking WHEN SELF IS wait AND TIMER >= 300;
	wait DEFAULT;
	ENTER ticking {
		n := n + 1;
		IF (n == 1) { SET PULSE TO on; }
	}
}
pulse_driver PulseDriver;
