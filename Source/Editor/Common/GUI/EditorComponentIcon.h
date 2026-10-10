/**
 * @file EditorComponentIcon.h
 * @brief 컴포넌트 타입 → 아이콘 글리프 · 색 · 빌보드 여부 표입니다(ImGui 없음 — EditorTest 가 시험합니다).
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
        const utf8* _pKey{ nullptr };     ///< 타입 이름(`TypeInfo::_name`) 또는 리플렉션 Category
        const utf8* _pGlyph{ nullptr };   ///< `editoricon::k*` 글리프
        Color4      _color{};             ///< 뷰포트 빌보드 · 카드 아이콘 색
        bool        _bBillboard{ false }; ///< 메시가 없어 뷰포트에 빌보드로 보이는 종류(빛 · 카메라 · 오디오 …)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorComponentIcon
     * @brief 언리얼 Outliner · Details 의 클래스 아이콘, 유니티 Inspector 머리 아이콘, Godot 씬 트리 노드 아이콘의 자리입니다.
     * @details 찾는 순서: 타입 이름 표를 타입 자신부터 부모로 올라가며 → 리플렉션 Category 표 → `editoricon::kComponent`.
     *          에디터는 GameFramework 를 링크하지 않으므로 타입 포인터가 아니라 짧은 타입 이름으로 맞춥니다(타입 이름을 바꾸면 그 줄이 조용히 죽는다 —
     *          `EditorComponentIconTest` 가 Engine 타입 이름이 레지스트리에 있는지 본다).
     */
    struct SW_EDITOR_API EditorComponentIcon
    {
        /** @brief 컴포넌트 타입의 아이콘 줄입니다. @p pType 이 nullptr 이면 기본 줄입니다. 반환값은 늘 유효합니다. */
        static const EditorComponentIconRow& findRow( const TypeInfo* pType );
        /**
         * @brief 오브젝트 줄의 아이콘입니다. 빌보드 종류 컴포넌트(빛 · 카메라)가 있으면 그 아이콘이고, 없으면 `editoricon::kGameObject` 입니다.
         * @return 빌보드 종류 컴포넌트가 있으면 그 줄, 없으면 오브젝트 기본 줄입니다.
         */
        static const EditorComponentIconRow& findObjectRow( const GameObject& object );
        /** @brief 타입 이름 표의 줄 수 · 줄입니다(시험이 타입 이름을 레지스트리와 맞춘다). */
        static uint32                        getTypeRowCount();
        static const EditorComponentIconRow& getTypeRow( uint32 index );
        /** @brief 글리프(UTF-8 한 글자)의 코드 포인트입니다. 잘못된 글이면 0 입니다(탐침이 숫자로 비교한다). */
        static uint32 decodeGlyph( const utf8* pGlyph );
    };
} // namespace sw::editor
