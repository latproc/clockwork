# Agent notes

This git repository is the Clockwork **product** (language, iod, cw_client,
tools, and their tests). It is published as GitHub `latproc/clockwork`.

It is **not** the site application tree. Plant LPC, `lib/`, and machine
programs live in a separate checkout (historically SVN) and must stay there.

## Product vs site

Do not mix the two in anything that lands in *this* repo:

- Commit messages and PR/patch text
- Source comments
- CMake compile definitions and test harnesses
- Fixtures under `tests/` and `iod/tests/`

Forbidden in those places:

- Site tree paths (`/opt/latproc/code/…`, SVN URLs, plant `lib/*.lpc`)
- Site host or cell names used as if they identified product code
- Site machine files (grab/core planners, plant `generic_*.lpc`, and similar)

`/opt/latproc` as an **install prefix** for *binaries built from this repo*
is fine in operator docs (`TRANSPORT.md`, service scripts). That does not
make site LPC a dependency of the product.

## Tests

`tests/` and `iod/tests/` must run with only this checkout. Load fixtures
from those trees. If a behaviour needs types that exist only on a plant,
add a generic in-tree stub (`iod/tests/fixtures/`) and keep instance names
generic.

A CTest (`no_site_tree_in_product_tests`) fails the build if site-tree
paths appear under the product test directories.

Git lines and port tags: `iod/docs/BRANCHES.md`.
