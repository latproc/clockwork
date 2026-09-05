#!/usr/bin/env bash
# upgrade_prod_fix.sh — pull tip of a prod*fix branch on plant hosts, rebuild
# iod/cw/plugins under tmux (or screen), and record pre-upgrade git hashes so
# you can roll back if needed.
#
# Host list: scripts/upgrade_prod_fix.hosts (next to this script) by default.
# No hosts are hardcoded — edit that file or pass --hosts / CLI hosts.
#
# Usage:
#   ./scripts/upgrade_prod_fix.sh                  # hosts from upgrade_prod_fix.hosts
#   ./scripts/upgrade_prod_fix.sh --hosts FILE      # alternate host list
#   ./scripts/upgrade_prod_fix.sh status            # poll remote jobs
#   ./scripts/upgrade_prod_fix.sh logs              # tail remote upgrade logs
#   ./scripts/upgrade_prod_fix.sh attach HOST       # attach tmux/screen session
#   ./scripts/upgrade_prod_fix.sh record            # show last recorded hashes
#   ./scripts/upgrade_prod_fix.sh --restart …       # restart services after install
#   ./scripts/upgrade_prod_fix.sh --services a,b …  # only these names (default: all)
#   ./scripts/upgrade_prod_fix.sh --branch NAME …   # override branch
#   ./scripts/upgrade_prod_fix.sh --generic-lib DIR  # curl/web LPC source (default below)
#   ./scripts/upgrade_prod_fix.sh --skip-generic-lib # do not push GenericLib web plugins
#   ./scripts/upgrade_prod_fix.sh HOST [HOST…]      # only these hosts (skip file)
#
# GenericLib plugins (curl web + system_exec):
#   Before the remote build, selected PLUGIN LPC sources are taken from
#   GENERIC_LIB_DIR (default: ~/src/latproc/GenericLib/trunk):
#     - generic_webrequest.lpc  (curl / web_request.so)
#     - generic_system_exec.lpc (SYSTEMEXEC → system_exec.so via iod exec_command.c)
#   Each file is compared by sha256 to the plant copy under code/lib/; if the
#   plant is missing or differs, the laptop copy is scp'd up. For webrequest
#   only, also mirrors to plugins/web_request.cw (legacy path).
#   system_exec LPC is the thin iod path (includes exec_command.c); rebuild the
#   .so after iod upgrades so plant stays on the tested implementation.
#
# Restart policy (--restart):
#   For each selected service under /etc/service/: if it is currently UP,
#   svc -t it. If it is DOWN (or missing), leave it alone — down stays down.
#
# What runs on each host (inside a detached session):
#   1. Record current HEAD + binary sha256 into /opt/latproc/log/upgrade-*/
#   2. Snapshot running binaries as *.prev-<hash>
#   3. git fetch + hard reset to origin/<branch> tip
#   4. make -C iod release-install
#   5. Rebuild every PLUGIN declared in code/lib/generic*.lpc
#      (files without PLUGIN lines are skipped — pure LPC)
#   6. Rebuild web_request from code/lib/generic_webrequest.lpc (or legacy path)
#   7. Record post-upgrade hashes; optional service restart
#
# Rollback (manual, per host):
#   cd /opt/latproc && git checkout <pre_hash>
#   cp -a iod/iod_sdo.prev-<pre_hash> iod/iod_sdo   # etc for cw/iosh/plugins
#   svc -t /etc/service/iod

set -euo pipefail

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_HOSTS_FILE="${SCRIPT_DIR}/upgrade_prod_fix.hosts"

SSH_USER="${SSH_USER:-root}"
SSH_PORT="${SSH_PORT:-2222}"
LATPROC="${LATPROC:-/opt/latproc}"
BRANCH="${BRANCH:-prod-experimental-mqtt-fix}"
SESSION_NAME="${SESSION_NAME:-latproc-upgrade}"
REMOTE_JOBS="${REMOTE_JOBS:--j4}"
DO_RESTART=0
# Comma/space-separated service names, or "all" (default) = every real
# supervise dir under /etc/service. Only UP services are bounced.
SERVICES="${SERVICES:-all}"
# Laptop tree of plant LPC (SVN GenericLib). webrequest + system_exec pushed from here.
GENERIC_LIB_DIR="${GENERIC_LIB_DIR:-$HOME/src/latproc/GenericLib/trunk}"
SYNC_GENERIC_LIB=1
ACTION="upgrade"
HOSTS_FILE=""
# Set when the operator passed hostnames on the CLI (skip hosts file).
CLI_HOSTS=0

# Parallel arrays: HOSTS[i], HOST_PORTS[i], HOST_USERS[i]
HOSTS=()
HOST_PORTS=()
HOST_USERS=()

