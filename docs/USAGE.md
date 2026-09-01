# Clockwork Usage Guide

How to use the recently added features. Part 1 covers the language additions
(timeouts and error recovery); Part 2 covers the native database support
(RECORD, `dbd`, and `datastore`). Every example is a complete, self-contained
snippet you can drop into a program.

Design authority: `docs/timeout-spec.md` (timeouts) and `docs/RECORD_DB.md`
(database). This guide is only the how-to.

---

# Part 1 — Timeouts and error recovery

A **timeout** is a distinct outcome, not an error and not an abort. A **failure**
is a separate outcome handled by `ON ERROR`. The two never cross: `ON TIMEOUT`
does not run on a failure, and `ON ERROR` does not run on a timeout.

## 1. Statement timeout

Give a blocking statement a deadline. When the deadline expires the `ON TIMEOUT`
block runs; if the statement finishes first, it runs normally and the block is
skipped.

```clockwork
COMMAND load {
    CALL find ON db
        WITH TIMEOUT 5000
        ON TIMEOUT { LOG "find timed out after " + TIMEOUT + "ms"; }

    WAITFOR cust IS clean
        WITH TIMEOUT 3000
        ON TIMEOUT { LOG "customer not clean in time"; }

    WAIT 1000
        WITH TIMEOUT 500
        ON TIMEOUT { LOG "wait cut short"; }
}
```

Supported statements: `CALL`, `WAITFOR`, `WAIT`, and `SET`. `<duration>` is an
integer number of milliseconds — a literal or a variable:

```clockwork
OPTION timeout 5000;
COMMAND load {
    CALL find ON db WITH TIMEOUT timeout ON TIMEOUT { LOG "timed out"; }
}
```

A synchronous statement (for example a plain `SET`) completes before it can
suspend, so its deadline has no observable effect — but the clause is legal:

```clockwork
SET x TO 1 WITH TIMEOUT 300 ON TIMEOUT { LOG "never runs"; }
```

## 2. `TRY` — a timed block

`TRY` runs a block under a deadline and gives it an `ON TIMEOUT` and/or
`ON ERROR` recovery block.

```clockwork
COMMAND start {
    TRY {
        CALL load;
        CALL setup;
        CALL run;
    } WITH TIMEOUT 10000 ON TIMEOUT {
        LOG "start timed out";
        SHUTDOWN;
    }
}
```

## 3. Handler deadlines

A `COMMAND`, `ENTER`, `LEAVE`, or `RECEIVE` handler can carry its own deadline
and/or recovery blocks. Any of `WITH TIMEOUT`, `ON TIMEOUT`, and `ON ERROR` is
optional and independent.

A handler deadline bounds the whole handler:

```clockwork
ENTER INIT {
    CALL load;
    CALL start;
} WITH TIMEOUT 3000 ON TIMEOUT { LOG "init timed out"; }
```

A handler can also catch a timeout or failure raised by a statement *inside* it,
without having its own deadline:

```clockwork
COMMAND run {
    WAITFOR flag IS on WITH TIMEOUT 2000;   # unhandled timeout here
} ON TIMEOUT { LOG "run timed out after " + TIMEOUT + "ms"; }
```

## 4. `AFTER` shorthand

For a handler, `AFTER <duration> { ... }` is exactly `WITH TIMEOUT <duration>
ON TIMEOUT { ... }`. It runs the block only if the deadline expires — never on
normal completion.

```clockwork
ENTER INIT {
    CALL load;
    CALL start;
} AFTER 5000 {
    LOG "failed to start";
    SHUTDOWN;
}
```

## 5. The `TIMEOUT` value

Inside an `ON TIMEOUT` block, `TIMEOUT` is the read-only configured budget in
milliseconds (not the actual elapsed time).

```clockwork
WAITFOR never == 1 WITH TIMEOUT 500 ON TIMEOUT {
    LOG "gave up after " + TIMEOUT + "ms";
}
```

