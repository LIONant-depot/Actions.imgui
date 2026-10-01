#ifndef XIMGUI_ACTIONS_KEYMAP_H
#define XIMGUI_ACTIONS_KEYMAP_H
#pragma once

// Keymap files for ximgui_actions.h: plain xtextfile property files written with xproperty::sprop, the same way the
// project's *.config.txt files are. A file holds ONLY the differences from the layer below it:
//
//      defaults (member_keys in code)  ->  base preset (m_Base)  ->  the user's file
//
// Include after ximgui_actions.h, xtextfile and xproperty's sprop serializer.
//
// Toolbars live in the same file on purpose: a toolbar is an ordered list of action paths, so a button, its tooltip and
// its key all come from one action, and "my keys + my toolbar" travel together as one preset.

#include <filesystem>
#include <format>

namespace ximgui::actions
{
    struct keymap_binding
    {
        std::string m_Path;     // "Level/Entity/Duplicate"
        std::string m_Keys;     // "Ctrl+Shift+D", "A,B" for several, "" = unbound

        XPROPERTY_DEF
        ( "Binding", keymap_binding
        , obj_member<"Path", &keymap_binding::m_Path>
        , obj_member<"Keys", &keymap_binding::m_Keys>
        )
    };
    XPROPERTY_REG(keymap_binding)

    struct toolbar_def
    {
        std::string                 m_Name;     // the toolbar's name ("Scene")
        std::vector<std::string>    m_Items;    // action paths in order; "-" is a separator

        XPROPERTY_DEF
        ( "Toolbar", toolbar_def
        , obj_member<"Name",  &toolbar_def::m_Name>
        , obj_member<"Items", &toolbar_def::m_Items>
        )
    };
    XPROPERTY_REG(toolbar_def)

    struct keymap_file
    {
        std::string                 m_Base;                 // name of the preset this one sits on ("" = the code defaults)
        std::vector<keymap_binding> m_Bindings;
        std::vector<toolbar_def>    m_Toolbars;             // a toolbar listed here replaces the same toolbar from the layer below

        XPROPERTY_DEF
        ( "Keymap", keymap_file
        , obj_member<"Base",     &keymap_file::m_Base>
        , obj_member<"Bindings", &keymap_file::m_Bindings>
        , obj_member<"Toolbars", &keymap_file::m_Toolbars>
        )
    };
    XPROPERTY_REG(keymap_file)

    inline std::wstring KeymapFileName(const std::wstring& Dir, const std::string& Name)
    {
        return std::format(L"{}\\{}.keymap.txt", Dir, std::wstring(Name.begin(), Name.end()));
    }

    // A missing file is not an error: File is left empty.
    inline xerr LoadKeymap(const std::wstring& Dir, const std::string& Name, keymap_file& File) noexcept
    {
        File = {};
        xtextfile::stream Stream;
        if (auto Err = Stream.Open(true, KeymapFileName(Dir, Name), { xtextfile::file_type::TEXT }); Err) return {};
        xproperty::settings::context Context;
        return xproperty::sprop::serializer::Stream(Stream, File, Context);
    }

    inline xerr SaveKeymap(const std::wstring& Dir, const std::string& Name, const keymap_file& File) noexcept
    {
        std::filesystem::create_directories(Dir);
        xtextfile::stream Stream;
        if (auto Err = Stream.Open(false, KeymapFileName(Dir, Name), { xtextfile::file_type::TEXT }); Err) return Err;
        xproperty::settings::context Context;
        return xproperty::sprop::serializer::Stream(Stream, const_cast<keymap_file&>(File), Context);
    }

    // Layers, lowest first: the chain of bases ending at UserName's file. A path listed in a layer replaces what the lower
    // layers said about it. Unknown paths are kept (a preset made with a plugin you do not have loaded survives).
    inline void ApplyKeymapLayers(context& Ctx, const std::wstring& Dir, const std::string& UserName)
    {
        std::vector<keymap_file> Chain;
        for (std::string Name = UserName; !Name.empty(); )
        {
            if (Chain.size() >= 8) { Ctx.Problem(std::format("keymap '{}': the chain of bases is too long or loops", UserName)); break; }
            keymap_file F;
            if (auto Err = LoadKeymap(Dir, Name, F); Err)
                Ctx.Problem(std::format("keymap '{}' could not be read: {}", Name, Err.getMessage()));
            Name = F.m_Base;
            Chain.push_back(std::move(F));
        }

        Ctx.m_Overrides.clear();
        Ctx.m_Toolbars.clear();
        for (auto It = Chain.rbegin(); It != Chain.rend(); ++It)
        {
            for (auto& B : It->m_Bindings) Ctx.m_Overrides[B.m_Path] = Ctx.ParseChecked(B.m_Keys, std::format("keymap binding '{}'", B.m_Path));
            for (auto& T : It->m_Toolbars) Ctx.m_Toolbars[T.m_Name] = T.m_Items;
        }
    }
}

#endif // XIMGUI_ACTIONS_KEYMAP_H
