# RECORD implementation plan (revised after review)

**Source:** `docs/RECORD_DB.md` plus Martin: *pretty much got the idea; write tests; standalone tools that generate scaffolding for operational code (create, update, list, etc.) from RECORDs.*

**Branch:** `feature/db-record-implementation`

Architecture from the spec stands. This file includes an in-depth review of the previous plan against the current parser, `loadConfig`, COPY, and `dbd`, then the corrected plan.

---

## Invariant: Clockwork is generic

**latproc / Clockwork is a general language and runtime.** RECORD, `dbd`, `cw-scaffold`, `cw-migrate`, and every test in this repo must be domain-neutral. Nothing in iod, the parser, the scaffolder, or `tests/` may be designed around wool sampling, warehouse stations, bales, weight notes, Grab/Core, or SamplingLine.

`docs/RECORD_DB.md` may keep plant background as *why the feature exists*. That is not a license to put those names into Clockwork code or tests.

| Allowed here (Clockwork) | Not allowed here |
| --- | --- |
| `Customer`, `Order`, `Item`, `Address` | `WEIGHTNOTE`, `BALEDETAILS`, `BaleWithLinks`, `GrabChamber` |
| `tests/datastore.cw`-style JSON `action`/`type`/`keys` | station queues, RFID, pack codes as first-class types |
| Generated `CustomerINTERFACE` | Generated `WEIGHTNOTEINTERFACE` as a Clockwork fixture |
| Table name `customer` | Table name `weightnote` in iod tests |

Rules:

1. **No wool identifiers** in `iod/src`, `iod/tests`, `tests/`, `iod/tools`, CMake test names, golden files, or comments that describe behaviour.
2. **Tests speak generic Clockwork** — same language as `tests/lists.cw` / `tests/datastore.cw` (`Customer MACHINE` already in tree). Fixtures: `Customer`, `OrderLine`, maybe a join **view** `CustomerWithAddress` — not plant APIs.
3. **Scaffolder** emits whatever RECORD classes it is given. Goldens use `Customer`, not a plant RECORD.
4. **Two-process tests** are “iod A and iod B share `dbsvr`”, not “Grab and Core”.
5. **Application rewrite** (JemalongDB / Warehouse LPC) lives in those application trees later. It is not a Clockwork PR and not a Clockwork test.

Finding **13** (this round): the draft used `WEIGHTNOTE` / Jemalong names in grammar samples, scaffolder signatures, and PR 9 as if they were Clockwork deliverables. That leaks plant domain into a generic language. Corrected below.

---

## In-depth review

Verdict: the RECORD-as-restricted-MACHINE split is right, and a standalone scaffolder is the right response to Martin. The first draft under-specified grammar, test isolation, and what “create/list” actually emit. Those would have produced a scaffolder that either would not parse or would emit schema-`create` / `COPY ALL FROM Class` that cannot run yet.

### What holds up

- RECORD as `MachineClass` + `is_record`, ordinary `MachineInstance` — FLAG/LIST already work this way (`clockwork.cpp` `makeFlagMachineClass`, constructor in `MachineClass.cpp`).
- Restricted body in bison, not a new runtime type.
- JSON persist on `DATABASE_CHANNEL`; sqlite stays in datastore.
- Tests without plant hardware for PRs 1–2.
- Scaffolder using the **real parser**, not a second RECORD grammar.
- Defer `COPY ALL FROM RecordClass` in generated `list` until that runtime exists.

### Findings (severity)

**1. Bug — `create` is not datastore `action: "create"`.**  
Martin asked for operational create/update/list. In `tests/datastore.cw` and `RECORD_DB.md`, `action: "create"` is **CREATE TABLE / schema**. Schema belongs to `cw-migrate`. Scaffolder **create = row insert**. Do not emit schema JSON.

**2. Bug — `loadConfig` is not reentrant; in-process gtest parse suites will lie.**  
`MachineClass::all_machine_classes` / `machine_classes` are process-static (`MachineClass.cpp`). `predefine_special_machines()` runs once (`cw_framework_initialised` in `clockwork.cpp`). `reset_parser()` only clears current_* pointers (`cwlang.ypp`), not classes, instances, or `num_errors`. `test_list_walkers.cpp` loads **once** in `SetUpTestSuite`.  
A gtest that `loadConfig`s a valid RECORD then a rejecting file in the same process will leak classes and error counts.  
**Fix:** parser/scaffold tests are **subprocess** tests: spawn `cw` / `cw-scaffold` (or `cw --parse-only`) per fixture. One-process gtest only for metadata on a **single** successful load, or skip gtest loadConfig entirely for PR 1.

