#ifndef XIMGUI_ACTIONS_UI_H
#define XIMGUI_ACTIONS_UI_H
#pragma once

// The two places a person FINDS and CHANGES keys, for ximgui_actions.h:
//
//   DrawPalette(Ctx)                  a search box over what is live where the person was working ("Ctrl+Shift+P"). Run it
//                                     once a frame, after the editors have drawn; Ctx.OpenPalette() (an action) opens it.
//   DrawKeymapPage(Ctx)               the keymap as an xproperty inspector: one row per action, "Level/Save" -> "Ctrl+S". Editing a value,
//                                     the "changed from default" marker and its revert button, the hover help: all the inspector's own.
//                                     Only a small "Set key" button (press the key you want) is added, through the inspector's append hook.
//
// Include after ximgui_actions.h and xproperty's imgui inspector (xPropertyImGuiInspector.h).

#include <map>

namespace ximgui::actions
{
    namespace details
    {
        // Lower is a better match; -1 = does not match. Every word of the query must match somewhere in the text.
        inline int Score(std::string_view Hay, std::string_view Query) noexcept
        {
            int Total = 0;
            while (!Query.empty())
            {
                while (!Query.empty() && Query.front() == ' ') Query.remove_prefix(1);
                if (Query.empty()) break;
                const auto End  = Query.find(' ');
                const auto Word = Query.substr(0, End);
                Query = (End == std::string_view::npos) ? std::string_view{} : Query.substr(End);

                int Best = -1;
                for (std::size_t i = 0; i + Word.size() <= Hay.size(); ++i)
                    if (IEquals(Hay.substr(i, Word.size()), Word))
                    {
                        const bool bWordStart = (i == 0) || Hay[i - 1] == ' ' || Hay[i - 1] == '/';
                        Best = (bWordStart ? 0 : 10) + static_cast<int>(i < 40 ? i : 40);
                        break;
                    }
                if (Best < 0)                                    // letters in order ("sav" ~ "Save")
                {
                    std::size_t w = 0;
                    for (std::size_t i = 0; i < Hay.size() && w < Word.size(); ++i)
                        if (std::tolower(static_cast<unsigned char>(Hay[i])) == std::tolower(static_cast<unsigned char>(Word[w]))) ++w;
                    if (w == Word.size()) Best = 100;
                }
                if (Best < 0) return -1;
                Total += Best;
            }
            return Total;
        }

        inline std::string JoinChords(const std::vector<ImGuiKeyChord>& C)
        {
            std::string S;
            for (auto X : C) S += (S.empty() ? "" : ",") + ChordName(X);
            return S;
        }
    }

    //==============================================================================================
    // The palette
    //==============================================================================================