usage() {
  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

# Load hosts from a file. Format per non-comment line:
#   HOST [PORT [USER]]
load_hosts_file() {
  local file="$1"
  local line host port user
  if [[ ! -f "$file" ]]; then
    echo "Hosts file not found: $file" >&2
    echo "Create it (one host per line) or pass hosts on the command line." >&2
    exit 2
  fi
  while IFS= read -r line || [[ -n "$line" ]]; do
    # strip comments and trim
    line="${line%%#*}"
    # shellcheck disable=SC2001
    line="$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
    [[ -z "$line" ]] && continue
    # shellcheck disable=SC2086
    set -- $line
    host="${1:?empty host in $file}"
    port="${2:-$SSH_PORT}"
    user="${3:-$SSH_USER}"
    HOSTS+=("$host")
    HOST_PORTS+=("$port")
    HOST_USERS+=("$user")
  done <"$file"
}

# ---------------------------------------------------------------------------
# Args
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage 0 ;;
    --restart) DO_RESTART=1; shift ;;
    --services)
      SERVICES="${2:?}"
      shift 2
      ;;
    --generic-lib)
      GENERIC_LIB_DIR="${2:?}"
      shift 2
      ;;
    --skip-generic-lib)
      SYNC_GENERIC_LIB=0
      shift
      ;;
    --branch) BRANCH="${2:?}"; shift 2 ;;
    --port) SSH_PORT="${2:?}"; shift 2 ;;
    --user) SSH_USER="${2:?}"; shift 2 ;;
    --latproc) LATPROC="${2:?}"; shift 2 ;;
    --session) SESSION_NAME="${2:?}"; shift 2 ;;
    --jobs|-j) REMOTE_JOBS="-j${2:?}"; shift 2 ;;
    --hosts)
      HOSTS_FILE="${2:?}"
      shift 2
      ;;
    status|logs|attach|record|upgrade)
      ACTION="$1"; shift
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage 2
      ;;
    *)
      CLI_HOSTS=1
      HOSTS+=("$1")
      HOST_PORTS+=("$SSH_PORT")
      HOST_USERS+=("$SSH_USER")
      shift
      ;;
  esac
done

if [[ "$CLI_HOSTS" -eq 0 ]]; then
  if [[ -z "$HOSTS_FILE" ]]; then
    HOSTS_FILE="$DEFAULT_HOSTS_FILE"
  fi
  load_hosts_file "$HOSTS_FILE"
fi

