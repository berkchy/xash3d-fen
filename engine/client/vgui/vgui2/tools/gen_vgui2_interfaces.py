#!/usr/bin/env python3
"""Generate engine-side VGUI2 interface stubs from the Valve headers.

The client library (libvgui2client.so) is Valve's GoldSource VGUI2 panel
code. It is useless without the engine half of VGUI2 - the surface, the panel
tree, the scheme manager - which Xash3D never ported. Those interfaces are
pure virtual with a few hundred methods between them, and a hand-written stub
that misses one method does not compile, while a stub that invents a method
that does not exist compiles fine and hides the omission. So nothing here is
transcribed by hand: the declarations and definitions are emitted from the
headers under ../headers/vgui, and a missing pure virtual cannot survive a
regeneration.

Usage: python3 gen_vgui2_interfaces.py <headers_dir> <output.inc> [classes...]
"""
import os
import re
import sys

HEADERS = [None]

# Interfaces the client library asks the factory for, in the order
# vgui2/src/tier2/tier2.cpp asks for them. SURFACE and PANEL are the ones that
# do the real work and are implemented separately; the rest are thin.
DEFAULT_CLASSES = [
    ("ISystem", "ISystem"),
    ("IVGui", "IVGui"),
    ("IInputInternal", "IInputInternal"),
    ("ILocalize", "ILocalize"),
]


def read(path):
    with open(path, encoding="latin-1") as handle:
        return handle.read()


def class_body(text, name):
    match = re.search(r"class\s+%s\b[^{]*\{(.*?)\n\};" % name, text, re.S)
    if not match:
        raise SystemExit("class %s not found" % name)
    return match.group(1)


def strip_defaults(args):
    """Drop "= value" from argument lists.

    A default argument belongs to the declaration, never to an out-of-class
    definition; repeating it is a compile error.
    """
    out = []
    depth = 0
    index = 0
    while index < len(args):
        char = args[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        if depth == 0 and char == "=" and not args[index + 1:index + 2] == "=":
            inner = depth
            while index < len(args) and not (args[index] == "," and inner == 0):
                if args[index] == "(":
                    inner += 1
                elif args[index] == ")":
                    inner -= 1
                index += 1
            continue
        out.append(char)
        index += 1
    return "".join(out)


def parse(signature):
    match = re.match(r"(.*?)\b(\w+)\s*\((.*)\)\s*$", signature.strip())
    if not match:
        raise SystemExit("cannot parse %r" % signature)
    return_type = match.group(1).strip()
    name = match.group(2)
    args = strip_defaults(match.group(3))
    if not return_type:
        return_type = "void"
    return return_type, name, args


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def bases(text, name):
    """Base classes of `name`, declared in this header or an inherited one."""
    head = re.search(r"class\s+%s\s*:\s*(?:public\s+)?([\w:]+)" % name,
                     strip_comments(text))
    if not head:
        return []
    base = head.group(1).split("::")[-1]
    if base in ("IBaseInterface",):
        return []
    return [base]


def virtuals(text, name, seen=None):
    """Pure virtuals of `name` and of everything it inherits from.

    IInputInternal is `class IInputInternal : public IInput`, and IInput is
    declared in its own header. Reading only the class body leaves the base
    half unimplemented and the class abstract, which is the whole failure
    this generator exists to prevent.
    """
    seen = seen or set()
    if name in seen:
        return []
    seen.add(name)

    text = strip_comments(text)
    body = class_body(text, name)
    found = []
    for match in re.finditer(r"virtual\s+(.*?)\s*=\s*0\s*;", body, re.S):
        found.append(parse(match.group(1)))

    for base in bases(text, name):
        for candidate in ("%s/vgui/%s.h" % (HEADERS[0], base),
                          "%s/%s.h" % (HEADERS[0], base)):
            if os.path.exists(candidate):
                found += virtuals(read(candidate), base, seen)
                break
        else:
            raise SystemExit("cannot find header for base class %s of %s"
                             % (base, name))
    return found


SCALARS = ("float", "double", "int", "short", "long", "unsigned int",
           "unsigned long", "unsigned short", "unsigned char", "byte", "char",
           "size_t", "uint32")


def zero_value(return_type):
    """A stub must still compile. Enums and typedefs do not accept NULL."""
    bare = return_type.replace("*", "").replace("&", "").strip()
    if "*" in return_type or "&" in return_type:
        return "NULL"
    if bare == "bool":
        return "false"
    if bare in SCALARS:
        return "0"
    # enums, typedefs, struct pointers by value
    return "(%s)0" % bare


def main():
    headers = sys.argv[1]
    HEADERS[0] = headers
    output = sys.argv[2]
    wanted = sys.argv[3:] or [name for name, _ in DEFAULT_CLASSES]

    lines = [
        "// Generated by tools/gen_vgui2_interfaces.py - do not edit.",
        "// Regenerate after any header change under headers/vgui.",
        "",
        "// Included, not compiled on its own: the file has no includes of",
        "// its own, and engine/wscript globs *.cpp in this directory, so a",
        "// .cpp here would be built as a translation unit that sees nothing.",
        "// NULL below needs a definition on some toolchains even after the",
        "// Valve headers are in.",
        "#include <stddef.h>",
        "",
        "// Inside the namespace so the headers' unqualified types (KeyCode,",
        "// StringIndex_t) resolve exactly as they do in the client library.",
        "namespace vgui2",
        "{",
        "",
    ]

    total = 0
    for cls in wanted:
        path = "%s/vgui/%s.h" % (headers, cls)
        try:
            text = read(path)
        except IOError:
            print("skip %s (no header)" % cls, file=sys.stderr)
            continue
        methods = virtuals(text, cls)
        total += len(methods)
        lines.append("class C%s : public vgui2::%s" % (cls, cls))
        lines.append("{")
        lines.append("public:")
        init = [m for m in methods if m[1] == "Init"]
        if init:
            ret, _, args = init[0]
            lines.append("\t%s Init(%s);" % (ret, args))
            # declaration only: the body is emitted below with the rest of
            # the out-of-line definitions, or the class body and the
            # definition would both claim the name
            methods = [m for m in methods if m[1] != "Init"]
        lines.append("")
        for return_type, name, args in methods:
            if return_type == "void":
                lines.append("\t%s %s(%s);" % (return_type, name, args))
            else:
                lines.append("\t%s %s(%s);" % (return_type, name, args))
        lines.append("")
        lines.append("\tCreateInterfaceFn *m_factoryList = nullptr;")
        lines.append("\tint m_numFactories = 0;")
        lines.append("};")
        lines.append("")
        if init:
            ret, _, args = init[0]
            lines.append("%s C%s::Init(%s)" % (ret, cls, args))
            lines.append("{")
            lines.append("\tm_factoryList = factoryList;")
            lines.append("\tm_numFactories = numFactories;")
            if ret != "void":
                lines.append("\treturn %s;" % zero_value(ret))
            lines.append("}")
            lines.append("")
        for return_type, name, args in methods:
            lines.append("")
            if return_type == "void":
                lines.append("void C%s::%s(%s) { }" % (cls, name, args))
            else:
                lines.append("%s C%s::%s(%s) { return %s; }" % (
                    return_type, cls, name, args, zero_value(return_type)))
        lines.append("")
        lines.append("// ---")

    lines.append("} // namespace vgui2")
    lines.append("")

    with open(output, "w") as handle:
        handle.write("\n".join(lines) + "\n")
    print("wrote %s: %d methods" % (output, total))


if __name__ == "__main__":
    main()
