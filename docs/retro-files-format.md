# retro-files: dragging files out of disc images, with their attributes

**MIME type:** `application/x-retro-files+json` - **version 1**

A small, open convention for drag-and-drop (and clipboard) between tools that edit retro disc images, so
that a file dragged out of one tool and into another keeps the attributes a plain host file can't hold:
load/exec addresses, access bits, a DFS directory letter, DOS attribute bits, CP/M user numbers, and so on.

Acorn Disk Wrangler, CP/M Disk Wrangler and FLEX Disk Wrangler produce and accept it. Nothing else is required to
understand it: a drag always carries ordinary files as well (see *How a drag is put together*), so any program that
accepts dropped files works, and a program that knows this format just gets more.

## Why this format is needed

A drag between programs normally carries only what the operating system understands about a file: its name,
its contents and perhaps its dates. Files on retro discs have more than that, and it is part of the file:

* An Acorn file has a **load address and an execution address**. Without them a machine-code program or
  BASIC program may not run, and the pair is also how ADFS stores a file's type and time stamp.
* Each filesystem has its own **attribute bits**: ADFS locked/read/write/execute, a DFS directory letter and
  lock flag, DOS read-only/hidden/system/archive, CP/M user numbers.
* Names can use characters or lengths a host file system refuses, and the same file may have to be renamed
  differently depending on where it lands.

A host file system has no place to keep any of this. A plain drag to Explorer or a file manager therefore
loses it, and a drag from one disc tool to another has no way to carry it at all. The usual workaround is a
sidecar file, and the Acorn `.inf` file is exactly that. It works, but it is specific to Acorn, so a CP/M
tool can't be expected to write or read it. It is also separate from the file, so it is easily left behind.

Drag and drop between programs, including between a virtual machine and its host, only passes standard types
such as `text/uri-list` and `text/plain`. There is no standard type for "a file plus the attributes of the
disc it came from". This format fills that gap:

* **No loss between disc tools.** Moving a file from one tool to another, or from one disc type to another,
  keeps everything the destination can use. The receiver converts what it can (for example widening or
  narrowing addresses) and drops the rest knowingly, rather than guessing from a bare file.
* **Nothing breaks for other programs.** The same drag also carries ordinary files, so Explorer, file
  managers and editors work as they always did.
* **One format for many tools.** Attributes are grouped by filesystem (`acorn`, `fat`, `cpm`), so a
  tool for a new kind of disc only has to add its own group and read the ones it understands.
* **Open and self-describing.** A small, versioned JSON document that anyone can implement or inspect
  without this program's source.

No existing standard covers this. Windows' virtual-file formats (`FileGroupDescriptorW` / `FileContents`) carry
names, sizes and dates only; CiderPress II has a JSON drag format, but it is private to that program and
specific to Apple II / Macintosh files. The Acorn `.inf` sidecar file is the nearest convention, and it is
Acorn-only, so it can't be expected of other tools.

## How a drag is put together

A drag offers several formats at once. A receiver takes the richest one it understands:

| Format | Who uses it | Contents |
|---|---|---|
| `application/x-retro-files+json` | tools that know this convention | the JSON document below |
| `text/uri-list` (Windows: `CF_HDROP`) | Explorer, file managers, any program | the top-level files and folders of the drag |
| tool-private formats | the sending tool itself | whatever it likes (Acorn Disk Wrangler uses `application/x-acorndiskwrangler-*` between its own windows in one process) |

The files are real files in a folder (the **base** folder) that the sending tool creates, usually under the
system temporary folder. The base folder may contain more than the listed items: Acorn Disk Wrangler writes
Acorn `.inf` sidecar files next to the files when its *Write .inf sidecar files when extracting* option is on,
for the benefit of Explorer and Acorn tools. **A receiver that uses the JSON must take only the items listed
in it** and ignore anything else in the base folder or in `text/uri-list`.

The sender may create the files only when a receiver first asks for `text/uri-list` or the JSON (Acorn Disk
Wrangler does), and keeps them until it exits. A receiver must copy the files during the drop and must not
keep references to them.

### On the clipboard

Copy and paste use the same formats. Both Wranglers' Edit > Copy put on the clipboard exactly what a drag of the
selection offers, plus GNOME's `x-special/gnome-copied-files` (`copy`, then one `file://` URL per line), which
GNOME Files pastes. The files are written to the base folder only when a receiver first asks for them, and are
kept until the sender exits, as for a drag; a sender clears the clipboard when it exits if it still owns it. A
receiver that pastes takes the JSON if it is there, else the URLs, exactly as for a drop. Both Wranglers paste
from a file manager's copy too (`text/uri-list`, or GNOME's list).

