#ifndef XIMGUI_ACTIONS_H
#define XIMGUI_ACTIONS_H
#pragma once

// ximgui actions - keys, menus, toolbar buttons and hints generated from xproperty descriptions.
//
// An ACTION is a plain xproperty function member:  obj_action<"Delete", &T::DeleteSelection, ...tags>
// Its identity is its property path ("Level/Entity/Delete"). Everything here reads that description:
//
//      keys          member_keys<"Ctrl+D">  (a tag; "A,B" gives several chords)
//      help          member_help<"...">     (xproperty's own)
//      unavailable   member_dynamic_reason<fn>  /  member_dynamic_flags<fn>   (xproperty's own)
//
// The library never sees commands, undo or an editor framework: an action is a function, what it does is its owner's
// business. Nothing is global: the state lives in a ximgui::actions::context the host holds.
//
// Include order (this is a header-only library like ximgui_toolbar.h): imgui.h, xdelegate, xproperty with its imgui
// inspector tags (my_property_ui.h), then this file. imgui_internal.h is used for the window parent chain.
//
// Per frame:   Ctx.NewFrame();                                   // resolves the keys pressed this frame (uses last frame's scopes)
//              inside each panel's Begin/End:  Ctx.Scope(Obj, "Viewport", "Entity");   // this window makes those scopes live
//              once from the host:             Ctx.Global(HostObj);
//              after the panels:               Ctx.EndFrame();

#include <algorithm>
#include <functional>
#include <optional>
#include <span>
#include <format>
#include <cctype>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "imgui_internal.h"

namespace ximgui::actions
{
    //==============================================================================================
    // Tags
    //==============================================================================================

    struct member_keys_t : xproperty::member_user_data<"ximgui.actions.Keys">
    {
        const char* m_pKeys;        // "Ctrl+D" or "Ctrl+Y,Ctrl+Shift+Z"
        bool        m_bInText;      // may fire while a text field has focus (Save, Palette...)
    };

    template< xproperty::details::fixed_string T_KEYS, bool T_IN_TEXT = false >
    struct member_keys : member_keys_t
    {
        constexpr member_keys() noexcept : member_keys_t{ .m_pKeys = T_KEYS.m_Value, .m_bInText = T_IN_TEXT } {}
    };

    //==============================================================================================
    // Chords
    //==============================================================================================

    namespace details
    {
        inline bool IEquals(std::string_view A, std::string_view B) noexcept
        {
            if (A.size() != B.size()) return false;
            for (std::size_t i = 0; i < A.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(A[i])) != std::tolower(static_cast<unsigned char>(B[i]))) return false;
            return true;
        }

        inline std::string_view Trim(std::string_view S) noexcept
        {
            while (!S.empty() && S.front() == ' ') S.remove_prefix(1);
            while (!S.empty() && S.back()  == ' ') S.remove_suffix(1);
            return S;
        }

        inline ImGuiKey FindKey(std::string_view Name) noexcept
        {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
                if (IEquals(ImGui::GetKeyName(static_cast<ImGuiKey>(k)), Name)) return static_cast<ImGuiKey>(k);
            return ImGuiKey_None;
        }
    }

    // "Ctrl+Shift+D" -> chord. 0 when empty or when a part is not understood.
    inline ImGuiKeyChord ParseChord(std::string_view Text) noexcept
    {
        ImGuiKeyChord Mods = 0;
        ImGuiKey      Key  = ImGuiKey_None;
        while (!Text.empty())
        {
            const auto  p   = Text.find('+');
            const auto  Tok = details::Trim(Text.substr(0, p));
            Text = (p == std::string_view::npos) ? std::string_view{} : Text.substr(p + 1);

            if      (details::IEquals(Tok, "Ctrl") || details::IEquals(Tok, "Control"))                                 Mods |= ImGuiMod_Ctrl;
            else if (details::IEquals(Tok, "Shift"))                                                                    Mods |= ImGuiMod_Shift;
            else if (details::IEquals(Tok, "Alt"))                                                                      Mods |= ImGuiMod_Alt;
            else if (details::IEquals(Tok, "Super") || details::IEquals(Tok, "Cmd") || details::IEquals(Tok, "Win"))    Mods |= ImGuiMod_Super;
            else if (Key = details::FindKey(Tok); Key == ImGuiKey_None)                                                 return 0;
        }
        return Key == ImGuiKey_None ? 0 : (Mods | Key);
    }

    inline std::string ChordName(ImGuiKeyChord Chord)
    {
        std::string S;
        if (Chord & ImGuiMod_Ctrl)  S += "Ctrl+";
        if (Chord & ImGuiMod_Shift) S += "Shift+";
        if (Chord & ImGuiMod_Alt)   S += "Alt+";
        if (Chord & ImGuiMod_Super) S += "Super+";
        S += ImGui::GetKeyName(static_cast<ImGuiKey>(Chord & ~ImGuiMod_Mask_));
        return S;
    }

    // "Ctrl+Y,Ctrl+Shift+Z" -> chords (parts that do not parse are dropped).
    inline std::vector<ImGuiKeyChord> ParseChords(std::string_view Text)
    {
        std::vector<ImGuiKeyChord> Out;
        while (!Text.empty())
        {
            const auto p = Text.find(',');
            if (const auto C = ParseChord(Text.substr(0, p)); C) Out.push_back(C);
            Text = (p == std::string_view::npos) ? std::string_view{} : Text.substr(p + 1);
        }
        return Out;
    }

    //==============================================================================================
    // The index of a type's actions (built once per type from the static description)
    //==============================================================================================

    struct action_info
    {
        std::string                     m_Path;         // "Level/Entity/Delete"
        std::string                     m_Scope;        // scopes between the object and the member: "Entity"; "" at the root
        const char*                     m_pName  = "";
        const char*                     m_pHelp  = nullptr;
        const xproperty::type::members* m_pMember = nullptr;
        std::vector<ImGuiKeyChord>      m_Default;
        bool                            m_bInText = false;
        const xproperty::type::object*  m_pObject = nullptr;
    };

    struct scope_entry
    {
        const xproperty::type::object*  m_pObject   = nullptr;
        void*                           m_pInstance = nullptr;
        std::vector<std::string>        m_Prefixes;             // scopes this panel makes live (the object's root actions always are)
        bool                            m_bGlobal   = false;    // registered with Global(): the host's own, live wherever the person is
    };

    struct input_result
    {
        bool         m_bMatched = false;    // some live action is bound to the chord
        bool         m_bRan     = false;
        std::string  m_Keys;
        std::string  m_Path;                // the action it went to (or the first one refused)
        std::string  m_Reason;              // why it did not run
    };

    // What a hint says about an action. The host shows it with its own hint window (context::m_pShowHint); without one a plain tooltip is used.
    struct hint_text
    {
        std::string m_Topic;        // "Tool Select"
        std::string m_Body;         // the action's help
        std::string m_Shortcut;     // "Q" ("" = none)
        std::string m_Disabled;     // why it cannot run now ("" = it can)
        std::string m_Detail;       // the action's path
    };

    //==============================================================================================
    // Every type that declares actions, whether or not an editor of that type is open - so the keymap page, the menus and BindKey know all
    // of them from the start. A type registers itself once, right after its XPROPERTY_REG:   XIMGUI_ACTIONS_OWNER(session_actions)
    //==============================================================================================

    inline std::vector<const xproperty::type::object* (*)() noexcept>& OwnerTypes() noexcept
    {
        static std::vector<const xproperty::type::object* (*)() noexcept> s_Types;
        return s_Types;
    }

    template<typename T>
    struct register_owner
    {
        register_owner() noexcept { OwnerTypes().push_back(+[]() noexcept { return xproperty::getObjectByType<T>(); }); }
    };
