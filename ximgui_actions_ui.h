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
    // The keyboard: the keys drawn as a keyboard, coloured by what is on them where the person was working.
    //   dark = nothing   blue = an action   tan = a host-wide action   mauve = both     (these describe the modifier layer being shown)
    //   a small corner mark = the key does more with other modifiers held
    //   a gold ring = the keys of the action the palette has selected
    // Ctrl / Shift / Alt are on the keyboard too: click them (or hold them) to see that layer. The name of the action is on its key.
    // The strip under the keyboard says what is on the key under the mouse (or on the key that was clicked, which keeps it).
    //
    // One widget, two homes: the F1 overlay, and the keyboard under the palette (Ctrl+Shift+P), where it follows the selected row.
    //==============================================================================================

    namespace details
    {
        // The colours of the keyboard and the mouse: soft and desaturated, so a layer reads at a glance without shouting, with dark ink on the
        // light faces (the pastel ones) and light ink on the dark ones.
        inline constexpr ImVec4 k_ColUnassigned { 0.30f, 0.30f, 0.31f, 1.0f };
        inline constexpr ImVec4 k_ColAction     { 0.43f, 0.55f, 0.74f, 1.0f };
        inline constexpr ImVec4 k_ColHost       { 0.73f, 0.59f, 0.47f, 1.0f };
        inline constexpr ImVec4 k_ColMixed      { 0.62f, 0.52f, 0.63f, 1.0f };
        inline constexpr ImVec4 k_ColModifierOn { 0.38f, 0.65f, 0.51f, 1.0f };
        inline constexpr ImVec4 k_ColModifierOff{ 0.21f, 0.21f, 0.23f, 1.0f };
        inline ImU32 Rim(const ImVec4& Face) noexcept { return ImGui::ColorConvertFloat4ToU32(ImVec4(Face.x * 0.55f, Face.y * 0.55f, Face.z * 0.55f, 1.0f)); }

        inline constexpr ImU32  k_InkOnLight    = IM_COL32(247, 248, 251, 245);
        inline constexpr ImU32  k_InkOnDark     = IM_COL32(205, 205, 212, 255);
        inline constexpr ImU32  k_InkName       = IM_COL32(247, 248, 251, 215);              // the action's name on a key or a button

        struct key_entry { ImGuiKeyChord m_Chord; const action_info* m_pA; std::string m_Why; bool m_bGlobal; };
        using keys_by_key = std::unordered_map<int, std::vector<key_entry>>;

        // Every chord of every action that is live in Order, by the key it ends with.
        inline keys_by_key KeysOf(context& Ctx, const std::vector<scope_entry>& Order)
        {
            keys_by_key ByKey;
            for (const scope_entry& E : Order)
                for (const action_info& A : Ctx.Actions(*E.m_pObject))
                {
                    if (!Ctx.Live(E, A)) continue;
                    const std::string Why = Ctx.Reason(A, E.m_pInstance);
                    for (const ImGuiKeyChord C : Ctx.Chords(A))
                    {
                        auto& V = ByKey[static_cast<int>(C & ~ImGuiMod_Mask_)];
                        if (std::none_of(V.begin(), V.end(), [&](const key_entry& X) { return X.m_Chord == C && X.m_pA == &A; })) V.push_back({ C, &A, Why, E.m_bGlobal });
                    }
                }
            return ByKey;
        }

        struct keyboard_args
        {
            float                               m_Unit      = 40.0f;        // the size of a 1.0 key
            bool                                m_bNames    = false;        // the name of the action on its key
            ImGuiKeyChord                       m_Mods      = 0;            // the modifier layer shown
            bool*                               m_pToggle[3]= {};           // Ctrl, Shift, Alt: clicking those keys flips these (null = just displayed)
            const std::vector<ImGuiKeyChord>*   m_pHighlight= nullptr;      // chords whose key gets the gold ring
            int*                                m_pPinned   = nullptr;      // the key that was clicked (an ImGuiKey; 0 = none)
        };

        inline constexpr float k_KeyboardUnitsW = 18.5f;
        inline constexpr float k_KeyboardUnitsH = 6.4f;

        // Draws the keyboard at the cursor. Returns the ImGuiKey under the mouse (0 = none).
        inline int DrawKeyboard(const keys_by_key& ByKey, const keyboard_args& A)
        {
            struct key { const char* m_pLabel; ImGuiKey m_Key; float m_X, m_Y, m_W; ImGuiKeyChord m_Mod = 0; };
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
            , {"Shift", ImGuiKey_None, 0, 4.4f, 2.3f, ImGuiMod_Shift}
            , {"Z", ImGuiKey_Z, 2.3f, 4.4f, 1}, {"X", ImGuiKey_X, 3.3f, 4.4f, 1}, {"C", ImGuiKey_C, 4.3f, 4.4f, 1}, {"V", ImGuiKey_V, 5.3f, 4.4f, 1}, {"B", ImGuiKey_B, 6.3f, 4.4f, 1}
            , {"N", ImGuiKey_N, 7.3f, 4.4f, 1}, {"M", ImGuiKey_M, 8.3f, 4.4f, 1}, {",", ImGuiKey_Comma, 9.3f, 4.4f, 1}, {".", ImGuiKey_Period, 10.3f, 4.4f, 1}, {"/", ImGuiKey_Slash, 11.3f, 4.4f, 1}
            , {"Shift", ImGuiKey_None, 12.3f, 4.4f, 2.7f, ImGuiMod_Shift}
            , {"Ctrl", ImGuiKey_None, 0, 5.4f, 1.5f, ImGuiMod_Ctrl}, {"Alt", ImGuiKey_None, 1.6f, 5.4f, 1.5f, ImGuiMod_Alt}
            , {"Space", ImGuiKey_Space, 3.2f, 5.4f, 6.7f}
            , {"Alt", ImGuiKey_None, 10.0f, 5.4f, 1.5f, ImGuiMod_Alt}, {"Ctrl", ImGuiKey_None, 11.6f, 5.4f, 1.5f, ImGuiMod_Ctrl}
            , {"Ins", ImGuiKey_Insert, 15.5f, 1.4f, 1}, {"Home", ImGuiKey_Home, 16.5f, 1.4f, 1}, {"PgUp", ImGuiKey_PageUp, 17.5f, 1.4f, 1}
            , {"Del", ImGuiKey_Delete, 15.5f, 2.4f, 1}, {"End", ImGuiKey_End, 16.5f, 2.4f, 1}, {"PgDn", ImGuiKey_PageDown, 17.5f, 2.4f, 1}
            , {"Up", ImGuiKey_UpArrow, 16.5f, 4.4f, 1}
            , {"Left", ImGuiKey_LeftArrow, 15.5f, 5.4f, 1}, {"Down", ImGuiKey_DownArrow, 16.5f, 5.4f, 1}, {"Right", ImGuiKey_RightArrow, 17.5f, 5.4f, 1}
            };

            const float  U      = A.m_Unit;
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            ImDrawList*  pList  = ImGui::GetWindowDrawList();
            ImFont*      pFont  = ImGui::GetFont();
            const float  Big    = ImGui::GetFontSize();
            const float  Small  = Big * 0.78f;
            int          Hovered = 0;

            for (const key& K : Keys)
            {
                const ImVec2 Min(Origin.x + K.m_X * U, Origin.y + K.m_Y * U);
                const ImVec2 Max(Min.x + K.m_W * U - 3.0f, Min.y + U - 3.0f);

                // What is on this key in the layer shown (and whether more is on it in the others).
                bool bAssigned = false, bGlobal = false, bOther = false;
                const key_entry* pFirst = nullptr;
                const auto It = (K.m_Mod == 0) ? ByKey.find(static_cast<int>(K.m_Key)) : ByKey.end();
                if (It != ByKey.end())
                    for (const key_entry& E : It->second)
                    {
                        if ((E.m_Chord & ImGuiMod_Mask_) == A.m_Mods) { (E.m_bGlobal ? bGlobal : bAssigned) = true; if (!pFirst || (!E.m_bGlobal && pFirst->m_bGlobal)) pFirst = &E; }
                        else                                          bOther = true;
                    }

                ImVec4 Color = k_ColUnassigned;                                                  // nothing
                if      (bAssigned && bGlobal) Color = k_ColMixed;                               // both
                else if (bAssigned)            Color = k_ColAction;                              // an action
                else if (bGlobal)              Color = k_ColHost;                                // host-wide
                const bool bModKey = K.m_Mod != 0;
                const bool bOn     = bModKey && (A.m_Mods & K.m_Mod) != 0;
                if (bModKey) Color = bOn ? k_ColModifierOn : k_ColModifierOff;

                ImGui::SetCursorScreenPos(Min);
                ImGui::PushID(&K);
                ImGui::InvisibleButton("##key", ImVec2(Max.x - Min.x, Max.y - Min.y));
                ImGui::PopID();
                const bool bHover = ImGui::IsItemHovered();
                if (bHover) { Hovered = bModKey ? 0 : static_cast<int>(K.m_Key); Color = ImVec4(Color.x + 0.08f, Color.y + 0.08f, Color.z + 0.08f, 1.0f); }
                if (ImGui::IsItemClicked())
                {
                    if (bModKey) { for (int m = 0; m < 3; ++m) if (A.m_pToggle[m] && K.m_Mod == (m == 0 ? ImGuiMod_Ctrl : m == 1 ? ImGuiMod_Shift : ImGuiMod_Alt)) *A.m_pToggle[m] = !*A.m_pToggle[m]; }
                    else if (A.m_pPinned)  *A.m_pPinned = (*A.m_pPinned == static_cast<int>(K.m_Key)) ? 0 : static_cast<int>(K.m_Key);
                }

                // The keycap: an outline, a dark rim (the side of the key, thicker at the bottom) and the face on top.
                pList->AddRectFilled(Min, Max, ImGui::ColorConvertFloat4ToU32(ImVec4(Color.x * 0.55f, Color.y * 0.55f, Color.z * 0.55f, 1.0f)), 5.0f);
                pList->AddRectFilled(ImVec2(Min.x + 1.5f, Min.y + 1.5f), ImVec2(Max.x - 1.5f, Max.y - 3.5f), ImGui::ColorConvertFloat4ToU32(Color), 4.0f);
                if (A.m_pPinned && !bModKey && *A.m_pPinned == static_cast<int>(K.m_Key)) pList->AddRect(Min, Max, IM_COL32(235, 235, 240, 255), 4.0f, 0, 1.5f);

                if (A.m_pHighlight)
                    for (const ImGuiKeyChord C : *A.m_pHighlight)
                        if (!bModKey && static_cast<int>(C & ~ImGuiMod_Mask_) == static_cast<int>(K.m_Key))
                            pList->AddRect(Min, Max, IM_COL32(255, 205, 70, 255), 4.0f, 0, 2.5f);

                // The label (top left), the action's name (below it), the corner mark.
                const ImU32 Ink = bAssigned || bGlobal || bOn ? k_InkOnLight : k_InkOnDark;
                pList->AddText(pFont, Big, ImVec2(Min.x + 5.0f, Min.y + 3.0f), Ink, K.m_pLabel);
                if (A.m_bNames && pFirst)
                {
                    const std::string& Path = pFirst->m_pA->m_Path;
                    const std::string  Name = Path.substr(Path.rfind('/') + 1);
                    pList->PushClipRect(Min, Max, true);
                    pList->AddText(pFont, Small, ImVec2(Min.x + 5.0f, Min.y + U * 0.5f), k_InkName, Name.c_str());
                    pList->PopClipRect();
                }
                if (bOther)
                {
                    pList->AddTriangleFilled(ImVec2(Max.x - 9.0f, Min.y + 1.0f), ImVec2(Max.x - 1.0f, Min.y + 1.0f), ImVec2(Max.x - 1.0f, Min.y + 9.0f), IM_COL32(190, 150, 215, 255));
                }
            }

            ImGui::SetCursorScreenPos(Origin);
            ImGui::Dummy(ImVec2(k_KeyboardUnitsW * U, k_KeyboardUnitsH * U));
            return Hovered;
        }

        // The legend, drawn with the same colours as the keys. bSelected: the palette's gold ring is explained too.
        inline void DrawKeyboardLegend(bool bSelected)
        {
            enum shape { box, corner, ring };
            struct swatch { ImVec4 m_Color; const char* m_pText; shape m_Shape; };
            const swatch Legend[] = { { k_ColAction, "action", box }, { k_ColHost, "host-wide", box }
                                    , { k_ColMixed, "both", box }, { ImVec4(0.75f, 0.59f, 0.84f, 1), "more with other modifiers", corner }
                                    , { k_ColModifierOn, "modifier on", box }, { ImVec4(1.0f, 0.80f, 0.27f, 1), "selected", ring } };
            const float H = ImGui::GetTextLineHeight();
            for (const swatch& L : Legend)
            {
                if (L.m_Shape == ring && !bSelected) continue;
                const ImVec2 P = ImGui::GetCursorScreenPos();
                ImDrawList*  d = ImGui::GetWindowDrawList();
                const ImU32  C = ImGui::ColorConvertFloat4ToU32(L.m_Color);
                if      (L.m_Shape == corner) d->AddTriangleFilled(ImVec2(P.x, P.y + 2.0f), ImVec2(P.x + H - 4.0f, P.y + 2.0f), ImVec2(P.x + H - 4.0f, P.y + H - 2.0f), C);
                else if (L.m_Shape == ring)   d->AddRect(ImVec2(P.x, P.y + 2.0f), ImVec2(P.x + H - 4.0f, P.y + H - 2.0f), C, 2.0f, 0, 2.0f);
                else                          d->AddRectFilled(ImVec2(P.x, P.y + 2.0f), ImVec2(P.x + H - 4.0f, P.y + H - 2.0f), C, 2.0f);
                ImGui::Dummy(ImVec2(H - 4.0f, H));
                ImGui::SameLine(0, 5.0f);
                ImGui::TextDisabled("%s", L.m_pText);
                ImGui::SameLine(0, 14.0f);
            }
            ImGui::NewLine();
        }

        // The strip under the keyboard: everything on one key (the layer shown first); with no key, the fallback action's own line.
        inline void DrawKeyDetails(context& Ctx, const keys_by_key& ByKey, int Key, ImGuiKeyChord Mods, int Lines, const action_info* pFallback)
        {
            if (!ImGui::BeginChild("##keydetails", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(Lines) + 6.0f), ImGuiChildFlags_Borders)) { ImGui::EndChild(); return; }
            const auto It = (Key != 0) ? ByKey.find(Key) : ByKey.end();
            if (It != ByKey.end())
            {
                auto V = It->second;
                std::sort(V.begin(), V.end(), [&](const key_entry& L, const key_entry& R)
                    { const bool a = (L.m_Chord & ImGuiMod_Mask_) == Mods, b = (R.m_Chord & ImGuiMod_Mask_) == Mods; return a != b ? a : (L.m_Chord & ImGuiMod_Mask_) < (R.m_Chord & ImGuiMod_Mask_); });
                for (const key_entry& E : V)
                {
                    const bool bHere = (E.m_Chord & ImGuiMod_Mask_) == Mods;
                    ImGui::TextColored(bHere ? ImVec4(1.0f, 0.82f, 0.35f, 1.0f) : ImVec4(0.65f, 0.65f, 0.70f, 1.0f), "%-18s", ChordName(E.m_Chord).c_str());
                    ImGui::SameLine();
                    ImGui::TextUnformatted(E.m_pA->m_Path.c_str());
                    if (!E.m_Why.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.38f, 1.0f), "- %s", E.m_Why.c_str()); }
                    else if (E.m_pA->m_pHelp) { ImGui::SameLine(); ImGui::TextDisabled("%s", E.m_pA->m_pHelp); }
                }
            }
            else if (Key != 0) ImGui::TextDisabled("Nothing is bound to this key here.");
            else if (pFallback)
            {
                ImGui::TextUnformatted(pFallback->m_Path.c_str());
                if (pFallback->m_pHelp) ImGui::TextDisabled("%s", pFallback->m_pHelp);
                const std::string K = Ctx.KeysText(*pFallback);
                ImGui::TextDisabled("%s", K.empty() ? "no key" : K.c_str());
            }
            else ImGui::TextDisabled("Hover a key to see what is on it. Click a key to keep it here.");
            ImGui::EndChild();
        }
        // The mouse, drawn beside the keyboard: its two buttons and the wheel, coloured like the keys by what the surfaces do with them in the layer
        // shown. Regions: -1 left button, -2 right button, -3 wheel / middle button. Returns the one under the mouse (0 = none).
        struct flat_gesture { const char* m_pSurface; const gesture* m_pG; };

        inline int MouseRegion(mouse_input In) noexcept { return In == mouse_input::Left ? -1 : In == mouse_input::Right ? -2 : -3; }

        // The outline of the mouse: wider at the top, narrower at the bottom, with soft corners (a squircle that tapers). Points go round the shape.
        inline std::vector<ImVec2> MouseOutline(ImVec2 Center, float HalfW, float HalfH, float Shrink = 1.0f)
        {
            constexpr int   N = 72;
            constexpr float Exponent = 3.0f;                    // 2 = ellipse, larger = squarer
            std::vector<ImVec2> P;
            P.reserve(N);
            for (int i = 0; i < N; ++i)
            {
                const float t  = 6.2831853f * static_cast<float>(i) / static_cast<float>(N);
                const float c  = std::cos(t), s = std::sin(t);
                const float x  = (c < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(c), 2.0f / Exponent);
                const float y  = (s < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(s), 2.0f / Exponent);      // -1 top ... +1 bottom
                const float k  = std::clamp((y + 0.25f) / 1.25f, 0.0f, 1.0f);                            // 0 on the upper part, 1 at the very bottom
                const float tp = 1.0f - 0.24f * k * k;                                                   // the taper
                P.push_back(ImVec2(Center.x + x * HalfW * tp * Shrink, Center.y + y * HalfH * Shrink));
            }
            return P;
        }

        // The part of a convex polygon inside a rectangle (Sutherland-Hodgman against its four sides).
        inline std::vector<ImVec2> ClipToRect(std::vector<ImVec2> In, float X0, float Y0, float X1, float Y1)
        {
            auto Clip = [](std::vector<ImVec2>& Poly, auto Inside, auto Cut)
            {
                std::vector<ImVec2> Out;
                for (std::size_t i = 0; i < Poly.size(); ++i)
                {
                    const ImVec2 A = Poly[i], B = Poly[(i + 1) % Poly.size()];
                    const bool   a = Inside(A), b = Inside(B);
                    if (a) Out.push_back(A);
                    if (a != b) Out.push_back(Cut(A, B));
                }
                Poly = std::move(Out);
            };
            auto AtX = [](ImVec2 A, ImVec2 B, float X) { const float t = (X - A.x) / (B.x - A.x); return ImVec2(X, A.y + t * (B.y - A.y)); };
            auto AtY = [](ImVec2 A, ImVec2 B, float Y) { const float t = (Y - A.y) / (B.y - A.y); return ImVec2(A.x + t * (B.x - A.x), Y); };
            Clip(In, [&](ImVec2 P) { return P.x >= X0; }, [&](ImVec2 A, ImVec2 B) { return AtX(A, B, X0); });
            Clip(In, [&](ImVec2 P) { return P.x <= X1; }, [&](ImVec2 A, ImVec2 B) { return AtX(A, B, X1); });
            Clip(In, [&](ImVec2 P) { return P.y >= Y0; }, [&](ImVec2 A, ImVec2 B) { return AtY(A, B, Y0); });
            Clip(In, [&](ImVec2 P) { return P.y <= Y1; }, [&](ImVec2 A, ImVec2 B) { return AtY(A, B, Y1); });
            return In;
        }

        inline int DrawMouse(const std::vector<flat_gesture>& All, ImGuiKeyChord Mods, float U, int* pPinned)
        {
            const ImVec2 O = ImGui::GetCursorScreenPos();
            ImDrawList*  d = ImGui::GetWindowDrawList();
            ImFont*      pFont = ImGui::GetFont();
            const float  Small = ImGui::GetFontSize() * 0.78f;
            const float  W = 3.4f * U, H = 5.4f * U, BtnH = 2.4f * U;
            const float  Half = W * 0.5f;
            const ImVec2 Center(O.x + Half, O.y + H * 0.5f);
            int Hovered = 0;

            // The body, then the two buttons cut out of a slightly smaller copy of it (so the body shows as a thin rim round them).
            const auto Body = MouseOutline(Center, Half, H * 0.5f);
            const auto Face = MouseOutline(Center, Half, H * 0.5f, 0.93f);
            d->AddConvexPolyFilled(Body.data(), static_cast<int>(Body.size()), Rim(k_ColUnassigned));                               // the rim, like a key's
            const auto BodyFace = MouseOutline(Center, Half - 2.5f, H * 0.5f - 2.5f);
            d->AddConvexPolyFilled(BodyFace.data(), static_cast<int>(BodyFace.size()), ImGui::ColorConvertFloat4ToU32(k_ColUnassigned));       // the colour of a key with nothing on it

            const float GapHalf = 0.3f * U;                                                  // half the space between the buttons: the wheel lives there
            struct region { int m_Id; ImVec2 m_Min, m_Max; const char* m_pLabel; };
            const region Regions[] =
            { { -1, ImVec2(O.x,                 O.y),             ImVec2(Center.x - GapHalf, O.y + BtnH),         "LMB" }
            , { -2, ImVec2(Center.x + GapHalf,  O.y),             ImVec2(O.x + W,            O.y + BtnH),         "RMB" }
            , { -3, ImVec2(Center.x - 0.2f * U, O.y + 0.4f * U),  ImVec2(Center.x + 0.2f * U, O.y + 1.5f * U),    ""    } };

            for (const region& R : Regions)
            {
                bool bHere = false, bOther = false;
                const char* pName = nullptr;
                const char* pName2 = nullptr;
                for (const flat_gesture& F : All)
                {
                    const int Id = MouseRegion(F.m_pG->m_Input == mouse_input::Middle ? mouse_input::Wheel : F.m_pG->m_Input);
                    if (Id != R.m_Id) continue;
                    if ((F.m_pG->m_Mods & ImGuiMod_Mask_) == Mods)
                    {
                        bHere = true;
                        if (!pName) pName = F.m_pG->m_pName;
                        else if (!pName2 && std::string_view(pName) != F.m_pG->m_pName) pName2 = F.m_pG->m_pName;
                    }
                    else bOther = true;
                }
                ImVec4 Color = bHere ? k_ColAction : k_ColUnassigned;

                ImGui::SetCursorScreenPos(R.m_Min);
                ImGui::PushID(R.m_Id);
                ImGui::InvisibleButton("##mouse", ImVec2(R.m_Max.x - R.m_Min.x, R.m_Max.y - R.m_Min.y));
                ImGui::PopID();
                if (ImGui::IsItemHovered()) { Hovered = R.m_Id; Color = ImVec4(Color.x + 0.08f, Color.y + 0.08f, Color.z + 0.08f, 1.0f); }
                if (ImGui::IsItemClicked() && pPinned) *pPinned = (*pPinned == R.m_Id) ? 0 : R.m_Id;
                const bool bPinned = pPinned && *pPinned == R.m_Id;
                const ImU32 Fill = ImGui::ColorConvertFloat4ToU32(Color);

                if (R.m_Id == -3)                                                            // the wheel: a pill
                {
                    d->AddRectFilled(R.m_Min, R.m_Max, Fill, 0.2f * U);
                    d->AddRect(R.m_Min, R.m_Max, bPinned ? IM_COL32(235, 235, 240, 255) : Rim(Color), 0.2f * U, 0, bPinned ? 1.5f : 2.0f);
                    if (bOther) d->AddTriangleFilled(ImVec2(R.m_Max.x - 8.0f, R.m_Min.y + 1.0f), ImVec2(R.m_Max.x - 1.0f, R.m_Min.y + 1.0f), ImVec2(R.m_Max.x - 1.0f, R.m_Min.y + 8.0f), IM_COL32(190, 150, 215, 255));
                    if (pName)                                                               // its names go under the buttons, centred
                    {
                        std::string Text = pName;
                        if (pName2) { Text += ", "; Text += pName2; }
                        const float TextW = ImGui::CalcTextSize(Text.c_str()).x * (Small / ImGui::GetFontSize());
                        d->AddText(pFont, Small, ImVec2(Center.x - TextW * 0.5f, O.y + BtnH + 0.25f * U), IM_COL32(255, 255, 255, 235), Text.c_str());
                    }
                    continue;
                }

                // A button: the face of the mouse cut to its side of the middle and to the top part, a few pixels in from its neighbours.
                const auto Poly = ClipToRect(Face, R.m_Min.x + 1.5f, R.m_Min.y, R.m_Max.x - 1.5f, R.m_Max.y);
                if (Poly.size() >= 3)
                {
                    d->AddConvexPolyFilled(Poly.data(), static_cast<int>(Poly.size()), Fill);
                    d->AddPolyline(Poly.data(), static_cast<int>(Poly.size()), bPinned ? IM_COL32(235, 235, 240, 255) : Rim(Color), ImDrawFlags_Closed, bPinned ? 1.5f : 2.0f);
                }
                const float TextX = (R.m_Id == -1 ? R.m_Min.x + 0.55f * U : R.m_Min.x + 0.2f * U);
                d->AddText(pFont, ImGui::GetFontSize(), ImVec2(TextX, R.m_Min.y + 0.55f * U), bHere ? k_InkOnLight : k_InkOnDark, R.m_pLabel);
                d->PushClipRect(R.m_Min, R.m_Max, true);
                if (pName)  d->AddText(pFont, Small, ImVec2(TextX, R.m_Min.y + 1.05f * U), k_InkName, pName);
                if (pName2) d->AddText(pFont, Small, ImVec2(TextX, R.m_Min.y + 1.4f * U),  k_InkName, pName2);
                d->PopClipRect();
                if (bOther) d->AddTriangleFilled(ImVec2(R.m_Max.x - 12.0f, R.m_Min.y + 0.6f * U), ImVec2(R.m_Max.x - 4.0f, R.m_Min.y + 0.6f * U), ImVec2(R.m_Max.x - 4.0f, R.m_Min.y + 0.6f * U + 8.0f), IM_COL32(190, 150, 215, 255));
            }

            ImGui::SetCursorScreenPos(O);
            ImGui::Dummy(ImVec2(W, H));
            return Hovered;
        }

        // The strip under the keyboard when the mouse is what is hovered / kept: every gesture on that button (the layer shown first).
        inline void DrawGestureDetails(const std::vector<flat_gesture>& All, int Region, ImGuiKeyChord Mods, int Lines)
        {
            if (!ImGui::BeginChild("##gesturedetails", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(Lines) + 6.0f), ImGuiChildFlags_Borders)) { ImGui::EndChild(); return; }
            std::vector<flat_gesture> V;
            for (const flat_gesture& F : All) if (MouseRegion(F.m_pG->m_Input == mouse_input::Middle ? mouse_input::Wheel : F.m_pG->m_Input) == Region) V.push_back(F);
            std::stable_sort(V.begin(), V.end(), [&](const flat_gesture& L, const flat_gesture& R)
                { return ((L.m_pG->m_Mods & ImGuiMod_Mask_) == Mods) > ((R.m_pG->m_Mods & ImGuiMod_Mask_) == Mods); });
            if (V.empty()) ImGui::TextDisabled("The mouse does nothing with this button here.");
            for (const flat_gesture& F : V)
            {
                const bool bHere = (F.m_pG->m_Mods & ImGuiMod_Mask_) == Mods;
                ImGui::TextColored(bHere ? ImVec4(1.0f, 0.82f, 0.35f, 1.0f) : ImVec4(0.65f, 0.65f, 0.70f, 1.0f), "%-26s", GestureText(*F.m_pG).c_str());
                ImGui::SameLine(); ImGui::Text("%s", F.m_pG->m_pName);
                ImGui::SameLine(); ImGui::TextDisabled("%s - %s", F.m_pSurface, F.m_pG->m_pHelp);
            }
            ImGui::EndChild();
        }
    }

    inline void DrawKeyboardOverlay(context& Ctx)
    {
        auto& O = Ctx.m_Overlay;
        if (!O.m_bOpen) return;

        const auto ByKey = details::KeysOf(Ctx, O.m_Order);

        const ImGuiIO& io = ImGui::GetIO();
        ImGuiKeyChord Mods = 0;
        if (O.m_bCtrl  || io.KeyCtrl)  Mods |= ImGuiMod_Ctrl;
        if (O.m_bShift || io.KeyShift) Mods |= ImGuiMod_Shift;
        if (O.m_bAlt   || io.KeyAlt)   Mods |= ImGuiMod_Alt;

        ImGuiViewport* pVp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(pVp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        bool bClose = false;
        const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                     | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
        bool bWindowOpen = true;                                                     // the X of the title bar clears it
        const bool bShown = ImGui::Begin("Keyboard##ActionsOverlay", &bWindowOpen, Flags);
        if (bShown)
        {
            ImGui::TextDisabled("What each key and mouse button does where you were working. Click Ctrl / Shift / Alt to see their layer; click a key or a mouse button to keep its details below.");
            details::DrawKeyboardLegend(false);
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            std::vector<details::flat_gesture> Flat;
            for (const gesture_set& S : O.m_Gestures) for (const gesture& G : S.m_List) Flat.push_back({ S.m_pSurface, &G });

            details::keyboard_args A;
            A.m_Unit      = std::clamp((pVp->Size.x * 0.9f - 24.0f) / (details::k_KeyboardUnitsW + 4.6f), 30.0f, 54.0f);
            A.m_bNames    = true;
            A.m_Mods      = Mods;
            A.m_pToggle[0]= &O.m_bCtrl; A.m_pToggle[1] = &O.m_bShift; A.m_pToggle[2] = &O.m_bAlt;
            A.m_pPinned   = &O.m_PinnedKey;
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            const int HoveredKey = details::DrawKeyboard(ByKey, A);

            // The mouse, to the right of the keyboard.
            ImGui::SetCursorScreenPos(ImVec2(Origin.x + (details::k_KeyboardUnitsW + 1.0f) * A.m_Unit, Origin.y + 0.3f * A.m_Unit));
            const int HoveredMouse = details::DrawMouse(Flat, Mods, A.m_Unit, &O.m_PinnedKey);
            ImGui::SetCursorScreenPos(ImVec2(Origin.x, Origin.y + details::k_KeyboardUnitsH * A.m_Unit + 6.0f));

            const int Shown = HoveredKey ? HoveredKey : HoveredMouse ? HoveredMouse : O.m_PinnedKey;
            if (Shown < 0) details::DrawGestureDetails(Flat, Shown, Mods, 4);
            else           details::DrawKeyDetails(Ctx, ByKey, Shown, Mods, 4, nullptr);
            ImGui::TextDisabled("F1 or Esc closes.");

            // (Not on the frame that opened it: F1 is still down then, and would close it again at once.)
            if (ImGui::GetFrameCount() > O.m_OpenFrame && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_F1, false))) bClose = true;
        }
        ImGui::End();
        if (!bWindowOpen) bClose = true;
        if (bClose) Ctx.CloseOverlay();
    }

    //==============================================================================================
    // The status line: what the mouse can do on the surface it is over, in a quiet strip at the bottom of the window.
    // Holding Ctrl / Shift / Alt shows that layer. Nothing is drawn over a surface that declares no gestures, or while a view is open.
    //==============================================================================================

    inline void DrawStatusLine(context& Ctx)
    {
        if (Ctx.m_Overlay.m_bOpen || Ctx.m_Palette.m_bOpen) return;
        const auto Sets = Ctx.GestureOrder(true);
        if (Sets.empty()) return;

        const ImGuiIO& io = ImGui::GetIO();
        ImGuiKeyChord Mods = 0;
        if (io.KeyCtrl)  Mods |= ImGuiMod_Ctrl;
        if (io.KeyShift) Mods |= ImGuiMod_Shift;
        if (io.KeyAlt)   Mods |= ImGuiMod_Alt;

        struct item { std::string m_Input, m_Name; };
        std::vector<item> Items;
        for (const gesture& G : Sets.front().m_List)
            if ((G.m_Mods & ImGuiMod_Mask_) == Mods) Items.push_back({ GestureText(G), G.m_pName });
        if (Items.empty()) return;

        ImGuiViewport* pVp = ImGui::GetMainViewport();
        ImDrawList*    d   = ImGui::GetForegroundDrawList(pVp);
        const float    H   = ImGui::GetTextLineHeight() + 10.0f;
        const float    Max = pVp->Size.x - 24.0f;

        float W = 14.0f + ImGui::CalcTextSize(Sets.front().m_pSurface).x;
        for (const item& I : Items) W += 18.0f + ImGui::CalcTextSize(I.m_Input.c_str()).x + 6.0f + ImGui::CalcTextSize(I.m_Name.c_str()).x;
        W = std::min(W + 10.0f, Max);

        const ImVec2 Min(pVp->Pos.x + 12.0f, pVp->Pos.y + pVp->Size.y - H - 8.0f);
        d->PushClipRect(Min, ImVec2(Min.x + W, Min.y + H), true);
        d->AddRectFilled(Min, ImVec2(Min.x + W, Min.y + H), IM_COL32(20, 20, 24, 225), 6.0f);
        d->AddRect(Min, ImVec2(Min.x + W, Min.y + H), IM_COL32(95, 95, 105, 255), 6.0f);
        float X = Min.x + 10.0f;
        const float Y = Min.y + 5.0f;
        d->AddText(ImVec2(X, Y), IM_COL32(150, 152, 160, 255), Sets.front().m_pSurface);
        X += ImGui::CalcTextSize(Sets.front().m_pSurface).x;
        for (const item& I : Items)
        {
            X += 18.0f;
            d->AddText(ImVec2(X, Y), IM_COL32(255, 209, 89, 255), I.m_Input.c_str());
            X += ImGui::CalcTextSize(I.m_Input.c_str()).x + 6.0f;
            d->AddText(ImVec2(X, Y), IM_COL32(235, 235, 240, 255), I.m_Name.c_str());
            X += ImGui::CalcTextSize(I.m_Name.c_str()).x;
        }
        d->PopClipRect();
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
        const float    W   = std::min(Ctx.m_bKeyboardInPalette ? 800.0f : 640.0f, pVp->Size.x * 0.9f);
        ImGui::SetNextWindowPos(ImVec2(pVp->GetCenter().x, pVp->Pos.y + pVp->Size.y * (Ctx.m_bKeyboardInPalette ? 0.04f : 0.12f)), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(W, 0.0f), ImVec2(W, pVp->Size.y * 0.94f));
        if (P.m_bFocus) ImGui::SetNextWindowFocus();

        bool bClose = false, bRestoreFocus = true;
        const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                     | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
        if (ImGui::Begin("##ActionPalette", nullptr, Flags))
        {
            bool bTyped;
            const float CloseSize = ImGui::GetFrameHeight();
            const float SearchW   = ImGui::GetContentRegionAvail().x - CloseSize - ImGui::GetStyle().ItemSpacing.x;       // room for the X that closes the palette
            if (Ctx.m_pSearchBox) bTyped = Ctx.m_pSearchBox(P.m_Query, SearchW, P.m_bFocus);
            else
            {
                char Buffer[128]{};
                std::snprintf(Buffer, sizeof(Buffer), "%s", P.m_Query.c_str());
                if (P.m_bFocus) ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(SearchW);
                bTyped = ImGui::InputTextWithHint("##query", "Type an action...", Buffer, sizeof(Buffer));
                P.m_Query = Buffer;
            }
            ImGui::SameLine();
            if (ImGui::Button("X##ClosePalette", ImVec2(CloseSize, CloseSize))) bClose = true;
            if (ImGui::IsItemHovered()) Ctx.ShowHint({ "Close", "Closes the palette. Esc does too.", "Esc", "", "" });
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

            if (ImGui::BeginChild("##rows", ImVec2(0.0f, std::min(N, Ctx.m_bKeyboardInPalette ? 8 : 12) * ImGui::GetTextLineHeightWithSpacing() + 4.0f)))
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
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Keyboard").x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::Checkbox("Keyboard", &Ctx.m_bKeyboardInPalette);

            // The keyboard: the keys of the selected action ringed in gold, on the layer of its first key.
            if (Ctx.m_bKeyboardInPalette)
            {
                const action_info* pSel = (N && P.m_Selected < N) ? Rows[P.m_Selected].m_pA : nullptr;
                const std::vector<ImGuiKeyChord> SelChords = pSel ? Ctx.Chords(*pSel) : std::vector<ImGuiKeyChord>{};

                const auto ByKey = details::KeysOf(Ctx, P.m_Order);
                details::keyboard_args A;
                A.m_Unit      = ImGui::GetContentRegionAvail().x / details::k_KeyboardUnitsW;
                A.m_Mods      = SelChords.empty() ? ImGuiKeyChord(0) : (SelChords.front() & ImGuiMod_Mask_);
                A.m_pHighlight= &SelChords;
                details::DrawKeyboardLegend(true);
                const int Hovered = details::DrawKeyboard(ByKey, A);
                details::DrawKeyDetails(Ctx, ByKey, Hovered, A.m_Mods, 4, pSel);
            }

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


    namespace details
    {
        // What the "Set key" row / the pinned card says while a key is being captured: "press a key", or the clash and its two buttons.
        inline void DrawCaptureStatus(context& Ctx)
        {
            auto& Cap = Ctx.m_Capture;
            if (!Cap.m_ConflictWith.empty())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.38f, 1.0f), "%s is also on %s.", ChordName(Cap.m_Chord).c_str(), Cap.m_ConflictWith.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Replace")) Ctx.ResolveClash(true);
                ImGui::SameLine();
                if (ImGui::SmallButton("Cancel"))  Ctx.ResolveClash(false);
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "press a key... (Esc cancels)");
                Ctx.PollCapture();
            }
        }
    }

    //==============================================================================================
    // The pinned hint card (F1 over something that has a hint): the card kept on screen, with what can be done to its key.
    //==============================================================================================

    inline void DrawPinnedCard(context& Ctx)
    {
        auto& P = Ctx.m_Pinned;
        if (!P.m_bOpen) return;
        const action_info* pA = Ctx.FindByPath(P.m_Path);
        if (!pA) { Ctx.ClosePinned(); return; }

        // Is it live here, and can it run? (The instance is whatever scope has it right now.)
        const std::string Why = Ctx.ReasonNow(*pA);

        ImGuiViewport* pVp = ImGui::GetMainViewport();
        const float    Width = 380.0f;
        ImGui::SetNextWindowPos(ImVec2(std::clamp(P.m_Pos.x - 12.0f, pVp->Pos.x + 8.0f, pVp->Pos.x + pVp->Size.x - Width - 24.0f)
                                     , std::clamp(P.m_Pos.y + 14.0f, pVp->Pos.y + 8.0f, pVp->Pos.y + pVp->Size.y - 230.0f)), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(Width, 0.0f), ImVec2(Width, pVp->Size.y));

        bool bClose = false;
        const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                     | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        if (ImGui::Begin("##ActionsPinnedCard", nullptr, Flags))
        {
            const hint_text H = Ctx.MakeHint(*pA, Why);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
            ImGui::TextUnformatted(H.m_Topic.c_str());
            ImGui::PopFont();
            if (!H.m_Shortcut.empty()) { ImGui::SameLine(0.0f, 14.0f); ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "[%s]", H.m_Shortcut.c_str()); }
            ImGui::SameLine(Width - 24.0f - ImGui::GetFrameHeight() * 0.8f);
            if (ImGui::SmallButton("X##ClosePinned")) bClose = true;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + Width - 24.0f);
            if (!H.m_Body.empty())     { ImGui::Spacing(); ImGui::TextUnformatted(H.m_Body.c_str()); }
            if (!H.m_Disabled.empty()) { ImGui::Spacing(); ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.38f, 1.0f), "Unavailable: %s", H.m_Disabled.c_str()); }
            ImGui::Spacing();
            ImGui::TextDisabled("%s", H.m_Detail.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();

            if (Ctx.m_Capture.m_Path == P.m_Path)
                details::DrawCaptureStatus(Ctx);
            else
            {
                if (ImGui::Button("Change shortcut...")) Ctx.m_Capture.m_Path = P.m_Path;
                if (Ctx.Chords(*pA) != pA->m_Default) { ImGui::SameLine(); if (ImGui::Button("Reset")) Ctx.ResetKeys(P.m_Path); }
                ImGui::SameLine();
                if (ImGui::Button("Copy command")) ImGui::SetClipboardText(("RunAction -Path " + P.m_Path).c_str());
                if (Ctx.m_OnShowInKeymap) { ImGui::SameLine(); if (ImGui::Button("Show in keymap")) { Ctx.m_OnShowInKeymap(P.m_Path); bClose = true; } }
                ImGui::SameLine();
                if (ImGui::Button("Keyboard")) { Ctx.ClosePinned(); Ctx.OpenOverlay(); ImGui::End(); ImGui::PopStyleVar(); return; }

                // Closing: Esc, or a click anywhere else (the first frames are let through: F1 itself opened it).
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) bClose = true;
                if (ImGui::GetFrameCount() > P.m_OpenFrame + 2 && !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) bClose = true;
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        if (bClose) Ctx.ClosePinned();
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
            else details::DrawCaptureStatus(*Self.m_pCtx);
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

        // Presets: the keymap this one sits on, and my keys saved as a keymap others can use.
        if (Ctx.m_Presets.m_List)
        {
            static char        NameBuf[64]{};
            static std::string Result;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Based on");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::BeginCombo("##base", Ctx.m_Presets.m_Base.empty() ? "(the defaults)" : Ctx.m_Presets.m_Base.c_str()))
            {
                if (ImGui::Selectable("(the defaults)", Ctx.m_Presets.m_Base.empty())) Ctx.m_Presets.m_SetBase({});
                for (const std::string& Name : Ctx.m_Presets.m_List())
                    if (ImGui::Selectable(Name.c_str(), Name == Ctx.m_Presets.m_Base)) { Ctx.m_Presets.m_SetBase(Name); break; }
                ImGui::EndCombo();
            }
            ImGui::SameLine(0.0f, 24.0f);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputTextWithHint("##presetname", "name of a new keymap", NameBuf, sizeof(NameBuf));
            ImGui::SameLine();
            if (ImGui::Button("Save my keys as it")) { Result = Ctx.m_Presets.m_SaveAs(NameBuf); if (Result.empty()) { Result = std::string("saved as ") + NameBuf; NameBuf[0] = 0; } }
            if (ImGui::IsItemHovered()) Ctx.ShowHint({ "Save my keys as a keymap", "Writes every key you have changed (and the toolbars) as a keymap file of that name in the project. Anyone can choose it in Based on, and it travels with the project.", "", "", "" });
            if (!Result.empty()) ImGui::TextDisabled("%s", Result.c_str());
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
