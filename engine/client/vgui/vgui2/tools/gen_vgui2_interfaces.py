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
# class name -> header file, when they differ
HEADER_FOR = {
    "ISchemeManager": "IScheme",
    "IFileSystem": "FileSystem",
    "IGameUI": "IGameUI",
    "IGameConsole": "IGameConsole",
    "IGameUIFuncs": "IGameUIFuncs",
    "IEngineVGui": "IEngineVGui",
    "IBaseUI": "IBaseUI",
}

DEFAULT_CLASSES = [
    ("ISystem", "ISystem"),
    ("IVGui", "IVGui"),
    ("IInputInternal", "IInputInternal"),
    ("ILocalize", "ILocalize"),
    ("IPanel", "IPanel"),
    ("ISurface", "ISurface"),
    ("ISchemeManager", "IScheme"),
    ("IGameUI", "IGameUI"),
    ("IGameConsole", "IGameConsole"),
    ("IGameUIFuncs", "IGameUIFuncs"),
    ("IEngineVGui", "IEngineVGui"),
    ("IBaseUI", "IBaseUI"),
    ("IFileSystem", "FileSystem"),
]

# Methods with a real body in another file. The generator emits their
# declaration and no stub, so a method here is a promise the hand-written
# file keeps; anything missing from this set gets a stub instead, which is
# how the tree in vgui2_panel.cpp and the rest stay in step.
IMPLEMENTED = {
    "CISurface": {
        "DrawSetColor", "DrawFilledRect", "DrawOutlinedRect", "DrawLine",
        "DrawPolyLine", "DrawSetTextColor", "DrawSetTextPos",
        "DrawGetTextPos", "DrawPrintText", "CreateNewTextureID",
        "DestroyTextureID", "IsTextureIDValid", "DrawSetTexture",
        "DrawGetTextureSize", "DrawSetTextureFile", "DrawSetTextureRGBA",
        "DrawTexturedRect", "CreateFont", "DrawSetTextFont",
        "GetFontTall", "GetTextSize", "GetScreenSize", "AddPanel",
        "ReleasePanel", "SetEmbeddedPanel", "GetEmbeddedPanel",
        "PaintTraverse", "SolveTraverse", "CreatePopup", "GetPopup",
        "GetPopupCount", "MovePopupToFront", "MovePopupToBack",
        "SetPanelVisible", "ShouldPaintChildPanel", "GetTopmostPopup",
        "SetModalPanel", "GetModalPanel", "SetForegroundWindow",
        "SetAsTopMost", "BringToFront", "SetMinimized", "IsMinimized",
        "FlashWindow", "SetTitle", "SetAsToolBar", "SwapBuffers",
        "Invalidate", "SetCursor", "IsCursorVisible", "LockCursor",
        "UnlockCursor", "HasCursorPosFunctions", "SurfaceGetCursorPos",
        "SurfaceSetCursorPos", "CalculateMouseVisible", "NeedKBInput",
        "GetWorkspaceBounds", "GetProportionalBase", "SetProportionalBase",
        "SetHDProportionalBase", "GetHDProportionalBase",
    },
    "CIPanel": {
        "Init", "SetPos", "GetPos", "GetAbsPos", "SetSize", "GetSize",
        "SetMinimumSize", "GetMinimumSize", "OnSizeChanged", "SetInset",
        "GetInset", "GetClipRect", "SetParent", "GetParent", "GetChildCount",
        "GetChild", "OnChildAdded", "DeletePanel", "SetVisible", "IsVisible",
        "IsFullyVisible", "SetZPos", "GetZPos", "SetRenderZPos",
        "GetRenderZPos", "MoveToFront", "MoveToBack", "HasParent", "IsPopup",
        "SetPopup", "Render_GetPopupVisible", "Render_SetPopupVisible",
        "IsEnabled", "SetEnabled", "SetAutoDelete",
        "SetKeyBoardInputEnabled", "IsKeyBoardInputEnabled",
        "SetMouseInputEnabled", "IsMouseInputEnabled", "SetProportional",
        "IsProportional", "Plat", "SetPlat", "Client", "GetName",
        "GetClassName", "GetModuleName", "GetPanel", "PaintTraverse",
        "IsWithinTraverse", "Repaint", "PerformApplySchemeSettings",
        "GetScheme", "SendMessage", "Think", "RequestInfo",
        "InternalFocusChanged", "RequestFocus", "RequestFocusPrev",
        "RequestFocusNext", "GetCurrentKeyFocus", "GetTabPosition",
    },
    "CISchemeManager": {
        "LoadSchemeFromFile", "ReloadSchemes", "GetDefaultScheme",
        "GetScheme", "GetIScheme", "GetImage", "GetImageID",
        "DeleteImage", "Shutdown", "GetProportionalScaledValue",
        "GetProportionalNormalizedValue", "GetProportionalScale",
        "GetHDProportionalScaledValue", "GetHDProportionalNormalizedValue",
    },
}


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
    signature = signature.strip()

    # A const member function keeps the const in its declaration - without
    # it the member does not override and the class stays abstract - but the
    # out-of-class definition must not repeat it. The flag is carried
    # through parse() and dropped where definitions are emitted.
    is_const = bool(re.search(r"\)\s*const\s*$", signature))
    signature = re.sub(r"\s+const\s*$", "", signature)

    # re.S: several of these signatures wrap across lines.
    match = re.match(r"(.*?)\b(\w+)\s*\((.*)\)\s*$", signature, re.S)
    if not match:
        raise SystemExit("cannot parse %r" % signature)
    return_type = match.group(1).strip()
    name = match.group(2)
    args = strip_defaults(match.group(3))
    if not return_type:
        return_type = "void"
    return return_type, name, args, is_const


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


