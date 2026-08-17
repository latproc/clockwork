# COMMAND / RECEIVE WITHIN lists, WHEN / REQUIRES guards, fallback, and
# first-match order.  Each step sets a sentinel, invokes the handler, then
# WAITFORs the expected result.  A wrong result (including a handler that
# should not have fired) stalls the script; the driver then times out in error.
#
# Run with the unit suite:
#   cw run_tests.cw arith.cw bitset.cw anyon.cw prop.lpc test_set_prop.cw command_guards.cw
# or this file plus the harness:
#   cw command_guards.cw command_guards_run.cw
#
# Parse-time failures (duplicates) are in command_guards_dup_*.cw and are
# checked with:  cw -t command_guards_dup_within.cw

CommandGuardsSubject MACHINE {
  OPTION result "none";
  OPTION counter 0;

  a INITIAL;
  b STATE;
  c STATE;

  # Distinct bodies for the same command, one state each.
  COMMAND split WITHIN a { result := "split_a"; }
  COMMAND split WITHIN b { result := "split_b"; }

  # One handler, list of states. No fallback — ignored in c.
  COMMAND listed WITHIN a, b { result := "listed"; }

  # Dispatch-time predicate. No fallback.
  COMMAND gated WHEN counter > 0 { result := "when"; }
  COMMAND req REQUIRES counter > 0 { result := "requires"; }

  # Combined WITHIN + WHEN. No fallback.
  COMMAND both WITHIN a WHEN counter > 0 { result := "both"; }

  # List, then WHEN, then unrestricted fallback.
  COMMAND ping WITHIN a, b { result := "ping_list"; }
  COMMAND ping WITHIN c WHEN counter > 0 { result := "ping_when"; }
  COMMAND ping { result := "ping_fallback"; }

  # First declared match wins (WITHIN before WHEN).
  COMMAND overlap WITHIN a { result := "overlap_within"; }
  COMMAND overlap WHEN counter > 0 { result := "overlap_when"; }

  # First declared match wins (WHEN before WITHIN).
  COMMAND overlap2 WHEN counter > 0 { result := "overlap2_when"; }
  COMMAND overlap2 WITHIN a { result := "overlap2_within"; }

  # Strict handlers used only for "must not fire" cases.
  COMMAND only_a WITHIN a { result := "only_a"; }
  COMMAND only_when WHEN counter > 0 { result := "only_when"; }
  COMMAND only_both WITHIN a WHEN counter > 0 { result := "only_both"; }

  RECEIVE tick WITHIN a, b { result := "recv_list"; }
  RECEIVE tick REQUIRES counter > 0 { result := "recv_req"; }
  RECEIVE tick { result := "recv_fallback"; }

  RECEIVE only_tick WITHIN a { result := "recv_only_a"; }
  RECEIVE gated_tick WHEN counter > 0 { result := "recv_gated"; }

  COMMAND go_a { SET SELF TO a; }
  COMMAND go_b { SET SELF TO b; }
  COMMAND go_c { SET SELF TO c; }
  COMMAND bump { INC counter; }
  COMMAND zero { counter := 0; }
  COMMAND clear { result := "none"; }
}

cg_subject CommandGuardsSubject;