### On each platform

* **Windows:** the format is the registered clipboard format named exactly `application/x-retro-files+json`
  (`RegisterClipboardFormatW`), delivered as `TYMED_HGLOBAL` containing the UTF-8 JSON bytes, no terminator
  required. This is what Qt 5 does for a custom MIME type, so a Qt receiver just asks for the MIME type.
* **Linux / BSD (X11, Wayland):** the MIME type itself, UTF-8 bytes.
* **macOS:** not tested. The pasteboard identifies data by Uniform Type Identifier (UTI), not MIME type, so this
  format will need a declared UTI there. Qt 5 makes one up from the MIME type for its own use; a non-Qt receiver
  would have to know it. To be settled when it is tested on a Mac.

## The JSON document

UTF-8, one JSON object. Example (a directory and a file dragged from an ADFS disc):

```json
{
  "format": "retro-files",
  "version": 1,
  "generator": "Acorn Disk Wrangler",
  "source": { "image": "Welcome.adl", "filesystem": "adfs" },
  "base": "file:///C:/Users/me/AppData/Local/Temp/acorndiskwrangler-drag-aB3xYz",
  "items": [
    { "path": "Games", "type": "directory", "name": "Games" },
    { "path": "Games/Elite", "type": "file", "name": "Elite", "size": 21504,
      "modified": "1987-03-01T12:00:00",
      "attributes": { "acorn": { "load": "FFFF1900", "exec": "FFFF8023", "access": "LWR" } } }
  ]
}
```

The same for two files dragged from a DFS disc (`"filesystem": "dfs"`). They have load and exec addresses too
(here widened to 32 bits, see `acorn` below), and `name` is the full DFS name, directory letter included:

```json
{ "path": "BOOT", "type": "file", "name": "$.BOOT", "size": 300,
  "attributes": { "acorn": { "load": "00001900", "exec": "00008023", "access": "WR" } } },
{ "path": "A.BOOT", "type": "file", "name": "A.BOOT", "size": 1234,
  "attributes": { "acorn": { "load": "FFFF1900", "exec": "FFFF8023", "access": "L" } } }
```

| Key | Required | Meaning |
|---|---|---|
| `format` | yes | always `"retro-files"` |
| `version` | yes | `1`. A receiver that finds a higher number should ignore the JSON and use the plain files. Version 1 receivers ignore keys they don't know, so new optional keys don't need a new version. |
| `generator` | no | the sending program, for information |
| `source.filesystem` | yes | the filesystem the items came from: `"adfs"`, `"dfs"`, `"fat"` (PC and Acorn DOS Plus), `"cpm"` (CP/M Disk Wrangler), `"flex"` and `"uniflex"` (FLEX Disk Wrangler), or another short lower-case name. A receiver uses it to interpret `name` and to decide how to convert. |
| `source.image` | no | file name of the disc image, for information |
| `base` | yes | `file:` URL of the base folder. Item paths are relative to it. |
| `items` | yes | every file and directory in the drag, **parents before their contents** |

Each item:

| Key | Required | Meaning |
|---|---|---|
| `path` | yes | path of the item relative to `base`, `/`-separated, exactly as written on the host. It never contains `..`, never starts with `/`, and a receiver must refuse an item whose path leaves the base folder. Its last component may differ from `name` (characters the host can't hold are replaced, and clashing names are made unique). |
| `type` | yes | `"file"` or `"directory"` |
| `name` | yes | the item's full name on the source filesystem, unchanged (it may contain characters the host can't). How to read it depends on `source.filesystem`; see *Names* below. |
| `size` | files | the file's length in bytes; equal to the size of the host file |
| `modified` | no | the item's date as the source filesystem records it, ISO 8601 `YYYY-MM-DDThh:mm:ss`, **no time zone** (retro filesystems don't record one; take it as the time shown on the original machine). The host file's modification time is set to the same value where the sender can. |
| `attributes` | no | an object of **namespaces**, each an object of filesystem-specific attributes. A receiver uses the namespaces it knows and ignores the rest. An item may carry several. |

### Names

`name` is the name as the source filesystem knows it, the one that tells the item apart from the others
beside it.

