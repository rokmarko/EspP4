---
name: kanardia-style
description: Kanardia house style for this project's own C++ in src/ and port/ - the copyright banner, #pragma once, plain // comments with no doxygen, and clang-format-20 against the repo's .clang-format. Use when creating a new .h/.cpp, when formatting or reformatting code, when pasting code in from another Kanardia tree, or before committing.
---

# Kanardia house style

Applies to **`src/` and `port/` only** -- this project's own code.
`managed_components/`, the shared `Public/Common` tree under `$HOME/Branch/v4_3`
and `port/pc/lv_conf.h` (LVGL's own template with a few values changed) carry
their own style and must be left exactly as they are. The script knows about
all three and refuses or skips them.

Everything below is enforced by one script, so do not do it by hand:

```bash
python3 .claude/skills/kanardia-style/style.py check          # report, change nothing
python3 .claude/skills/kanardia-style/style.py format         # fix src/ and port/
python3 .claude/skills/kanardia-style/style.py format src/Foo.cpp    # or named files
python3 .claude/skills/kanardia-style/style.py new Foo        # scaffold src/Foo.h + .cpp
python3 .claude/skills/kanardia-style/style.py new Foo -n scale      # in namespace scale
python3 .claude/skills/kanardia-style/style.py new Foo -d port/pc    # in a port
```

`check` exits non-zero when something is off, so it works as a pre-commit gate.
`format` is idempotent: running it on a clean tree changes nothing.

## The four rules

**1. Every `.h` and `.cpp` opens with the copyright banner**, verbatim, before
anything else:

```c
/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/
```

This is the one `/* */` block in the project; it is matched by the literal
`Kanardia d.o.o.`, which is what lets `format` recognise an older banner -- a
2019 GPL header, or one pasted in from another Kanardia tree -- and replace it
rather than add a second one. `check` reports that as `outdated copyright
banner`.

**2. Headers are guarded with `#pragma once`**, directly under the banner,
never with `#ifndef`/`#define`. `format` deletes an include guard it finds --
an `#ifndef X` whose `#define X` has no value. An `#ifndef` around a *valued*
`#define` is a configuration default, not a guard, and is left alone; that is
what `CAN_NODE_ID` in `ApplicationDefines.h` is.

**3. Comments are plain prose in `//` lines.** No doxygen anywhere: no `/**`,
`///`, `///<`, `@file`, `@brief`, `@param`, `@return`, `@p`. A multi-paragraph
file header is a run of `//` lines with `//` as the blank line between
paragraphs. `format` rewrites all of that, turning `@return x` into
`Returns x` and `@param foo  bar` into `foo:  bar`.

`/* */` survives in exactly one situation: a comment sitting mid-line inside an
expression, where `//` would swallow the rest of the line. There is one in the
project, in `AppModel.cpp`'s constructor initialiser list.

**4. Layout is `clang-format-20` against the repo's `.clang-format`**: tabs,
`IndentWidth: 3`, Allman braces for definitions and K&R for control flow,
`SpaceBeforeParens: Never` (`if(x)`, not `if (x)`), `PointerAlignment: Left`
(`char* p`), `ColumnLimit: 120`, `SortIncludes: false`. Install it with
`sudo apt install clang-format-20`.

Naming is the other half of the style and the script cannot check it: `m_`
Hungarian members (`m_fPhase`, `m_eMode`, `m_canvas`), PascalCase methods
(`Build()`, `DrawGauge()`). Calls into LVGL and `lvgl_cpp` keep those
libraries' own snake_case.

## Two traps in the formatter

Both are handled by `style.py`; they matter if you ever reach for
`clang-format-20 -i` directly.

- **It needs several passes to converge.** Trailing-comment positions settle
  late, so one pass leaves violations that a second or third pass fixes.
  `style.py` loops until `--dry-run -Werror` is quiet.
- **It leaves one stray space per tab in front of comment lines.** Converting a
  space-indented source to `IndentWidth: 3` tabs, the remainder (4n - 3n = n
  columns) lands as spaces before `//`, so the comment sits a column off from
  the code it describes -- and clang-format considers that a fixed point, so it
  never fixes itself. `style.py` strips it between passes. Leading tabs
  followed by *more* spaces than tabs are real continuation-line alignment and
  are left alone.

## Adding a new source file

`style.py new Foo` writes `src/Foo.h` and `src/Foo.cpp` already banded,
guarded, formatted and in `namespace app`. It does **not** touch the build:
add the `.cpp` to the right list in `cmake/KanardiaSources.cmake` (or, for a
port-only file, to that port's own `CMakeLists.txt`) yourself, then
`idf.py build`. See the run-espp4 skill for building and driving the board.