CommandGuardsScript MACHINE test {
  OPTION step 0;

  ok WHEN SELF IS ok || SELF IS working;
  idle DEFAULT;

  working DURING run {
    # --- split WITHIN a / WITHIN b ---
    INC step;
    CALL go_a ON test; CALL clear ON test;
    CALL split ON test;
    WAITFOR test.result == "split_a";

    INC step;
    CALL go_b ON test; CALL clear ON test;
    CALL split ON test;
    WAITFOR test.result == "split_b";

    # split has no handler in c — must not change result
    INC step;
    CALL go_c ON test; CALL clear ON test;
    CALL split ON test;
    WAITFOR test.result == "none";

    # --- WITHIN a, b list ---
    INC step;
    CALL go_a ON test; CALL clear ON test;
    CALL listed ON test;
    WAITFOR test.result == "listed";

    INC step;
    CALL go_b ON test; CALL clear ON test;
    CALL listed ON test;
    WAITFOR test.result == "listed";

    INC step;
    CALL go_c ON test; CALL clear ON test;
    CALL listed ON test;
    WAITFOR test.result == "none";

    # --- WHEN (false then true) ---
    INC step;
    CALL zero ON test; CALL go_a ON test; CALL clear ON test;
    CALL gated ON test;
    WAITFOR test.result == "none";

    INC step;
    CALL bump ON test; CALL clear ON test;
    CALL gated ON test;
    WAITFOR test.result == "when";

    # --- REQUIRES alias ---
    INC step;
    CALL zero ON test; CALL clear ON test;
    CALL req ON test;
    WAITFOR test.result == "none";

    INC step;
    CALL bump ON test; CALL clear ON test;
    CALL req ON test;
    WAITFOR test.result == "requires";

    # --- WITHIN + WHEN combined ---
    INC step;
    CALL zero ON test; CALL go_a ON test; CALL clear ON test;
    CALL both ON test;
    WAITFOR test.result == "none";

    INC step;
    CALL bump ON test; CALL clear ON test;
    CALL both ON test;
    WAITFOR test.result == "both";

    INC step;
    CALL go_b ON test; CALL clear ON test;
    CALL both ON test;
    WAITFOR test.result == "none";

    # --- ping: list / when / fallback ---
    INC step;
    CALL zero ON test; CALL go_a ON test; CALL clear ON test;
    CALL ping ON test;
    WAITFOR test.result == "ping_list";

    INC step;
    CALL go_b ON test; CALL clear ON test;
    CALL ping ON test;
    WAITFOR test.result == "ping_list";

    INC step;
    CALL go_c ON test; CALL clear ON test;
    CALL ping ON test;
    WAITFOR test.result == "ping_fallback";

    INC step;
    CALL bump ON test; CALL clear ON test;
    CALL ping ON test;
    WAITFOR test.result == "ping_when";

    # --- first match wins ---
    INC step;
    CALL zero ON test; CALL bump ON test; CALL go_a ON test; CALL clear ON test;
    CALL overlap ON test;
    WAITFOR test.result == "overlap_within";

    INC step;
    CALL go_c ON test; CALL clear ON test;
    CALL overlap ON test;
    WAITFOR test.result == "overlap_when";

    INC step;
    CALL go_a ON test; CALL clear ON test;
    CALL overlap2 ON test;
    WAITFOR test.result == "overlap2_when";

    # --- strict only_a / only_when / only_both must stay silent ---
    INC step;
    CALL zero ON test; CALL go_b ON test; CALL clear ON test;
    CALL only_a ON test;
    WAITFOR test.result == "none";

    INC step;
    CALL go_a ON test; CALL clear ON test;
    CALL only_a ON test;
    WAITFOR test.result == "only_a";

    INC step;
    CALL zero ON test; CALL clear ON test;
    CALL only_when ON test;
    WAITFOR test.result == "none";

    INC step;
    CALL bump ON test; CALL clear ON test;
    CALL only_when ON test;
    WAITFOR test.result == "only_when";

    INC step;
    CALL zero ON test; CALL go_a ON test; CALL clear ON test;
    CALL only_both ON test;
    WAITFOR test.result == "none";

    INC step;
    CALL bump ON test; CALL go_c ON test; CALL clear ON test;
    CALL only_both ON test;
    WAITFOR test.result == "none";

    INC step;
    CALL go_a ON test; CALL clear ON test;
    CALL only_both ON test;
    WAITFOR test.result == "only_both";

    # --- RECEIVE list / REQUIRES / fallback ---
    INC step;
    CALL zero ON test; CALL go_a ON test; CALL clear ON test;
    SEND tick TO test;
    WAITFOR test.result == "recv_list";

    INC step;
    CALL go_c ON test; CALL clear ON test;
    SEND tick TO test;
    WAITFOR test.result == "recv_fallback";

    INC step;
    CALL bump ON test; CALL clear ON test;
    SEND tick TO test;
    WAITFOR test.result == "recv_req";

    # --- RECEIVE must not fire ---
    INC step;
    CALL zero ON test; CALL go_c ON test; CALL clear ON test;
    SEND only_tick TO test;
    WAIT 30;
    WAITFOR test.result == "none";

    INC step;
    CALL go_a ON test; CALL clear ON test;
    SEND only_tick TO test;
    WAITFOR test.result == "recv_only_a";

    INC step;
    CALL zero ON test; CALL clear ON test;
    SEND gated_tick TO test;
    WAIT 30;
    WAITFOR test.result == "none";

    INC step;
    CALL bump ON test; CALL clear ON test;
    SEND gated_tick TO test;
    WAITFOR test.result == "recv_gated";

    LOG "PASSED command guards";
    SET SELF TO ok;
  }
}

Test_CommandGuardsDriver MACHINE test {
  OPTION execution_timeout 15000;
  script CommandGuardsScript test;
  error WHEN SELF IS error || SELF IS waiting && TIMER > execution_timeout;
  ok WHEN script IS ok;
  waiting WHEN script IS working;
  idle DEFAULT;

  COMMAND run { SEND run TO script; WAITFOR script IS working }
  COMMAND abort { DISABLE script; ENABLE script; }
  ENTER error { LOG "command_guards error at step: " + script.step; CALL abort ON SELF }
}

test_command_guards_driver Test_CommandGuardsDriver cg_subject;