**3. Bug — generated `COPY ALL FROM Customer` cannot work until PR 6.**  
`COPY ALL FROM SYMBOL` looks up an **instance** (`SetOperationAction.cpp` `owner->lookup(source_a)`), then walks `parameters` as list members. A class name is not a list. First draft already deferred this; keep it as a hard rule: PR 2 `list` = JSON `find` with empty/absent keys (existing `find` with no keys / find-all shape).

**4. Bug — `NOT NULL` must not introduce a `NULL` lexer keyword.**  
`OPTION myProp NULL` works because `NULL` is a **symbol** folded to `Value::t_empty` (`value.cpp`). A `NULL` token would break existing programs (`tests/null.cw`).  
Annotations: reuse existing `KEY` token (`cwlang.lpp:134`, today only `KEY STRINGVAL` on CHANNEL). Add `UNIQUE`. Parse `NOT NULL` as `NOT` + symbol `"NULL"`.

**5. Bug — generated Ops had no LIST parameter and a bad class name.**  
`list` has to fill a LIST instance the program already has. Companion signature:

```
CustomerINTERFACE MACHINE record, items {
    GLOBAL DATABASE_CHANNEL;
    …
}
```

Name it **`<RecordClass>INTERFACE`** (e.g. `Customer` → `CustomerINTERFACE`). That matches the existing Clockwork INTERFACE idea and `RecordManager` in `tests/datastore.cw`. Do not bake plant class names into the generator.

**6. Bug — `respond_to` vs per-column apply are different protocols.**  
`dbd.cpp` `send_response_to_clockwork` expects `respond_to` as a **string** `"machine.property"`, then PROPERTY that blob. `tests/datastore.cw` assigns a JSON **object** to `respond_to` — already a sketch mismatch.  
PR 2 generates today’s INTERFACE pattern (copy OPTIONS into JSON, SEND). PR 3 should apply rows by **`(type, key)` registry onto RECORD OPTIONS**, and keep blob `respond_to` as backward compatible. Do not require generated code to interpolate the Ops instance name into `respond_to`.

**7. Suggestion — RECORD still has builtin `INIT`.**  
`MachineClass` constructor always `addState("INIT")`, `initial_state("INIT")`. Spec “no user states” means reject WHEN/COMMAND/user states, not a stateless object. Disable auto state changes (like FLAG). Do not add `on`/`off`. HMI may see `INIT`; that is OK.

**8. Suggestion — VIEW in PR 1 is cheap; QUERY is not.**  
Header `VIEW "name"` / `TABLE "name"` is a few productions. Keep it in PR 1 so the scaffolder can omit writes on views. Do not implement QUERY.

**9. Suggestion — `cw` is already a loader; a second binary is justified but heavy.**  
`cw` links `cw_interpreter` and runs programs. `cw-scaffold` as its own executable matches “standalone tools” and keeps ESP32 export out of the scaffolder. Alternative: `cw --scaffold --out dir`. Prefer **new `cw-scaffold` binary** plus optional `cw --parse-only` for tests (exit 0/2). Do not start iod/ZMQ for parse-only.

**10. Suggestion — CMake: `dbd` is skipped without libmodbus.**  
`iod/CMakeLists.txt` only builds `dbd` if `MODBUS_FOUND`. PR 3 tests on a Mac without libmodbus will not run `dbd`. Call that out; parser/scaffold tests only need `cw` / `cw-scaffold`.

**11. Nit — `KEYWORD` RECORD vs instance named RECORD.**  
Same as MACHINE. Lexer keyword. Document it.

**12. Nit — table naming.**  
Spec still open (Q3). Plan locks **lowercase class name** unless `TABLE "…"`. Scaffolder `type` field uses that. Do not silently use exact class name.

### Grammar sketch (reviewed)

Lexer: `RECORD`, `UNIQUE`, `VIEW`, `TABLE` (TABLE/VIEW as tokens only if they do not collide; `TABLE` unused today; `VIEW` unused). `KEY` already exists.