* **`adfs`, `fat`, `cpm`:** the leaf name (`"Elite"`, `"README.TXT"`). The directory it is in is given by the
  item's parent in `items`.
* **`dfs`:** always `D.NAME`: the directory character, a dot, then the name (`"$.BOOT"`, `"A.BOOT"`), the way
  DFS and `.inf` files write it. The directory letter is part of a DFS file's identity: `$.BOOT` and `A.BOOT`
  are different files. DFS names can't contain a dot, so the first character and dot are always the directory
  and the rest is the name. What a receiver does with the letter depends on where the file is going: a DFS
  receiver keeps it, an ADFS receiver can make it a subdirectory (Acorn Disk Wrangler does), and a receiver for
  a non-Acorn filesystem just drops it and uses the name.

**Character set.** Acorn and DOS names are bytes in the source machine's character set, while JSON is Unicode
text. `name` carries the bytes **unchanged, one character per byte**: byte *n* becomes the character U+00*nn*
(U+0000-U+00FF, the same as decoding the bytes as ISO 8859-1), whatever the byte means on the source machine.
A receiver gets the original bytes back by taking each character's number, so nothing is lost or converted on
the way. A `name` never contains a character above U+00FF. What the name *looks like* is the receiver's
business, because the same byte means different things on different machines:

| Byte on the disc | In `name` | Shown on the source machine as |
|---|---|---|
| `&60` on a BBC Micro DFS or ADFS disc | `` ` `` (U+0060) | `£` (the BBC's character set puts `£` at `&60`) |
| `&A3` in a RISC OS name | `£` (U+00A3) | `£` (RISC OS uses Latin-1) |
| `&9C` in a DOS name | U+009C (a control character, written `\u009c` in JSON) | `£` (DOS code page 437) |

So a DFS file the BBC lists as `$.£5` is sent as ``"name": "$.`5"``. A tool may show it as `£5` if it knows the
disc came from a BBC; it must still write the byte `&60` back. (JSON writers may escape any character as
`\uXXXX`; it is the same character either way.)

The host file's name (the last part of `path`) is a separate thing: what the sender could write on the host.
Acorn Disk Wrangler writes `BOOT` for `$.BOOT` and `A.BOOT` for `A.BOOT`, matching `.inf` practice.

## Attribute namespaces

### `acorn` - Acorn file attributes (ADFS, DFS)

The attributes every Acorn filesystem has in common. **Both ADFS and DFS files carry `load` and `exec`**, and a
sender whose source is either of them must give both for every file.