## 6. `ON ERROR` — handling failures

`ON ERROR` handles a **failure** (for example a `CALL` to a machine that does not
exist). It runs instead of `ON TIMEOUT`:

```clockwork
COMMAND load {
    TRY {
        CALL find ON db;
    } WITH TIMEOUT 5000 ON TIMEOUT {
        LOG "find timed out";
    } ON ERROR {
        LOG "find failed";
    }
}
```

If an `ON TIMEOUT` block uses `ABORT` (or `RETURN` / `THROW`), the `ON ERROR`
block runs next, with the timeout context still available:

```clockwork
ENTER off {
    SET light TO off WITH TIMEOUT 10;
}
ON TIMEOUT {
    LOG "light timed out after " + TIMEOUT + "ms";
    ABORT;
}
ON ERROR {
    LOG "failed to turn light off";
}
```

## 7. Nested deadlines

An inner timeout's recovery block runs under the still-active outer deadline;
the outer deadline can cancel a blocking inner recovery.

```clockwork
COMMAND run {
    TRY {
        TRY {
            WAITFOR never == 1;
        } WITH TIMEOUT 2000 ON TIMEOUT {
            WAITFOR never == 2;   # blocks; outer deadline cancels this
        }
    } WITH TIMEOUT 4000 ON TIMEOUT {
        LOG "outer timeout";
    }
}
```

## 8. Semantics worth knowing

- **Completion race.** A suspended body succeeds only if its completion time is
  strictly before the deadline. Completing at or after the deadline loses to the
  timeout.
- **Reply correlation.** A `CALL` that times out does not cancel the target's
  command; the target may finish later. Its late reply is discarded and cannot
  complete a later `CALL` to the same command.
- **Unhandled timeout.** A timeout with no `ON TIMEOUT` block propagates to the
  nearest enclosing timed scope, which routes it to its own `ON TIMEOUT` (not
  `ON ERROR`).

---

# Part 2 — Database systems (RECORD, `dbd`, `datastore`)

Clockwork talks to a JSON database over ZMQ; SQL stays outside the language.
`RECORD` is a restricted `MACHINE` whose OPTIONS are table columns. `dbd` is the
channel adapter; `datastore` (`dbsvr`) is the database server.

## 1. A `RECORD` class

A `RECORD` is a `MACHINE` with only OPTIONS (no `WHEN`/`COMMAND` logic). OPTIONS
are the columns of a table or view. `KEY` marks the single-column primary key.

```clockwork
Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION email "";
    OPTION age 0 NOT NULL;
    LOCAL OPTION tmp false;      # logic only, not a column
}

cust Customer;                    # a named instance (program-owned)
```

A named RECORD has three built-in **system states** (you do not write them —
they are set by the runtime):

| state | when |
| --- | --- |
| `empty` | just declared; nothing loaded |
| `dirty` | a program/HMI edit changed a column OPTION |
| `clean` | a row was applied (APPLY / `COPY PROPERTIES`) |

```clockwork
Watcher MACHINE cust {
    quiet WHEN cust IS empty;
    live  WHEN cust IS clean;
}
w Watcher cust;
```

Column flags: `KEY`, `UNIQUE`, `NOT NULL`, `PRIVATE` (a column that is stored but
not published), and `LOCAL OPTION` (not a column at all).

```clockwork
Account RECORD {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION password "" PRIVATE;   # stored, but hidden from iosh/sampler
}
```

A **view** is a `RECORD` over a SQL view (created by `cw-migrate`):

```clockwork
CustomerWithAddress RECORD VIEW "customer_with_address" {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION city "";
}
```

## 2. A `MACHINE` bound to a table