```
definition_header:
  SYMBOL RECORD record_header_tail parameters

record_header_tail:
  /* empty */
| VIEW STRINGVAL
| TABLE STRINGVAL

record_body:   # NOT definition_body
  record_section | record_body record_section

record_section:
  OPTION option_settings ';'
| LOCAL OPTION local_option_settings ';'

option_setting:
  SYMBOL value
| SYMBOL value option_annots

option_annots:
  KEY | UNIQUE | NOT SYMBOL   # SYMBOL must be "NULL"
| option_annots KEY | …
```

Putting KEY/UNIQUE/NOT NULL on **all** OPTION (including MACHINE) is acceptable: ignore annotations if `!is_record`, or error “KEY only valid on RECORD”. Prefer **error on non-RECORD** so people do not think MACHINE KEY persists.

Reject WHEN/COMMAND/states by **not having those productions** in `record_body`. A user writing `COMMAND x` inside RECORD gets a syntax error at that token, which is what Martin wants.

### Scaffolder output (reviewed)

For

```
Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION email "";
    OPTION age 0;
    LOCAL OPTION dirty false;
}
```

emit `CustomerINTERFACE MACHINE record, items` with:

| COMMAND | JSON `action` | Notes |
| --- | --- | --- |
| create | `insert` | data = persisted non-LOCAL OPTIONS except maybe KEY if integer 0 means autoincrement — **v1 copies all persisted OPTIONS including KEY** |
| update | `update` | keys = KEY columns; data = other persisted OPTIONS |
| find | `find` | keys = KEY; fields = persisted OPTIONS |
| list | `find` | keys omitted or `{}`; fields = persisted OPTIONS |
| delete | `delete` | keys = KEY |

- `type` = table name (`customer`)
- `auth` = `"xxx"`
- no schema `create`
- VIEW RECORD: find + list only
- LOCAL omitted
- `SEND request TO DATABASE_CHANNEL` as in `tests/datastore.cw`
- `GLOBAL DATABASE_CHANNEL;`
- user instantiates: `cust Customer; all_cust LIST; mgr CustomerINTERFACE cust, all_cust;`

Golden tests compare generated text and **subprocess-parse** RECORD + generated INTERFACE + `tests/db-channel.cw`.

---

## Corrected PR plan

### PR 1 — RECORD grammar + subprocess parse tests

**Files:** `cwlang.lpp`, `cwlang.ypp`, `MachineClass.h/.cpp` (`is_record`, column flags, table/view name), maybe `semantic_analysis` for “RECORD needs a KEY” (warning vs error: **error for base table, warning for VIEW**).

**Tests (subprocess, no dbsvr, no dbd):**
Fixtures under `iod/tests/fixtures/record/` (generic names only), e.g. `customer.cw`, `customer_with_address_view.cw`, `reject_when.cw`:

- `cw --parse-only fixtures/record/customer.cw` exits 0
- reject WHEN / COMMAND / user state / transition (exit 2, stderr mentions RECORD or syntax error)
- KEY / UNIQUE / NOT NULL stored; LOCAL not a column
- VIEW/TABLE name stored
- `OPTION x KEY` on a MACHINE is an error
- keyword RECORD cannot be a class name

Add `cw --parse-only`: `loadConfig`, then exit (0 ok, 2 errors). No interpreter loop, no ZMQ if practical. If wiring that into `cw.cpp` is messy, a tiny `cw-parse` binary is fine — still a standalone tool.

Do **not** test LIST membership here (true of every machine).

### PR 2 — `cw-scaffold` + golden tests

**Depends:** PR 1

**Binary:** `cw-scaffold --from a.cw --out dir/`  
Loads via `loadConfig`, emits one `<Class>INTERFACE.lpc` per RECORD.

**Tests (generic language only):**
- golden LPC for `Customer` (and optionally `OrderLine`)
- VIEW `CustomerWithAddress` → find + list only, no create/update/delete
- LOCAL absent from JSON
- no KEY on table RECORD → scaffolder exits non-zero
- generated file + RECORD + `tests/db-channel.cw` parse with `cw --parse-only`

**Docs:** amend `RECORD_DB.md` Key Decisions: operational CRUD is generated INTERFACE, not RECORD builtins. Q7 answered for v1. Language-ref examples use Customer, not plant types.

### Datastore (`../datastore`) — issues that belong in that repo

Clockwork PRs 1–2 do **not** need datastore changes. Round-trips against **today’s** `insert`/`find`/`update`/`delete` are enough for generated INTERFACE JSON. Everything below is a **datastore** change (generic `customer` tests, no plant types).

