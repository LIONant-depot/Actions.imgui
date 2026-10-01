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
    };

    struct input_result
    {
        bool         m_bMatched = false;    // some live action is bound to the chord
        bool         m_bRan     = false;
        std::string  m_Keys;
        std::string  m_Path;                // the action it went to (or the first one refused)
        std::string  m_Reason;              // why it did not run
    };

    // What a hint card shows; listeners of context::m_OnCardSection add their own lines to it.
    struct card
    {
        const action_info*  m_pAction   = nullptr;
        void*               m_pInstance = nullptr;
    };

    //==============================================================================================
    // The context
    //==============================================================================================

    struct context
    {
        xproperty::settings::context                                m_Settings;
        xdelegate::thread_unsafe<context&, const card&>             m_OnCardSection;    // several listeners may each add a section to a hint card
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

        struct capture_state
        {
            std::string   m_Path;                                // the action whose key is being captured ("" = not capturing)
            ImGuiKeyChord m_Chord = 0;                           // a captured chord that another action already has...
            std::string   m_ConflictWith;                        // ...and that action's path (waiting for Replace / Cancel)
        } m_Capture;

        // A person edited a binding on the keymap page: Path, the new keys ("" = unbound), or bReset (back to the layer below).
        // The keymap layers install this; without it the edit only lives in m_Overrides.
        std::function<void(context&, const std::string&, const std::string&, bool)> m_OnBindingChange;

        // The host's own search box (xeditor::RenderTreeSearchBar), so the palette looks like every other search field. Text, width,
        // "put the keyboard in it"; true when the text changed. Without one the palette uses a plain ImGui input.
        bool (*m_pSearchBox)(std::string&, float, bool) noexcept = nullptr;

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

        const std::unordered_map<const xproperty::type::object*, std::vector<action_info>>& Types() const noexcept { return m_Types; }

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

        // Live whenever nothing more specific takes the key (the host's own actions).
        template<typename T, typename...T_PREFIXES>
        void Global(T& Owner, T_PREFIXES...Prefixes)
        {
            m_Globals.push_back(MakeEntry(*xproperty::getObject(Owner), &Owner, { Prefixes... }));
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
            for (const scope_entry* pE : AllEntries())
                for (auto& A : Actions(*pE->m_pObject))
                {
                    if (!IsLive(*pE, A) || Hidden(A, pE->m_pInstance)) continue;
                    const auto Why = Reason(A, pE->m_pInstance);
                    S += A.m_Path + "\t" + KeysText(A) + "\t" + (Why.empty() ? std::string("ok") : Why) + "\n";
                }
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

        // After any item that stands for an action. One line on hover; hold Alt for the full card.
        void Hint(const action_info& A, void* pInstance)
        {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip)) return;
            const auto Why  = Reason(A, pInstance);
            const auto Keys = KeysText(A);

            if (!ImGui::GetIO().KeyAlt)
            {
                if (!Why.empty())        ImGui::SetTooltip("%s - %s", A.m_pName, Why.c_str());
                else if (!Keys.empty())  ImGui::SetTooltip("%s  (%s)", A.m_pName, Keys.c_str());
                else                     ImGui::SetTooltip("%s", A.m_pName);
                return;
            }
            DrawCard(A, pInstance, Why, Keys);
        }

        // For a library that asks "who draws the help of this path?" (the inspector's m_OnHelp). True when the path is an action.
        bool HintForPath(const xproperty::type::object& Obj, void* pInstance, std::string_view Path)
        {
            for (auto& A : Actions(Obj))
                if (A.m_Path == Path)
                {
                    const auto Why  = Reason(A, pInstance);
                    DrawCard(A, pInstance, Why, KeysText(A));
                    return true;
                }
            return false;
        }

        // The hint card of an action, without asking whether it can run (the keymap page does not know the instance).
        void HintCard(const action_info& A) { DrawCard(A, nullptr, std::string{}, KeysText(A)); }

        // The actions chosen from menus/buttons this frame run here (outside every menu), once, in order.
        void RunPending()
        {
            auto Todo = std::move(m_Pending); m_Pending.clear();
            for (auto& P : Todo) Run(*P.first, P.second);
        }

    private:

        struct window_scope { ImGuiID m_Window; scope_entry m_Scope; };

        std::size_t                                                                     m_ValidatedTypes = 0;
        std::unordered_map<const xproperty::type::object*, std::vector<action_info>>    m_Types;
        std::vector<window_scope>                                                       m_Windows, m_PrevWindows;
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
            if (ImGuiContext* g = ImGui::GetCurrentContext(); g) { Chain(g->NavWindow); Chain(g->HoveredWindow); }
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

        void DrawCard(const action_info& A, void* pInstance, const std::string& Why, const std::string& Keys)
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            if (ImGui::BeginTable("##ActionCard", 2, ImGuiTableFlags_SizingFixedFit))
            {
                auto Row = [](const char* pL, const char* pR) { ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", pL); ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(pR); };
                Row("Action:", A.m_pName);
                Row("Path:",   A.m_Path.c_str());
                if (!Keys.empty()) Row("Keys:", Keys.c_str());
                if (!Why.empty())  Row("Unavailable:", Why.c_str());
                ImGui::EndTable();
            }
            if (A.m_pHelp) { ImGui::Separator(); ImGui::TextUnformatted(A.m_pHelp); }
            m_OnCardSection.NotifyAll(*this, card{ &A, pInstance });
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };
}

#endif // XIMGUI_ACTIONS_H