A full `MACHINE` (with `WHEN`/`COMMAND`/`EXPORT`) can bind a table (or view) with
`TABLE "name"` (or `VIEW "name"`) plus a `KEY`. It is then a row window exactly
like a `RECORD`, but with your own logic and states. `RECORD APPLY` projects onto
it; the row lifecycle lives in a `LOCAL OPTION`, because Clockwork `STATE` is
yours (`idle`/`active`), not the RECORD's `empty`/`dirty`/`clean`.

```clockwork
CustomerPanel MACHINE TABLE "customer" {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION age 0;                 # projection — email on the table is ignored
    LOCAL OPTION state "empty";   # row lifecycle; STATE is idle/active
    OPTION note "";               # extra OPTION: not a column unless the table has it

    EXPORT RW name, age;
    EXPORT STATES idle, active;

    active WHEN age > 0;
    idle DEFAULT;

    COMMAND clear { name := ""; age := 0; }
}

cust CustomerPanel (id: 1);
```

Notes:

- Any `MACHINE` with `TABLE`/`VIEW` + `OPTION … KEY` is a row. A `RECORD` class
  is only the **canonical schema** (for `cw-scaffold` and `cw-migrate`); it is
  not required for the bind. Two machines may bind the same `(type, key)` — two
  windows on one row.
- `RECORD APPLY` matches `(type, key)` and writes only the declared column
  OPTIONS (extra JSON fields like `email` are ignored). It does **not**
  `setState` on a MACHINE — your `WHEN` owns `STATE`; the row lifecycle is the
  `LOCAL OPTION state`.

### Fill — a bind does not read the database

Declaring `cust CustomerPanel (id: 1)` does **not** fetch the row. OPTIONS start
at class defaults and `state` is `empty`. A row arrives only by an explicit
`find`/`load`/`QUERY`, then `RECORD APPLY` (or `COPY PROPERTIES` from a LIST
member). Two patterns:

1. **KEY known in the program** — `cust CustomerPanel (id: 1)`; INTERFACE `find`
   with that KEY; `RECORD APPLY` writes `cust` and sets `state` to `clean`.

2. **Slot, KEY from a query** — `slot CustomerPanel;`. A loader MACHINE owns a
   static selector, hydrates, and binds onto the slot:

```clockwork
slot CustomerPanel;
occupancy LIST;

loader MACHINE {
    OPTION city "Perth";
    COMMAND bind {
        CLEAR occupancy;
        COPY ALL FROM Customer TO occupancy WHERE ITEM.city == city;
        IF (SIZE OF occupancy == 0) {
            SEND clear TO slot;                 # slot -> empty
        } ELSE {
            row := TAKE FIRST FROM occupancy;
            COPY PROPERTIES FROM row TO slot;   # slot -> clean
        }
    }
}
```

`QUERY` and INTERFACE `load` are SENDs; they do not wait for `dbsvr`. The loader
uses its own `WHEN`/`WAITFOR` for "hydrate done", then binds.

### Rules

- Do **not** `COPY PROPERTIES` row OPTIONS from one slot to another (prev →
  enter → exit): that is a second in-memory copy of the row. Two slots that show
  the same row both **bind** to it (same KEY / same APPLY). A real move updates
  a query column (`update`) and each loader `load`s and rebinds. Cycle-only
  OPTIONS (`LOCAL`, timers, maps) stay on the MACHINE.
- Composition (`Editor MACHINE cust`) still works: two instances; APPLY hits
  `cust`; `WHEN`/HMI sit on the editor.

## 3. Creating a database

A database is created and migrated by **tools**, not by a running program. The
RECORD classes are the model; `cw-scaffold` turns them into `CREATE TABLE` SQL;
`cw-migrate` versions and applies that SQL; `dbsvr` serves the resulting file.
Operational row writes are `insert` requests, never schema changes.

### Step 1 — define the model (RECORD classes)

Every bare `OPTION` on a RECORD is a column. `KEY` is the single-column primary
key; `UNIQUE` / `NOT NULL` add constraints; `PRIVATE` is a column that is stored
but not published; `LOCAL OPTION` and `PERSISTENT OPTION` are not columns.

