#!/usr/bin/env python3
"""
Kanardia house style for this project's own C++.

  style.py check  [paths...]   report what is off, change nothing
  style.py format [paths...]   banner, plain // comments, clang-format
  style.py new <Name> [-n ns]  scaffold src/<Name>.h and src/<Name>.cpp

With no paths, acts on src/, port/esp/ and port/pc/. Never touch
managed_components or the shared Kanardia tree -- both carry their own style.
"""

import argparse
import difflib
import glob
import os
import re
import subprocess
import sys

ROOT        = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
CLANG       = 'clang-format-20'
DEFAULT     = ['src/*.cpp', 'src/*.h',
               'src/Item/*.cpp', 'src/Item/*.h',
               'port/esp/*.cpp', 'port/esp/*.h',
               'port/pc/*.cpp', 'port/pc/*.h']
BANNER_MARK = 'Kanardia d.o.o.'
# Ours by location, not by authorship: port/pc/lv_conf.h is LVGL's own
# lv_conf_template.h with a handful of values changed, and is kept in LVGL's
# style so that upgrading LVGL stays a re-copy and a diff.
VENDORED    = ['port/pc/lv_conf.h']

BANNER = """\
/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/"""

# ---------------------------------------------------------------------------
# doxygen and /* */ prose -> plain // comments
# ---------------------------------------------------------------------------

def strip_tags(lines):
    """Turn one comment's text, already stripped of its markers, into prose."""
    out, shift = [], 0
    for line in lines:
        m = re.match(r'^(?P<lead>\s*)@(?P<tag>file|brief|param|return)\b(?P<rest>.*)$', line)
        if m:
            lead, tag, rest = m.group('lead'), m.group('tag'), m.group('rest')
            shift = 0
            if tag == 'file':
                continue                       # the file already carries its name
            if tag == 'brief':
                line = lead + rest.lstrip(' ')
            elif tag == 'return':
                # '@return ' and 'Returns ' are the same width, so continuation
                # lines stay aligned under the text they were aligned under.
                line = lead + 'Returns' + rest
            else:
                pm = re.match(r'^\s+(?P<name>[A-Za-z_]\w*)(?P<gap>\s*)(?P<desc>.*)$', rest)
                if pm:
                    line  = lead + pm.group('name') + ':' + pm.group('gap') + pm.group('desc')
                    shift = len('@param ') - len(':')
                else:
                    line = lead + rest.lstrip(' ')
        elif shift:
            cm = re.match(r'^(\s{%d,})(\S.*)$' % (shift + 1), line)
            if cm:
                line = cm.group(1)[shift:] + cm.group(2)
            else:
                shift = 0
        for pat, rep in ((r'@return\b\s*', 'Returns '), (r'@param\s+([A-Za-z_]\w*)', r'\1:'),
                         (r'@brief\b\s*', ''), (r'@p\s+(?=[A-Za-z_])', '')):
            line = re.sub(pat, rep, line)
        out.append(line.rstrip())
    while out and not out[0]:
        out.pop(0)
    while out and not out[-1]:
        out.pop()
    return out


def next_block(src, i):
    """Start of the next /* ... */ that is not inside a string or a // comment."""
    n = len(src)
    while i < n:
        c = src[i]
        if c in '"\'':
            i += 1
            while i < n and src[i] != c:
                i += 2 if src[i] == '\\' else 1
            i += 1
        elif src.startswith('//', i):
            i = src.find('\n', i)
            if i < 0:
                return -1
        elif src.startswith('/*', i):
            return i
        else:
            i += 1
    return -1


def undox(src):
    """Rewrite block and doxygen comments as // lines. The banner is left alone."""
    out, pos = [], 0
    while True:
        b = next_block(src, pos)
        if b < 0:
            break
        e = src.find('*/', b + 2)
        if e < 0:
            break
        e += 2
        if BANNER_MARK in src[b:e]:
            out.append(src[pos:e])
            pos = e
            continue

        bol  = src.rfind('\n', 0, b) + 1
        eol  = src.find('\n', e)
        eol  = len(src) if eol < 0 else eol
        head = src[bol:b]                      # what sits before the comment
        if src[e:eol].strip():                 # code on both sides: has to stay /* */
            out.append(src[pos:e])
            pos = e
            continue

        body = src[b + 2:e - 2]
        if body.startswith('*'):               # the doxygen /** opener
            body = body[1:]
        lines    = body.split('\n')
        lines    = [lines[0].strip()] + [re.sub(r'^\s*\*\s?', '', l).rstrip() for l in lines[1:]]
        lines    = strip_tags(lines)

        if head.strip():                       # trailing comment: keep it on the line
            joined = ' '.join(l.strip() for l in lines if l.strip())
            out.append(src[pos:b] + ('// ' + joined if joined else '').rstrip())
            pos = e
        elif not lines:                        # nothing left once the tags are gone
            out.append(src[pos:bol])
            pos = min(eol + 1, len(src))
        else:
            out.append(src[pos:bol])
            out.append('\n'.join((head + '// ' + l).rstrip() if l else (head + '//').rstrip()
                                 for l in lines))
            pos = e
    out.append(src[pos:])
    src = ''.join(out)

    src = re.sub(r'///<', '//', src)
    src = re.sub(r'(?<![:/])///(?!/)', '//', src)
    src = re.sub(r'//!', '//', src)
    return src

