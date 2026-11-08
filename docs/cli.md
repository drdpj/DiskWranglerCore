# The command-line programs

`acorndw` (Acorn Disk Wrangler) and `cpmdw` (CP/M Disk Wrangler) are console programs built from the same code as the
graphical ones: the same detection, the same filesystem classes, the same checked save. Everything they do is the
`dw::cli` layer of DiskWranglerCore (`diskwrangler/cli/`, QtCore only: no display is needed); the two programs differ
only in their `Backend` (how an image is opened, how a blank one is made, how host files become items and back).

```
acorndw [GLOBAL OPTIONS] COMMAND [OPTIONS] ARGS...
```

Global options may come before or after the command word.

## Global options

| Option | Meaning |
| --- | --- |
| `--json` | One JSON document on stdout (below); nothing else is written there |
| `--dry-run` | Do everything, including every check, but write nothing |
| `--vol=NAME` | Which volume of the image (`adfs`, `drive0`, `drive1`, `dos`, `a`...). `all` for `ls` and `info` |
| `--fs=ID` | Read the image as this filesystem, whatever detection says (the ids `info` lists) |
| `--backup` | Keep the old file as `NAME.bak` when saving |
| `-q`, `--quiet` | No output on success |
| `--sides=auto\|interleaved\|sequential` | `acorndw`: the side order of a double-sided image |
| `-f NAME`, `--format=NAME` | `cpmdw`: a library diskdef; on `new`, the format to make |
| `--diskdefs=FILE` | `cpmdw`: more diskdefs (repeatable) |
| `--charset=NAME` | `cpmdw`: the character set for names and text |
| `--version`, `-h`, `--help` | |

## Commands

An image and a path are separate arguments, never `image:path` (the colon clashes with Windows drive letters and ADFS
`:0`). Paths are written as in the program's GUI: `$.Games.Elite` or `Games.Elite` on ADFS (a dot separates the
levels, and `/` is an ordinary character in an ADFS name), `A.PROG` on DFS, `/GAMES/ELITE.COM` (either slash) on FAT,
`0:FILE.TXT` or `FILE.TXT` (user 0) on CP/M. On CP/M `3:` is a user area, `*:FILE.TXT` is that name in every user area,
`*:` every user area, and the root (`/` or nothing) is the whole disc. Wildcards `*` and `?` expand in `get`, `rm`,
`attr`, `ls`.

| Command | Does |
| --- | --- |
| `info IMAGE` | The container, the detection result (with the other readings), the volumes, free space |
| `check IMAGE [--strict]` | Check every filesystem; exit 4 if one fails (`--strict`: warnings too) |
| `ls IMAGE [PATH] [-R] [-d]` | List a directory (`-R` the tree, `-d` the directory itself) |
| `get IMAGE PATH... HOSTDIR` | Extract; the last argument is the host folder (`-` writes one file to stdout) |
| `put IMAGE HOST... DIR` | Import host files and folders into DIR |
| `rm IMAGE PATH... [-r] [--force]` | Delete (`-r` for directories, `--force` for locked files) |
| `mkdir IMAGE PATH... [-p]` | Make directories (`-p`: and the ones above) |
| `mv IMAGE FROM TO` | Rename, or move where the filesystem can |
| `attr IMAGE PATH... [--set KEY=VALUE]...` | Show or change attributes |
| `title IMAGE [TEXT]`, `boot IMAGE [OPTION]` | Show or set the disc title (label) and boot option |
| `new FILE --format=ID [...]` | Make a blank image (`new --list` lists the formats) |
| `convert IMAGE [OUT]` | Write the image to another container, or change its side order in place |
| `copy SRC DEST PATH...` | Copy between images (or inside one), mapping names like drag and drop |
| `script IMAGE [FILE]` | Run many commands on one image with one save; `-` or no file reads stdin |

Options of the transfer commands: `--replace` and `--skip` (what to do when a name exists: neither means exit 5),
`--letters=dirs|flatten` (DFS items arriving on ADFS), `--force` (replace a locked or read-only file),
`--sidecars=inf|none` (`acorndw`), `--retro=FILE` (a retro-files manifest or JSON, repeatable), `--manifest=FILE`
(`get` writes one), `--user-folders=auto|on|off` and `--trim` (`cpmdw get`).

A name that clashes within one `put` (after the program's name rules) gets `~1`, `~2`... as in the GUI; the run says so
in `warnings` and in `renamed`.

`cpmdw`: a disc whose format isn't confirmed (an empty one, or one found by search) opens read-only, so every command
that changes it, and every command on a blank disc made by `new`, needs `--format=NAME`. `new --list` gives each
format's `id`, which is what `--format` takes. `--stamps` (no value) makes a CP/M 3 disc with time stamps, and
`--label=TEXT` needs it. `get --user-folders=auto` makes `userN` folders only when the items come from more than one
user area; `put` reads `user0`..`user15` folders as user areas.

## One invocation, one transaction

A run opens the image, applies its operations as one transaction and saves once, through the same checked copy, read
back and rename as the GUI. If any operation fails the run is abandoned: the image is byte-identical to before
(`script` stops at the first failing line). `--dry-run` runs every check, including `validateAll`, and writes nothing.

`SOURCE_DATE_EPOCH` makes a run repeatable: the clock that stamps FAT entries, ImageDisk headers and CP/M 3 stamps is
set to it (`dw::now`). Host file dates are the files' own.

## JSON (schema 1)

```json
{
  "schema": 1, "tool": "acorndw", "command": "put", "image": "game.ssd", "vol": "drive0",
  "ok": true, "exit": 0, "dryRun": false, "saved": true,
  "result": { },
  "changes": [ { "op": "add", "vol": "drive0", "path": "$.ELITE", "from": "elite", "note": "" } ],
  "warnings": [],
  "error": null
}
```

On failure `ok` is false, `exit` is the code and `error` is `{code, kind, message, path}` (`kind` is the name below). `vol` is
null when the command touched no volume. `result` depends on the command (`info`: `container`, `app`, `verdict`,
`candidates`, `volumes`; `ls`: `entries`; `script`: `lines`, each with its own `exit`, `changes` and `error`).
A unix-style `get -` can't be combined with `--json`.

## Exit codes

| Code | Kind | Meaning |
| --- | --- | --- |
| 0 | ok | |
| 1 | failed | The operation failed (disc full, bad name, unreadable file) |
| 2 | usage | Wrong arguments |
| 3 | not-found | The image, a path on it, or a host file isn't there |
| 4 | check | The disc fails its check, so nothing was written |
| 5 | conflict | A name exists and neither `--replace` nor `--skip` was given |
| 6 | unsupported | The filesystem or container can't do that (a move between FAT directories; TeleDisk can't be written) |
| 7 | foreign | The disc belongs to the other Wrangler (`info` names it; the program never launches it) |
| 8 | ambiguous | Two readings are equally likely: say `--fs=ID` |
| 9 | unrecognised | Nothing recognised the disc (`--fs=ID` tries a reading anyway) |
| 10 | save | The checked save failed; the original is untouched |

## Writing a Backend

`dw::runCli(argc, argv, info)` is the whole of `main`. `info.backend` implements `dw::cli::Backend` (`open`, `create`,
`formats`, `hostPolicy`, `readHost`, and optionally `prepareIncoming` and `configureTarget`) and `OpenedImage` (the
`Disc`, `save`, `app()`, and optionally `convertTo`, `convertSides`, `setCharset`). `tests/clitest.cpp` is a complete
small example (a FAT backend in 60 lines). The qmake include is `diskwrangler-cli.pri`; CMake links `DiskWrangler::Cli`.