# Declared in the base with a body, but the base body is wrong for us: the
# stock IsFullyVisible answers from the visible flag alone and ignores the
# clip rectangle. Declared in the generated class so the hand-written file
# can override them.
# Data members a hand-written implementation needs on the generated class.
# They go in the same declaration, because a second class of the same name in
# one translation unit is a redefinition.
EXTRA_MEMBERS = {
    "CISchemeManager": [
        "CISchemeManager();",
        "int m_proportionalBase[2];",
        "int m_screenSize[2];",
    ],
    "CISurface": [
        "CISurface();",
        "SurfaceTexture *Find( int id );",
        "void SetPanels( CIPanel *panels ) { m_panels = panels; }",
        "CIPanel *m_panels;",
        "int m_currentTexture;",
        "int m_drawColor[4];",
        "int m_textColor[4];",
        "int m_textPos[2];",
        "int m_font;",
        "int m_proportionalBase[2];",
        "int m_screenSize[2];",
        "bool m_cursorVisible;",
        "bool m_hasFocus;",
        "VPANEL m_embeddedPanel;",
        "VPANEL m_modalPanel;",
        "VPANEL m_notifyPanel;",
        "vgui2::CVGui2FontSet m_fonts;",
        "vgui2::SurfaceTexture m_textures[vgui2::kMaxTextures];",
        "int m_textureCount;",
        "int m_freeTextures[vgui2::kMaxTextures];",
        "int m_freeTextureCount;",
        "VPANEL m_popups[vgui2::kMaxPopups];",
        "int m_popupCount;",
    ],
}

EXTRA_OVERRIDES = {
    # IsFullyVisible, SetAutoDelete, SetRenderZPos and friends are commented
    # out in Valve's GoldSource IPanel - the GS build disabled them - so they
    # are not part of the interface and overriding them would only produce
    # methods nothing calls. Only what the header really declares goes here.
    "CISurface": {
        "DrawSetColor", "DrawFilledRect", "DrawOutlinedRect", "DrawLine",
        "DrawPolyLine", "DrawSetTextColor", "DrawSetTextPos",
        "DrawGetTextPos", "DrawPrintText", "CreateNewTextureID",
        "DestroyTextureID", "IsTextureIDValid", "DrawSetTexture",
        "DrawGetTextureSize", "DrawSetTextureFile", "DrawSetTextureRGBA",
        "DrawTexturedRect", "CreateFont", "DrawSetTextFont",
        "GetFontTall", "GetTextSize", "GetScreenSize", "AddPanel",
        "ReleasePanel", "SetEmbeddedPanel", "GetEmbeddedPanel",
        "PaintTraverse", "SolveTraverse", "CreatePopup", "GetPopup",
        "GetPopupCount", "MovePopupToFront", "MovePopupToBack",
        "SetPanelVisible", "ShouldPaintChildPanel", "GetTopmostPopup",
        "SetModalPanel", "GetModalPanel", "SetForegroundWindow",
        "SetAsTopMost", "BringToFront", "SetMinimized", "IsMinimized",
        "FlashWindow", "SetTitle", "SetAsToolBar", "SwapBuffers",
        "Invalidate", "SetCursor", "IsCursorVisible", "LockCursor",
        "UnlockCursor", "HasCursorPosFunctions", "SurfaceGetCursorPos",
        "SurfaceSetCursorPos", "CalculateMouseVisible", "NeedKBInput",
        "GetWorkspaceBounds", "GetProportionalBase", "SetProportionalBase",
        "SetHDProportionalBase", "GetHDProportionalBase",
    },
    "CIPanel": {
        "GetTabPosition",
    },
}