| Key | Meaning |
|---|---|
| `load`, `exec` | load and execution addresses, 8 upper-case hex digits (32 bits). Files only. Stored exactly as on the disc, including RISC OS file types and dates (see below). |
| `access` | access letters, **case-sensitive**, each at most once, in any order: owner `R` read, `W` write, `E` execute-only, `L` locked; public (other users, e.g. RISC OS and Econet file servers) `r`, `w`, `e`, `l`. These are the letters of the [Stardot `.inf` format](https://github.com/stardot/inf_format/blob/main/inf_format_full.md), which also gives each its bit (`R` &01, `W` &02, `E` &04, `L` &08, `r` &10, `w` &20, `e` &40, `l` &80). A file with `E` and no `R` is `*RUN`-only, as on ROMFS/CFS. DFS has only *locked*: a locked DFS file is `"L"`, an unlocked one `"WR"`. A typical new RISC OS file is `"WRr"`. Directories have access too; there is no letter for "directory" (`type` says that). |
| `title` | directories only, optional: an ADFS directory's title, if it differs from its name. (Acorn Disk Wrangler does not produce or use it yet.) |

**ADFS and DFS addresses.** ADFS keeps full 32-bit addresses. DFS keeps 18 bits (`&00000`-`&3FFFF`), so a
sender widens them the way `.inf` files do: an address with bits 16-17 both set (`&3xxxx`) is written as
`&FFFFxxxx`, which means an address in the I/O processor - the BBC Micro itself, as opposed to a second
processor. A receiver that stores them in
DFS narrows them again (keeps the low 18 bits). A receiver moving a file between the two keeps the 32-bit values
when the target is ADFS, and has no place for them if they won't fit an 18-bit DFS address. That is a loss the
receiver should report, not hide.

**Econet.** Econet file servers are Acorn filesystems too, and this namespace is where their attributes will go
(the public access letters above are one). Owner and server-side dates aren't defined yet: Level 3, the
FileStore, MDFS and the Econet bridges store them differently, so the keys should be settled with someone who
implements one.

#### RISC OS file types and dates

There is no file-type key, on purpose. RISC OS stores a file's type and date *in* its load and exec addresses,
so `load` and `exec` already carry them, and a second copy could only disagree. This section describes the
encoding so that tools can read and write it correctly.

A "typed and time-stamped" file has:

```
load = &FFFtttdd    top 12 bits all set: the file is typed
                    ttt = the file type (12 bits)
                    dd  = bits 32-39 of the time
exec = &xxxxxxxx    bits 0-31 of the time
time = centiseconds since 1900-01-01 00:00:00, a 40-bit number, no time zone
```

So `type = (load >> 8) & 0xFFF` and `time = ((load & 0xFF) << 32) | exec`. A file whose `load` doesn't have
the top 12 bits set has an ordinary load and execution address and no type.

Common types: `FFF` Text, `FFB` BBC BASIC, `FF8` absolute (machine code), `FFD` data, `FFA` module, `FEB` Obey,
`FF9` sprite, `DDC` archive. RISC OS Open keeps the register of allocated types. For example, `load`
`FFFFFF5A` is a Text file dated between June 2022 and November 2023 (the low 32 bits of the time, in `exec`, say exactly when).

* **Reading.** The top-12-bits rule on its own is RISC OS's, but I/O-processor addresses (`&FFFFxxxx`) satisfy
  it too, every DFS address widened as above does, and 8-bit software uses such addresses on ADFS discs that
  RISC OS also writes typed files to. So a reader can only guess. Treat `load` as a type and date only when
  `source.filesystem` is `"adfs"` and the date it gives is plausible (Acorn Disk Wrangler uses 1981-2100),
  and **when unsure, treat them as addresses**. `load` and `exec` are kept raw either way, so taking a stamp for
  an address loses nothing, while taking an address for a stamp shows a real address as a type and a date. Even
  the date test can be fooled: `&FFFF1900` is safely read as an address (its stamp would be in 1901), but a
  load address such as `&FFFF1950` gives a plausible date. Never *change* `load` or `exec` on the strength of the
  guess. A DFS file never has a file type. Directories have no load or exec and so no type.
* **Writing.** A sender that has a type but no load/exec (for example from a host file named `Name,ffb`, see
  below) builds them from the formula above: `time` from the file's date in centiseconds since 1900 (or
  the current time if it has no date), then `load = 0xFFF00000 | type << 8 | time >> 32` and
  `exec = time & 0xFFFFFFFF`. It also gives that date as `modified`.
