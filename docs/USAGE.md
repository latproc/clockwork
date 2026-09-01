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

A full `MACHINE` (with `WHEN`/`COMMAND`) can bind the same table, keeping its
own lifecycle in a `LOCAL OPTION`:

```clockwork
CustomerPanel MACHINE TABLE "customer" {
    OPTION id 0 KEY;
    OPTION name "";
    LOCAL OPTION state "empty";
    active WHEN name != "";
    idle DEFAULT;
}

cust CustomerPanel (id: 1);
```

## 3. Querying with `QUERY` and JSON

`QUERY` sends a JSON request property to `DATABASE_CHANNEL`; the reply is turned
into a LIST with `AS LIST` (or `PUSH ITEMS FROM`).

```clockwork
Customer RECORD { OPTION id 0 KEY; OPTION name ""; }
all LIST;

ed MACHINE {
    OPTION q JSON_VALUE {
        "action": "find", "type": "customer", "auth": "xxx", "keys": {}
    };
    COMMAND refresh {
        QUERY q INTO all;          # SEND q to DATABASE_CHANNEL
    }
}

# the reply (a JSON array) becomes a LIST:
#   all := reply AS LIST;
```

`AS LIST` turns any JSON array into a LIST:

```clockwork
all := result AS LIST;
```

## 4. Copying rows into a LIST or RECORD

`COPY ALL FROM <RecordClass> TO <list>` copies the held RECORD instances:

```clockwork
CustomerINTERFACE MACHINE record, items {
    COMMAND list {
        CLEAR items;
        COPY ALL FROM Customer TO items;
    }
}
```

`COPY PROPERTIES FROM <row> TO <record>` binds a query result row onto a named
RECORD (projecting only declared columns).

## 5. `cw-scaffold` — generated CRUD

`cw-scaffold` generates a `<Class>INTERFACE MACHINE record, items` with
`create`/`update`/`delete`/`find`/`load`/`list` commands. `create` maps to a JSON
`insert` (a row insert, not a schema create).

```
cw-scaffold --from customer.cw --out dir/          # generate the INTERFACE
cw-scaffold --from customer.cw --out dir/ --sql    # + CREATE TABLE SQL
```

## 6. `cw-migrate` — versioned schema

Schema (including `CREATE VIEW`) is versioned and applied by `cw-migrate`, not by
`dbd` or on `dbsvr` start.

```
cw-migrate current
cw-migrate upgrade
cw-migrate downgrade --rev 1
cw-migrate generate --sql 0002_customer_with_city.sql
```

## 7. Two Clockworks, one datastore

After a commit, `dbsvr` publishes the changed row; every `dbd` that holds that
RECORD applies it onto its OPTIONS, so all Clockworks stay in sync without
polling.