Repo today: `dbsvr` ZMQ REP `:5554`, `SQLInterface::buildSQL` concatenates SQL, `Store` wraps sqlite3. **No test target** in `CMakeLists.txt`. `safeTableName` / `safeString` always return true (`sql_interface.cpp`). `db_server.cpp` **rejects** statements with bind parameters (`unsupported number of parameters`).

| # | Issue | Why RECORD cares | Where |
| --- | --- | --- | --- |
| D1 | **SQL is concatenated; binds are rejected** | Spec: identifier allow-list + bound values. LPC must not be able to inject SQL. Current server cannot use `?` at all. | `sql_interface.cpp` `buildSQL`; `db_server.cpp` ~103–106 |
| D2 | **Replies stringify every column** | `sqlite3_column_text` → JSON strings; NULL text pointer is unsafe. Clockwork OPTIONS are integer/string/float/boolean/NULL. Apply-OPTIONS will coerce wrongly or crash on NULL. | `db_server.cpp` ~124–127 |
| D3 | **insert/update/delete do not return the row** | Spec: one persist, OPTIONS filled from the **reply**, no follow-up GET. sqlite INSERT yields no `SQLITE_ROW` unless `RETURNING`. Empty `response: []` is not enough for dbd. | `db_server.cpp` performRequest; need `RETURNING *` (sqlite 3.35+) or explicit return of `data`+assigned keys |
| D4 | **JSON null / boolean not emitted as SQL** | `collectValuesString` only string and number. RECORD `OPTION cacheWeight NULL` cannot persist. | `sql_interface.cpp` `collectValuesString` |
| D5 | **No `select` / `join` / `order` / `limit`** | COPY WHERE and QUERY need more than equality `keys`. Named **views** can already be `find` with `type: "customer_with_address"` if the VIEW exists (sqlite). Ad-hoc `join` arrays are new. | `buildSQL` actions |
| D6 | **WHERE is equality-AND only** | `COPY … WHERE Customer.ITEM.age > 10` cannot push down. | `buildWhereClause` |
| D7 | **ZMQ REP only — no notify** | Open question 6: second Clockwork never hears a commit. Notify-after-commit (PUB) or documented refresh lives **here**, not by putting sqlite in `dbd`. | `dbsvr.cpp` bind REP only |
| D8 | **`action: "create"` is CREATE TABLE** | README says “add one instance”; code creates a table. Schema moves to `cw-migrate`. Keep `create` as a hatch or deprecate; do not use it as scaffolder “create”. | `buildSQL` create; `README.md` |
| D9 | **`action: "sql"` is unsandboxed** | Spec: operator/debug only, still parameterized. Not the migration tool. | `buildSQL` sql |
| D10 | **No identifier catalog** | Allow-list should be sqlite schema / migration catalog (`customer`, columns), not “any string in `type`”. | new; used by D1 |
| D11 | **No tests** | Martin: write tests. SQLInterface + Store + protocol tests with a temp `customer` table. | new `tests/` in datastore |
| D12 | **`cw-migrate` belongs next to datastore** | Versioned SQL including `CREATE VIEW`; `cw_revision` table; no auto-upgrade on `dbsvr` start (mismatch = error). Examples: `customer`, not plant tables. | new tool in datastore repo |
| D13 | **1000-byte SQL buffer** | `char buf[1000]` will truncate wide rows / joins. | `buildSQL` |
| D14 | **README / protocol docs** | Document typed replies, RETURNING, `select` JSON, auth still `"xxx"`. | `README.md` |
| D15 | **No WAL, no busy timeout, no request transaction** | `StoreInternals` opens READWRITE/READONLY only (`store.cpp` ~30–31). No `PRAGMA journal_mode=WAL`, no `sqlite3_busy_timeout`, no `BEGIN`/`COMMIT`/`ROLLBACK`. Default busy handler is NULL → `SQLITE_BUSY` fails the JSON request immediately. Each `step()` is sqlite autocommit. Two Clockworks (via one `dbsvr`) and any extra reader (`cw-migrate`, CLI) need WAL + retry + one transaction per JSON request. | `store.cpp` `connect` / `StoreInternals`; `db_server.cpp` `performRequestMessage` |

**Not datastore:** RECORD lexer, `cw-scaffold`, dbd PROPERTY apply, LIST COPY-from-class. Those stay in Clockwork.

---

## ZMQ channels and process restarts (`dbd` ↔ iod, `dbd` ↔ `dbsvr`)