```clockwork
Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION email "";
    OPTION age 0 NOT NULL;
    LOCAL OPTION tmp false;      # logic only, not a column
}
```

A Clockwork value maps to a sqlite type as follows (other Stores map their own
types):

| Clockwork default | sqlite column |
| --- | --- |
| `0` (integer) | `INTEGER` |
| `""` (string) | `TEXT` |
| `0.0` (float) | `REAL` |
| `true` / `false` | `INTEGER` (0/1) |
| NULL | `NULL` |

`KEY` becomes `PRIMARY KEY`; `NOT NULL` becomes `NOT NULL DEFAULT <default>`.
`UNIQUE` adds a `UNIQUE` constraint.

### Step 2 — generate `CREATE TABLE`

`cw-scaffold --sql` writes the table DDL from the RECORD class:

```
cw-scaffold --from customer.cw --out dir/ --sql
```

For the class above it emits (in `dir/expected_Customer.sql`):

```sql
CREATE TABLE customer (
  age INTEGER NOT NULL DEFAULT 0,
  email TEXT DEFAULT '',
  id INTEGER PRIMARY KEY,
  name TEXT DEFAULT ''
);
```

A `VIEW` class only gets a comment — its join SQL is written by hand and applied
through `cw-migrate` as a `CREATE VIEW` revision.

### Step 3 — version and apply (`cw-migrate`)

Schema lives in SQL revision files, not in the program. `cw-migrate` keeps a
`cw_revision` table and applies/rolls back revisions in order.

A revision file (`db/versions/0001_customer.sql`):

```sql
-- revision: 0001
-- down_revision: none
-- upgrade
CREATE TABLE customer (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL DEFAULT '',
  age INTEGER NOT NULL DEFAULT 0
);
-- downgrade
DROP TABLE customer;
```

Commands:

```
cw-migrate current --db clockwork.db                        # print the applied revision
cw-migrate generate --dir versions --sql 0002.sql           # wrap a SQL file as the next revision
cw-migrate upgrade --db clockwork.db --dir versions         # apply pending revisions
cw-migrate downgrade --db clockwork.db --dir versions --rev 1   # roll back to revision 1
```

Rules: adding a column is `ALTER TABLE … ADD COLUMN`; removing or renaming a
column is an explicit revision (never auto-applied by loading a program). `dbsvr`
does **not** auto-upgrade on start — the operator runs `cw-migrate upgrade`, and a
mismatch is a startup error.

### Step 4 — start `dbsvr`

`dbsvr` is the datastore server. It opens the database named in its config and
serves JSON over ZMQ:

```
dbsvr --config db.conf        # binds tcp://*:5554 (REP) + notify :5556 (PUB)
```

On connect it sets `journal_mode=WAL`, `synchronous=NORMAL`, `foreign_keys=ON`,
and `busy_timeout=5000`; each JSON request runs as one transaction. Never send
`BEGIN`/`COMMIT`/`ROLLBACK` yourself.

### Worked example

```console
$ cw-scaffold --from customer.cw --out db/ --sql     # CustomerINTERFACE.lpc + Customer.sql
$ cw-migrate generate --dir db/versions --sql db/Customer.sql   # -> 0001_customer.sql
$ cw-migrate upgrade --db clockwork.db --dir db/versions         # CREATE TABLE customer
$ dbsvr --config db.conf                             # serve the database
```

Now `customer` exists and a Clockwork program can `insert`, `find`, `update`, and
`delete` rows on it.

## 4. The query process

All database access is a JSON request sent to `DATABASE_CHANNEL`. `dbd` forwards
it to `dbsvr` (`tcp://127.0.0.1:5554`), `dbsvr` compiles it to SQL and runs it
against the `Store`, and the reply comes back the same way. SQL never appears in
a `.cw`/`.lpc` file.

### 4.1 Request format