if [[ ${#HOSTS[@]} -eq 0 ]]; then
  echo "No hosts to act on. Edit ${HOSTS_FILE:-$DEFAULT_HOSTS_FILE} or pass HOST …" >&2
  exit 2
fi

# ssh to HOSTS[i] using matching port/user
ssh_host_at() {
  local idx="$1"; shift
  local host="${HOSTS[$idx]}"
  local port="${HOST_PORTS[$idx]}"
  local user="${HOST_USERS[$idx]}"
  ssh -p "$port" \
    -o BatchMode=yes \
    -o ConnectTimeout=15 \
    -o StrictHostKeyChecking=accept-new \
    "${user}@${host}" "$@"
}

# Resolve host argument to an index (exact match on HOSTS[]), or -1
host_index() {
  local want="$1" i
  for i in "${!HOSTS[@]}"; do
    if [[ "${HOSTS[$i]}" == "$want" ]]; then
      echo "$i"
      return 0
    fi
  done
  echo "-1"
}

# Back-compat helper used when a single host string is enough (attach CLI).
ssh_host() {
  local host="$1"; shift
  local idx port user
  idx="$(host_index "$host")"
  if [[ "$idx" != "-1" ]]; then
    port="${HOST_PORTS[$idx]}"
    user="${HOST_USERS[$idx]}"
  else
    port="$SSH_PORT"
    user="$SSH_USER"
  fi
  ssh -p "$port" \
    -o BatchMode=yes \
    -o ConnectTimeout=15 \
    -o StrictHostKeyChecking=accept-new \
    "${user}@${host}" "$@"
}

# ---------------------------------------------------------------------------
# Remote payload — written to a temp file and uploaded/executed on the host
# ---------------------------------------------------------------------------
# shellcheck disable=SC2016
remote_upgrade_script() {
  cat <<'REMOTE_SCRIPT'
#!/usr/bin/env bash
# Runs on the plant host. Environment injected by the orchestrator:
#   LATPROC BRANCH SESSION_NAME REMOTE_JOBS DO_RESTART
set -euo pipefail

: "${LATPROC:?}" "${BRANCH:?}" "${SESSION_NAME:?}"
REMOTE_JOBS="${REMOTE_JOBS:--j4}"
DO_RESTART="${DO_RESTART:-0}"

log()  { printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"; }
fail() { log "ERROR: $*"; exit 1; }

cd "$LATPROC" || fail "cannot cd $LATPROC"
test -d .git || fail "$LATPROC is not a git checkout"

STAMP="$(date '+%Y%m%d-%H%M%S')"
HOST_SHORT="$(hostname -s 2>/dev/null || hostname)"
LOGDIR="${LATPROC}/log/upgrade-${STAMP}"
mkdir -p "$LOGDIR"
LOGFILE="${LOGDIR}/upgrade.log"
exec > >(tee -a "$LOGFILE") 2>&1

log "=== upgrade start on ${HOST_SHORT} ==="
log "branch=${BRANCH} latproc=${LATPROC} jobs=${REMOTE_JOBS} restart=${DO_RESTART}"

# --- 1. Record pre-upgrade state ---
PRE_HASH="$(git rev-parse HEAD)"
PRE_SHORT="$(git rev-parse --short HEAD)"
PRE_BRANCH="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
PRE_DESCRIBE="$(git describe --always --dirty 2>/dev/null || echo "$PRE_SHORT")"

{
  echo "host=${HOST_SHORT}"
  echo "timestamp=${STAMP}"
  echo "pre_branch=${PRE_BRANCH}"
  echo "pre_hash=${PRE_HASH}"
  echo "pre_short=${PRE_SHORT}"
  echo "pre_describe=${PRE_DESCRIBE}"
  echo "target_branch=${BRANCH}"
} | tee "${LOGDIR}/pre_version.txt"

# Keep a stable "last" pointer for easy rollback lookup
cp -a "${LOGDIR}/pre_version.txt" "${LATPROC}/log/LAST_PRE_UPGRADE.txt"
echo "$PRE_HASH" > "${LATPROC}/log/LAST_PRE_HASH"

log "pre-upgrade HEAD ${PRE_SHORT} (${PRE_HASH}) on ${PRE_BRANCH}"

BINARIES=(
  "${LATPROC}/iod/iod_sdo"
  "${LATPROC}/iod/cw"
  "${LATPROC}/iod/iosh"
  "${LATPROC}/iod/device_connector"
  "${LATPROC}/iod/persistd"
)
PLUGIN_DIR="${LATPROC}/code/plugins"
if [[ -d "$PLUGIN_DIR" ]]; then
  while IFS= read -r -d '' so; do
    BINARIES+=("$so")
  done < <(find "$PLUGIN_DIR" -maxdepth 1 -type f -name '*.so*' -print0 2>/dev/null || true)
fi

sha_manifest() {
  local out="$1"; shift
  : >"$out"
  for f in "$@"; do
    if [[ -f "$f" ]]; then
      sha256sum "$f" >>"$out" || true
      ls -la "$f" >>"${out}.ls" || true
    fi
  done
}

sha_manifest "${LOGDIR}/pre_binaries.sha256" "${BINARIES[@]}"
log "wrote ${LOGDIR}/pre_binaries.sha256"

# --- 2. Snapshot binaries for binary-level rollback ---
SNAPDIR="${LOGDIR}/binary_snapshot"
mkdir -p "$SNAPDIR"
for f in "${BINARIES[@]}"; do
  [[ -f "$f" ]] || continue
  base="$(basename "$f")"
  # In-tree prev- sibling (matches plant habit: iod_sdo.prev-*)
  parent="$(dirname "$f")"
  prev="${parent}/${base}.prev-${PRE_SHORT}"
  cp -a "$f" "$prev"
  cp -a "$f" "${SNAPDIR}/${base}"
  log "snapshot ${base} -> ${prev}"
done
# Also record git tree object for reference
git rev-parse HEAD > "${SNAPDIR}/git_hash"
git log -1 --format='%H%n%s%n%an <%ae>%n%ci' HEAD > "${SNAPDIR}/git_log.txt"

# --- 3. Update sources to branch tip ---
log "git fetch origin"
git fetch origin --prune

if ! git show-ref --verify --quiet "refs/remotes/origin/${BRANCH}"; then
  fail "origin/${BRANCH} not found after fetch"
fi

TIP="$(git rev-parse "origin/${BRANCH}")"
TIP_SHORT="$(git rev-parse --short "origin/${BRANCH}")"
log "origin/${BRANCH} tip is ${TIP_SHORT} (${TIP})"

if [[ "$PRE_HASH" == "$TIP" ]]; then
  log "already at tip ${TIP_SHORT}; still rebuilding to ensure install is current"
else
  log "moving ${PRE_SHORT} -> ${TIP_SHORT}"
fi

# Preserve untracked plant data; reset tracked tree only.
# Local commits on the branch would be discarded — warn if any.
LOCAL_AHEAD="$(git rev-list --count "origin/${BRANCH}..HEAD" 2>/dev/null || echo 0)"
if [[ "${LOCAL_AHEAD}" != "0" ]]; then
  log "WARNING: ${LOCAL_AHEAD} local commit(s) not on origin/${BRANCH} will be left behind by reset"
  git log --oneline "origin/${BRANCH}..HEAD" | tee "${LOGDIR}/local_commits_left_behind.txt" || true
fi

git checkout -B "$BRANCH" "origin/${BRANCH}"
git reset --hard "origin/${BRANCH}"
git clean -fd --exclude=sampling \
  --exclude='iod/*.prev-*' --exclude='iod/*.new-*' \
  --exclude='code/plugins/*.prev-*' \
  --exclude='log/' || true

POST_PULL="$(git rev-parse HEAD)"
log "now at $(git rev-parse --short HEAD) — $(git log -1 --oneline)"

# --- 4. Build + install iod ---
log "building iod (make release-install ${REMOTE_JOBS})"
(
  cd "${LATPROC}/iod"
  # Prefer clean-ish Release rebuild; reuse existing build tree for speed.
  make release-install JOBS="${REMOTE_JOBS}"
)

# --- 5. Rebuild plugins declared in code/lib/generic*.lpc ---
# Only files that contain a PLUGIN "name.so..." line are real C plugins.
# Pure LPC generics are skipped.
build_one_plugin() {
  local src="$1"
  local out name tmpc rc=0
  local -a extra_libs=()

  name="$(
    awk '
      /PLUGIN[[:space:]]+"/ {
        if (match($0, /"[^"]+\.so[^"]*"/)) {
          print substr($0, RSTART+1, RLENGTH-2)
          exit
        }
      }
    ' "$src"
  )"
  [[ -n "$name" ]] || return 0

  log "plugin build: ${src} -> ${name}"
  tmpc="$(mktemp "${TMPDIR:-/tmp}/plugin.XXXXXX.c")"
  awk -v file="$src" '
    /^%END_PLUGIN/ { copy=0 }
    copy == 1 { print }
    $1 ~ /^%BEGIN_PLUGIN/ {
      printf "#line %d \"%s\"\n", NR+1, file
      copy=1
    }
  ' "$src" >"$tmpc"

  if [[ ! -s "$tmpc" ]]; then
    log "WARNING: no %BEGIN_PLUGIN body in ${src}; skipping"
    rm -f "$tmpc"
    return 0
  fi

  # web_request (and anything that pulls curl) needs -lcurl
  if grep -qE 'curl/|exec_web_request|libcurl' "$tmpc" 2>/dev/null; then
    extra_libs+=(-lcurl)
  fi
  if grep -qE 'openssl/|exec_digest|EVP_' "$tmpc" 2>/dev/null; then
    extra_libs+=(-lcrypto)
  fi

  out="${PLUGIN_DIR}/${name}"
  mkdir -p "$PLUGIN_DIR"
  if [[ -f "$out" ]]; then
    cp -a "$out" "${out}.prev-${PRE_SHORT}"
  fi

  # Compile into plugin dir (matches LD_LIBRARY_PATH used by iod run scripts)
  if ! gcc -shared -Wall -fPIC -g \
      -Wl,-soname,"${name}" \
      -I"${LATPROC}/iod/src" \
      -I"${LATPROC}/iod" \
      "$tmpc" -o "$out" \
      -ldl "${extra_libs[@]+"${extra_libs[@]}"}"; then
    log "ERROR: gcc failed for ${name}"
    rm -f "$tmpc"
    return 1
  fi
  chmod 755 "$out"
  rm -f "$tmpc"
  log "installed ${out} ($(stat -c%s "$out" 2>/dev/null || stat -f%z "$out") bytes)"
  return 0
}

PLUGIN_FAIL=0
LIBDIR="${LATPROC}/code/lib"
BUILT_WEB_REQUEST=0
if [[ -d "$LIBDIR" ]]; then
  log "scanning ${LIBDIR}/generic*.lpc for PLUGIN declarations"
  shopt -s nullglob
  for src in "${LIBDIR}"/generic*.lpc "${LIBDIR}"/generic*.cw; do
    if grep -qE 'PLUGIN[[:space:]]+"[^"]+\.so' "$src"; then
      if ! build_one_plugin "$src"; then
        PLUGIN_FAIL=1
      else
        # Track so we do not rebuild the same .so from plugins/web_request.cw
        if [[ "$(basename "$src")" == "generic_webrequest.lpc" ]]; then
          BUILT_WEB_REQUEST=1
        fi
      fi
    else
      log "skip (no PLUGIN): $(basename "$src")"
    fi
  done
  shopt -u nullglob
else
  log "WARNING: ${LIBDIR} missing; no generic plugins rebuilt"
fi

# Legacy plant path if GenericLib file was never installed under code/lib
WR_LEGACY="${LATPROC}/plugins/web_request.cw"
if [[ "$BUILT_WEB_REQUEST" -eq 0 && -f "$WR_LEGACY" ]]; then
  log "rebuilding curl web plugin from legacy ${WR_LEGACY}"
  if ! build_one_plugin "$WR_LEGACY"; then
    PLUGIN_FAIL=1
  fi
elif [[ "$BUILT_WEB_REQUEST" -eq 0 ]]; then
  log "NOTE: no generic_webrequest.lpc / web_request.cw on host; web plugin not rebuilt"
fi

# --- 6. Post-upgrade record ---
POST_HASH="$(git rev-parse HEAD)"
POST_SHORT="$(git rev-parse --short HEAD)"
{
  echo "host=${HOST_SHORT}"
  echo "timestamp=$(date '+%Y%m%d-%H%M%S')"
  echo "post_branch=$(git rev-parse --abbrev-ref HEAD)"
  echo "post_hash=${POST_HASH}"
  echo "post_short=${POST_SHORT}"
  echo "post_describe=$(git describe --always --dirty 2>/dev/null || echo "$POST_SHORT")"
  echo "pre_hash=${PRE_HASH}"
  echo "pre_short=${PRE_SHORT}"
  echo "logdir=${LOGDIR}"
  echo "plugin_fail=${PLUGIN_FAIL}"
} | tee "${LOGDIR}/post_version.txt"

cp -a "${LOGDIR}/post_version.txt" "${LATPROC}/log/LAST_POST_UPGRADE.txt"
echo "$POST_HASH" > "${LATPROC}/log/LAST_POST_HASH"

# Refresh binary list after install
BINARIES_POST=(
  "${LATPROC}/iod/iod_sdo"
  "${LATPROC}/iod/cw"
  "${LATPROC}/iod/iosh"
  "${LATPROC}/iod/device_connector"
  "${LATPROC}/iod/persistd"
)
if [[ -d "$PLUGIN_DIR" ]]; then
  while IFS= read -r -d '' so; do
    BINARIES_POST+=("$so")
  done < <(find "$PLUGIN_DIR" -maxdepth 1 -type f -name '*.so*' ! -name '*.prev-*' -print0 2>/dev/null || true)
fi
sha_manifest "${LOGDIR}/post_binaries.sha256" "${BINARIES_POST[@]}"

# Symlink latest upgrade dir for convenience
ln -sfn "$LOGDIR" "${LATPROC}/log/upgrade-latest"

if [[ "$PLUGIN_FAIL" != "0" ]]; then
  log "=== upgrade FINISHED WITH PLUGIN ERRORS ==="
  log "iod binaries may be current; NOT restarting services"
  echo "FAILED_PLUGINS" > "${LOGDIR}/status"
  exit 1
fi

# --- 7. Optional restart (UP services only; down stays down) ---
# SERVICES env: "all" or comma/space-separated names under /etc/service.
# Only runs after a clean build so we do not bounce plant onto a half-failed install.
if [[ "$DO_RESTART" == "1" ]]; then
  if ! command -v svc >/dev/null 2>&1; then
    log "WARNING: --restart requested but svc not found"
  elif [[ ! -d /etc/service ]]; then
    log "WARNING: --restart requested but /etc/service missing"
  else
    SERVICES="${SERVICES:-all}"
    declare -a want=()
    if [[ "$SERVICES" == "all" || "$SERVICES" == "*" ]]; then
      for d in /etc/service/*; do
        [[ -d "$d" ]] || continue
        # Real supervise service has a run script (or supervise dir); skip
        # bare symlinks like the historic "run" -> iod.sh at service root.
        base="$(basename "$d")"
        if [[ -e "$d/run" || -d "$d/supervise" ]]; then
          want+=("$base")
        else
          log "skip non-service entry: ${base}"
        fi
      done
    else
      # shellcheck disable=SC2206
      want=( ${SERVICES//,/ } )
    fi

    log "service restart pass (only if currently UP): ${want[*]:-(none)}"
    restarted=0
    skipped_down=0
    skipped_missing=0
    for name in "${want[@]}"; do
      [[ -n "$name" ]] || continue
      svcdir="/etc/service/${name}"
      if [[ ! -d "$svcdir" ]]; then
        log "skip missing service: ${name}"
        skipped_missing=$((skipped_missing + 1))
        continue
      fi
      # svstat: "/etc/service/iod: up (pid …)" or "…: down …"
      st="$(svstat "$svcdir" 2>/dev/null || true)"
      if echo "$st" | grep -qE ':[[:space:]]+up[[:space:]]'; then
        log "restart (was up): ${name}  [${st}]"
        svc -t "$svcdir"
        restarted=$((restarted + 1))
      else
        log "leave down: ${name}  [${st:-unknown}]"
        skipped_down=$((skipped_down + 1))
      fi
    done
    sleep 2
    log "post-restart status (restarted=${restarted} left_down=${skipped_down} missing=${skipped_missing}):"
    for name in "${want[@]}"; do
      [[ -d "/etc/service/${name}" ]] || continue
      svstat "/etc/service/${name}" 2>/dev/null || true
    done
  fi
else
  log "not restarting services (pass --restart to orchestrator to enable)"
fi

echo "OK" > "${LOGDIR}/status"
log "=== upgrade OK: ${PRE_SHORT} -> ${POST_SHORT} ==="
log "pre hash file:  ${LATPROC}/log/LAST_PRE_HASH"
log "post hash file: ${LATPROC}/log/LAST_POST_HASH"
log "full log:       ${LOGFILE}"
log "rollback hint:  git checkout ${PRE_HASH}; restore *.prev-${PRE_SHORT}"
REMOTE_SCRIPT
}

# ---------------------------------------------------------------------------
# Session helpers (tmux preferred, else screen)
# ---------------------------------------------------------------------------
remote_start_session() {
  local host="$1"
  local script_b64
  script_b64="$(remote_upgrade_script | gzip -c | base64 | tr -d '\n')"

  ssh_host "$host" bash -s <<EOF
set -euo pipefail
export LATPROC=$(printf '%q' "$LATPROC")
export BRANCH=$(printf '%q' "$BRANCH")
export SESSION_NAME=$(printf '%q' "$SESSION_NAME")
export REMOTE_JOBS=$(printf '%q' "$REMOTE_JOBS")
export DO_RESTART=$(printf '%q' "$DO_RESTART")
export SERVICES=$(printf '%q' "$SERVICES")

mkdir -p "\$LATPROC/log"
PAYLOAD="\$LATPROC/log/${SESSION_NAME}-worker.sh"
echo '${script_b64}' | base64 -d | gzip -dc > "\$PAYLOAD"
chmod 700 "\$PAYLOAD"

# Already running?
if command -v tmux >/dev/null 2>&1; then
  if tmux has-session -t "\$SESSION_NAME" 2>/dev/null; then
    echo "session '\$SESSION_NAME' already running on \$(hostname -s)"
    echo "  attach: tmux attach -t \$SESSION_NAME"
    echo "  or: $0 attach ${host}"
    exit 0
  fi
  tmux new-session -d -s "\$SESSION_NAME" \\
    "env LATPROC=\$LATPROC BRANCH=\$BRANCH SESSION_NAME=\$SESSION_NAME REMOTE_JOBS=\$REMOTE_JOBS DO_RESTART=\$DO_RESTART SERVICES=\$SERVICES bash \$PAYLOAD; echo EXIT:\$?; sleep 5"
  echo "started tmux session '\$SESSION_NAME' on \$(hostname -s)"
  echo "  attach: tmux attach -t \$SESSION_NAME"
elif command -v screen >/dev/null 2>&1; then
  if screen -list 2>/dev/null | grep -q "[.]\$SESSION_NAME[[:space:]]"; then
    echo "screen session '\$SESSION_NAME' already running on \$(hostname -s)"
    exit 0
  fi
  screen -dmS "\$SESSION_NAME" \\
    env LATPROC="\$LATPROC" BRANCH="\$BRANCH" SESSION_NAME="\$SESSION_NAME" \\
        REMOTE_JOBS="\$REMOTE_JOBS" DO_RESTART="\$DO_RESTART" SERVICES="\$SERVICES" \\
        bash "\$PAYLOAD"
  echo "started screen session '\$SESSION_NAME' on \$(hostname -s)"
  echo "  attach: screen -r \$SESSION_NAME"
else
  echo "ERROR: neither tmux nor screen is installed" >&2
  exit 1
fi
EOF
}

remote_status() {
  local host="$1"
  echo "---- ${host} ----"
  ssh_host "$host" bash -s <<EOF || echo "(ssh failed)"
set +e
LATPROC=$(printf '%q' "$LATPROC")
SESSION_NAME=$(printf '%q' "$SESSION_NAME")
echo -n "host: "; hostname -s
if [[ -f "\$LATPROC/log/LAST_PRE_HASH" ]]; then
  echo "pre:  \$(cat \$LATPROC/log/LAST_PRE_HASH) (\$(git -C \$LATPROC rev-parse --short \$(cat \$LATPROC/log/LAST_PRE_HASH) 2>/dev/null))"
fi
if [[ -f "\$LATPROC/log/LAST_POST_HASH" ]]; then
  echo "post: \$(cat \$LATPROC/log/LAST_POST_HASH) (\$(git -C \$LATPROC rev-parse --short \$(cat \$LATPROC/log/LAST_POST_HASH) 2>/dev/null))"
fi
echo -n "HEAD: "; git -C "\$LATPROC" rev-parse --short HEAD 2>/dev/null; git -C "\$LATPROC" log -1 --oneline 2>/dev/null
if command -v tmux >/dev/null 2>&1 && tmux has-session -t "\$SESSION_NAME" 2>/dev/null; then
  echo "session: tmux '\$SESSION_NAME' RUNNING"
  tmux capture-pane -pt "\$SESSION_NAME" -S -20 2>/dev/null | tail -20
elif command -v screen >/dev/null 2>&1 && screen -list 2>/dev/null | grep -q "[.]\$SESSION_NAME[[:space:]]"; then
  echo "session: screen '\$SESSION_NAME' RUNNING"
else
  echo "session: not running"
fi
if [[ -L "\$LATPROC/log/upgrade-latest" || -d "\$LATPROC/log/upgrade-latest" ]]; then
  L=\$(readlink -f "\$LATPROC/log/upgrade-latest" 2>/dev/null || echo "\$LATPROC/log/upgrade-latest")
  echo "latest: \$L"
  [[ -f "\$L/status" ]] && echo "status: \$(cat \$L/status)"
  [[ -f "\$L/upgrade.log" ]] && echo "--- log tail ---" && tail -15 "\$L/upgrade.log"
fi
if command -v svstat >/dev/null 2>&1 && [[ -d /etc/service/iod ]]; then
  svstat /etc/service/iod 2>/dev/null
fi
EOF
}

remote_logs() {
  local host="$1"
  echo "---- ${host} ----"
  ssh_host "$host" bash -s <<EOF || true
LATPROC=$(printf '%q' "$LATPROC")
if [[ -f "\$LATPROC/log/upgrade-latest/upgrade.log" ]]; then
  tail -n 80 "\$LATPROC/log/upgrade-latest/upgrade.log"
else
  ls -lt "\$LATPROC/log"/upgrade-*/upgrade.log 2>/dev/null | head -3
  latest=\$(ls -1dt "\$LATPROC/log"/upgrade-* 2>/dev/null | head -1)
  [[ -n "\$latest" ]] && tail -n 80 "\$latest/upgrade.log"
fi
EOF
}

remote_record() {
  local host="$1"
  echo "---- ${host} ----"
  ssh_host "$host" bash -s <<EOF || true
LATPROC=$(printf '%q' "$LATPROC")
echo "== LAST_PRE_UPGRADE =="
cat "\$LATPROC/log/LAST_PRE_UPGRADE.txt" 2>/dev/null || echo "(none)"
echo "== LAST_POST_UPGRADE =="
cat "\$LATPROC/log/LAST_POST_UPGRADE.txt" 2>/dev/null || echo "(none)"
echo "== current HEAD =="
git -C "\$LATPROC" log -1 --format='%H %ci %s' 2>/dev/null
echo "== prev binary siblings (iod) =="
ls -la "\$LATPROC/iod"/*.prev-* 2>/dev/null | tail -20 || echo "(none)"
echo "== prev plugins =="
ls -la "\$LATPROC/code/plugins"/*.prev-* 2>/dev/null | tail -20 || echo "(none)"
EOF
}

remote_attach() {
  local host="$1"
  local idx port user
  idx="$(host_index "$host")"
  if [[ "$idx" != "-1" ]]; then
    port="${HOST_PORTS[$idx]}"
    user="${HOST_USERS[$idx]}"
  else
    port="$SSH_PORT"
    user="$SSH_USER"
  fi
  echo "Attaching to ${user}@${host}:${port} session '${SESSION_NAME}' (detach: tmux Ctrl-b d / screen Ctrl-a d)..."
  # Interactive: no BatchMode
  ssh -t -p "$port" "${user}@${host}" \
    "if command -v tmux >/dev/null && tmux has-session -t ${SESSION_NAME} 2>/dev/null; then
       exec tmux attach -t ${SESSION_NAME}
     elif command -v screen >/dev/null; then
       exec screen -r ${SESSION_NAME}
     else
       echo 'no session found'; exit 1
     fi"
}

hosts_summary() {
  local i
  for i in "${!HOSTS[@]}"; do
    printf '  %s  (port %s, user %s)\n' "${HOSTS[$i]}" "${HOST_PORTS[$i]}" "${HOST_USERS[$i]}"
  done
}

# Local file digest (macOS shasum / Linux sha256sum)
local_sha256() {
  local f="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | awk '{print $1}'
  else
    shasum -a 256 "$f" | awk '{print $1}'
  fi
}

# Discover GenericLib PLUGIN LPC sources we push to plant (web + system_exec).
# A file qualifies if it declares PLUGIN "….so…" and matches one of the
# known plugin families below.
list_generic_lib_plugin_sources() {
  local dir="$1"
  local f
  shopt -s nullglob
  for f in \
      "$dir"/generic_web*.lpc "$dir"/generic_web*.cw \
      "$dir"/*webrequest*.lpc "$dir"/*web_request*.lpc "$dir"/*web_request*.cw \
      "$dir"/generic_system_exec.lpc "$dir"/generic_system_exec.cw \
      "$dir"/*system_exec*.lpc "$dir"/*system_exec*.cw
  do
    [[ -f "$f" ]] || continue
    if ! grep -qE 'PLUGIN[[:space:]]+"[^"]+\.so' "$f"; then
      continue
    fi
    # Curl / web_request family
    if grep -qE 'exec_web_request|curl/|libcurl|web_request\.so' "$f"; then
      printf '%s\n' "$f"
      continue
    fi
    # system_exec family (inline GenericLib body or so name)
    if grep -qE 'system_exec\.so|exec_command\(|SYSTEMEXEC|SystemExec' "$f"; then
      # Prefer the GenericLib plant source name; skip accidental matches
      case "$(basename "$f")" in
        *system_exec*|*SystemExec*|*systemexec*)
          printf '%s\n' "$f"
          ;;
      esac
    fi
  done | sort -u
  shopt -u nullglob
}

# Push GenericLib PLUGIN LPC to one host when plant copy is missing or differs.
# Writes: code/lib/<basename>
# Extra mirror: generic_webrequest.lpc -> plugins/web_request.cw (legacy path).
# Does NOT overwrite plugins/system_exec.cw (latproc stub differs from GenericLib).
sync_generic_lib_plugins_to_host() {
  local host="$1"
  local idx port user src base local_hash remote_hash dests d updated=0

  if [[ "$SYNC_GENERIC_LIB" != "1" ]]; then
    echo "  skip GenericLib plugin sync (--skip-generic-lib)"
    return 0
  fi

  if [[ ! -d "$GENERIC_LIB_DIR" ]]; then
    echo "  WARNING: GENERIC_LIB_DIR missing: $GENERIC_LIB_DIR (skip plugin sync)" >&2
    return 0
  fi

  idx="$(host_index "$host")"
  if [[ "$idx" != "-1" ]]; then
    port="${HOST_PORTS[$idx]}"
    user="${HOST_USERS[$idx]}"
  else
    port="$SSH_PORT"
    user="$SSH_USER"
  fi

  sources=()
  while IFS= read -r src || [[ -n "$src" ]]; do
    [[ -n "$src" ]] && sources+=("$src")
  done < <(list_generic_lib_plugin_sources "$GENERIC_LIB_DIR")

  if [[ ${#sources[@]} -eq 0 ]]; then
    echo "  no GenericLib plugin sources found in $GENERIC_LIB_DIR"
    return 0
  fi

  echo "  candidates: $(printf '%s ' "${sources[@]##*/}")"

  for src in "${sources[@]}"; do
    base="$(basename "$src")"
    local_hash="$(local_sha256 "$src")"
    # Primary plant path (GenericLib layout under code/lib)
    dests="${LATPROC}/code/lib/${base}"
    # Keep legacy clockwork path in lock-step with GenericLib webrequest only.
    if [[ "$base" == "generic_webrequest.lpc" ]]; then
      dests="${dests}"$'\n'"${LATPROC}/plugins/web_request.cw"
    fi

    while IFS= read -r d || [[ -n "$d" ]]; do
      [[ -n "$d" ]] || continue
      remote_hash="$(
        ssh -p "$port" -o BatchMode=yes -o ConnectTimeout=15 \
          "${user}@${host}" \
          "if [ -f $(printf '%q' "$d") ]; then
             sha256sum $(printf '%q' "$d") 2>/dev/null | awk '{print \$1}'
           fi" 2>/dev/null || true
      )"
      remote_hash="$(echo "$remote_hash" | tr -d '[:space:]')"
      if [[ -n "$remote_hash" && "$remote_hash" == "$local_hash" ]]; then
        echo "  up-to-date: ${d}  (${local_hash:0:12}…)"
        continue
      fi
      if [[ -z "$remote_hash" ]]; then
        echo "  install:  ${src} -> ${user}@${host}:${d}"
      else
        echo "  update:   ${src} -> ${user}@${host}:${d}"
        echo "            local ${local_hash:0:12}…  remote ${remote_hash:0:12}…"
      fi
      ssh -p "$port" -o BatchMode=yes "${user}@${host}" \
        "mkdir -p $(printf '%q' "$(dirname "$d")")"
      # Backup existing plant file before overwrite
      ssh -p "$port" -o BatchMode=yes "${user}@${host}" \
        "if [ -f $(printf '%q' "$d") ]; then
           cp -a $(printf '%q' "$d") $(printf '%q' "${d}.prev-genericlib")
         fi"
      scp -P "$port" -o BatchMode=yes -q "$src" "${user}@${host}:${d}"
      updated=$((updated + 1))
    done <<< "$dests"
  done

  echo "  GenericLib plugin sync: ${updated} file(s) written on ${host}"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
case "$ACTION" in
  upgrade)
    echo "Upgrading hosts to origin/${BRANCH}"
    if [[ "$CLI_HOSTS" -eq 1 ]]; then
      echo "  hosts source: command line"
    else
      echo "  hosts file:   ${HOSTS_FILE}"
    fi
    hosts_summary
    echo "  latproc:      ${LATPROC}"
    echo "  jobs:         ${REMOTE_JOBS}"
    echo "  restart:      ${DO_RESTART}  (services=${SERVICES}; only UP ones are bounced)"
    if [[ "$SYNC_GENERIC_LIB" == "1" ]]; then
      echo "  generic-lib:  ${GENERIC_LIB_DIR}"
    else
      echo "  generic-lib:  (skipped)"
    fi
    echo
    for host in "${HOSTS[@]}"; do
      echo ">>> ${host}"
      echo "--- GenericLib plugins (webrequest + system_exec) ---"
      sync_generic_lib_plugins_to_host "$host"
      echo "--- remote build session ---"
      remote_start_session "$host"
      echo
    done
    echo "Jobs launched in detached sessions named '${SESSION_NAME}'."
    echo "  Monitor:  $0 status"
    echo "  Logs:     $0 logs"
    echo "  Attach:   $0 attach <host>"
    echo "  Record:   $0 record"
    if [[ "$DO_RESTART" != "1" ]]; then
      echo
      echo "Note: services are NOT restarted. Re-run with --restart when ready, or:"
      echo "  # bounce every currently-up service under /etc/service"
      echo "  ssh -p PORT USER@HOST 'for s in /etc/service/*/; do svstat \"\$s\" | grep -q \" up \" && svc -t \"\$s\"; done'"
    fi
    ;;
  status)
    for host in "${HOSTS[@]}"; do remote_status "$host"; echo; done
    ;;
  logs)
    for host in "${HOSTS[@]}"; do remote_logs "$host"; echo; done
    ;;
  record)
    for host in "${HOSTS[@]}"; do remote_record "$host"; echo; done
    ;;
  attach)
    # attach must name exactly one host (never pick the first of a multi-host list)
    if [[ "$CLI_HOSTS" -ne 1 || ${#HOSTS[@]} -ne 1 ]]; then
      echo "attach requires exactly one host, e.g. $0 attach 172.29.51.1" >&2
      echo "Known hosts:" >&2
      hosts_summary >&2
      exit 2
    fi
    remote_attach "${HOSTS[0]}"
    ;;
  *)
    echo "Unknown action: $ACTION" >&2
    usage 2
    ;;
esac