* **`modified`.** For a typed file, the sender gives the date held in the stamp as `modified`, as calendar time
  with no zone (the machine's own clock reading). `load` and `exec` remain the authority.
* **Copying to DFS.** DFS can only keep 18 bits, so a receiver narrows `load` and `exec` the same way for
  every file, keeping the low 18 bits, typed or not (Acorn Disk Wrangler does). It doesn't set them to 0 for a
  file it thinks is typed: that guess may be wrong, and then a real address would be lost.

**Host file names.** On the host, RISC OS emulators keep the type in the file name. RPCEmu's host file system
writes a typed file as `Name,ttt` (a comma and three hex digits, left off for Text, `FFF`), takes the date
from the host file's time, and writes an untyped file as `Name,llllllll-eeeeeeee` (load and exec in hex).
That is a host-file convention, not part of this format: a tool that reads or writes such files converts the
suffix to and from `load` and `exec`.

### `fat` - DOS FAT attributes (PC, Acorn DOS Plus)

| Key | Meaning |
|---|---|
| `readonly`, `hidden`, `system`, `archive` | booleans, the FAT attribute bits |

### `cpm` - CP/M (produced by CP/M Disk Wrangler)

| Key | Meaning |
|---|---|
| `user` | the user number (0-15 on CP/M 3; CP/M 2.2, P2DOS and ZSDOS also allow 16-31). Files only. |
| `readonly`, `system`, `archive` | booleans, the `T1'`-`T3'` attribute bits (the high bits of the extension's three characters). `system` files are the ones `DIR` doesn't list. |
| `f1`, `f2`, `f3`, `f4` | optional booleans, the `F1'`-`F4'` "interface" attribute bits (the high bits of the first four name characters); a sender includes a key only when the bit is set. |

CP/M has no directories, so a CP/M source sends files only; the user number is an attribute, not part of the
path. `name` is the 8.3 name without padding (`"README.TXT"`, `"PIP.COM"`, `"NOEXT"`), with the attribute bits
cleared, bytes as characters as described under *Names*. Two files with the same name in different user numbers
are different files: the sender makes their host paths unique.

Some CP/M discs were written on machines with a national variant of ASCII, where `[ \ ] { | }` were letters
(Danish `br|kregning` is *brøkregning*). `name` is still the bytes as written, never converted to Unicode; a
sender may show the national letters in the host file name (`path`), which is a separate thing, but never in
`name`.

A CP/M file's length is a whole number of 128-byte records unless the disc records the bytes used in the last
record (CP/M 3 does; cpmtools writes it on every disc). `size` is the length the sender read, so a text file
may end in `^Z` padding; CP/M Disk Wrangler sends the padding unless its *Trim the ^Z padding* option is on.
`modified` is the CP/M 3 / P2DOS update time stamp (or the create stamp if there is no update stamp), when the
disc keeps them.

**Converting between namespaces** (what CP/M Disk Wrangler does with a drop):

* `fat`: `readonly` -> `readonly`; `system` or `hidden` -> `system` (CP/M's SYS attribute hides a file from
  `DIR`); `archive` -> `archive`.
* `acorn`: `L` (owner locked) -> `readonly`; load and exec addresses have no place on a CP/M disc and are dropped.
* `name`: kept if it is a valid CP/M 8.3 name (upper-cased), otherwise turned into one (invalid characters
  become `-`, the name part is cut to 8 and the extension to 3 characters). A DFS name loses its directory letter.
  A name from a `cpm` source is kept exactly, including `[ \ ] |`, which were letters on the disc it came from
  (it is not converted, whatever national variant either disc uses); only the name of a file dropped from the
  host is read as Unicode and, if the disc has a national variant chosen, put into it.
* Directories: a directory named `userN`, `user N`, `uN` or `N` (as CP/M Disk Wrangler's *Extract* writes them)
  puts its files in user number N; any other directory is flattened into the user number the files were
  dropped on.
* The user number: dropping on a user number's row in the tree puts the files there; dropping anywhere else
  keeps each file's `cpm.user` (0 for files from other filesystems).
* A sender of another filesystem that wants a file to land in a particular user number can add a `cpm`
  namespace to the item: a receiver uses the namespaces it knows and ignores the rest.

### `flex` - FLEX file attributes (produced by FLEX Disk Wrangler)

TSC FLEX (6800 FLEX 2, 6809 FLEX 9). Files only: FLEX has one directory per disc.

| Key | Meaning |
|---|---|
| `writeProtect` | boolean, FLEX's write protection (directory attribute `$80`) |
| `deleteProtect` | boolean, delete protection (`$40`) |
| `readProtect` | boolean, read protection (`$20`) |
| `catalogProtect` | boolean, catalogue protection (`$10`): `CAT` doesn't list the file |
| `random` | boolean, a random-access file. The data is the file's data sectors without FLEX's two sector-map sectors; a receiver that writes FLEX discs makes a new map (FLEX Disk Wrangler does, from the sectors the file gets, as FLEX does) |

`name` is the name as stored, upper case, `NAME.EXT` (1-8 characters, a dot, up to 3; letters, digits, `-` and
`_`, starting with a letter); the host file's name is lower-cased. `modified` is the FLEX date (day precision, at
midnight). The data is the file as stored on the disc: the data bytes of each of its sectors in chain order. A FLEX
sector is 256 bytes, of which the first 4 are the link to the next sector and the record number, so each sector
carries 252 bytes of data and `size` is always a multiple of 252. A text file keeps FLEX's CR line ends, TAB + count
space compression and NUL padding.

### `uniflex` - UniFLEX file attributes (produced by FLEX Disk Wrangler)

TSC UniFLEX, the multi-user system for the same 6809 machines. Every key is on files and directories alike. The bits
are an fdn's access byte (FLEX Disk Wrangler's `docs/uniflex-format.md`).

| Key | Meaning |
|---|---|
| `ownerRead` | boolean, the owner may read it (`$01`) |
| `ownerWrite` | boolean, the owner may write it (`$02`) |
| `ownerExecute` | boolean, the owner may run it, or search it if a directory (`$04`) |
| `othersRead` | boolean, everyone else may read it (`$08`) |
| `othersWrite` | boolean, everyone else may write it (`$10`) |
| `othersExecute` | boolean, everyone else may run it (`$20`) |
| `setUserId` | boolean, it runs with its owner's user id (`$40`) |
| `owner` | number, the owner's user id (0-65535) |

`name` is the name's bytes as characters (Latin-1, case kept, 1-14 bytes). `modified` is the fdn's time of last
change, to the second (UniFLEX keeps seconds since 1980 in GMT). The data is the file's bytes, exactly its size;
blocks UniFLEX never wrote come out as zeros. Text files keep UniFLEX's CR line ends and their TABs. A directory
travels as a directory with its contents. A device file travels as an empty file (its device numbers don't travel).

### `uef` - a tape's chunks as they are (produced by Acorn Disk Wrangler)

For what a tape holds that a file's attributes can't carry: data the cassette filing system can't read (a game's own
loader format) and files with errors (a bad CRC, a missing block). A receiver that writes tapes adds the chunks to its
tape as they are, so the copy is the same, errors and all; any other receiver ignores the namespace and takes the
item's data as a file (with its `acorn` attributes, if it has them).

| Key | Meaning |
|---|---|
| `chunks` | string, base64: UEF chunk records (a 16-bit id and a 32-bit length, little-endian, then the body), with the base frequency (`&0113`) and baud rate (`&0117`) they were played at in front when those aren't the defaults |

The item's data is still the bytes the program reads from the chunks (unrecognised data: the bytes no block took; a
damaged file: what of it could be read).