A request is an object with an `action`, a `type` (the table or view), and
action-specific fields. `auth` is the token (placeholder `"xxx"`).

| action | purpose | extra fields |
| --- | --- | --- |
| `insert` | add one row | `data` |
| `find` | matching rows (by `keys`) | `keys`, `fields` |
| `select` | matching rows (rich filter) | `where`, `order`, `limit`, `fields` |
| `update` | update matching rows | `keys`, `data` |
| `delete` | delete matching rows | `keys` (omit to delete all) |
| `create` | **CREATE TABLE** (schema), not a row | `schema` |

`insert` is the operational "create a row"; `action: "create"` is DDL (and is
normally superseded by `cw-migrate`). `action: "sql"` is rejected.

```json
{ "action": "insert", "auth": "xxx", "type": "customer",
  "data": { "name": "Fred" } }

{ "action": "find", "auth": "xxx", "type": "customer",
  "keys": { "name": "Fred" }, "fields": ["age"] }

{ "action": "update", "auth": "xxx", "type": "customer",
  "keys": { "name": "Fred" }, "data": { "age": 20 } }

{ "action": "delete", "auth": "xxx", "type": "customer",
  "keys": { "name": "Bill" } }

{ "action": "select", "auth": "xxx", "type": "customer",
  "where": { "age": { "gt": 18 } }, "order": ["name"], "limit": 10 }
```

### 4.2 Selecting rows (`where`, `order`, `limit`, `fields`)

`select` (and `find`) support filtering and shaping:

- **Equality** — `"where": { "age": 18 }`.
- **Comparisons** — `{ "age": { "gt": 18 } }` with `eq`, `neq`, `gt`, `lt`,
  `ge`, `le`.
- **Set membership** — `{ "age": { "in": [18, 20, 22] } }`.
- **Pattern** — `{ "name": { "like": "%Fred%" } }` (bound, not interpolated).
- **Null** — `{ "age": null }`.
- **`order`** — `["name"]` ascending, `["-name"]` descending.
- **`limit`** — an integer cap.
- **`fields`** — the columns to return.

Joined shapes are named SQL views (created by `cw-migrate` as `CREATE VIEW`),
referenced with `type` / `VIEW "name"` — not ad-hoc joins in a request.

### 4.3 The round trip

```clockwork
Customer RECORD { OPTION id 0 KEY; OPTION name ""; }
all LIST;

ed MACHINE {
    OPTION q JSON_VALUE {
        "action": "select", "type": "customer", "auth": "xxx",
        "where": { "age": { "gt": 18 } }, "order": ["name"]
    };
    COMMAND refresh {
        QUERY q INTO all;        # SEND q to DATABASE_CHANNEL
    }
}
```

1. `QUERY q INTO all` SENDs the JSON property `q` to `DATABASE_CHANNEL`.
   (`QUERY { ... } INTO all` sends a literal object.)
2. `dbd` (subscribed as `DATABASE_CHANNEL`) forwards the payload to `dbsvr`.
3. `dbsvr` compiles it to SQL (`SQLInterface` → `Store`), runs it in one
   transaction, and returns a JSON reply.
4. `dbd` applies the reply: a row reply becomes `RECORD APPLY` (per-column
   OPTION writes on held RECORDs) or the legacy blob `respond_to` PROPERTY.
5. The program turns the reply JSON into a LIST and drains it:

```clockwork
all := reply AS LIST;             # a JSON array becomes a LIST
```

`AS LIST` is `CLEAR list` + `PUSH ITEMS FROM json TO list`, for any JSON array.

### 4.4 Responses

```json
{ "status": 0, "request": "{…}", "response": [ { "age": 21, "name": "Fred" } ] }
```

`status` is `0` (success), `1` (error), or `2` (unauthorized). `response` is the
result data (or an error message). `insert`/`update`/`delete` replies include the
affected rows (sqlite `RETURNING *`); `delete` includes the deleted rows. Column
values are typed JSON (numbers, strings, or null — not all strings).

