# actions.imgui

Keys, menus, toolbar buttons and hint cards for Dear ImGui tools, **generated from xproperty descriptions**.

An action is a plain xproperty function member, `obj_action<"Delete", &T::DeleteSelection, member_keys<"Delete">, member_help<"...">>`,
and its identity is its property path (`Level/Entity/Delete`). Keymaps and toolbar layouts are layered xtextfile files holding only the
differences from the layer below. Nothing is global; the library never sees commands or undo.

| File | What |
|---|---|
| `ximgui_actions.h` | tags, chords, scopes, key resolution, `MenuItem` / `ToolbarButton` / `DrawToolbar`, hints |
| `ximgui_actions_keymap.h` | keymap + toolbar layout files and their layers |

Header-only. Include after imgui, xdelegate, and xproperty (with its imgui inspector tags `my_property_ui.h`).
Design: `documentation/Editors/actions_and_keybindings.md` in the xLION repository.
