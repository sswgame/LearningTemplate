/**
 * @file EditorCommandIDs.h
 * @brief C++ 가 이름으로 부르는 에디터 명령 id 상수입니다. 등록하는 쪽과 부르는 쪽이 같은 상수를 써서 오타가 컴파일에서 멈춘다.
 * @details 단축키 · 메뉴는 데이터(`Content/editor/input/editor.input.xml` · `*.commands.xml`)가 id 를 적고, 그쪽은 읽을 때 등록에 없는 id 를 오류로 멈춘다
 *          (`EditorShortcutMap`). 언리얼 `TCommands<>` 처럼 명령마다 형식을 두지는 않는다 — C++ 에서 이름으로 부르는 자리가 몇 곳뿐이라 상수로 충분하다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor::editorcommand
{
    inline constexpr const utf8* kEditUndo        = "edit.undo";
    inline constexpr const utf8* kEditRedo        = "edit.redo";
    inline constexpr const utf8* kBuildCompileAll = "build.compileAll";
    inline constexpr const utf8* kPlayStart       = "play.start";
    inline constexpr const utf8* kAssetBrowseTo   = "asset.browseTo";
} // namespace sw::editor::editorcommand
