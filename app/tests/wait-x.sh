#!/usr/bin/bash
set -eu

waitx=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin" "$tmp/sock"
cat > "$tmp/bin/xprop" <<'STUB'
#!/usr/bin/bash
echo "$*" >> "$XPROP_LOG"
(( $(wc -l < "$XPROP_LOG") > $(< "$XPROP_FAILS") ))
STUB
chmod +x "$tmp/bin/xprop"
export PATH=$tmp/bin:$PATH MICROSET_X_SOCKET_DIR=$tmp/sock DISPLAY=:7 XPROP_LOG=$tmp/xprop.log XPROP_FAILS=$tmp/fails

failed=0
fail() { echo "FAIL: $*" >&2; failed=1; }
ms() { local up; read -r up _ < /proc/uptime; echo $(( ${up%.*} * 1000 + 10#${up#*.}0 )); }
run() {
  : > "$XPROP_LOG"
  echo "$1" > "$XPROP_FAILS"
  rc=0
  local start
  start=$(ms)
  MICROSET_WAIT_X_MS=$2 "$waitx" 2> "$tmp/err" || rc=$?
  took=$(( $(ms) - start ))
  calls=$(wc -l < "$XPROP_LOG")
}

( sleep 0.5; : > "$tmp/sock/X7" ) &
run 0 5000
wait
(( rc == 0 )) || fail "socket created later: exit $rc"
(( took >= 400 )) || fail "socket created later: returned after $took ms"
[[ $(head -n 1 "$XPROP_LOG") == "-root -display :7" ]] || fail "xprop arguments: $(head -n 1 "$XPROP_LOG")"

run 0 5000
(( rc == 0 && calls == 1 )) || fail "socket present: exit $rc, $calls xprop calls"
(( took < 400 )) || fail "socket present: took $took ms"

run 2 5000
(( rc == 0 && calls == 3 )) || fail "server up on the third try: exit $rc, $calls xprop calls"

rm -f "$tmp/sock/X7"
run 0 600
(( rc == 1 )) || fail "no socket: exit $rc"
(( took >= 600 )) || fail "no socket: gave up after $took ms"
(( calls == 0 )) || fail "no socket: $calls xprop calls"
grep -qx "microset-wait-x: no X server on :7 after 600 ms" "$tmp/err" || fail "no socket: $(cat "$tmp/err")"

: > "$tmp/sock/X7"
run 1000 600
(( rc == 1 && calls >= 2 )) || fail "socket without a server: exit $rc, $calls xprop calls"

: > "$tmp/sock/X0"
: > "$XPROP_LOG"
echo 0 > "$XPROP_FAILS"
rc=0
env -u DISPLAY MICROSET_WAIT_X_MS=600 "$waitx" 2> /dev/null || rc=$?
(( rc == 0 )) || fail "default display: exit $rc"
[[ $(head -n 1 "$XPROP_LOG") == "-root -display :0" ]] || fail "default display: $(head -n 1 "$XPROP_LOG")"

(( failed == 0 )) || exit 1
echo "wait-x: ok"
