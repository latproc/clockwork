# Dependency graph export (`-g FILE`)

**Applies to:** the programs that export a graph — `cw`, `iod`, `iod_sdo`,
`iod-elc` — whichever of them this git line builds (see `BRANCHES.md`).
**Status:** one Graphviz DOT file, written by the loader.

```
cw  -g plant.dot -t                  plant.cw
cw  -g plant.dot -r Controller -t    plant.cw
iod -g plant.dot -t                  plant.cw
```

`-g FILE` writes a Graphviz `digraph` to `FILE`. `-r NAME` restricts it to the
connected component containing the instance `NAME`; without `-r` every loaded
instance is written.

Add `-t` (test only) to get a static artifact: the graph is written and the
program then exits instead of starting a plant. Without `-t` the graph is still
written, but the program carries on into its runtime. `-t` also writes the
modbus mapping file into the current directory. (This line has no `--parse-only`;
where it exists, `--parse-only -g FILE` writes the graph with a full load and no
runtime at all.)

The graph is one artifact. Everything below is expressed as node and edge
attributes inside that file rather than as a second file, so `dot` still renders
it and nothing downstream has to keep two artifacts in sync.

## Output shape

```dot
digraph G {
  node [shape=box];
  "ctrl" [label="ctrl", name="ctrl", class="Controller", state="idle",
          enabled="false", file="plant.cw", line="24"];
  "ctrl.watch" [label="watch", name="watch", class="FLAG", ...];
  "class:Controller" [label="class Controller", style=dashed, shape=box];
  "class:Controller::opt:GateDelay" [shape=note, label="GateDelay = 40"];
  "prop:gate_two::TravelTime" [shape=note, label="TravelTime = 40"];
  "class:Controller::rules" [shape=point, label=""];
  "class:Controller::state:opening" [shape=ellipse, label="opening"];
  ...
}
```

| Written | Meaning |
|---|---|
| instance node | one loaded `MachineInstance` |
| `class:NAME` | a `MACHINE` class, reached by an `instance_of` edge |
| `class:NAME::opt:OPT` | a class `OPTION`, with its value |
| `prop:INSTANCE::PROP` | a property this instance carries that differs from the class default |
| `class:NAME::rules` | the class rule list; its outgoing edges are the `WHEN` rules |
| `class:NAME::state:S` | a declared state; carries `initial="true"` / `default="true"` |

| Edge label | Direction | Meaning |
|---|---|---|
| `parameter` | parameter machine → user | positional parameter binding |
| `depends` | instance → instance | dependency the interpreter tracks |
| `owner` | instance → owner | lexical owner (a class local's parent) |
| `instance_of` | instance → class | class the instance was made from |
| `option` | option → class | class-level constant |
| `property` | property → instance | per-instance value |

Rule edges carry `rule` (the evaluation index), `kind` (`when` or `transition`),
and for `WHEN` rules `enter` / `leave` (whether that state has an `ENTER` or
`LEAVE` action). `TRANSITION` edges also carry `trigger` and `guard`.

## Reading it

Evaluation order is the `rule` attribute. `WHEN` rules are listed in the order
the interpreter tests them — `semantic_analysis()` moves the `DEFAULT` rule to
the end, and the export reports the result, not the source order. Rule indices
continue across `WHEN` rules and `TRANSITION` statements, so a `TRANSITION` after
three `WHEN` rules is `rule="3"`.

`enter` / `leave` are what make a rule's *effect* visible rather than just its
existence: a rule that moves a machine into a state with no `ENTER` action does
nothing else, and that is a property of the state, not of the rule.

`initial` and `default` are separate declarations and are **not** the same thing.
`X INITIAL` sets the state the machine starts in; `X DEFAULT` is the rule that
applies when no other rule matches. A state can be either, both or neither, so
the two are written as independent flags on the state node and both are
reported:

```dot
"class:Gate::state:stopped"        [shape=ellipse, label="stopped", initial="true"];
"class:Gate::state:parked"         [shape=ellipse, label="parked",  default="true"];
"class:FLAG::state:off"            [shape=ellipse, label="off", initial="true", default="true"];
```

Every declared state gets a node, so a state that only an `INITIAL` or `DEFAULT`
declaration names is present rather than missing. That includes the built-in
`INIT` state every class carries, which appears unflagged where the class
declares its own `INITIAL`.

An instance property node appears only where the class declares the property and
the instance holds a different value. Those are the values the instance was
constructed with. Class `OPTION`s are class-level constants and are written once
against the class instead, and generated properties no class declares (such as
`NAME`) are not written at all.

Node identity is the full name, including the owner chain. A local declared in a
class body is instantiated once per owner, so two of them can share a short name;
using the short name alone would merge them into a single node. `label` and the
`name` attribute stay the short name, which is unchanged for top-level
instances.

## Limits

This is a structural and configuration view of what the loader built. It does
**not** model:

- control flow *inside* an action block, or which action writes a property last;
- whether a guard is true — guards are reported as text, so an edge is a
  possible transition, not one that will fire;
- `%BEGIN_PLUGIN` C bodies, which are opaque to the loader and can hold their own
  state machines and short-circuits;
- runtime state: the graph is written before the runtime starts, so `state` is
  the initial value and `enabled` is `false`.

Questions of the first two kinds need the program's semantics, not its bindings:
control-flow and control-dependence analysis, path conditions, or a model checker
over the machine states.

## History

The export used to write bare instance names with unlabelled edges: `cw` wrote
parameter bindings, `iod` wrote `depends`, both in opposite directions, with no
way to tell the two apart, and neither carried order, guards, options or
per-instance values. `-r` walked parameter bindings downwards only (and `iod`
ignored it entirely), so a root could silently omit the machines it depended on.
The graph was also only written on the way into a runtime, so getting a static
artifact meant adding `-t` to exit again.

The default node shape is now `box` rather than `record`: guard text contains
`|` and `||`, which a `record` shape treats as field separators.
