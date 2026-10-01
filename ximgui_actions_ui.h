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
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
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
            ImGui::Dummy(ImVec2(0.0f, 4.0f));            // the search box draws a focus ring: keep what follows clear of it

            const int N = static_cast<int>(Rows.size());
            bool bMoved = false;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && N) { P.m_Selected = (P.m_Selected + 1) % N;     bMoved = true; }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)   && N) { P.m_Selected = (P.m_Selected + N - 1) % N; bMoved = true; }
            P.m_Selected = N ? std::clamp(P.m_Selected, 0, N - 1) : 0;

            int Activate = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) Activate = P.m_Selected;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) bClose = true;

            if (ImGui::BeginChild("##rows", ImVec2(0.0f, std::min(N, 12) * ImGui::GetTextLineHeightWithSpacing() + 4.0f)))
            {
                for (int i = 0; i < N; ++i)
                {
                    const float RowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;      // the right edge of the row, scrollbar excluded
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
                        ImGui::SameLine(RowRight - ImGui::CalcTextSize(Keys.c_str()).x - 4.0f);
                        ImGui::TextUnformatted(Keys.c_str());
                    }
                    ImGui::PopID();
                }
                if (N == 0) ImGui::TextDisabled("Nothing matches.");
            }
            ImGui::EndChild();
            ImGui::TextDisabled("Enter runs   Esc closes   Up / Down to choose");

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
        ImGui::PopStyleVar();

        if (bClose) { if (bRestoreFocus) Ctx.ClosePalette(); else Ctx.m_Palette = {}; }
    }


    //==============================================================================================
    // The keyboard overlay (F1): the keys as a keyboard, coloured by what is on them where the person was working.
    //   dark = nothing   blue = an action   tan = a host-wide action   mauve = both, or only with other modifiers held
    // Hover a key for the actions on it (every modifier combination). Holding Ctrl / Shift / Alt, or ticking them, shows that layer.
    //==============================================================================================

    inline void DrawKeyboardOverlay(context& Ctx)
    {
        auto& O = Ctx.m_Overlay;
        if (!O.m_bOpen) return;

        struct entry { ImGuiKeyChord m_Chord; const action_info* m_pA; std::string m_Why; bool m_bGlobal; };
        std::unordered_map<int, std::vector<entry>> ByKey;
        for (const scope_entry& E : O.m_Order)
            for (const action_info& A : Ctx.Actions(*E.m_pObject))
            {
                if (!Ctx.Live(E, A)) continue;
                const std::string Why = Ctx.Reason(A, E.m_pInstance);
                for (const ImGuiKeyChord C : Ctx.Chords(A))
                {
                    auto& V = ByKey[static_cast<int>(C & ~ImGuiMod_Mask_)];
                    if (std::none_of(V.begin(), V.end(), [&](const entry& X) { return X.m_Chord == C && X.m_pA == &A; })) V.push_back({ C, &A, Why, E.m_bGlobal });
                }
            }

        const ImGuiIO& io = ImGui::GetIO();
        ImGuiKeyChord Mods = 0;
        if (O.m_bCtrl  || io.KeyCtrl)  Mods |= ImGuiMod_Ctrl;
        if (O.m_bShift || io.KeyShift) Mods |= ImGuiMod_Shift;
        if (O.m_bAlt   || io.KeyAlt)   Mods |= ImGuiMod_Alt;

        struct key { const char* m_pLabel; ImGuiKey m_Key; float m_X, m_Y, m_W; };
        static const key Keys[] =
        { {"Esc", ImGuiKey_Escape, 0, 0, 1}
        , {"F1", ImGuiKey_F1, 2, 0, 1}, {"F2", ImGuiKey_F2, 3, 0, 1}, {"F3", ImGuiKey_F3, 4, 0, 1}, {"F4", ImGuiKey_F4, 5, 0, 1}
        , {"F5", ImGuiKey_F5, 6.5f, 0, 1}, {"F6", ImGuiKey_F6, 7.5f, 0, 1}, {"F7", ImGuiKey_F7, 8.5f, 0, 1}, {"F8", ImGuiKey_F8, 9.5f, 0, 1}
        , {"F9", ImGuiKey_F9, 11, 0, 1}, {"F10", ImGuiKey_F10, 12, 0, 1}, {"F11", ImGuiKey_F11, 13, 0, 1}, {"F12", ImGuiKey_F12, 14, 0, 1}
        , {"`", ImGuiKey_GraveAccent, 0, 1.4f, 1}
        , {"1", ImGuiKey_1, 1, 1.4f, 1}, {"2", ImGuiKey_2, 2, 1.4f, 1}, {"3", ImGuiKey_3, 3, 1.4f, 1}, {"4", ImGuiKey_4, 4, 1.4f, 1}, {"5", ImGuiKey_5, 5, 1.4f, 1}
        , {"6", ImGuiKey_6, 6, 1.4f, 1}, {"7", ImGuiKey_7, 7, 1.4f, 1}, {"8", ImGuiKey_8, 8, 1.4f, 1}, {"9", ImGuiKey_9, 9, 1.4f, 1}, {"0", ImGuiKey_0, 10, 1.4f, 1}
        , {"-", ImGuiKey_Minus, 11, 1.4f, 1}, {"=", ImGuiKey_Equal, 12, 1.4f, 1}, {"Bksp", ImGuiKey_Backspace, 13, 1.4f, 2}
        , {"Tab", ImGuiKey_Tab, 0, 2.4f, 1.5f}
        , {"Q", ImGuiKey_Q, 1.5f, 2.4f, 1}, {"W", ImGuiKey_W, 2.5f, 2.4f, 1}, {"E", ImGuiKey_E, 3.5f, 2.4f, 1}, {"R", ImGuiKey_R, 4.5f, 2.4f, 1}, {"T", ImGuiKey_T, 5.5f, 2.4f, 1}
        , {"Y", ImGuiKey_Y, 6.5f, 2.4f, 1}, {"U", ImGuiKey_U, 7.5f, 2.4f, 1}, {"I", ImGuiKey_I, 8.5f, 2.4f, 1}, {"O", ImGuiKey_O, 9.5f, 2.4f, 1}, {"P", ImGuiKey_P, 10.5f, 2.4f, 1}
        , {"[", ImGuiKey_LeftBracket, 11.5f, 2.4f, 1}, {"]", ImGuiKey_RightBracket, 12.5f, 2.4f, 1}, {"\\", ImGuiKey_Backslash, 13.5f, 2.4f, 1.5f}
        , {"Caps", ImGuiKey_CapsLock, 0, 3.4f, 1.8f}
        , {"A", ImGuiKey_A, 1.8f, 3.4f, 1}, {"S", ImGuiKey_S, 2.8f, 3.4f, 1}, {"D", ImGuiKey_D, 3.8f, 3.4f, 1}, {"F", ImGuiKey_F, 4.8f, 3.4f, 1}, {"G", ImGuiKey_G, 5.8f, 3.4f, 1}
        , {"H", ImGuiKey_H, 6.8f, 3.4f, 1}, {"J", ImGuiKey_J, 7.8f, 3.4f, 1}, {"K", ImGuiKey_K, 8.8f, 3.4f, 1}, {"L", ImGuiKey_L, 9.8f, 3.4f, 1}
        , {";", ImGuiKey_Semicolon, 10.8f, 3.4f, 1}, {"'", ImGuiKey_Apostrophe, 11.8f, 3.4f, 1}, {"Enter", ImGuiKey_Enter, 12.8f, 3.4f, 2.2f}
        , {"Z", ImGuiKey_Z, 2.3f, 4.4f, 1}, {"X", ImGuiKey_X, 3.3f, 4.4f, 1}, {"C", ImGuiKey_C, 4.3f, 4.4f, 1}, {"V", ImGuiKey_V, 5.3f, 4.4f, 1}, {"B", ImGuiKey_B, 6.3f, 4.4f, 1}
        , {"N", ImGuiKey_N, 7.3f, 4.4f, 1}, {"M", ImGuiKey_M, 8.3f, 4.4f, 1}, {",", ImGuiKey_Comma, 9.3f, 4.4f, 1}, {".", ImGuiKey_Period, 10.3f, 4.4f, 1}, {"/", ImGuiKey_Slash, 11.3f, 4.4f, 1}
        , {"Space", ImGuiKey_Space, 3.8f, 5.4f, 6.2f}
        , {"Ins", ImGuiKey_Insert, 15.5f, 1.4f, 1}, {"Home", ImGuiKey_Home, 16.5f, 1.4f, 1}, {"PgUp", ImGuiKey_PageUp, 17.5f, 1.4f, 1}
        , {"Del", ImGuiKey_Delete, 15.5f, 2.4f, 1}, {"End", ImGuiKey_End, 16.5f, 2.4f, 1}, {"PgDn", ImGuiKey_PageDown, 17.5f, 2.4f, 1}
        , {"Up", ImGuiKey_UpArrow, 16.5f, 4.4f, 1}
        , {"Left", ImGuiKey_LeftArrow, 15.5f, 5.4f, 1}, {"Down", ImGuiKey_DownArrow, 16.5f, 5.4f, 1}, {"Right", ImGuiKey_RightArrow, 17.5f, 5.4f, 1}
        };

        const float U = 40.0f;
        ImGuiViewport* pVp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(pVp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(18.8f * U + 24.0f, 7.4f * U + 100.0f));
        bool bClose = false;
        const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
        if (ImGui::Begin("Keyboard##ActionsOverlay", nullptr, Flags))
        {
            ImGui::Checkbox("Ctrl", &O.m_bCtrl); ImGui::SameLine(); ImGui::Checkbox("Shift", &O.m_bShift); ImGui::SameLine(); ImGui::Checkbox("Alt", &O.m_bAlt);
            ImGui::SameLine(0, 24.0f);
            ImGui::TextDisabled("what each key does where you were working:");
            {   // the legend: the colours the keys below use
                struct swatch { ImVec4 m_Color; const char* m_pText; };
                const swatch Legend[] = { { ImVec4(0.32f, 0.50f, 0.78f, 1), "an action" }, { ImVec4(0.72f, 0.58f, 0.45f, 1), "always available (host)" }
                                        , { ImVec4(0.55f, 0.45f, 0.55f, 1), "both" }, { ImVec4(0.47f, 0.40f, 0.50f, 1), "only with other modifiers held" }, { ImVec4(0.30f, 0.30f, 0.33f, 1), "nothing" } };
                for (const swatch& L : Legend) { ImGui::TextColored(L.m_Color, "\xe2\x96\xa0"); ImGui::SameLine(0, 4.0f); ImGui::TextDisabled("%s", L.m_pText); ImGui::SameLine(0, 18.0f); }
                ImGui::NewLine();
            }
            const ImVec2 Origin = ImGui::GetCursorScreenPos();

            for (const key& K : Keys)
            {
                const auto  It   = ByKey.find(static_cast<int>(K.m_Key));
                bool bAssigned = false, bGlobal = false, bOther = false;
                if (It != ByKey.end())
                    for (const entry& E : It->second)
                    {
                        if ((E.m_Chord & ImGuiMod_Mask_) == Mods) { (E.m_bGlobal ? bGlobal : bAssigned) = true; }
                        else                                      bOther = true;
                    }
                ImVec4 Color(0.22f, 0.22f, 0.24f, 1.0f);                                        // nothing
                if      (bAssigned && bGlobal)        Color = ImVec4(0.55f, 0.45f, 0.55f, 1.0f); // mixed
                else if (bAssigned)                   Color = ImVec4(0.32f, 0.50f, 0.78f, 1.0f); // an action
                else if (bGlobal)                     Color = ImVec4(0.72f, 0.58f, 0.45f, 1.0f); // host-wide
                else if (bOther)                      Color = ImVec4(0.47f, 0.40f, 0.50f, 1.0f); // only with other modifiers
                ImGui::SetCursorScreenPos(ImVec2(Origin.x + K.m_X * U, Origin.y + K.m_Y * U));
                ImGui::PushStyleColor(ImGuiCol_Button, Color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(Color.x + 0.08f, Color.y + 0.08f, Color.z + 0.08f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color);
                ImGui::Button(K.m_pLabel, ImVec2(K.m_W * U - 3.0f, U - 3.0f));
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                {
                    // One line per action on this key: the chord, the action, why it cannot run now. The ones for the modifiers held come first.
                    std::string Lines;
                    if (It != ByKey.end())
                    {
                        auto V = It->second;
                        std::sort(V.begin(), V.end(), [&](const entry& L, const entry& R)
                            { const bool a = (L.m_Chord & ImGuiMod_Mask_) == Mods, b = (R.m_Chord & ImGuiMod_Mask_) == Mods; return a != b ? a : (L.m_Chord & ImGuiMod_Mask_) < (R.m_Chord & ImGuiMod_Mask_); });
                        for (const entry& E : V)
                            Lines += ChordName(E.m_Chord) + "   " + E.m_pA->m_Path + (E.m_Why.empty() ? "" : "  - " + E.m_Why) + "\n";
                        if (!Lines.empty()) Lines.pop_back();
                    }
                    Ctx.ShowHint({ K.m_pLabel, It == ByKey.end() ? "Nothing is bound to this key here." : Lines, "", "", "" });
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + 6.6f * U));
            ImGui::TextDisabled("Hover a key for what is on it.  F1 or Esc closes.");

            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_F1, false)) bClose = true;
        }
        ImGui::End();
        if (bClose) Ctx.CloseOverlay();
    }

    //==============================================================================================
    // The keymap page: an xproperty object. One row per action (a std::map: path -> keys).
    //==============================================================================================

    struct keymap_page
    {
        using section = std::map<std::string, std::string>;         // "Level/Save" -> "Ctrl+S" ("A,B" for two keys, "" = unbound)
        std::map<std::string, section>      m_Keys;                 // one section per editor: the first part of the path ("Level", "Editor", "Assets"...)
        context*                            m_pCtx = nullptr;

        // The editor an action belongs to: "Level/Entity/Delete" -> "Level".
        static std::string SectionOf(const std::string& ActionPath) { return ActionPath.substr(0, ActionPath.find('/')); }

        // The action path of a row: the key of the inner map entry, "Keymap/Keys[s:Level][s:Level/Save]". The section's own size row
        // ("Keymap/Keys[s:Level]") is not an action and gives "".
        static std::string RowPath(std::string_view Path)
        {
            const auto b = Path.rfind("[s:");
            const auto e = Path.rfind(']');
            if (b == std::string_view::npos || b == Path.find("[s:") || e == std::string_view::npos || e < b + 3) return {};
            return std::string(Path.substr(b + 3, e - b - 3));
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
                if (ImGui::IsItemHovered()) Self.m_pCtx->ShowHint({ "Set key", "Click, then press the key (or key combination) you want. Esc cancels.", "", "", Row });

                // Different from the default: the inspector's override marker is not evaluated for list rows, so it is drawn here.
                if (Self.Changed(Row))
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset")) Self.m_pCtx->ResetKeys(Row);
                    if (ImGui::IsItemHovered()) Self.m_pCtx->ShowHint({ "Reset", "This key was changed from the default. Puts the default back.", "", "", Row });
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
        static std::map<std::string, keymap_page::section> Built;
        static xproperty::inspector                 Inspector{ "Keymap" };
        static bool                                 bWired = false;

        Page.m_pCtx = &Ctx;
        if (!bWired)
        {
            bWired = true;
            // The card of a row is the action's own hint card; any other help is the inspector's.
            Inspector.m_OnHelp.Register<+[](xproperty::inspector&, const xproperty::inspector::help_info& Info, bool& bHandled)
            {
                if (Info.m_pRootObject != xproperty::getObjectByType<keymap_page>()) return;
                auto& Self = *static_cast<keymap_page*>(Info.m_pRootInstance);
                if (const auto* pA = Self.Find(keymap_page::RowPath(Info.m_Path)))
                { Self.m_pCtx->HintCard(*pA); bHandled = true; }
            }>();
        }

        // What the context says now. The inspector is rebuilt only when this changes (never unconditionally).
        std::map<std::string, keymap_page::section> Now;
        for (auto& [pObj, Actions] : Ctx.Types())
            for (auto& A : Actions) Now[keymap_page::SectionOf(A.m_Path)][A.m_Path] = details::JoinChords(Ctx.Chords(A));

        if (Now != Built)
        {
            Built       = Now;
            Page.m_Keys = Now;
            Inspector.clear();
            Inspector.AppendEntity();
            Inspector.AppendEntityComponent(*xproperty::getObjectByType<keymap_page>(), &Page);
        }

        ImGui::TextWrapped("Keys of every editor, one section each. Edit a value, or click 'Set key' and press the new key. A marked row differs from the default; its button puts the default back. Saved in your own keymap file.");
        ImGui::Separator();

        Inspector.m_Settings = StyleFrom.m_Settings;           // the same row colours, paddings and spacing as the tab's own inspector
        // ...and the same header colours it draws with: the theme's Header colour is the selection blue, which would paint every hovered or
        // active row (and the component header) blue. The tab's inspector pushes these greys around its draw, so this one does too.
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0x38 / 255.0f, 0x38 / 255.0f, 0x38 / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0x4A / 255.0f, 0x4A / 255.0f, 0x4A / 255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0x4A / 255.0f, 0x4A / 255.0f, 0x4A / 255.0f, 1.0f));
        Inspector.ShowEmbedded(Ctx.m_Settings);
        ImGui::PopStyleColor(3);

        // Edited in the inspector (typed): apply whatever differs from what was built.
        std::vector<std::pair<std::string, std::string>> Edited;
        for (auto& [Name, Section] : Page.m_Keys)
            for (auto& [Path, Keys] : Section)
                if (auto Sec = Built.find(Name); Sec != Built.end())
                    if (auto It = Sec->second.find(Path); It != Sec->second.end() && It->second != Keys) Edited.push_back({ Path, Keys });
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
