/**
 * @file EditorComponentIconRow.h
 * @brief 컴포넌트 타입 → 아이콘 글리프 · 색 · 빌보드 여부를 리플렉션 메타와 컴포넌트 아이콘 표(데이터)에서 읽습니다(ImGui 없음 — EditorTest 가 테스트합니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct TypeInfo;

    class GameObject;
} // namespace sw

namespace sw::editor
{
    /** @brief 컴포넌트 아이콘 한 줄입니다. 부를 때마다 지금 아이콘 표 · 테마에서 찾은 값입니다(담아 두지 않는다 — 글꼴 · 테마를 바꾸면 달라진다). */
    struct SW_EDITOR_API EditorComponentIconRow
    {
        const utf8* _pGlyph{ nullptr };   ///< 아이콘 글리프(`EditorIconGlyphTable`)
        Color       _color{};             ///< 뷰포트 빌보드 · 카드 아이콘 색(테마 키 `Component.<이름>`)
        bool        _bBillboard{ false }; ///< 메시가 없어 뷰포트에 빌보드로 보이는 종류(빛 · 카메라 · 오디오 …)

        // 언리얼 Outliner · Details 의 클래스 아이콘(`ClassIcon.<Class>` 스타일), 유니티 Inspector 머리 아이콘, Godot 씬 트리 노드 아이콘에 해당합니다.
        // 아이콘은 컴포넌트 선언이 정합니다 — `REFLECT( ..., Meta = "Icon=LightPoint, IconColor=Light, Billboard" )`.
        /** @brief 컴포넌트 타입의 아이콘 줄입니다. @p pType 이 nullptr 이면 기본 줄입니다. */
        static EditorComponentIconRow findRow( const TypeInfo* pType );
        /** @brief 오브젝트 줄의 아이콘입니다. 빌보드 종류 컴포넌트(빛 · 카메라)가 있으면 그 줄, 없으면 표의 오브젝트 아이콘 줄입니다. */
        static EditorComponentIconRow findObjectRow( const GameObject& object );
        /**
         * @brief 데이터가 이름으로 드는 컴포넌트 아이콘 · 색이 모두 아이콘 표 · 지금 테마에 있으면 true 입니다. 없는 것마다 오류를 남깁니다.
         * @details 컴포넌트 선언의 메타(`Icon=` · `IconColor=`)와 컴포넌트 아이콘 표를 본다. 편집기가 설 때 한 번 부르고(틀리면 서지 않는다), 타입 테이블 세대가 바뀌면
         *          (모듈 로드 · 리로드) 다시 본다 — 엔진 · 게임 선언은 에디터 목록을 모르므로 컴파일이 아니라 여기서 막는다.
         */
        [[nodiscard]] static bool validateNames();
        /** @brief 글리프(UTF-8 한 글자)의 코드 포인트입니다. 잘못된 글이면 0 입니다(감시 값이 숫자로 비교한다). */
        static uint32 decodeGlyph( const utf8* pGlyph );
    };
} // namespace sw::editor