# ---------------------------------------------------------------------------
# banner and the clang-format tab quirk
# ---------------------------------------------------------------------------

def banner_span(src):
    """Where the file's leading /* ... */ banner sits, or None if it has none."""
    i = 0
    while i < len(src) and src[i] in ' \t\r\n':
        i += 1
    if not src.startswith('/*', i):
        return None
    e = src.find('*/', i + 2)
    if e < 0:
        return None
    e += 2
    return (i, e) if BANNER_MARK in src[i:e] else None


def add_banner(src):
    """
    Give the file the current banner. A file carrying an older one -- a 2019
    GPL header, or something pasted in from another Kanardia tree -- has it
    replaced rather than doubled up.
    """
    span = banner_span(src)
    if span:
        b, e = span
        return src if src[b:e] == BANNER else src[:b] + BANNER + src[e:]
    body = src.lstrip('\n')
    m    = re.match(r'(#pragma once\n)\s*', body)   # keep it right under the banner
    if m:
        body = m.group(1) + '\n' + body[m.end():]
    return BANNER + '\n\n' + body


GUARD = re.compile(r'(?m)^[ \t]*#ifndef[ \t]+(\w+)[ \t]*\n[ \t]*\n?[ \t]*#define[ \t]+\1[ \t]*\n')


def drop_include_guard(src, path):
    """
    Headers are guarded with #pragma once. An #ifndef/#define pair whose symbol
    is redefined with nothing is an include guard and goes; anything with a
    value is a configuration default (ApplicationDefines.h's CAN_NODE_ID) and
    stays.
    """
    if not path.endswith('.h'):
        return src
    m = GUARD.search(src)
    if not m:
        return src
    end = src.rfind('#endif')
    if end < m.end():
        return src
    eol = src.find('\n', end)
    src = src[:end] + src[(len(src) if eol < 0 else eol + 1):]
    return src[:m.start()] + src[m.end():]


def add_pragma(src, path):
    """Headers are guarded with #pragma once, never with an #ifndef."""
    if not path.endswith('.h') or re.search(r'(?m)^#pragma once$', src):
        return src
    head, sep, rest = src.partition(BANNER.splitlines()[-1])
    if not sep:                                # no banner yet: add_banner runs next
        return '#pragma once\n\n' + src
    return head + sep + '\n\n#pragma once\n\n' + rest.lstrip('\n')


def fix_comment_indent(src):
    """
    clang-format turns a 4-space indent into tabs and leaves the remainder as
    spaces in front of a comment -- one stray space per tab. Code lines do not
    get it, so the comment ends up a column off from what it describes. It
    considers that a fixed point, so it has to be undone by hand.
    """
    return re.sub(r'(?m)^(\t+)( +)(?=//)',
                  lambda m: m.group(1) if len(m.group(2)) <= len(m.group(1)) else m.group(0),
                  src)

# ---------------------------------------------------------------------------
# driving clang-format
# ---------------------------------------------------------------------------

def clang(args, paths, capture=True):
    return subprocess.run([CLANG, '--style=file'] + args + paths,
                          cwd=ROOT, capture_output=capture, text=True)


def converge(paths, rounds=4):
    """clang-format needs a few passes to settle trailing comments."""
    for i in range(rounds):
        clang(['-i'], paths)
        for p in paths:
            full = os.path.join(ROOT, p)
            with open(full, encoding='utf-8') as f:
                src = f.read()
            fixed = fix_comment_indent(src)
            if fixed != src:
                with open(full, 'w', encoding='utf-8') as f:
                    f.write(fixed)
        if clang(['--dry-run', '-Werror'], paths).returncode == 0:
            return i + 1
    return rounds

# ---------------------------------------------------------------------------