#define XIMGUI_ACTIONS_OWNER(T) inline const ximgui::actions::register_owner<T> g_ximgui_actions_owner_##T;

    //==============================================================================================
    // The context
    //==============================================================================================

    //==============================================================================================
    // Mouse gestures: what the mouse does on a surface. Descriptions only (the surface handles the mouse itself, as it always did), so
    // they can be listed - on the mouse in the F1 view, and in the status line under the cursor. A surface declares its table once:
    //
    //      static constexpr gesture Viewport[] =
    //      { { 0,              mouse_input::Left,  mouse_kind::Click, "Select",    "Selects the entity under the mouse." }
    //      , { ImGuiMod_Ctrl,  mouse_input::Left,  mouse_kind::Click, "Add",       "Adds it to the selection, or takes it out." } };
    //
    // and the panel registers it every frame, next to its scopes: Ctx.Gestures("Viewport", Viewport) inside Begin/End, or GesturesWindow.
    //==============================================================================================

    enum class mouse_input : std::uint8_t { Left, Right, Middle, Wheel };
    enum class mouse_kind  : std::uint8_t { Click, DoubleClick, Drag, Scroll };

    struct gesture
    {
        ImGuiKeyChord m_Mods;           // ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiMod_Alt held while it is done (0 = none)
        mouse_input   m_Input;
        mouse_kind    m_Kind;
        const char*   m_pName;          // "Select"
        const char*   m_pHelp;          // what it does, one sentence
        const char*   m_pAlso = nullptr;// keys that go with it ("W A S D Q E"), or nullptr
    };

    // "Ctrl+LMB click", "RMB drag + W A S D Q E", "Wheel".
    inline std::string GestureText(const gesture& G)
    {
        std::string S;
        if (G.m_Mods & ImGuiMod_Ctrl)  S += "Ctrl+";
        if (G.m_Mods & ImGuiMod_Shift) S += "Shift+";
        if (G.m_Mods & ImGuiMod_Alt)   S += "Alt+";
        switch (G.m_Input) { case mouse_input::Left: S += "LMB"; break; case mouse_input::Right: S += "RMB"; break; case mouse_input::Middle: S += "MMB"; break; case mouse_input::Wheel: S += "Wheel"; break; }
        switch (G.m_Kind)  { case mouse_kind::Click: S += " click"; break; case mouse_kind::DoubleClick: S += " double-click"; break; case mouse_kind::Drag: S += " drag"; break; case mouse_kind::Scroll: break; }
        if (G.m_pAlso) { S += " + "; S += G.m_pAlso; }
        return S;
    }

    // The gestures one surface (a window) makes live.
    struct gesture_set
    {
        ImGuiID                     m_Window  = 0;
        const char*                 m_pSurface= "";     // "Viewport"
        std::span<const gesture>    m_List;
    };

    struct context
    {
        xproperty::settings::context                                m_Settings;
        xdelegate::thread_unsafe<context&, const input_result&>     m_OnInput;          // a chord reached (or was refused by) an action
        input_result                                                m_Last;             // the last one: what "Explain last key" says

        // Everything that went wrong with the setup (a keymap that does not load, a key that does not parse, two actions on one key),
        // kept for a report and announced through m_OnProblem as it happens. Never thrown, never silent.
        std::vector<std::string>                                    m_Problems;
        xdelegate::thread_unsafe<context&, const std::string&>      m_OnProblem;

        void Problem(std::string Text)
        {
            if (std::find(m_Problems.begin(), m_Problems.end(), Text) != m_Problems.end()) return;
            m_Problems.push_back(std::move(Text));
            m_OnProblem.NotifyAll(*this, m_Problems.back());
        }

        // Parses "A,B" and reports every part that is not a key chord.
        std::vector<ImGuiKeyChord> ParseChecked(std::string_view Text, std::string_view Where)
        {
            std::vector<ImGuiKeyChord> Out;
            while (!Text.empty())
            {
                const auto p    = Text.find(',');
                const auto Part = details::Trim(Text.substr(0, p));
                if (!Part.empty())
                {
                    if (const auto C = ParseChord(Part); C) Out.push_back(C);
                    else Problem(std::format("{}: '{}' is not a key chord", Where, Part));
                }
                Text = (p == std::string_view::npos) ? std::string_view{} : Text.substr(p + 1);
            }
            return Out;
        }

        // Two actions on one key in the same scope of one object can never both be reached. Reported as problems; the text lists
        // them, plus the keymap bindings no indexed action matches (informational: its editor may simply not be open).
        std::string Validate()
        {
            IndexKnownTypes();
            std::unordered_map<std::string, std::string> Owner;
            std::vector<std::string> Known;
            for (auto& [pObj, Actions] : m_Types)
            {
                Owner.clear();
                for (auto& A : Actions)
                {
                    Known.push_back(A.m_Path);
                    for (const ImGuiKeyChord C : Chords(A))
                    {
                        const auto Key = A.m_Scope + "|" + ChordName(C);
                        if (auto It = Owner.find(Key); It != Owner.end() && It->second != A.m_Path)
                            Problem(std::format("{} and {} are both on {}{}", It->second, A.m_Path, ChordName(C), A.m_Scope.empty() ? "" : " in " + A.m_Scope));
                        else Owner[Key] = A.m_Path;
                    }
                }
            }
            std::string Text;
            for (auto& P : m_Problems) Text += P + "\n";
            for (auto& [Path, Chords] : m_Overrides)
                if (std::find(Known.begin(), Known.end(), Path) == Known.end()) Text += std::format("note: keymap binding '{}' matches no action of an editor seen so far (kept)\n", Path);
            return Text;
        }

        // path -> chords that REPLACE the defaults (an empty list = unbound). Filled from keymap layers.
        std::unordered_map<std::string, std::vector<ImGuiKeyChord>> m_Overrides;

        // toolbar name -> action paths in order ("-" = separator). Filled from keymap layers; a toolbar with no entry
        // here is drawn by its owner as before.
        std::unordered_map<std::string, std::vector<std::string>>   m_Toolbars;

        //------------------------------------------------------------------------------------------
        // UI state (drawn by ximgui_actions_ui.h: the command palette and the keymap page)
        //------------------------------------------------------------------------------------------

        struct palette_state
        {
            bool                     m_bOpen      = false;
            bool                     m_bFocus     = false;       // put the keyboard in the search box (the frame it opens)
            int                      m_OpenFrame  = 0;
            int                      m_Selected   = 0;
            std::string              m_Query;
            ImGuiWindow*             m_pPrevFocus = nullptr;     // the window that had the focus when it opened (gets it back)
            std::vector<scope_entry> m_Order;                    // what was live THEN: the palette itself takes the focus
        } m_Palette;

        // The last hint that was shown, and the frame: F1 pins it (Explain) if it was on screen a moment ago.
        hint_text   m_LastHint;
        int         m_LastHintFrame = -100;

        // The pinned hint card: an action's card kept on screen, with what can be done to its key. Drawn by ximgui_actions_ui.h.
        struct pinned_state
        {
            bool         m_bOpen      = false;
            int          m_OpenFrame  = 0;
            std::string  m_Path;                                 // the action
            ImVec2       m_Pos{ 0, 0 };                          // where the cursor was
            ImGuiWindow* m_pPrevFocus = nullptr;
        } m_Pinned;

        // The host jumps to the Keymap page, at this action. Unset: the card has no "Show in keymap".
        std::function<void(const std::string&)> m_OnShowInKeymap;

        struct capture_state
        {
            std::string   m_Path;                                // the action whose key is being captured ("" = not capturing)
            ImGuiKeyChord m_Chord = 0;                           // a captured chord that another action already has...
            std::string   m_ConflictWith;                        // ...and that action's path (waiting for Replace / Cancel)
        } m_Capture;

        // Presets: keymap files that can be the base of a person's own, and saving one's own keys as a new shareable one. Filled in by
        // ApplyKeymapLayers (ximgui_actions_keymap.h); empty until a keymap is loaded.
        struct preset_api
        {
            std::string                                     m_Base;         // the preset the person's file sits on ("" = the code defaults)
            std::function<std::vector<std::string>()>       m_List;         // the presets there are (every keymap file but the person's own)
            std::function<void(const std::string&)>         m_SetBase;      // sit on this one ("" = the defaults)
            std::function<std::string(const std::string&)>  m_SaveAs;       // write my keys as a keymap named so; "" = done, otherwise why not
        } m_Presets;

        // A person edited a binding on the keymap page: Path, the new keys ("" = unbound), or bReset (back to the layer below).
        // The keymap layers install this; without it the edit only lives in m_Overrides.
        std::function<void(context&, const std::string&, const std::string&, bool)> m_OnBindingChange;

        // The host's own search box (xeditor::RenderTreeSearchBar), so the palette looks like every other search field. Text, width,
        // "put the keyboard in it"; true when the text changed. Without one the palette uses a plain ImGui input.
        bool (*m_pSearchBox)(std::string&, float, bool) noexcept = nullptr;

        // The keyboard overlay (F1): every key coloured by what is on it in the place the person was working. Drawn by ximgui_actions_ui.h.
        struct overlay_state
        {
            bool                     m_bOpen      = false;
            int                      m_OpenFrame  = 0;
            bool                     m_bCtrl = false, m_bShift = false, m_bAlt = false;     // the modifier layer being shown (held keys add to it)
            int                      m_PinnedKey  = 0;           // the key that was clicked (an ImGuiKey): its details stay on screen
            ImGuiWindow*             m_pPrevFocus = nullptr;
            std::vector<scope_entry> m_Order;                    // what was live THEN
            std::vector<gesture_set> m_Gestures;                 // the mouse gestures that applied THEN
        } m_Overlay;

        // The keyboard under the palette's search box (it follows the selected row). The person's choice, so it outlives the palette.
        bool m_bKeyboardInPalette = true;

        // F1: explains what the mouse rests on - pins the card of the action it has a hint for - or, over nothing, shows the keyboard.
        void Explain()
        {
            if (m_Pinned.m_bOpen)  { ClosePinned();  return; }
            if (m_Overlay.m_bOpen) { CloseOverlay(); return; }
            if (ImGui::GetFrameCount() - m_LastHintFrame <= 2 && FindByPath(m_LastHint.m_Detail))
            {
                m_Pinned = {};
                m_Pinned.m_bOpen     = true;
                m_Pinned.m_OpenFrame = ImGui::GetFrameCount();
                m_Pinned.m_Path      = m_LastHint.m_Detail;
                m_Pinned.m_Pos       = ImGui::GetIO().MousePos;
                if (ImGuiContext* g = ImGui::GetCurrentContext(); g) m_Pinned.m_pPrevFocus = g->NavWindow;
                return;
            }
            OpenOverlay();
        }
        void ClosePinned()
        {
            if (m_Capture.m_Path == m_Pinned.m_Path) m_Capture = {};
            if (m_Pinned.m_pPrevFocus) ImGui::FocusWindow(m_Pinned.m_pPrevFocus);
            m_Pinned = {};
        }

        void OpenOverlay()
        {
            if (m_Overlay.m_bOpen) { CloseOverlay(); return; }
            m_Overlay = {};
            m_Overlay.m_bOpen     = true;
            m_Overlay.m_OpenFrame = ImGui::GetFrameCount();
            if (ImGuiContext* g = ImGui::GetCurrentContext(); g) m_Overlay.m_pPrevFocus = g->NavWindow;
            auto Order = FocusOrder();
            if (Order.empty()) Order = AllEntries();
            for (const scope_entry* p : Order) m_Overlay.m_Order.push_back(*p);
            m_Overlay.m_Gestures = GestureOrder();
        }
        void CloseOverlay()
        {
            if (m_Overlay.m_pPrevFocus) ImGui::FocusWindow(m_Overlay.m_pPrevFocus);
            m_Overlay = {};
        }

        // How a hint is drawn (xeditor::hint::Draw): the hint window of the editors, kept on screen. Without one, a plain ImGui tooltip.
        void (*m_pShowHint)(const hint_text&) noexcept = nullptr;

        bool IsCapturing() const noexcept { return !m_Capture.m_Path.empty(); }

        // Opens the palette (or closes it when it is open) over what is live in the window that has the focus.
        void OpenPalette()
        {
            if (m_Palette.m_bOpen) { ClosePalette(); return; }
            m_Palette = {};
            m_Palette.m_bOpen     = true;
            m_Palette.m_bFocus    = true;
            m_Palette.m_OpenFrame = ImGui::GetFrameCount();
            if (ImGuiContext* g = ImGui::GetCurrentContext(); g) m_Palette.m_pPrevFocus = g->NavWindow;
            auto Order = FocusOrder();
            if (Order.empty()) Order = AllEntries();
            for (const scope_entry* p : Order) m_Palette.m_Order.push_back(*p);
        }

        void ClosePalette()
        {
            if (m_Palette.m_pPrevFocus) ImGui::FocusWindow(m_Palette.m_pPrevFocus);
            m_Palette = {};
        }

        // Every type with actions (the registered ones, and any other that was scoped), indexed.
        const std::unordered_map<const xproperty::type::object*, std::vector<action_info>>& Types() noexcept
        {
            IndexKnownTypes();
            return m_Types;
        }

        void IndexKnownTypes()
        {
            if (m_KnownIndexed == OwnerTypes().size()) return;
            for (auto* pFn : OwnerTypes()) if (const auto* pObj = pFn()) (void)Actions(*pObj);
            m_KnownIndexed = OwnerTypes().size();
        }

        // Another action of the same object and scope that already has this chord (it could never be reached next to A).
        const action_info* FindConflict(const action_info& A, ImGuiKeyChord C)
        {
            for (auto& B : Actions(*A.m_pObject))
            {
                if (&B == &A || B.m_Scope != A.m_Scope) continue;
                for (const ImGuiKeyChord X : Chords(B)) if (X == C) return &B;
            }
            return nullptr;
        }

        // Is A live for this scope entry and visible?
        bool Live(const scope_entry& E, const action_info& A) { return IsLive(E, A) && !Hidden(A, E.m_pInstance); }

        // Run it at the start of the next frame (what menus, buttons and the palette do).
        void Queue(const action_info& A, void* pInstance) { m_Pending.push_back({ &A, pInstance }); }

        void SetKeys(const std::string& Path, const std::string& Keys)
        {
            m_Overrides[Path] = ParseChecked(Keys, "keymap page");
            if (m_OnBindingChange) m_OnBindingChange(*this, Path, Keys, false);
        }

        // Can it run where the person was working? "" = yes; otherwise why not (including "not available there").
        std::string ReasonNow(const action_info& A)
        {
            for (const scope_entry* pE : AllEntries())
                if (pE->m_pObject == A.m_pObject && Live(*pE, A)) return Reason(A, pE->m_pInstance);
            return "not available where you were working";
        }

        // Any action of any editor seen so far, by its path.
        const action_info* FindByPath(std::string_view Path)
        {
            if (Path.empty()) return nullptr;
            for (auto& [pObj, Actions] : Types())
                for (auto& A : Actions) if (A.m_Path == Path) return &A;
            return nullptr;
        }

        // The other action of the same editor and scope that already has Chord (the two would fight for the key). "" = none.
        std::string FindClash(const std::string& Path, ImGuiKeyChord Chord)
        {
            const action_info* pMe = FindByPath(Path);
            if (!pMe) return {};
            for (auto& A : Actions(*pMe->m_pObject))
            {
                if (A.m_Path == Path || A.m_Scope != pMe->m_Scope) continue;
                for (const ImGuiKeyChord C : Chords(A)) if (C == Chord) return A.m_Path;
            }
            return {};
        }

        // "Set key": while an action's key is being captured, the next key pressed becomes its key. Esc cancels. A key another action of the
        // same scope has waits for Replace / Cancel (ResolveClash). Call every frame while IsCapturing() and the person has to press something.
        void PollCapture()
        {
            if (!m_Capture.m_ConflictWith.empty()) return;                      // waiting for the person to choose
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_Capture = {}; return; }
            for (int k = ImGuiKey_Tab; k <= ImGuiKey_Oem102; ++k)
            {
                if (k >= ImGuiKey_LeftCtrl && k <= ImGuiKey_RightSuper) continue;     // a modifier alone is not a key
                if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) continue;
                const ImGuiKeyChord Chord = (static_cast<ImGuiKeyChord>(ImGui::GetIO().KeyMods) & ImGuiMod_Mask_) | k;
                if (std::string Other = FindClash(m_Capture.m_Path, Chord); !Other.empty()) { m_Capture.m_Chord = Chord; m_Capture.m_ConflictWith = std::move(Other); }
                else { SetKeys(m_Capture.m_Path, ChordName(Chord)); m_Capture = {}; }
                return;
            }
        }

        // The person chose: Replace takes the key off the other action and gives it to this one; Cancel leaves everything as it was.
        void ResolveClash(bool bReplace)
        {
            if (bReplace)
                if (const action_info* pOther = FindByPath(m_Capture.m_ConflictWith); pOther)
                {
                    std::string Rest;
                    for (const ImGuiKeyChord C : Chords(*pOther)) if (C != m_Capture.m_Chord) Rest += (Rest.empty() ? "" : ",") + ChordName(C);
                    SetKeys(pOther->m_Path, Rest);
                    SetKeys(m_Capture.m_Path, ChordName(m_Capture.m_Chord));
                }
            m_Capture = {};
        }

        void ResetKeys(const std::string& Path)
        {
            if (m_OnBindingChange) m_OnBindingChange(*this, Path, {}, true);
            else                   m_Overrides.erase(Path);
        }

        //------------------------------------------------------------------------------------------
        // Index
        //------------------------------------------------------------------------------------------

        const std::vector<action_info>& Actions(const xproperty::type::object& Obj)
        {
            auto It = m_Types.find(&Obj);
            if (It != m_Types.end()) return It->second;

            std::vector<action_info> Out;
            Walk(Obj, std::string{}, static_cast<const xproperty::type::members::scope&>(Obj), Out);
            return m_Types.emplace(&Obj, std::move(Out)).first->second;
        }

        // SubPath is relative to the object: "Entity/Delete".
        const action_info* Find(const xproperty::type::object& Obj, std::string_view SubPath)
        {
            for (auto& A : Actions(Obj))
                if (A.m_Path.size() == std::string_view(Obj.m_pName).size() + 1 + SubPath.size()
                 && std::string_view(A.m_Path).substr(std::string_view(Obj.m_pName).size() + 1) == SubPath) return &A;
            return nullptr;
        }

        std::vector<ImGuiKeyChord> Chords(const action_info& A) const
        {
            if (auto It = m_Overrides.find(A.m_Path); It != m_Overrides.end()) return It->second;
            return A.m_Default;
        }

        // The keys of an action by its full path (what a menu shows); nullopt when no indexed action has that path.
        std::optional<std::string> KeysOfPath(std::string_view Path)
        {
            IndexKnownTypes();
            for (auto& [pObj, Actions] : m_Types)
                for (auto& A : Actions) if (A.m_Path == Path) return KeysText(A);
            return std::nullopt;
        }

        std::string KeysText(const action_info& A) const
        {
            const auto C = Chords(A);
            return C.empty() ? std::string{} : ChordName(C.front());
        }

        //------------------------------------------------------------------------------------------
        // Availability and running
        //------------------------------------------------------------------------------------------

        bool Hidden(const action_info& A, void* pInstance)
        {
            if (auto* F = A.m_pMember->getUserData<xproperty::settings::member_dynamic_flags_t>(); F)
                return F->m_pCallback(pInstance, m_Settings).m_bDontShow;
            return false;
        }

        // "" when the action can run now, otherwise why not.
        std::string Reason(const action_info& A, void* pInstance)
        {
            if (auto* R = A.m_pMember->getUserData<xproperty::settings::member_dynamic_reason_t>(); R)
                if (const char* p = R->m_pCallback(pInstance, m_Settings); p && *p) return p;
            if (auto* F = A.m_pMember->getUserData<xproperty::settings::member_dynamic_flags_t>(); F)
                if (F->m_pCallback(pInstance, m_Settings).m_bShowReadOnly) return "unavailable";
            return {};
        }

        // Must be called outside any Begin/End the action does not own (NewFrame() and the pipe do).
        bool Run(const action_info& A, void* pInstance)
        {
            const auto* pFn = std::get_if<xproperty::type::members::function>(&A.m_pMember->m_Variant);
            if (pFn == nullptr || pInstance == nullptr) return false;
            return static_cast<bool>(pFn->TryCallFunction(*static_cast<char*>(pInstance)));
        }

        //------------------------------------------------------------------------------------------
        // Scopes: immediate mode, registered again every frame (nothing to unregister, nothing dangles)
        //------------------------------------------------------------------------------------------

        template<typename T, typename...T_PREFIXES>
        void Scope(T& Owner, T_PREFIXES...Prefixes)
        {
            ScopeObject(*xproperty::getObject(Owner), &Owner, { Prefixes... });
        }

        // Call inside the panel's Begin/End: this window (and its child windows) makes these scopes live.
        void ScopeObject(const xproperty::type::object& Obj, void* pInstance, std::initializer_list<const char*> Prefixes)
        {
            if (ImGuiWindow* pWindow = ImGui::GetCurrentWindowRead(); pWindow)
                m_Windows.push_back({ pWindow->ID, MakeEntry(Obj, pInstance, Prefixes) });
        }

        // The same, for a window that is not the one being drawn (an editor that owns several named windows registers
        // them all from one place, instead of from inside each Begin/End).
        template<typename T, typename...T_PREFIXES>
        void ScopeWindow(ImGuiWindow* pWindow, T& Owner, T_PREFIXES...Prefixes)
        {
            if (pWindow) m_Windows.push_back({ pWindow->ID, MakeEntry(*xproperty::getObject(Owner), &Owner, { Prefixes... }) });
        }

        // Call inside the panel's Begin/End: the mouse gestures of this surface (see gesture). The table must outlive the frame (make it static).
        void Gestures(const char* pSurface, std::span<const gesture> List)
        {
            if (ImGuiWindow* pWindow = ImGui::GetCurrentWindowRead(); pWindow) m_WindowGestures.push_back({ pWindow->ID, pSurface, List });
        }
        void GesturesWindow(ImGuiWindow* pWindow, const char* pSurface, std::span<const gesture> List)
        {
            if (pWindow) m_WindowGestures.push_back({ pWindow->ID, pSurface, List });
        }

        // The gestures that apply where the mouse is: the window under it (innermost first), then the focused one. One entry per surface.
        std::vector<gesture_set> GestureOrder(bool bHoveredOnly = false)
        {
            std::vector<gesture_set> Out;
            auto Chain = [&](ImGuiWindow* pW)
            {
                for (; pW; pW = pW->ParentWindow)
                    for (const gesture_set& G : m_PrevWindowGestures)
                        if (G.m_Window == pW->ID && std::none_of(Out.begin(), Out.end(), [&](const gesture_set& X) { return X.m_pSurface == G.m_pSurface; })) Out.push_back(G);
            };
            if (ImGuiContext* g = ImGui::GetCurrentContext(); g) { Chain(g->HoveredWindow); if (!bHoveredOnly) Chain(g->NavWindow); }
            return Out;
        }

        // Live whenever nothing more specific takes the key (the host's own actions).
        template<typename T, typename...T_PREFIXES>
        void Global(T& Owner, T_PREFIXES...Prefixes)
        {
            auto E = MakeEntry(*xproperty::getObject(Owner), &Owner, { Prefixes... });
            E.m_bGlobal = true;
            m_Globals.push_back(std::move(E));
        }

        //------------------------------------------------------------------------------------------
        // Keys
        //------------------------------------------------------------------------------------------

        // Call after ImGui::NewFrame, before the panels draw: runs what menus/buttons chose last frame, then resolves the
        // chords pressed this frame against the scopes of the last COMPLETED frame (EndFrame keeps them).
        void NewFrame()
        {
            RunPending();
            if (IsCapturing()) return;          // the keymap page is listening for a key: it must not run an action

            const ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;

            auto IsPressed = [&](ImGuiKeyChord C)
            {
                return (static_cast<ImGuiKeyChord>(io.KeyMods) & ImGuiMod_Mask_) == (C & ImGuiMod_Mask_)
                    && ImGui::IsKeyPressed(static_cast<ImGuiKey>(C & ~ImGuiMod_Mask_), false);
            };
            const auto R = Resolve(FocusOrder(), IsPressed, io.WantTextInput);
            if (R.m_bMatched) { m_Last = R; m_OnInput.NotifyAll(*this, R); }
        }

        // Call once the panels are drawn: this frame's scopes become the ones keys (and the pipe, between frames) resolve against.
        void EndFrame()
        {
            if (m_Types.size() != m_ValidatedTypes) { m_ValidatedTypes = m_Types.size(); (void)Validate(); }   // a new editor type was seen: check its keys
            m_PrevWindows = std::move(m_Windows);   m_Windows.clear();
            m_PrevWindowGestures = std::move(m_WindowGestures);   m_WindowGestures.clear();
            m_PrevGlobals = std::move(m_Globals);   m_Globals.clear();
        }

        // A key press that did not come from the keyboard (the pipe, tests): same resolution as the real thing.
        input_result Press(ImGuiKeyChord Chord)
        {
            auto Order = FocusOrder();
            if (Order.empty()) Order = AllEntries();      // nothing focused (headless-ish, or the window is in the background)
            const auto R = Resolve(Order, [&](ImGuiKeyChord C) { return C == Chord; }, ImGui::GetIO().WantTextInput);
            m_Last = R; m_Last.m_Keys = ChordName(Chord);
            if (R.m_bMatched) m_OnInput.NotifyAll(*this, m_Last);
            return m_Last;
        }

        // Run an action by its full path, wherever it is live. "" when it ran, otherwise the reason (or "not found").
        std::string RunPath(std::string_view Path)
        {
            for (const scope_entry* pE : AllEntries())
                for (auto& A : Actions(*pE->m_pObject))
                    if (A.m_Path == Path)
                    {
                        if (auto Why = Reason(A, pE->m_pInstance); !Why.empty()) return Why;
                        return Run(A, pE->m_pInstance) ? std::string{} : std::string("not a function");
                    }
            return "no live action with that path";
        }

        // One line per live action: path, keys, "ok" or why not.
        std::string List()
        {
            std::string S;
            std::vector<std::pair<const action_info*, void*>> Seen;        // an editor registers several windows: list each of its actions once
            for (const scope_entry* pE : AllEntries())
                for (auto& A : Actions(*pE->m_pObject))
                {
                    if (!IsLive(*pE, A) || Hidden(A, pE->m_pInstance)) continue;
                    if (std::find(Seen.begin(), Seen.end(), std::make_pair(&A, pE->m_pInstance)) != Seen.end()) continue;
                    Seen.emplace_back(&A, pE->m_pInstance);
                    const auto Why = Reason(A, pE->m_pInstance);
                    S += A.m_Path + "\t" + KeysText(A) + "\t" + (Why.empty() ? std::string("ok") : Why) + "\n";
                }
            return S;
        }

        // The actions that can be used where the person was working, as (path, "keys - help"): the palette's list, for others to offer too.
        std::vector<std::pair<std::string, std::string>> LiveActions()
        {
            std::vector<std::pair<std::string, std::string>> Out;
            std::vector<std::pair<const action_info*, void*>> Seen;
            for (const scope_entry* pE : AllEntries())
                for (auto& A : Actions(*pE->m_pObject))
                {
                    if (!IsLive(*pE, A) || Hidden(A, pE->m_pInstance)) continue;
                    if (std::find(Seen.begin(), Seen.end(), std::make_pair(&A, pE->m_pInstance)) != Seen.end()) continue;
                    Seen.emplace_back(&A, pE->m_pInstance);
                    const std::string Keys = KeysText(A);
                    Out.push_back({ A.m_Path, (Keys.empty() ? std::string{} : "[" + Keys + "] ") + (A.m_pHelp ? A.m_pHelp : "") });
                }
            return Out;
        }

        // Every mouse gesture the surfaces of the last frame declared: "Surface<tab>LMB click<tab>Name" per line.
        std::string ListGestures()
        {
            std::string S;
            for (const gesture_set& G : m_PrevWindowGestures)
                for (const gesture& X : G.m_List) S += std::string(G.m_pSurface) + "\t" + GestureText(X) + "\t" + X.m_pName + "\n";
            return S;
        }

        //------------------------------------------------------------------------------------------
        // Menus, toolbars, hints
        //------------------------------------------------------------------------------------------

        template<typename T>
        bool MenuItem(T& Owner, std::string_view SubPath, const char* pLabel = nullptr)
        {
            const auto* pA = Find(*xproperty::getObject(Owner), SubPath);
            if (pA == nullptr) { ImGui::TextDisabled("%.*s?", int(SubPath.size()), SubPath.data()); return false; }

            const auto Why  = Reason(*pA, &Owner);
            const auto Keys = KeysText(*pA);
            const bool bHit = ImGui::MenuItem(pLabel ? pLabel : pA->m_pName, Keys.empty() ? nullptr : Keys.c_str(), false, Why.empty());
            Hint(*pA, &Owner);
            if (bHit) m_Pending.push_back({ pA, &Owner });
            return bHit;
        }

        template<typename T>
        bool ToolbarButton(T& Owner, std::string_view SubPath, const char* pLabel = nullptr)
        {
            const auto* pA = Find(*xproperty::getObject(Owner), SubPath);
            if (pA == nullptr) { ImGui::TextDisabled("%.*s?", int(SubPath.size()), SubPath.data()); return false; }
            return Button(*pA, &Owner, pLabel);
        }

        bool Button(const action_info& A, void* pInstance, const char* pLabel = nullptr)
        {
            const auto Why = Reason(A, pInstance);
            ImGui::BeginDisabled(!Why.empty());
            const bool bHit = ImGui::Button(pLabel ? pLabel : A.m_pName);
            ImGui::EndDisabled();
            Hint(A, pInstance);
            if (bHit) m_Pending.push_back({ &A, pInstance });
            return bHit;
        }

        // The user's layout of a toolbar (see m_Toolbars). False when there is none: the owner draws its default.
        // Items whose action is not live right now (another editor's, or a plugin that is not loaded) are skipped.
        bool DrawToolbar(std::string_view Name, bool bVertical)
        {
            const auto It = m_Toolbars.find(std::string(Name));
            if (It == m_Toolbars.end()) return false;

            bool bFirst = true;
            for (const std::string& Item : It->second)
            {
                if (Item == "-")
                {
                    if (!bVertical) { ImGui::SameLine(0, 8); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); }
                    else            ImGui::Separator();
                    bFirst = false;
                    continue;
                }
                const auto [pA, pInst] = FindLive(Item);
                if (pA == nullptr) continue;
                if (!bFirst && !bVertical) ImGui::SameLine(0, 4);
                Button(*pA, pInst);
                bFirst = false;
            }
            return true;
        }

        // Where an action is live right now (any panel), by full path.
        std::pair<const action_info*, void*> FindLive(std::string_view Path)
        {
            for (const scope_entry* pE : AllEntries())
                for (auto& A : Actions(*pE->m_pObject))
                    if (A.m_Path == Path && IsLive(*pE, A) && !Hidden(A, pE->m_pInstance)) return { &A, pE->m_pInstance };
            return { nullptr, nullptr };
        }

        // "ToolSelect" -> "Tool Select"
        static std::string Prettify(const char* pName)
        {
            std::string Out;
            for (const char* p = pName; *p; ++p)
            {
                if (p != pName && std::isupper(static_cast<unsigned char>(*p)) && !std::isupper(static_cast<unsigned char>(p[-1]))) Out += ' ';
                Out += *p;
            }
            return Out;
        }

        hint_text MakeHint(const action_info& A, const std::string& Why) const
        {
            return { Prettify(A.m_pName), A.m_pHelp ? A.m_pHelp : "", KeysText(A), Why, A.m_Path };
        }

        void ShowHint(const hint_text& H)
        {
            m_LastHint = H;  m_LastHintFrame = ImGui::GetFrameCount();          // what F1 explains (Explain)
            if (m_pShowHint) { m_pShowHint(H); return; }
            std::string Text = H.m_Topic + (H.m_Shortcut.empty() ? "" : "  (" + H.m_Shortcut + ")");
            if (!H.m_Disabled.empty()) Text += " - " + H.m_Disabled;
            ImGui::SetTooltip("%s", Text.c_str());
        }

        // After any item that stands for an action: its hint while the mouse rests on it.
        void Hint(const action_info& A, void* pInstance)
        {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) return;
            ShowHint(MakeHint(A, Reason(A, pInstance)));
        }

        // For a library that asks "who draws the help of this path?" (the inspector's m_OnHelp). True when the path is an action.
        bool HintForPath(const xproperty::type::object& Obj, void* pInstance, std::string_view Path)
        {
            for (auto& A : Actions(Obj))
                if (A.m_Path == Path) { ShowHint(MakeHint(A, Reason(A, pInstance))); return true; }
            return false;
        }

        // The hint of an action, without asking whether it can run (the keymap page does not know the instance).
        void HintCard(const action_info& A) { ShowHint(MakeHint(A, {})); }

        // The actions chosen from menus/buttons this frame run here (outside every menu), once, in order.
        void RunPending()
        {
            auto Todo = std::move(m_Pending); m_Pending.clear();
            for (auto& P : Todo) Run(*P.first, P.second);
        }

    private:

        struct window_scope { ImGuiID m_Window; scope_entry m_Scope; };

        std::size_t                                                                     m_ValidatedTypes = 0;
        std::size_t                                                                     m_KnownIndexed   = 0;
        std::unordered_map<const xproperty::type::object*, std::vector<action_info>>    m_Types;
        std::vector<window_scope>                                                       m_Windows, m_PrevWindows;
        std::vector<gesture_set>                                                        m_WindowGestures, m_PrevWindowGestures;
        std::vector<scope_entry>                                                        m_Globals, m_PrevGlobals;
        std::vector<std::pair<const action_info*, void*>>                               m_Pending;

        static scope_entry MakeEntry(const xproperty::type::object& Obj, void* pInstance, std::initializer_list<const char*> Prefixes)
        {
            scope_entry E{ &Obj, pInstance, {} };
            for (const char* p : Prefixes) E.m_Prefixes.emplace_back(p);
            return E;
        }

        void Walk(const xproperty::type::object& Obj, const std::string& Scope, const xproperty::type::members::scope& S, std::vector<action_info>& Out)
        {
            const std::string_view ObjName = Obj.m_pName;
            for (const auto& M : S.m_Members)
            {
                if (const auto* pScope = std::get_if<xproperty::type::members::scope>(&M.m_Variant))
                {
                    Walk(Obj, Scope.empty() ? std::string(M.m_pName) : Scope + "/" + M.m_pName, *pScope, Out);
                }
                else if (std::holds_alternative<xproperty::type::members::function>(M.m_Variant))
                {
                    action_info A;
                    A.m_Scope   = Scope;
                    A.m_Path    = std::string(ObjName) + "/" + (Scope.empty() ? "" : Scope + "/") + M.m_pName;
                    A.m_pName   = M.m_pName;
                    A.m_pMember = &M;
                    A.m_pObject = &Obj;
                    if (auto* H = M.getUserData<xproperty::settings::member_help_t>(); H) A.m_pHelp = H->m_pHelp;
                    if (auto* K = M.getUserData<member_keys_t>(); K) { A.m_Default = ParseChords(K->m_pKeys); A.m_bInText = K->m_bInText; }
                    Out.push_back(std::move(A));
                }
            }
        }

        // The panel's listed scopes (and anything nested in them) plus the object's root actions.
        static bool IsLive(const scope_entry& E, const action_info& A)
        {
            if (A.m_Scope.empty()) return true;
            for (auto& P : E.m_Prefixes)
                if (A.m_Scope == P || (A.m_Scope.size() > P.size() && A.m_Scope.compare(0, P.size(), P) == 0 && A.m_Scope[P.size()] == '/')) return true;
            return false;
        }

        // Focus first, then what the mouse is over, then the host's own. Innermost first inside each.
        std::vector<const scope_entry*> FocusOrder()
        {
            std::vector<const scope_entry*> Out;
            auto Add = [&](const scope_entry* p) { if (std::find(Out.begin(), Out.end(), p) == Out.end()) Out.push_back(p); };
            auto Chain = [&](ImGuiWindow* pW)
            {
                for (; pW; pW = pW->ParentWindow)
                    for (auto& W : m_PrevWindows) if (W.m_Window == pW->ID) Add(&W.m_Scope);
            };
            if (ImGuiContext* g = ImGui::GetCurrentContext(); g)
            {
                Chain(g->NavWindow);
                // What the mouse is over counts too (Q/W/E/R over the viewport while the tree has the focus), but only for the editor
                // that has the focus: a Delete aimed at the asset browser must never reach an entity of a Level that is merely under the mouse.
                const std::size_t nFocus = Out.size();
                std::vector<const scope_entry*> Hovered;
                { auto Save = std::move(Out); Out.clear(); Chain(g->HoveredWindow); Hovered = std::move(Out); Out = std::move(Save); }
                for (const scope_entry* pH : Hovered)
                    if (nFocus == 0 || std::any_of(Out.begin(), Out.begin() + nFocus, [&](const scope_entry* pF) { return pF->m_pInstance == pH->m_pInstance; })) Add(pH);
            }
            for (auto& G : m_PrevGlobals) Add(&G);
            return Out;
        }

        std::vector<const scope_entry*> AllEntries()
        {
            std::vector<const scope_entry*> Out;
            for (auto& W : m_PrevWindows) Out.push_back(&W.m_Scope);
            for (auto& G : m_PrevGlobals) Out.push_back(&G);
            return Out;
        }

        // The first live, available action bound to a pressed chord runs. If every bound one is unavailable nothing runs
        // and the first reason is reported.
        template<typename T_PRESSED>
        input_result Resolve(const std::vector<const scope_entry*>& Order, T_PRESSED&& IsPressed, bool bTextInput)
        {
            input_result R;
            for (const scope_entry* pE : Order)
            {
                // Actions are visited prefix by prefix (the order the panel listed them), root actions last.
                auto Visit = [&](const action_info& A) -> bool
                {
                    if (bTextInput && !A.m_bInText) return false;
                    for (const ImGuiKeyChord C : Chords(A))
                    {
                        if (!IsPressed(C)) continue;
                        if (Hidden(A, pE->m_pInstance)) continue;
                        const auto Why = Reason(A, pE->m_pInstance);
                        if (R.m_Path.empty()) { R.m_bMatched = true; R.m_Keys = ChordName(C); R.m_Path = A.m_Path; R.m_Reason = Why; }
                        if (!Why.empty()) continue;
                        R.m_bMatched = true; R.m_Keys = ChordName(C); R.m_Path = A.m_Path; R.m_Reason.clear();
                        R.m_bRan = Run(A, pE->m_pInstance);
                        return true;
                    }
                    return false;
                };

                const auto& All = Actions(*pE->m_pObject);
                for (const std::string& P : pE->m_Prefixes)
                    for (auto& A : All) if (!A.m_Scope.empty() && IsLive(*pE, A) && (A.m_Scope == P || A.m_Scope.starts_with(P + "/")) && Visit(A)) return R;
                for (auto& A : All) if (A.m_Scope.empty() && Visit(A)) return R;
            }
            return R;
        }
    };
}

#endif // XIMGUI_ACTIONS_H
