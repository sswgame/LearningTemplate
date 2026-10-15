/**
 * @file EditorComponentIcon.h
 * @brief 컴포넌트 타입 → 아이콘 글리프 · 색 · 빌보드 여부를 리플렉션 메타에서 읽습니다(ImGui 없음 — EditorTest 가 테스트합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct TypeInfo;

    class GameObject;
} // namespace sw

namespace sw::editor
{
    /** @brief 컴포넌트 아이콘 한 줄입니다. */
    struct EditorComponentIconRow
    {
        const utf8* _pGlyph{ nullptr };   ///< `editoricon::k*` 글리프
        Color       _color{};             ///< 뷰포트 빌보드 · 카드 아이콘 색
        bool        _bBillboard{ false }; ///< 메시가 없어 뷰포트에 빌보드로 보이는 종류(빛 · 카메라 · 오디오 …)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorComponentIcon
     * @brief 언리얼 Outliner · Details 의 클래스 아이콘(`ClassIcon.<Class>` 스타일), 유니티 Inspector 머리 아이콘, Godot 씬 트리 노드 아이콘의 자리입니다.
     * @details 아이콘은 컴포넌트 선언이 정합니다 — `REFLECT( ..., Meta = "Icon=LightPoint, IconColor=Light, Billboard" )`.
     *          `Icon` 은 `editoricon::kArrNamedIcon` 의 이름(상수에서 `k` 를 뗀 것), `IconColor` 는 Light · Camera · Audio · Physics · Navigation · Environment
     *          (없으면 중립색), `Billboard` 는 뷰포트에 빌보드로 그릴 종류입니다.
     *          찾는 순서: 메타를 타입 자신부터 부모로 올라가며 → 리플렉션 Category 표 → `editoricon::kComponent`. 키트 · 게임 컴포넌트도 같은 메타로 아이콘을 갖습니다.
     *          모르는 아이콘 · 색 이름은 `EditorComponentIconTest` 가 모든 등록 타입을 훑어 막습니다.
     */
    struct SW_EDITOR_API EditorComponentIcon
    {
        /** @brief 컴포넌트 타입의 아이콘 줄입니다. @p pType 이 nullptr 이면 기본 줄입니다. */
        static EditorComponentIconRow findRow( const TypeInfo* pType );
        /** @brief 오브젝트 줄의 아이콘입니다. 빌보드 종류 컴포넌트(빛 · 카메라)가 있으면 그 줄, 없으면 `editoricon::kGameObject` 줄입니다. */
        static EditorComponentIconRow findObjectRow( const GameObject& object );
        /** @brief 아이콘 이름(`LightPoint`)의 글리프입니다. 없으면 nullptr 입니다. */
        static const utf8* findGlyph( string_view iconName );
        /** @brief 색 이름(`Light`)이 아는 이름인지 봅니다. */
        static bool isKnownColorName( string_view colorName );
        /** @brief 글리프(UTF-8 한 글자)의 코드 포인트입니다. 잘못된 글이면 0 입니다(탐침이 숫자로 비교한다). */
        static uint32 decodeGlyph( const utf8* pGlyph );
    };
} // namespace sw::editor