New namespaces should be short, lower-case and named after the filesystem or machine family. Please add them
here.

## What CP/M Disk Wrangler does with a drop

* From one of its own windows in the same process: copies directly, keeping user numbers and attributes.
* A drop with this JSON: imports the listed files using the conversions above, and sets the CP/M 3 / P2DOS time
  stamps from `modified` when the disc keeps stamps.
* Otherwise, plain files: names converted to 8.3; folders as above; Acorn `.inf` sidecars are skipped.

## What Acorn Disk Wrangler does with a drop

* From one of its own windows in the same process: copies directly, with full attributes (private format).
* A drop with this JSON (another copy of the program, or another tool): imports the listed items using
  `source.filesystem` and the attributes, exactly as a copy between two of its own windows would; files from a
  filesystem it doesn't know (e.g. `cpm`) are treated like files dropped from the host. Dates are not applied
  (new files get the current time, as for any other copy).
* Otherwise, plain files: imported as files from the host, reading `.inf` sidecars if that option is on.
* On a tape: an item with `uef` chunks goes on the end of the tape as those chunks are; any other file is saved as
  the BBC Micro's `*SAVE` saves it.

## What FLEX Disk Wrangler does with a drop

* From one of its own windows in the same process: copies directly, with the same attributes as the JSON below;
  within one UniFLEX disc's tree, a drag moves the items into the directory dropped on instead.
* A drop with this JSON, or plain files:
  * On a **FLEX** disc: the file gets the item's date (today's when it has none) and the four `flex` protection keys,
    as FLEX's `COPY` carries them; a `random` file becomes random-access with a new sector map. `name` is kept if it
    is a valid FLEX name (upper-cased), otherwise turned into one (cut to 8 and 3 characters; characters FLEX
    doesn't allow are dropped, spaces and dots become `_`; an `F` in front if it doesn't start with a letter).
    Directories are flattened: FLEX has one. Other namespaces aren't converted.
  * On a **UniFLEX** disc, into the directory dropped on (or the one selected): a file gets the `uniflex` access bits
    and owner the item brings; keys left out give owner 0 and `rw-rw-`, as UniFLEX's shell makes a file. Its date is
    not kept: UniFLEX gives a file the time it is written. A directory becomes a directory. `name` is kept, cut to
    14 bytes; `/`, NUL and characters beyond Latin-1 become `_`.

## History

* Version 1 (2026): first version, Acorn Disk Wrangler. The `cpm` namespace was settled with CP/M Disk Wrangler,
  which produces it (adding the optional `f1`-`f4` keys); no change of version was needed.
* The clipboard (Copy / Paste) carries the same document; no change of version was needed.
* The `flex` and `uniflex` namespaces (FLEX Disk Wrangler, 2026), kept in that program's docs until they moved here;
  no change of version was needed.
* National variants of ASCII (CP/M Disk Wrangler, 2026): `name` stays the disc's bytes; clarified above, no
  change of version was needed.
* The `uef` namespace (Acorn Disk Wrangler's tapes, 2026); no change of version was needed.
