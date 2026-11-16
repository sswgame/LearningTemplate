/**
 * @file EditorWidgetID.h
 * @brief 에디터 입력 위젯(`EditorWidgets::inputInt` · `dragFloat` · `sliderFloat` · `combo` …)이 받는 숨은 id 입니다. 보이는 이름표를 담을 수 없습니다.
 * @details ImGui 는 위젯 이름표를 칸 **오른쪽**에 붙이고 칸이 창 폭을 따라 늘어, 좁은 칸에서 이름표 · 옆 칸이 잘렸다(시퀀서 · 타일 맵 · 스프라이트 클립 · 멀티플레이 · 캐릭터 에디터, uiaudit).
 *          이름표는 앞(왼쪽)에 따로 그린다 — 행이면 `EditorWidgets::drawPropertyRowLabel`, 도구줄이면 `drawInlineLabel`. 콤보는 `EditorWidgets::beginCombo`,
 *          글 칸은 `drawTextField` 가 보이는 이름표를 스스로 앞으로 옮긴다.
 *          예전에는 린트(editor-widget-labels)가 첫 문자열 인자를 글로 봤지만 리터럴만 볼 수 있어 실행 중 만든 이름표(속성 이름 · 번역 글)가 오른쪽에 남았다.
 *          이제 형식이 가른다: 리터럴은 `SW_WIDGET_ID( "##…" )` 가 `##` 로 시작하는지 템플릿 인자의 static_assert 로 컴파일 때 보고(C++17 — consteval 이 없다), 맨 리터럴은 받지 않으며,
 *          실행 중 만든 id 는 이름 붙은 입구 `fromString` 만 받는다.
 *          에디터 코드는 ImGui 입력 위젯을 직접 부르지 못한다(`EditorInputBan.h` 가 사용 중단으로 다시 선언) — 이 형식을 받는 감싼 함수만 남는다.
 *          언리얼 Slate 의 `SNumericEntryBox` 처럼 칸과 이름표는 서로 다른 위젯이다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

#include <cstddef>

namespace sw::editor
{
    /** @brief 리터럴 id 검사입니다. `SW_WIDGET_ID` 가 리터럴의 첫 두 글자를 템플릿 인자로 넘겨 컴파일 때 멈춘다(실행 비용 없음). */
    template <bool bStartsWithHashes>
    struct EditorWidgetIDLiteralCheck
    {
        static_assert( bStartsWithHashes, "an editor widget id literal must start with '##' - draw the label in front (drawPropertyRowLabel / drawInlineLabel) instead of on the right of the field" );
    };

    /**
     * @class EditorWidgetID
     * @brief `##` 로 시작하는 ImGui 위젯 id 입니다. 리터럴은 `SW_WIDGET_ID` 로, 실행 중에는 `fromString` 으로 만듭니다.
     * @details 클래스는 내보내지 않고 실행 중 입구만 내보낸다.
     */
    class EditorWidgetID
    {
    public:
        /** @brief 리터럴 id 입니다 — `SW_WIDGET_ID` 만 부른다(검사를 지난 표지를 받는다). */
        static constexpr EditorWidgetID fromLiteral( EditorWidgetIDLiteralCheck<true>, const utf8* pLiteralID ) { return EditorWidgetID{ pLiteralID }; }

        /** @brief 실행 중 만든 id(번호 · 이름을 붙인 `##…` 글)입니다. `##` 로 시작하지 않으면 모든 구성에서 멈춘다(오른쪽 이름표를 남기지 않는다). */
        [[nodiscard]] SW_EDITOR_API static EditorWidgetID fromString( const utf8* pHiddenID );

        [[nodiscard]] const utf8* c_str() const { return _pID; }

    private:
        explicit constexpr EditorWidgetID( const utf8* pID )
            : _pID( pID )
        {
        }

    private:
        const utf8* _pID;
    };
} // namespace sw::editor

/** @brief `##` 로 시작하는 리터럴에서 위젯 id 를 만듭니다. 아니면 컴파일 오류입니다. */
#define SW_WIDGET_ID( literalID ) ::sw::editor::EditorWidgetID::fromLiteral( ::sw::editor::EditorWidgetIDLiteralCheck<( sizeof( literalID ) >= 3 && ( literalID )[0] == '#' && ( literalID )[1] == '#' )>{}, literalID )
