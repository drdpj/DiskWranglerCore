#!/usr/bin/env bash
# Part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler.
# Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
#
# core/ci/source-archive.sh <output dir> <name>
#
# Writes <output dir>/<name>.tar.gz: the committed source of the program in the current folder (its source root)
# and of every submodule in it, in one folder called <name>. GitHub's own "Source code" archives of a release leave
# the submodules out, so without this a release would not carry the whole of the source the GPL asks for. The
# release job runs it after a checkout with submodules, and attaches the result to the release.
set -euo pipefail
out=$(cd "${1:?usage: source-archive.sh <output dir> <name>}" && pwd)
name=${2:?usage: source-archive.sh <output dir> <name>}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git archive --prefix="$name/" HEAD | tar -x -C "$tmp"
# each submodule's committed files into its (empty) folder; displaypath is relative to this source root
export DW_ARCHIVE_ROOT="$tmp/$name"
git submodule foreach --quiet --recursive \
    'mkdir -p "$DW_ARCHIVE_ROOT/$displaypath" && git archive HEAD | tar -x -C "$DW_ARCHIVE_ROOT/$displaypath"'
tar -C "$tmp" -czf "$out/$name.tar.gz" "$name"
