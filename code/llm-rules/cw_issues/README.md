# Clockwork Issue Handoffs

This directory contains portable investigation handoffs for Clockwork/IOD
issues that need to be reproduced away from an operational machine.

## Required use

Before using an issue handoff, read the parent project instructions in the
normal order:

1. `../LLM_CONTEXT.md`
2. `../CW_RULES.md`
3. `../PROJECT_CODING_RULES.md`
4. `../TOOLS.md`
5. the applicable machine-class notes
6. the issue files listed below

An issue handoff records evidence and a proposed test method. It is not
authorization to edit, build, deploy, restart, or profile an operational
machine.

## Open issues

### EtherCAT cyclic scheduling and fleet rollout

Read both files completely:

- `IOD_ETHERCAT_CYCLIC_WATCHDOG_20260721.md`
- `IOD_ETHERCAT_FLEET_PREFLIGHT_AND_ROLLOUT.md`

### EtherCAT SDO scheduling

- `IOD_SDO_SCHEDULER_DESIGN.md`
  - Read-before-default startup semantics, opt-in polling, write confirmation,
    cross-thread queue ownership, coalescing, timeout recovery and tests for
    legacy or newer Clockwork/IOD implementations.

### IOD-SDO EL5152 sync-manager watchdog

- `IOD_SDO_EL5152_SM_WATCHDOG_20260810.md`
  - 1G2C-121 legacy iod-sdo incident: sparse EL5152 command RxPDO must retain
    its explicit ESI watchdog mode. Includes the lifecycle restriction that
    module configuration must not read `MachineInstance` properties.

The first file records the 2G4C-120 incident and tested correction. The second
is the mandatory read-only compatibility survey and controlled rollout
procedure for other machines, including older Linux and non-PREEMPT_RT
controllers.

For the 4C09/Focal hardware rollout and the shared `machine` SVN artifacts,
also read:

- `../2G4C/RT_ETHERCAT_IOD_COMMISSIONING_4C115_20260724.md`
  - Records the `2G4C-115` kernel/EtherLab/IOD installation, revision 20004
    launcher and NIC-tuning files, host-local versus shared boundaries,
    local-only `LocalCMakeLists.txt`, rollback, and sister-machine checklist.

### PREEMPT_RT kernel package builds

- `BUILD_2G4C_4_19_PREEMPT_RT_DEBS.md`
  - Ubuntu 18.04-era 2G4C controllers using the complete
    `4.19.254-0419254-lowlatency` target config.
- `BUILD_4C09_FOCAL_4_19_PREEMPT_RT_DEBS.md`
  - Separate Ubuntu 20.04 Focal/4C09 build using the complete
    `4.19.248-0419248-lowlatency` target config and `-4c09-rt` package identity.

Do not interchange packages between these procedures merely because both use
Linux `4.19.255` with PREEMPT_RT `rt113`. The compiled target config and
commissioning evidence are part of the artifact identity.

### IOD WEBREQUEST activity-dependent memory growth

Read both files completely:

- `IOD_WEBREQUEST_MEMORY_GROWTH_20260721.md`
- `IOD_WEBREQUEST_REPRODUCTION_PLAYBOOK.md`

Also: `/opt/latproc/iod/MEMORY_LEAK_INVESTIGATION.md` and Track F in
`/opt/latproc/iod/OPEN_WORK_PLAN.md`.

The first handoff is the incident record plus **2026-07-28/29 long-run
findings** (overnight flat after ITEM DEFAULT fix; daytime live cjson to
~1.8M). The playbook is for **offline** Linux/macOS reproduction (no plant
required for remaining WEBREQUEST / Result-retention work).

### Processing load / idle storms (startup overload, sticky under load)

- `IOD_PROCESSING_LOAD_AND_IDLE_STORMS_20260725.md`
  - Stacked CW/IO causes (LIST cascade, TIMER AND, plugins, analog work queue,
    wait-loop pacing, bus vs poll, accidental PROCSNAP). Cherry-pick hashes,
    live checklist, and STALLSNAP (`12d65404` on line A, 2026-08-13).
    Still to port to elc / the commandclock work branch.

### TIMER soft clocks vs COMMANDCLOCK (control cadence design)

- `IOD_TIMER_SOFT_CLOCKS_AND_COMMANDCLOCK_20260812.md`
  - Why TIMER dwell is fragile as a PID soft clock; thread model (no RT CW
    thread); silent COUNTER + `calcAdjust` dependency; Option 1 load-safe
    overdue recovery (`TimerOverduePolicy`); elc COMMANDCLOCK vs TIMER delay
    issue; STALLSNAP vs processing path; design ladder. Cross-links plant
    `../2G4C/PIDLISTCLOCK_TIMER_STALL_20260805.md` and the idle-storms handoff.

### COMMANDCLOCK stall hardening (post-migration residuals)

- `IOD_COMMANDCLOCK_STALL_HARDENING_20260812.md`
  - After elc COMMANDCLOCK migration: `hasPending` mute, motion deadline
    (on-thread vs off-thread), STALLSNAP, SYSTEMEXEC image scripts
    (`camera_capture.sh` / `image_weight.sh`), WEBREQUEST residual apply path.
  - Work branch: `feature/commandclock-stall-hardening` from
    `feature/iod-elc-kernel-transport`.
  - **On that work branch (not this 2G4C-120 WC):** item 1 `fcbe65f4`;
    `cw --mqtt` `67230602`; OP CoE reapply skip `934c9c63`. Confirm those
    on 1G2C / iod-elc. STALLSNAP is on **this** line A (`12d65404`) and
    still to port there.

### 1G2C-122 mains-return iod wedge (other class — do not verify here)

- `../1G2C/IOD_POWER_RETURN_REAPPLY_WEDGE_20260811.md`
  - 2026-08-11: worker must not `setValue` / publish into CW.
  - 2026-08-13 git record (`934c9c63`): apply recipe status on processing,
    snapshot recipes, skip CoE unless AL is PREOP/SAFEOP. Sources are not
    in this `iod_sdo` tree.

### IO notify + COMMANDCLOCK (elc-path design)

- `../IO_NOTIFY_COMMANDCLOCK_DESIGN.md` (lives under `llm-rules/`, not
  `code/Docs` — plant Docs is not the CW/iod issues store)
  - Silent VALUE/IOTIME, `notify_period` + change-only dependant commands,
    `COMMANDCLOCK` without List bags. **Runtime is iod-elc /
    `feature/iod-elc-kernel-transport` only**; see also `../CW_RULES.md`
    (EtherCAT ANALOGINPUT / COUNTER section).
  - For TIMER vs COMMANDCLOCK architecture notes, also read
    `IOD_TIMER_SOFT_CLOCKS_AND_COMMANDCLOCK_20260812.md`.

### iod-elc multi-domain + slave identity (open work)

- `IOD_ELC_OPEN_WORK_20260726.md`
  - Multi-domain stages. **Stage 4 (servo control power-off) proven elc + CW
    2026-07-27** — domain 2 down, domain 1 unaffected; CW `ECDomain_2`
    tracks INCOMPLETE after status-mirror fix.
  - Slave identity risk (e.g. EL5152 different `RevisionNo` / product code):
    auto-match from bus scan + ESI/XML, with explicit overrides for
    non-compliant hardware. **Fail-closed startup**: mapping errors without
    a clear override must abort before cycle activate / output arm.
  - Coordinate runtime with kernel-module work.
