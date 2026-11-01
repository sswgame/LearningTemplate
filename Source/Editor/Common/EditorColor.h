/**
 * @file EditorColor.h
 * @brief 에디터가 쓰는 색 타입(Core 의 `sw::Color`)을 들여옵니다. ImGui 없이 쓸 수 있습니다(애셋 종류 등록 · 테스트).
 * @details 색 값은 여기 두지 않는다. UI 색은 테마 토큰(`Content/editor/theme/<이름>.tokens.json`)이고 `EditorThemeUtil` 접근자로 읽는다.
 *          ImGui 값(`ImVec4` · `ImU32`)으로 바꾸는 곳은 `EditorColorUtil` 하나입니다.
 */
#pragma once
#include "Core/Math/Color.h"

/**
 * @brief 테마 색 키 리터럴 `Ns.Key` 입니다. 등록 줄처럼 색을 나중에 찾는 자리가 키를 들 때 쓴다(찾기는 `EditorThemeUtil::findColor`).
 * @details 두 인자 모두 문자열 리터럴이어야 한다 — 테마 키 테스트(`EditorThemeKeys`)가 `SW_THEMECOLOR` 처럼 훑는다.
 */
#define SW_THEMECOLOR_KEY( NamespaceLiteral, KeyLiteral ) NamespaceLiteral "." KeyLiteral