def extra_signature(text, name, method):
    """Find the base declaration of a non-pure virtual so it can be mirrored."""
    body = strip_comments(text)
    for match in re.finditer(r"virtual\s+([^;{]*?)\s*(?:=\s*0)?\s*;", body, re.S):
        sig = re.sub(r"\s+", " ", match.group(1)).strip()
        parsed = parse(sig)
        if parsed[1] == method:
            return parsed
    return None


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
    for match in re.finditer(r"virtual\s+([^;{]*?)\s*=\s*0\s*;", body, re.S):
        found.append(parse(match.group(1)))

    for base in bases(text, name):
        for candidate in ("%s/vgui/%s.h" % (HEADERS[0], base),
                          "%s/%s.h" % (HEADERS[0], base),
                          "%s/vgui_controls/%s.h" % (HEADERS[0], base),
                          "%s/tier1/%s.h" % (HEADERS[0], base)):
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


def collect_with_overrides(text, cls, methods, hand):
    """Add the non-pure virtuals the hand-written file wants to override."""
    extra = EXTRA_OVERRIDES.get("C%s" % cls, set())
    if not extra:
        return methods

    known = {name for _, name, _, _ in methods}
    result = list(methods)
    for method in sorted(extra):
        if method in known:
            continue
        signature = extra_signature(text, cls, method)
        if signature:
            result.append(signature)
    return result


def namespace_of(text, cls):
    """Which namespace the class is declared in.

    Most of Valve's VGUI2 is inside `namespace vgui2`, but IGameUI,
    IBaseUI and IFileSystem are not - they predate it. Qualifying those
    with vgui2:: produces "expected class name", so the base is written the
    way the header declares it.
    """
    index = text.find("class %s" % cls)
    if index < 0:
        return "vgui2"

    before = text[:index]
    if "namespace vgui2" not in before:
        return ""

    # A closing brace of the namespace after its last opening means we are
    # outside it again.
    opens = before.count("namespace vgui2")
    closes = before.count("}")
    return "vgui2" if opens > closes else ""


def emit_declarations(cls, methods, hand, base=""):
    """Class declaration only. Safe to include from many translation units."""
    qualified = ("%s::%s" % (base, cls)) if base else cls
    out = ["class C%s : public %s" % (cls, qualified), "{", "public:"]

    init = [m for m in methods if m[1] == "Init"]
    body_methods = [m for m in methods if m[1] != "Init"]
    if init:
        out.append("\t%s Init(%s);" % (init[0][0], init[0][2]))
    # Shutdown is declared by the header like every other method; writing it
    # out here as well is a redeclaration, not a default.

    for entry in body_methods:
        return_type, name, args = entry[0], entry[1], entry[2]
        is_const = entry[3] if len(entry) > 3 else False

        # A destructor arrives as "~ IEngineVGui( void )": no return type,
        # and its name does not carry a scope, so emitting it as a normal
        # member produces "expected the class name after '~'".
        if return_type.strip() == "~":
            continue
        out.append("\t%s %s(%s)%s;" % (return_type, name, args,
                                        " const" if is_const else ""))

    members = EXTRA_MEMBERS.get("C%s" % cls, [])
    if members:
        out.append("")
        out.append("\t// Implementation-side members and helpers, from")
        out.append("\t// tools/gen_vgui2_interfaces.py EXTRA_MEMBERS.")
        for member in members:
            out.append("\t%s" % member)

    out.append("")
    out.append("\tCreateInterfaceFn *m_factoryList = nullptr;")
    out.append("\tint m_numFactories = 0;")
    out.append("};")
    out.append("")

    # No Init definition here. This file is included by every translation
    # unit that needs the classes; the definition belongs in the one that
    # emits bodies, or it lands in two objects and the link fails on
    # duplicate symbols.
    return out

    # No definitions here. A hand-implemented method is declared and then
    # defined in the file that implements it - defining it here as well
    # would put the same symbol in two translation units.
    return out