def expand(patterns):
    # A default pattern that matches nothing is a directory this build does not
    # carry yet, not a mistake; one the caller typed out is.
    given = bool(patterns)
    paths = []
    for pat in patterns or DEFAULT:
        hits = sorted(glob.glob(pat, root_dir=ROOT)) or ([pat] if os.path.exists(os.path.join(ROOT, pat)) else [])
        if not hits:
            if given:
                sys.exit('no such file: ' + pat)
            continue
        paths += hits
    paths = [p for p in paths if p not in VENDORED]
    bad = [p for p in paths if p.startswith(('managed_components/', 'build/'))]
    if bad:
        sys.exit('refusing to reformat third-party code: ' + ', '.join(bad))
    return paths


def rewrite(path):
    full = os.path.join(ROOT, path)
    with open(full, encoding='utf-8') as f:
        src = f.read()
    out = add_banner(add_pragma(drop_include_guard(undox(src), path), path))
    return full, src, out


def cmd_format(paths):
    paths = expand(paths)
    changed = []
    for p in paths:
        full, src, out = rewrite(p)
        if out != src:
            with open(full, 'w', encoding='utf-8') as f:
                f.write(out)
            changed.append(p)
    n = converge(paths)
    print('%d file(s) touched before clang-format, %d pass(es) to converge' % (len(changed), n))
    for p in changed:
        print('  ', p)
    return 0 if clang(['--dry-run', '-Werror'], paths).returncode == 0 else 1


def cmd_check(paths):
    paths, bad = expand(paths), []
    for p in paths:
        full, src, out = rewrite(p)
        why = []
        span = banner_span(src)
        if span is None:
            why.append('no copyright banner')
        elif src[span[0]:span[1]] != BANNER:
            why.append('outdated copyright banner')
        if p.endswith('.h') and not re.search(r'(?m)^#pragma once$', src):
            why.append('no #pragma once')
        if GUARD.search(src):
            why.append('#ifndef include guard')
        # /**...  but not the banner's /*****, and not an empty /**/
        if re.search(r'/\*\*(?![*/])|(?<![:/])///(?!/)|//!|@brief|@file|@param\s|@return\s', undox(src)):
            why.append('doxygen markup')
        if out != src and not why:
            why.append('block comments that should be //')
        if why:
            bad.append((p, why))
    fmt = clang(['--dry-run', '-Werror'], paths)
    off = sorted({l.split(':')[0] for l in fmt.stderr.splitlines() if 'clang-format-violations' in l})
    for p, why in bad:
        print('%-32s %s' % (p, ', '.join(why)))
    for p in off:
        print('%-32s not clang-format clean' % p)
    if not bad and not off:
        print('%d file(s): clean' % len(paths))
        return 0
    print('\nrun: python3 .claude/skills/kanardia-style/style.py format')
    return 1


HEADER = '''%(banner)s

#pragma once

// One line saying what this is, then a blank // line and the paragraphs that
// say why it exists and what will bite whoever changes it.

namespace %(ns)s {

class %(name)s
{
public:
%(tab)s%(name)s();

private:
};

} // namespace %(ns)s
'''

SOURCE = '''%(banner)s

#include "%(name)s.h"

namespace %(ns)s {

%(name)s::%(name)s() {}

} // namespace %(ns)s
'''


def cmd_new(name, ns, where='src'):
    made = []
    for tmpl, ext in ((HEADER, '.h'), (SOURCE, '.cpp')):
        path = os.path.join(where, name + ext)
        full = os.path.join(ROOT, path)
        if os.path.exists(full):
            print('exists, left alone:', path)
            continue
        with open(full, 'w', encoding='utf-8') as f:
            f.write(tmpl % {'banner': BANNER, 'name': name, 'ns': ns, 'tab': '\t'})
        made.append(path)
    if made:
        converge(made)
        print('created:', ', '.join(made))
        print('remember: add the .cpp to the source list in cmake/KanardiaSources.cmake')
    return 0


def main():
    ap  = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for verb in ('check', 'format'):
        s = sub.add_parser(verb)
        s.add_argument('paths', nargs='*')
    s = sub.add_parser('new')
    s.add_argument('name')
    s.add_argument('-n', '--namespace', default='app')
    s.add_argument('-d', '--dir', default='src', help='src, port/esp or port/pc')
    a = ap.parse_args()

    if subprocess.run(['which', CLANG], capture_output=True).returncode != 0:
        sys.exit(CLANG + ' not found: sudo apt install clang-format-20')
    if a.cmd == 'new':
        return cmd_new(a.name, a.namespace, a.dir)
    return (cmd_check if a.cmd == 'check' else cmd_format)(a.paths)


if __name__ == '__main__':
    sys.exit(main())