Plant already burned time on this for humid/modbusd/persistd (`prod-client-zmq-fix` / `ConnectionManager::forceFullReconnect`). **`dbd` did not get that treatment.** `dbsvr` is a raw REP with linger commented out. RECORD persist will hang or exit on the first iod/`dbsvr` bounce unless this is fixed **before** round-trip tests are trusted.

### Topology today

```
iod DATABASE_CHANNEL (PUB, CHANNEL grant)
        ▲  SubscriptionManager SUB + setup REQ :5555
        │
       dbd
        │  throwaway ZMQ_REQ + new context per request
        ▼
     dbsvr REP :5554   (linger unset; comment in dbsvr.cpp)
```

Reply path: **another** throwaway context + REQ to iod `:5555` (`send_response_to_clockwork`). `g_iodcmd` is created in `main` and then ignored for replies.

### What persistd/modbusd already do (copy this)

- `ZMQ_LINGER = 0` on every socket (`setSocketLinger0`)
- `sendWithDeadline` — REQ never blocks forever; on timeout/EFSM **recreate** the socket
- `SubscriptionManager::forceFullReconnect` on disconnect, stuck `e_done`, poll/recv errors — **do not `exit()`**
- Do not treat ZMQ auto-reconnect as a valid CHANNEL grant after peer restart (stale SUB)

`dbd` still `exit(1)`/`exit(2)` on subscriber EFSM/ENOTSOCK and on poll exceptions (`dbd.cpp` ~421–470). On iod `STARTUP` it logs “restarting” and **`exit(0)`** with an empty try (`~525–538`) — supervise restarts the process instead of in-process reconnect. Disconnect monitor only prints `IOD disconnected`.

### `dbd` ↔ `dbsvr` REQ/REP (the new failure mode)

| Issue | Code | Restart effect |
| --- | --- | --- |
| New `zmq::context_t` **per request** | `dbd.cpp` ~496–498 and `send_response_to_clockwork` ~93–95 | Context teardown uses **default infinite linger** → dbd can stick on exit; sockets do not share iod’s recovery path |
| `makeRemoteRequest` recv with flags `0` | `dbd.cpp` ~314 | If `dbsvr` dies after send, REQ FSM is half-open **forever**. No deadline. |
| `connect("tcp://127.0.0.1:5554")` hardcoded | `dbd.cpp` ~498 | Second Clockwork on another PC never reaches a shared `dbsvr`. Config needed (`db_host` / port). |
| `std::cout << buf` before null check | `dbd.cpp` ~506 | Failed request can crash dbd |
| Double `free(data)` | `dbd.cpp` ~518 and ~522 | Heap corruption after a message |
| `dbsvr` linger **commented out** | `dbsvr.cpp` ~42–43 | Kill -9 / restart: bind `:5554` fails until TCP linger expires (`EADDRINUSE`) |
| REP + DONTWAIT send loop | `db_server.cpp` `handleIncomingRequest` | Client gone → EFSM or tight EAGAIN loop; no socket reset |
| One REP, many dbds | `dbsvr` single-threaded | Fine for low rate; a hung client blocks everyone. Document; later ROUTER if needed |
| No heartbeat | both | Dead peer looks like “connected” |

Classic ZMQ: **REQ/REP does not survive a peer restart** without linger 0, timeouts, and a **new socket**. Auto-reconnect delivers the next send to a new REP that is not expecting that request (or the old REQ never leaves `recv`).

### Required behaviour (generic; no plant names)

1. **One long-lived context** in dbd (already `MessagingInterface::setContext`). Reuse `g_iodcmd` / `sendWithDeadline` for PROPERTY apply. Never `new zmq::context_t` per request.
2. **Long-lived REQ to `dbsvr`**, linger 0, recreate on timeout/EFSM/disconnect — same helper as persistd.
3. **Configurable** `dbsvr` endpoint (default `127.0.0.1:5554`), not a string literal.
4. **`dbsvr`**: linger 0 on bind; on send/recv failure reset to wait-for-request (do not stay in `sending` forever). Bind `EADDRINUSE`: retry/log, do not hang.
5. **iod CHANNEL path**: dbd uses `forceFullReconnect` like persistd. `STARTUP` → reconnect in-process, **do not `exit(0)`**. Subscriber EFSM → recreate, not process death.
6. **Tests** (subprocess, generic): start `dbsvr` + dbd + `cw` with `Customer` INTERFACE; kill/restart `dbsvr`; next insert/find succeeds within busy/REQ deadline. Kill/restart iod (or send STARTUP); dbd stays up and resubscribes. Immediate restart of `dbsvr` rebinds `:5554` (linger 0).

