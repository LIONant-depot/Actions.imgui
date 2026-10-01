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

        // Presets: the other keymap files in the same folder.
        Ctx.m_Presets = {};
        if (!UserName.empty() && !Chain.empty())
        {
            Ctx.m_Presets.m_Base = Chain.front().m_Base;
            Ctx.m_Presets.m_List = [Dir, UserName]
            {
                std::vector<std::string> Names;
                std::error_code Ec;
                for (const auto& E : std::filesystem::directory_iterator(Dir, Ec))
                {
                    const std::string File = E.path().filename().string();
                    constexpr std::string_view Ext = ".keymap.txt";
                    if (File.size() > Ext.size() && File.compare(File.size() - Ext.size(), Ext.size(), Ext) == 0)
                        if (std::string Name = File.substr(0, File.size() - Ext.size()); Name != UserName) Names.push_back(std::move(Name));
                }
                std::sort(Names.begin(), Names.end());
                return Names;
            };
            Ctx.m_Presets.m_SetBase = [&Ctx, Dir, UserName](const std::string& Base)
            {
                keymap_file User;
                (void)LoadKeymap(Dir, UserName, User);
                User.m_Base = (Base == UserName) ? std::string{} : Base;
                if (auto Err = SaveKeymap(Dir, UserName, User); Err) Ctx.Problem(std::format("keymap '{}' could not be saved: {}", UserName, Err.getMessage()));
                const std::wstring D = Dir; const std::string U = UserName;     // this closure is replaced by the call below: copy first
                ApplyKeymapLayers(Ctx, D, U);
            };
            Ctx.m_Presets.m_SaveAs = [&Ctx, Dir, UserName](const std::string& Name) -> std::string
            {
                if (Name.empty()) return "give the keymap a name";
                if (Name == UserName) return "that is your own keymap file; pick another name";
                for (const char c : Name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return "use letters, digits, '-' and '_' in the name";
                keymap_file Out;
                for (const auto& [Path, Chords] : Ctx.m_Overrides)
                {
                    std::string Keys;
                    for (const ImGuiKeyChord C : Chords) Keys += (Keys.empty() ? "" : ",") + ChordName(C);
                    Out.m_Bindings.push_back({ Path, Keys });
                }
                for (const auto& [ToolbarName, Items] : Ctx.m_Toolbars) Out.m_Toolbars.push_back({ ToolbarName, Items });
                if (auto Err = SaveKeymap(Dir, Name, Out); Err) return std::format("could not be saved: {}", Err.getMessage());
                return {};
            };
        }

        // Editing on the keymap page changes only this person's own file (the first of the chain), then everything is layered again.
        if (!UserName.empty() && !Chain.empty())
        {
            Ctx.m_OnBindingChange = [Dir, UserName, User = Chain.front()](context& C, const std::string& Path, const std::string& Keys, bool bReset) mutable
            {
                (void)LoadKeymap(Dir, UserName, User);          // pick up edits made by hand since
                std::erase_if(User.m_Bindings, [&](const keymap_binding& B) { return B.m_Path == Path; });
                if (!bReset) User.m_Bindings.push_back({ Path, Keys });
                if (auto Err = SaveKeymap(Dir, UserName, User); Err)
                    C.Problem(std::format("keymap '{}' could not be saved: {}", UserName, Err.getMessage()));
                const std::wstring D = Dir; const std::string U = UserName;       // this closure is replaced by the call below: copy first
                ApplyKeymapLayers(C, D, U);                                          // re-layers and re-installs this hook
            };
        }
    }
}

#endif // XIMGUI_ACTIONS_KEYMAP_H
