#!/usr/bin/bash
set -eu

root=${1:-}
list=${2:-}
if [[ -z $root || -z $list ]]; then
  echo "usage: check-copies.sh <qml dir> <list>" >&2
  exit 2
fi

fail=0
count=0
while read -r want name commit source; do
  [[ -z $want ]] && continue
  file=$root/$name
  if [[ ! -f $file ]]; then
    echo "missing $name" >&2
    fail=1
    continue
  fi
  got=$(sha256sum "$file" | cut -d' ' -f1)
  count=$((count + 1))
  if [[ $got != "$want" ]]; then
    echo "$name differs from $commit:$source" >&2
    echo "  want $want" >&2
    echo "  got  $got" >&2
    fail=1
  fi
done < "$list"
echo "copies: $count checked, $fail failed"
exit $fail