### PR placement

**DS-0b / Clockwork dbd-zmq** — do this **with or right after DS-0**, **before** trusting persist round-trips (Clockwork PR 3). Same client-zmq discipline as persistd; do not invent a second recovery FSM.

Notify-after-commit (DS-6) must use PUB/SUB **or** a new socket type with the same linger/timeout rules. Do not add a second silent REQ.

**Datastore PRs (own repo, generic tests):**

### How the Python API uses SQLite (copy the PRAGMAs, not the domain)

Reference: `SamplingLineProjects/WoolSamplingLineAPI/app/db.py` (and the same PRAGMAs in `alembic/env.py`, documented in `specs/03-data-model-and-migrations.md` and `README.md`). Live `bales.db-wal` / `bales.db-shm` show WAL is actually on.

**Copy into datastore (generic Store, no plant tables):**

```python
# WoolSamplingLineAPI/app/db.py — on every SQLite connect
PRAGMA foreign_keys=ON
PRAGMA journal_mode=WAL
PRAGMA synchronous=NORMAL
PRAGMA busy_timeout=5000
```

SQLAlchemy session: `sessionmaker(autocommit=False, autoflush=False)`. One session per HTTP request (`get_db` yields, `finally: db.close()`). Handlers call `db.commit()`; `IntegrityError` paths call `db.rollback()` (`crud.py`). Close without commit discards the work.

**Map to `dbsvr` (one JSON request = one HTTP request):**

| Python | datastore |
| --- | --- |
| connect PRAGMAs above | same four PRAGMAs on every `sqlite3_open` |
| `autocommit=False` | server `BEGIN` … `COMMIT`/`ROLLBACK`; Clockwork does not send those |
| explicit `db.commit()` in the route | automatic `COMMIT` after a successful JSON action |
| `db.rollback()` on error / close | automatic `ROLLBACK` on SQL error or request failure |
| Alembic revisions, not `create_all` in prod | `cw-migrate`; no schema mutate on `dbsvr` start |
| ORM joins in Python | JSON `find`/`select` + SQL views in datastore (not SQLAlchemy) |

Do **not** copy: SQLAlchemy, Alembic Python files, `BaleInstance` / plant models, or `create_all` on startup. RFID Capture `app/db.py` has **no** WAL PRAGMAs — ignore that file.

Alembic `env.py` currently sets FK/WAL/NORMAL but **omits** `busy_timeout`. Datastore (and `cw-migrate`) should apply **all four** on every connection.

---

0. **DS-0 — WAL, contention, automatic transactions** (Store; do this first)  
   Match WoolSamplingLineAPI `app/db.py` on writable `connect()`:
   - Open `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE` (create file if missing).
   - `PRAGMA foreign_keys=ON`
   - `PRAGMA journal_mode=WAL` (must stick; fail connect if the pragma does not return `wal`).
   - `PRAGMA synchronous=NORMAL`
   - `PRAGMA busy_timeout=5000` (or `sqlite3_busy_timeout(db, 5000)` — same 5s as Python).
   - `sqlite3_wal_autocheckpoint` at sqlite default (1000 pages); checkpoint on clean `dbsvr` shutdown (`TRUNCATE` if idle).

   Per JSON request in `performRequestMessage` (Clockwork never sends BEGIN):
   - **Writes** (`insert`/`update`/`delete`/`create`/schema): `BEGIN IMMEDIATE` → statements (including `RETURNING`) → `COMMIT`. Any error → `ROLLBACK`, status ≠ 0, no partial write.
   - **Reads** (`find`/`select`): `BEGIN DEFERRED` (or a single snapshot statement) → `COMMIT`. Do **not** `BEGIN IMMEDIATE` on reads — that would serialize readers and throw away WAL.
   - Reject client SQL that contains `BEGIN`/`COMMIT`/`ROLLBACK` if `action: "sql"` remains (server owns the transaction).
   - Retry the whole request on `SQLITE_BUSY` / `SQLITE_LOCKED` until busy_timeout elapses, then error (do not leave a half transaction).

   Tests (temp `customer.db`, generic):
   - After open, `PRAGMA journal_mode` is `wal`.
   - Second connection can `SELECT` while the first has an open write transaction (WAL readers).
   - Two writers: second waits then succeeds or times out cleanly; DB not corrupt.
   - Failed statement rolls back; a following find does not see partial data.
   - No `BEGIN` in Clockwork JSON.

   This is independent of RECORD grammar. Clockwork stays unaware of WAL.

