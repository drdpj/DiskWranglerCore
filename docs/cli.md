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

### Text output

Text output is for people and is not a stable interface: scripts use `--json`. The JSON keeps every existing field
as it is; the changes below add fields and change only the wording of messages and of CP/M's `why`. Text output is
plain ASCII and is left out with `-q`.

#### `ls` text layout

Each volume listed starts with a header line:

    VOL: LABEL "TITLE", USED used, FREE free[, read-only]

VOL is the volume's name (what `--vol` takes: `adfs`, `dos`, `side0`, `cpm`), LABEL its label, USED and FREE in
bytes. `"TITLE"` is left out when the volume has no title or it is empty; `, read-only` is added when the volume
can't be written, for whatever reason. `cpmdw` adds `(detected - see info)` after the label (`cpm: CP/M (detected - see info), ...`) when the disc's layout is not in its library: the reading is the program's own, and `info` shows it. There is one header per volume (so `--vol=all` gives one per volume), not one
per directory under `-R`. `-d` prints no header.

Then one line per entry, with the fields in this order:

    NAME  TYPE  ATTRIBUTES  SIZE  DATE

- **NAME** is the path as the volume writes it (`displayPath`): `0:PIP.COM` on CP/M, `$.Games.Elite` on ADFS. It is
  left-aligned and padded to the widest name in the listing. With `--vol=all` it starts with the volume's name and
  `:` (`adfs:$.Games`, `cpm:0:PIP.COM`), the name `--vol` takes (as in the `vol` of each JSON entry), not the label.
- **TYPE** is `D` for a directory and blank for a file. The column is left out when the listing has no directories.
  (CP/M user areas are not entries in a listing: their files are listed with the user number in the name.)
- **ATTRIBUTES** are the yes/no file attributes set on the entry, one letter each, in a fixed order per filesystem:
  ADFS `L` (locked), `W` (write), `R` (read), `E` (execute only), the order `*INFO` and the `.inf` access string use,
  so a plain file shows `WR` and a locked one `LWR`; a directory shows only `L`. CP/M `R` (read-only), `S` (system),
  `A` (archive); F1-F4 are not shown (see `attr`). DFS `L`. FAT `R` (read-only), `H` (hidden), `S` (system), `A`
  (archive). The letters come from each attribute's `AttrSpec` (a new one-letter field, not the `sample` width hint),
  only for attributes shown as list columns (`column`), in the order the volume lists its attributes, except where a
  volume overrides that order (ADFS lists them R W L E, so it overrides to L W R E; its spec order, and with it JSON's
  `fileAttrSpecs`, stays as it is). The column is as wide as the widest entry or as the filesystem has letters,
  whichever is more, and it stays when no entry has any set (so a CP/M listing keeps its shape).
- **SIZE** is in bytes, right-aligned, padded to the widest size in the listing. A directory's is blank. Where the
  filesystem knows the size only to a record (`sizeExact` false: a CP/M file with no exact byte count), it is shown
  with `~` before it (`~1536`); the `~` counts in the width, so the numbers still line up on the right. CP/M shows
  the plain number, as the program's own list does (`Volume::marksInexactSizes` is false for it), so it never has `~`.
- **DATE** is `yyyy-MM-dd HH:mm`. It is shown only when the entry has one (an ADFS file whose load and exec addresses
  carry a date stamp, a FAT file, a CP/M 3 file on a disc with time stamps). The column is left out when no entry has
  one.

A column with no value in any entry is left out, except ATTRIBUTES where the filesystem has attributes to show. Columns are
separated by two spaces, and trailing spaces are trimmed.

Without `-R`, `ls` lists only the named directory, or the root when no path is given. `-R` lists every directory below
it, depth first, each directory followed by its contents. `-d` shows the named node itself.

Entries are sorted by name, ignoring case, with directories and files mixed (as `*CAT` does); on CP/M by user number,
then name. Under `-R` the sort applies within each directory. JSON keeps the order in which the volume lists them.

Example of the CP/M layout (illustrative):

    cpm: CP/M, 39936 used, 206848 free
    0:PAUSE.Z8K      13952
    0:XCON.Z8K   R   19584
    0:ZCC.Z8K        17664
    3:NOTES.TXT       1536

Example of the ADFS layout (illustrative):

    adfs: ADFS "Games", 27648 used, 627712 free
    $.Games        D  L
    $.Games.Elite     WR  20480  2026-03-02 14:12

#### `info` text layout

`info` prints these sections, in this order:

1. **Container**: `container: <kind>, <size> bytes`, with `, read-only` when the file can't be written (TeleDisk, or
   a file without write permission).
2. **Detection**: `detection: <verdict>: <description> (<tier>)`, the chosen reading's notes indented below it, then
   one indented `also: <description> (<tier>)` line for each other reading. The other readings are what `--fs=ID`
   chooses between after exit 8, so they stay in the text.
3. **Format**, by program:
   - `cpmdw`: `format: <name> (<origin>)`, then the format summary indented; a layout in no library (named `detected`) is
     `format: custom, not in the library`. The origin is what the format library
     records: `built-in` or `cpmtools` (the two parts of the library), the path of a `--diskdefs` file, or `detected`
     (a layout worked out from the disc, in no library).
   - `acorndw`: `sides: <order>, <how>` for a double-sided image, where `<order>` is `interleaved` or `sequential`
     and `<how>` is `detected`, `assumed` (couldn't be detected, so the format's default) or `forced` (`--sides`).
     Nothing for a single-sided image.