def emit_stubs(cls, methods, hand):
    """Definitions for everything nobody wrote by hand.

    These exist so the class is concrete and the client library can start;
    each one is a no-op and the header comment says which ones they are, so a
    stub is never mistaken for a finished feature.
    """
    out = []
    _ = cls

    init = [m for m in methods if m[1] == "Init" and "CreateInterfaceFn" in m[2]]
    if init:
        ret, args = init[0][0], init[0][2]
        out.append("%s C%s::Init(%s)" % (ret, cls, args))
        out.append("{")
        out.append("\tm_factoryList = factoryList;")
        out.append("\tm_numFactories = numFactories;")
        if ret != "void":
            out.append("\treturn %s;" % zero_value(ret))
        out.append("}")
        out.append("")

    stubbed = [m for m in methods if m[1] not in hand and m[1] != "Init"]
    for entry in stubbed:
        return_type, name, args = entry[0], entry[1], entry[2]
        # A const member function repeats its const in the out-of-class
        # definition. Unlike a default argument - which belongs to the
        # declaration alone and must not be repeated - the const is part of
        # the declarator, and leaving it off makes the definition match
        # nothing.
        is_const = entry[3] if len(entry) > 3 else False
        # Destructors, for the reason given in emit_declarations.
        if return_type.strip() == "~":
            continue

        # Some declarations carry the keyword with the type, e.g.
        # "virtual vgui2::VPANEL GetPanel(...)"; the definition must not.
        return_type = re.sub(r"^virtual\s+", "", return_type.strip())

        out.append("")
        tail = " const" if is_const else ""
        if return_type == "void":
            out.append("void C%s::%s(%s)%s { }" % (cls, name, args, tail))
        else:
            out.append("%s C%s::%s(%s)%s { return %s; }" % (
                return_type, cls, name, args, tail, zero_value(return_type)))
    if stubbed:
        out.append("")
        out.append("// C%s: %d methods still stubbed - the no-ops above are not" % (cls, len(stubbed)))
        out.append("// finished features, they only keep the class concrete.")
    return out


def main():
    headers = sys.argv[1]
    HEADERS[0] = headers
    output = sys.argv[2]
    decl_output = output.replace(".gen.inc", ".decl.inc")
    wanted = sys.argv[3:] or [name for name, _ in DEFAULT_CLASSES]

    banner = [
        "// Generated by tools/gen_vgui2_interfaces.py - do not edit.",
        "// Regenerate after any header change under headers/vgui.",
        "",
        "// Declarations and definitions are emitted separately so that a file",
        "// which implements some of these methods by hand can include the",
        "// declarations without also pulling in the stubs: two translation",
        "// units defining the same method is what LNK4006 was.",
        "#include <stddef.h>",
        "",
        "// Self-contained: this file declares every class it emits, so it has",
        "// to bring the headers those classes are declared in with it.",
        "#include <FileSystem.h>",
        "#include <IBaseUI.h>",
        "#include <IClientVGUI.h>",
        "#include <IGameConsole.h>",
        "#include <IGameUI.h>",
        "#include <IGameUIFuncs.h>",
        "#include <IEngineVGui.h>",
        "#include <vgui/ILocalize.h>",
        "#include <vgui/IInputInternal.h>",
        "#include <vgui/IPanel.h>",
        "#include <vgui/IScheme.h>",
        "#include <vgui/ISurface.h>",
        "#include <vgui/ISystem.h>",
        "#include <vgui/IVGui.h>",
        "#include <vgui_controls/Panel.h>",
        "",
        "// Types the generated CISurface members refer to. Included here so",
        "// the declarations stand on their own; guarded because the hand",
        "// written file includes it too.",
        "#ifndef VGUI2_SURFACE_H",
        "#define VGUI2_SURFACE_H",
        "#include \"vgui2_surface.h\"",
        "#endif",
        "",
        "namespace vgui2",
        "{",
        "",
    ]

    decls = list(banner)
    bodies = list(banner)
    total = 0

    for cls in wanted:
        stem = HEADER_FOR.get(cls, cls)
        candidates = ["%s/vgui/%s.h" % (headers, stem),
                      "%s/%s.h" % (headers, stem)]
        text = None
        for candidate in candidates:
            if os.path.exists(candidate):
                text = read(candidate)
                break
        if text is None:
            print("skip %s (no header)" % cls, file=sys.stderr)
            continue

        methods = virtuals(text, cls)
        hand = IMPLEMENTED.get("C%s" % cls, set())
        methods = collect_with_overrides(text, cls, methods, hand)
        total += len(methods)

        base = namespace_of(text, cls)
        decls += emit_declarations(cls, methods, hand, base)
        bodies += emit_stubs(cls, methods, hand)
        decls.append("")
        bodies.append("")

    decls.append("} // namespace vgui2")
    decls.append("")
    bodies.append("} // namespace vgui2")
    bodies.append("")

    with open(decl_output, "w") as handle:
        handle.write("\n".join(decls) + "\n")
    with open(output, "w") as handle:
        handle.write("\n".join(bodies) + "\n")
    print("wrote %s and %s: %d methods" % (output, decl_output, total))


if __name__ == "__main__":
    main()