1. **DS-1 — tests + typed replies + NULL + RETURNING**  
   Fixture `customer` table. insert/find/update/delete round-trip JSON types (int/string/null). insert/update replies include the row. Can land **before** Clockwork PR 3 so dbd has a real row to apply. Uses DS-0 transactions.

2. **DS-2 — bound parameters + identifier allow-list**  
   Flip `db_server` from “binds unsupported” to binding values. Identifiers from catalog only. Fixes D1/D10/D13.

3. **DS-3 — `select` / `order` / `limit` / richer `where`**  
   Equality plus a small operator set (`eq`, `gt`, `lt`, …). Needed for COPY WHERE push-down. Named view as `type` or `from` (sqlite VIEW already works with `find`).

4. **DS-4 — JSON `join` (optional with DS-3)**  
   Ad-hoc joins when no VIEW yet. Clockwork QUERY PR depends on this **or** on named views only (open question 5). Prefer named views first (smaller).

5. **DS-5 — `cw-migrate`**  
   generate/upgrade/downgrade/current; `CREATE TABLE`/`CREATE VIEW`; refuse silent mutate on `dbsvr` start.

6. **DS-6 — notify-after-commit (Q6)**  
   Only once the mechanism is chosen. ZMQ PUB of `{type, keys}` after commit is the datastore-shaped option. Clockwork two-client test depends on this **or** on dbd refresh.

DS-1 can parallel Clockwork PR 1. DS-2 can parallel too. DS-3 before Clockwork COPY-from-class. DS-5 after RECORD classes exist (generate --from-program) but upgrade itself does not need iod.

### Clockwork PR 3+ (after / beside datastore)

- **PR 3 dbd:** map result **typed** rows onto RECORD OPTIONS by type+key registry; keep blob `respond_to`; inbound PROPERTY does not persist. Wants DS-1 reply shape.
- **PR 5** two Clockworks / notify (Q6 / DS-6). Test names: client A / client B.
- **PR 6** COPY ALL FROM RecordClass; scaffolder `list` switches to COPY. Wants DS-3 for WHERE push-down; without it, COPY can still `find` all and filter in iod (acceptable v1, document it).
- **PR 7** QUERY INTO — wants DS-3 and optionally DS-4.
- **Later, other repos:** applications may *use* RECORD. Not Clockwork or datastore CI.

---

## Testing rules (Martin: “write tests if you can”)

| Layer | Isolation | Needs datastore |
| --- | --- | --- |
| Parse RECORD / reject logic | subprocess `cw --parse-only` | no |
| Scaffold goldens + parse output | subprocess `cw-scaffold` + `cw --parse-only` | no |
| dbd apply OPTIONS | gtest on extracted mapper + optional live dbd | fixture JSON; dbsvr optional |
| COPY FROM class | `tests/record_list.cw` | yes (find; WHERE push-down needs DS-3) |
| Two iods | later | yes + DS-6 or refresh |
| datastore SQLInterface / replies | gtest in `../datastore` | no Clockwork |

Never `loadConfig` twice in one process for conflicting programs.

---

## Out of scope (unchanged + generic)

SQL in WHEN; sqlite in `dbd`; new MachineInstance subclass; write-through; sharing any application DB file; **any wool/warehouse/bale types in Clockwork or its tests**. Plant LPC stays in plant repos.

---

## First slice after approval

Clockwork (this repo):

1. PR 1 grammar + `cw --parse-only` + generic fixtures  
2. PR 2 `cw-scaffold` + goldens  

Datastore (`../datastore`, can start in parallel):

1. DS-0 WAL + busy timeout + automatic BEGIN/COMMIT/ROLLBACK  
2. DS-0b linger 0 on REP bind; recover send/recv (with Clockwork dbd-zmq)  
3. DS-1 typed JSON + NULL + RETURNING rows  

Clockwork dbd-zmq (linger 0, one context, REQ deadline, `forceFullReconnect`, no `exit` on STARTUP) lands **before** persist round-trip tests.

Neither first slice needs plant types or a live warehouse.
