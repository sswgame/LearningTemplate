/**
 * @file EditorIconName.h
 * @brief 에디터 아이콘 이름 형(`EditorIconName`)과 `SW_EDITORICON_NAME` 입니다 — 등록 줄 · 상수 표가 아이콘 표(`EditorIconGlyphTable.h`)를 끌어오지 않고 이름을 든다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 아이콘 이름입니다(글리프가 아니다 — 그릴 때 `EditorIconUtil::findGlyphByName` 으로 바꾼다). `SW_EDITORICON_NAME` 이 만든다. */
    struct EditorIconName
    {
        const utf8* _pName{ nullptr };

        constexpr EditorIconName() = default;
        constexpr explicit EditorIconName( const utf8* pName )
            : _pName{ pName }
        {
        }
        constexpr bool isEmpty() const { return _pName == nullptr || _pName[0] == 0; }
    };
} // namespace sw::editor

/**
 * @brief 아이콘 이름 리터럴입니다(`EditorIconName`). 등록 줄 · 상수 표 · 데이터처럼 나중에 찾는 자리가 쓴다. 단위 테스트가 `SW_EDITORICON` 처럼 훑는다.
 * @details 글이 아니라 형이다 — 이름을 단추 글로 넘기면(`ImGui::Button( 이름 )`) 컴파일되지 않는다. 예전에는 같은 `const utf8*` 라 출력 로그 설정 단추가 "Set",
 *          인스펙터 잠금 단추가 "Unl" 로 이름 글이 잘려 그려졌다(uiaudit 00). 그리는 쪽은 `EditorIconUtil::findGlyphByName` 으로 글리프를 받는다.
 */
#define SW_EDITORICON_NAME( NameLiteral ) \
    ::sw::editor::EditorIconName          \
    {                                     \
        NameLiteral                       \
    }
