/**
 * @file EditorColor.h
 * @brief 에디터가 쓰는 색 타입(Core 의 `sw::Color`)을 들여옵니다. ImGui 없이 쓸 수 있습니다(애셋 종류 표 · 테스트).
 * @details 색 값은 여기 두지 않는다. UI 색은 테마 토큰(`Resource/editor/theme/<이름>.tokens.json`)이고 `EditorThemeUtil` 접근자로 읽는다.
 *          ImGui 값(`ImVec4` · `ImU32`)으로 바꾸는 곳은 `EditorColorUtil` 하나입니다.
 */
#pragma once
#include "Core/Math/Color.h"