    inline void DrawPalette(context& Ctx)
    {
        auto& P = Ctx.m_Palette;
        if (!P.m_bOpen) return;

        struct row { const action_info* m_pA; void* m_pInst; std::string m_Why; int m_Score; };
        std::vector<row> Rows;
        for (const scope_entry& E : P.m_Order)
            for (const action_info& A : Ctx.Actions(*E.m_pObject))
            {
                if (!Ctx.Live(E, A)) continue;
                if (std::any_of(Rows.begin(), Rows.end(), [&](const row& R) { return R.m_pA == &A && R.m_pInst == E.m_pInstance; })) continue;
                const std::string Hay = std::string(A.m_pName) + " " + A.m_Path + " " + (A.m_pHelp ? A.m_pHelp : "");
                const int S = details::Score(Hay, P.m_Query);
                if (S >= 0) Rows.push_back({ &A, E.m_pInstance, Ctx.Reason(A, E.m_pInstance), S });
            }
        if (!P.m_Query.empty()) std::stable_sort(Rows.begin(), Rows.end(), [](const row& L, const row& R) { return L.m_Score < R.m_Score; });
        // unavailable ones last, in either case
        std::stable_sort(Rows.begin(), Rows.end(), [](const row& L, const row& R) { return L.m_Why.empty() && !R.m_Why.empty(); });

        ImGuiViewport* pVp = ImGui::GetMainViewport();
        const float    W   = std::min(640.0f, pVp->Size.x * 0.8f);
        ImGui::SetNextWindowPos(ImVec2(pVp->GetCenter().x, pVp->Pos.y + pVp->Size.y * 0.12f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(W, 0.0f), ImVec2(W, pVp->Size.y * 0.7f));
        if (P.m_bFocus) ImGui::SetNextWindowFocus();

        bool bClose = false, bRestoreFocus = true;
        const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                     | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin("##ActionPalette", nullptr, Flags))
        {
            bool bTyped;
            if (Ctx.m_pSearchBox) bTyped = Ctx.m_pSearchBox(P.m_Query, ImGui::GetContentRegionAvail().x, P.m_bFocus);
            else
            {
                char Buffer[128]{};
                std::snprintf(Buffer, sizeof(Buffer), "%s", P.m_Query.c_str());
                if (P.m_bFocus) ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(-1.0f);
                bTyped = ImGui::InputTextWithHint("##query", "Type an action...", Buffer, sizeof(Buffer));
                P.m_Query = Buffer;
            }
            P.m_bFocus = false;
            if (bTyped) P.m_Selected = 0;
            ImGui::TextDisabled("Enter runs  -  Esc closes  -  Up/Down to choose");

            const int N = static_cast<int>(Rows.size());
            bool bMoved = false;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && N) { P.m_Selected = (P.m_Selected + 1) % N;     bMoved = true; }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)   && N) { P.m_Selected = (P.m_Selected + N - 1) % N; bMoved = true; }
            P.m_Selected = N ? std::clamp(P.m_Selected, 0, N - 1) : 0;

            int Activate = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) Activate = P.m_Selected;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) bClose = true;

            ImGui::Separator();
            if (ImGui::BeginChild("##rows", ImVec2(0.0f, std::min(N, 12) * ImGui::GetTextLineHeightWithSpacing() + 4.0f)))
            {
                for (int i = 0; i < N; ++i)
                {
                    const row& R = Rows[i];
                    const std::string Keys = Ctx.KeysText(*R.m_pA);
                    ImGui::PushID(i);
                    ImGui::BeginDisabled(!R.m_Why.empty());
                    const std::string Shown = R.m_pA->m_Path.substr(std::string_view(R.m_pA->m_pObject->m_pName).size() + 1);     // "Viewport/ToolMove", not "ToolMove"
                    if (ImGui::Selectable(Shown.c_str(), i == P.m_Selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) Activate = i;
                    ImGui::EndDisabled();
                    if (i == P.m_Selected && bMoved) ImGui::SetScrollHereY();
                    ImGui::SameLine(); ImGui::TextDisabled("%s", R.m_pA->m_pObject->m_pName);
                    if (!R.m_Why.empty()) { ImGui::SameLine(); ImGui::TextDisabled("- %s", R.m_Why.c_str()); }
                    if (!Keys.empty())
                    {
                        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(Keys.c_str()).x - ImGui::GetStyle().WindowPadding.x - 4.0f);
                        ImGui::TextUnformatted(Keys.c_str());
                    }
                    ImGui::PopID();
                }
                if (N == 0) ImGui::TextDisabled("Nothing matches.");
            }
            ImGui::EndChild();

            if (Activate >= 0 && Activate < N && Rows[Activate].m_Why.empty())
            {
                Ctx.Queue(*Rows[Activate].m_pA, Rows[Activate].m_pInst);
                bClose = true;
            }

            // Clicking somewhere else closes it without taking the focus back from where the person clicked.
            if (!bClose && ImGui::GetFrameCount() > P.m_OpenFrame + 2 && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                { bClose = true; bRestoreFocus = false; }
        }
        ImGui::End();

        if (bClose) { if (bRestoreFocus) Ctx.ClosePalette(); else Ctx.m_Palette = {}; }
    }

    //==============================================================================================
    // The keymap page: an xproperty object. One row per action (a std::map: path -> keys).
    //==============================================================================================

    struct keymap_page
    {
        std::map<std::string, std::string>  m_Keys;                 // "Level/Save" -> "Ctrl+S" ("A,B" for two keys, "" = unbound)
        context*                            m_pCtx = nullptr;

        // The action path of a row: the key of the map entry, "Keymap/Keys[s:Level/Save]".
        static std::string RowPath(std::string_view Path)
        {
            const auto b = Path.find("[s:");
            const auto e = Path.rfind(']');
            return (b == std::string_view::npos || e == std::string_view::npos || e < b + 3) ? std::string{} : std::string(Path.substr(b + 3, e - b - 3));
        }

        const action_info* Find(const std::string& Path) const
        {
            for (auto& [pObj, Actions] : m_pCtx->Types())
                for (auto& A : Actions) if (A.m_Path == Path) return &A;
            return nullptr;
        }

        bool Changed(const std::string& Path) const
        {
            const auto* pA = Find(Path);
            return pA && m_pCtx->Chords(*pA) != pA->m_Default;
        }

        // "Set key": press the key you want. Esc cancels. While it listens no action may run (Context::IsCapturing).
        static void Capture(xproperty::inspector&, const xproperty::type::object&, void* pInst, std::string_view Path, const xproperty::any&) noexcept
        {
            auto& Self = *static_cast<keymap_page*>(pInst);
            const std::string Row = RowPath(Path);
            if (Row.empty()) return;                     // the size row of the map, not an action
            auto& Cap = Self.m_pCtx->m_Capture;

            ImGui::PushID(Row.c_str());
            if (Cap.m_Path != Row)
            {
                if (ImGui::SmallButton("Set key")) Cap.m_Path = Row;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click, then press the key (or key combination) you want");

                // Different from the default: the inspector's override marker is not evaluated for list rows, so it is drawn here.
                if (Self.Changed(Row))
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset")) Self.m_pCtx->ResetKeys(Row);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Changed from the default - put the default back");
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "press a key... (Esc cancels)");
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) Cap.m_Path.clear();
                else
                    for (int k = ImGuiKey_Tab; k <= ImGuiKey_Oem102; ++k)
                    {
                        if (k >= ImGuiKey_LeftCtrl && k <= ImGuiKey_RightSuper) continue;     // a modifier alone is not a key
                        if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) continue;
                        const ImGuiKeyChord Chord = (static_cast<ImGuiKeyChord>(ImGui::GetIO().KeyMods) & ImGuiMod_Mask_) | k;
                        Self.m_pCtx->SetKeys(Row, ChordName(Chord));        // a clash shows up in the problems list under the page
                        Cap.m_Path.clear();
                        break;
                    }
            }
            ImGui::PopID();
        }

        XPROPERTY_DEF
        ( "Keymap", keymap_page
        , obj_member<"Keys", &keymap_page::m_Keys
            , member_array_size_readonly<>
            , member_ui_open<true>
            , member_item_width<-130.0f>                                 // room for the 'Set key' and 'Reset' buttons after the value
            , member_help<"Type the keys, e.g. Ctrl+Shift+D, or press 'Set key'. Several keys: Ctrl+Y,Ctrl+Shift+Z. Empty = unbound.">
            , member_custom_render_append<&keymap_page::Capture> >
        )
    };
    XPROPERTY_REG(keymap_page)

    // The whole page. It owns its own inspector (the Project Settings tab's is shared with the other sections, which rebuild it
    // from their own data), and looks like the one it replaces: StyleFrom is the tab's inspector, whose settings it copies.
    inline void DrawKeymapPage(context& Ctx, const xproperty::inspector& StyleFrom)
    {
        static keymap_page                          Page;
        static std::map<std::string, std::string>   Built;
        static xproperty::inspector                 Inspector{ "Keymap" };
        static bool                                 bWired = false;

        Page.m_pCtx = &Ctx;
        if (!bWired)
        {
            bWired = true;
            // The card of a row is the action's own hint card; any other help is the inspector's.
            Inspector.m_OnHelp.Register<+[](xproperty::inspector&, const xproperty::type::object& Obj, void* pInst, std::string_view Path, bool& bHandled)
            {
                if (&Obj != xproperty::getObjectByType<keymap_page>()) return;
                auto& Self = *static_cast<keymap_page*>(pInst);
                if (const auto* pA = Self.Find(keymap_page::RowPath(Path)))
                { Self.m_pCtx->HintCard(*pA); bHandled = true; }
            }>();
        }

        // What the context says now. The inspector is rebuilt only when this changes (never unconditionally).
        std::map<std::string, std::string> Now;
        for (auto& [pObj, Actions] : Ctx.Types())
            for (auto& A : Actions) Now[A.m_Path] = details::JoinChords(Ctx.Chords(A));

        if (Now != Built)
        {
            Built       = Now;
            Page.m_Keys = Now;
            Inspector.clear();
            Inspector.AppendEntity();
            Inspector.AppendEntityComponent(*xproperty::getObjectByType<keymap_page>(), &Page);
        }

        ImGui::TextWrapped("Keys of every editor you have opened. Edit a value, or click 'Set key' and press the new key. A marked row differs from the default; its button puts the default back. Saved in your own keymap file.");
        ImGui::Separator();

        Inspector.m_Settings = StyleFrom.m_Settings;           // the same row colours, paddings and spacing as the tab's own inspector
        Inspector.ShowEmbedded(Ctx.m_Settings);

        // Edited in the inspector (typed): apply whatever differs from what was built.
        std::vector<std::pair<std::string, std::string>> Edited;
        for (auto& [Path, Keys] : Page.m_Keys)
            if (auto It = Built.find(Path); It != Built.end() && It->second != Keys) Edited.push_back({ Path, Keys });
        for (auto& [Path, Keys] : Edited) Ctx.SetKeys(Path, Keys);

        if (!Ctx.m_Problems.empty())
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Problems");
            for (auto& P : Ctx.m_Problems) ImGui::TextWrapped("%s", P.c_str());
        }
    }
}

#endif // XIMGUI_ACTIONS_UI_H