4. **Reading**: `reading: <confidence>, safe to write` or `reading: <confidence>, read-only`, followed by the reason
   in one sentence, indented, then the instruction (see Read-only messages) when it is read-only. `<confidence>` is
   CP/M's `high`, `medium`, `low` or `none`; `acorndw` uses the chosen reading's tier (`certain`, `likely`,
   `possible`).
5. **Definition** (`cpmdw`), only when the reading's layout is not in the library (`fromLibrary` false): the diskdefs
   text from `Format::toDiskdef()`, indented, with the instruction below it.
6. **Volumes**: one block per mount: `volume <name>: <label> (<fsId>)`, with `, read-only` and the reason when it
   can't be written; then `title: "<title>"` when it has one; then `<used> used, <free> free, <total> bytes`.

For a layout not in the library, the instruction is: "Save this definition to a file, give it a name, and pass
`--diskdefs=FILE --format=NAME` with each command that changes the disc." For a library format the instruction is
`--format=NAME`; for a format from a `--diskdefs` file, `--diskdefs=FILE --format=NAME`, because the file is read only
in runs that name it.

JSON gains, in `info`'s `app` for `cpmdw`: `format.diskdef` (the `toDiskdef()` text, always) and `fromLibrary`
(bool). Nothing else is added.

#### Read-only messages

Every refused write exits 6 (unsupported) with a message `read-only: <reason>`, where the reason says what to do. The
reasons:

- `cpmdw`, format not confirmed, layout in the library: `read-only: the format is not confirmed; use --format=NAME`
  (or `--diskdefs=FILE --format=NAME` for a format that came from a diskdefs file), NAME being the name `info`
  reports. A blank disc made by `new` is in this case: its empty directory can't tell formats of the same shape
  apart, so every command that changes it names the format (see above).
- `cpmdw`, format not confirmed, layout not in the library: `read-only: the format is not in the library; see the
  definition under info`.
- TeleDisk: `read-only: TeleDisk files can't be written; use convert to make a writable copy`.
- The image file can't be written: `read-only: the image file can't be written`.
- A volume the disc mounts read-only: `read-only: <volume name>: <the volume's reason>`.

A disc whose check finds errors is a different case: a command that would change it exits 4 (check),
`<volume name> has errors (see check), so nothing was changed`.

Read commands (`info`, `check`, `ls`, `get`, `attr` without `--set`, `title` and `boot` without a value) work on a
read-only disc without `--format`. Only commands that change the disc need it.

#### Wording

Detection texts say what was found and whether changes are safe, without menu names. Each front end adds its own
instruction: the GUI the path to Tools > Disc format (or Tools > Check disc), the CLI the `--format` or `--diskdefs`
instruction above (or `check`). Menu names must not appear in CLI output, text or JSON. For `cpmdw` this means the
detection result carries the instruction as a kind (choose a format, confirm the format, two-byte block pointers, see
the check) beside `why`, and each front end turns the kind into its own words; `why` in JSON is the finding only.

The CLI reads no QSettings, so a format confirmed in the GUI is not remembered. Each command that changes such a disc
has to name the format.

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
| 4 | check | The disc fails its check, so nothing was written (`check`, or a change to a disc with errors) |
| 5 | conflict | A name exists and neither `--replace` nor `--skip` was given |
| 6 | unsupported | The filesystem or container can't do that (a move between FAT directories), or the disc is read-only (TeleDisk, a CP/M format not confirmed): see Read-only messages |
| 7 | foreign | The disc belongs to the other Wrangler (`info` names it; the program never launches it) |
| 8 | ambiguous | Two readings are equally likely: say `--fs=ID` |
| 9 | unrecognised | Nothing recognised the disc (`--fs=ID` tries a reading anyway) |
| 10 | save | The checked save failed; the original is untouched |

## Writing a Backend

`dw::runCli(argc, argv, info)` is the whole of `main`. `info.backend` implements `dw::cli::Backend` (`open`, `create`,
`formats`, `hostPolicy`, `readHost`, and optionally `prepareIncoming` and `configureTarget`) and `OpenedImage` (the
`Disc`, `save`, `app()`, and optionally `convertTo`, `convertSides`, `setCharset`). `tests/clitest.cpp` is a complete
small example (a FAT backend in 60 lines). The qmake include is `diskwrangler-cli.pri`; CMake links `DiskWrangler::Cli`.

A backend's `readHost` reads host files and folders with the core's one host reader, `dw::xfer::readHost`
(`diskwrangler/xfer/hostread.h`; the same reader the programs' windows use): folders first, then files, names in order
ignoring case, symbolic links inside a folder skipped, any depth, the program's size limit checked before a file is
read. The program gives it its own per-file hook (`.inf` sidecars) and size limit, and turns the errors it returns into
its own messages; `dw::cli::hostReadFailed` gives the console programs' words and exit codes for the first one
(`<path>: no such file or folder`, exit 3; `<path>: cannot read (<reason>)`, exit 1).