## 5. Copying rows into a LIST or RECORD

`COPY ALL FROM <RecordClass> TO <list>` copies the RECORD instances currently
held in memory (the ones `RECORD APPLY` materialized):

```clockwork
CustomerINTERFACE MACHINE record, items {
    COMMAND list {
        CLEAR items;
        COPY ALL FROM Customer TO items;
    }
}
```

Filter while copying with `WHERE`:

```clockwork
COMMAND named_ann {
    CLEAR items;
    COPY ALL FROM Customer TO items WHERE ITEM.name == "Ann";
}
```

A query-result LIST is an ordinary LIST — the normal LIST commands work on it:

```clockwork
COMMAND newest {
    SORT items BY PROPERTY name;         # ascending
    SORT items BY PROPERTY name DESC;    # descending
    row := TAKE FIRST FROM items;        # take and remove the first member
    IF (SIZE OF items > 0) { LOG "more rows left"; }
}
```

`COPY PROPERTIES FROM <row> TO <record>` binds one row onto a **named** RECORD,
projecting only the declared columns (extra fields ignored) and leaving it
`clean`:

```clockwork
row := TAKE FIRST FROM items;
COPY PROPERTIES FROM row TO cust;        # cust gets id/name/age; cust is clean
```

Reactions go through a statically-declared RECORD that other machines already
depend on — `WHEN` fires when its state/OPTIONS change:

```clockwork
Watcher MACHINE cust {
    live    WHEN cust IS clean;
    changed WHEN cust IS dirty;
}
w Watcher cust;
```

## 6. `cw-scaffold` — generated INTERFACE

`cw-scaffold` generates a `<Class>INTERFACE MACHINE record, items` with
`create`/`update`/`delete`/`find`/`load`/`list` commands. `create` maps to a JSON
`insert` (a row insert, not a schema create); `list` is `COPY ALL FROM <Class>`;
`load` is a `find` with empty keys so `dbd` materializes the rows first.

```
cw-scaffold --from customer.cw --out dir/          # generate the INTERFACE
cw-scaffold --from customer.cw --out dir/ --sql    # + CREATE TABLE SQL
```

## 7. Two Clockworks, one datastore

Several Clockwork processes can share one `dbsvr`. After a commit, `dbsvr`
publishes the changed row on its notify socket, and **every** `dbd` that holds
that RECORD applies it onto its OPTIONS — so all Clockworks stay in sync with no
polling and no second `find`.

```
                ┌──────────────┐         ┌──────────────┐
                │   iod A      │         │   iod B      │
                └──────┬───────┘         └──────┬───────┘
                       │ (RECORD APPLY)         │ (RECORD APPLY)
                ┌──────┴───────┐         ┌──────┴───────┐
                │    dbd A     │         │    dbd B     │
                └──────┬───────┘         └──────┬───────┘
                       │                        │
                       └──────────┬─────────────┘
                                  │ (SUB on notify :5556)
                         ┌────────┴────────┐
                         │     dbsvr       │   PUB {action,type,keys,row}
                         └─────────────────┘
```

1. `iod A` edits a row and `insert`s/`update`s it (via its INTERFACE).
2. `dbsvr` commits and PUBs `{ action, type, keys, row }` on the notify socket.
3. `dbd A` **and** `dbd B` both receive it and send `RECORD APPLY` to their iod.
4. Both `cust.name` (and any bound MACHINE windows) update, and their `WHEN`
   dependents re-check.

```clockwork
# same Customer RECORD on both programs:
#   cust Customer (id: 1);
# After A inserts Ann, cust.name is "Ann" on B too — no find, no poll.
```

Deletes are the same, except `dbsvr` PUBs `action: delete` and `dbd` sends
`RECORD REMOVE` (a `Class#key` cache is dropped and unlinked from LISTs; a
**named** instance stays and resets to `empty` with non-KEY defaults).
