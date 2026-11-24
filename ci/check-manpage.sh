#!/bin/bash
# Part of DiskWranglerCore, used by Acorn Disk Wrangler, CP/M Disk Wrangler and FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
#
# core/ci/check-manpage.sh <console program> <manual page>
#
# Checks a console program's manual page (man/<tool>.1, man(7) macros) against the program itself, so the page can't
# drift from what the program does:
#   - groff formats it with no warnings (-ww), and lexgrog finds its NAME line (what whatis and apropos read);
#   - every command '<tool> --help' lists has a section of its own, a line ".SS <command>";
#   - every option the program takes - the global ones from '--help', each command's own from '<tool> COMMAND --help'
#     (core v1.5.0) - is in the page, written as roff writes dashes (\-\-name, and \-x for a short form);
#   - every \-\-name in the page is an option of this program (no stale or borrowed options).
# Prints what is wrong and exits 1, or prints "ok <page>: N commands, M options" and exits 0.
set -uo pipefail
tool=${1:?usage: check-manpage.sh <console program> <manual page>}
page=${2:?usage: check-manpage.sh <console program> <manual page>}
[ -x "$tool" ] || { echo "check-manpage.sh: $tool is not a program"; exit 2; }
[ -f "$page" ] || { echo "check-manpage.sh: no manual page $page"; exit 1; }
fail=0
bad() { echo "FAIL $page: $*"; fail=1; }

warn=$(LC_ALL=C.UTF-8 groff -man -ww -z -t "$page" 2>&1)
[ -z "$warn" ] || bad "groff warns: $warn"
if command -v lexgrog >/dev/null 2>&1; then
    lexgrog "$page" >/dev/null 2>&1 || bad "lexgrog can't parse its NAME section"
fi

help=$("$tool" --help) || { bad "'$tool --help' failed"; exit 1; }
cmds=$(printf '%s\n' "$help" | sed -n 's/^  \([a-z][a-z0-9-]*\) .*/\1/p')
[ -n "$cmds" ] || bad "'$tool --help' lists no commands"
opts=$(printf '%s\n' "$help" | sed -n 's/^global options: //p' | grep -o -- '--[a-z][a-z0-9-]*\|-[a-zA-Z]\b' )
for c in $cmds; do
    grep -qx "\.SS $c" "$page" || bad "no section '.SS $c' for the command $c"
    ch=$("$tool" "$c" --help) || { bad "'$tool $c --help' failed"; continue; }
    opts+=$'\n'$(printf '%s\n' "$ch" | sed -n 's/^options: //p' | grep -o -- '--[a-z][a-z0-9-]*\|-[a-zA-Z],' | tr -d ,)
done
opts=$(printf '%s\n' "$opts" | sed '/^$/d' | sort -u)
for o in $opts; do
    roff=$(printf '%s' "$o" | sed 's/-/\\-/g')                   # --dry-run -> \-\-dry\-run
    grep -qF -- "$roff" "$page" || bad "the option $o isn't in the page (as $roff)"
done
# the other way: every long option the page names is one this program takes
for o in $(grep -o -- '\\-\\-[a-z][a-z0-9\\-]*[a-z0-9]' "$page" | sed 's/\\-/-/g' | sort -u); do
    printf '%s\n' "$opts" | grep -qx -- "$o" || bad "the page names $o, which $(basename "$tool") doesn't take"
done
[ $fail = 0 ] && echo "ok $page: $(printf '%s\n' "$cmds" | wc -l) commands, $(printf '%s\n' "$opts" | wc -l) options"
exit $fail
