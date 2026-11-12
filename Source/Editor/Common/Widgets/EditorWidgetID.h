/**
 * @file EditorWidgetID.h
 * @brief 에디터 입력 위젯(`EditorWidgets::inputInt` · `dragFloat` · `sliderFloat` · `combo` …)이 받는 숨은 id 입니다. 보이는 이름표를 담을 수 없습니다.
 * @details ImGui 는 위젯 이름표를 칸 **오른쪽**에 붙이고 칸이 창 폭을 따라 늘어, 좁은 칸에서 이름표 · 옆 칸이 잘렸다(시퀀서 · 타일 맵 · 스프라이트 클립 · 멀티플레이 · 캐릭터 에디터, uiaudit).
 *          이름표는 앞(왼쪽)에 따로 그린다 — 행이면 `EditorWidgets::drawPropertyRowLabel`, 도구줄이면 `drawInlineLabel`. 콤보는 `EditorWidgets::beginCombo`,
 *          글 칸은 `drawTextField` 가 보이는 이름표를 스스로 앞으로 옮긴다.
 *          예전에는 린트(editor-widget-labels)가 첫 문자열 인자를 글로 봤지만 리터럴만 볼 수 있어 실행 중 만든 이름표(속성 이름 · 번역 글)가 오른쪽에 남았다.
 *          이제 형식이 가른다: 리터럴은 consteval 생성자가 `##` 로 시작하는지 컴파일 때 보고(아니면 컴파일 오류), 실행 중 만든 id 는 이름 붙은 입구 `fromString` 만 받는다.
 *          에디터 코드는 ImGui 입력 위젯을 직접 부르지 못한다(`EditorInputBan.h` 가 사용 중단으로 다시 선언) — 이 형식을 받는 감싼 함수만 남는다.
 *          언리얼 Slate 의 `SNumericEntryBox` 처럼 칸과 이름표는 서로 다른 위젯이다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

#include <cstddef>

namespace sw::editor
{
    namespace EditorWidgetIDInternal
    {
        /** @brief 정의가 없다. consteval 생성자가 이것을 부르면 상수식이 아니라 컴파일이 멈추고, 이 이름이 오류 문구에 남는다. */
        void reportLabelOnTheRightOfField();
    } // namespace EditorWidgetIDInternal

    /**
     * @class EditorWidgetID
     * @brief `##` 로 시작하는 ImGui 위젯 id 입니다. 리터럴에서 컴파일 때 만들거나 `fromString` 으로 실행 중에 만듭니다.
     * @details 클래스는 내보내지 않는다 — consteval 생성자를 dllexport 클래스에 두지 않고, 실행 중 입구만 내보낸다.
     */
    class EditorWidgetID
    {
    public:
        /** @brief 리터럴 id 입니다. `##` 로 시작하지 않으면 컴파일 오류입니다. */
        template <std::size_t N>
        consteval EditorWidgetID( const utf8 ( &literalID )[N] ) // NOLINT(google-explicit-constructor): 리터럴을 그대로 받는 자리다
            : _pID( literalID )
        {
            if ( N < 3 || literalID[0] != '#' || literalID[1] != '#' )
                EditorWidgetIDInternal::reportLabelOnTheRightOfField();
        }

        /** @brief 실행 중 만든 id(번호 · 이름을 붙인 `##…` 글)입니다. `##` 로 시작하지 않으면 모든 구성에서 멈춘다(오른쪽 이름표를 남기지 않는다). */
        [[nodiscard]] SW_EDITOR_API static EditorWidgetID fromString( const utf8* pHiddenID );

        [[nodiscard]] const utf8* c_str() const { return _pID; }

    private:
        struct RuntimeTag
        {
        };
        constexpr EditorWidgetID( RuntimeTag, const utf8* pID )
            : _pID( pID )
        {
        }

    private:
        const utf8* _pID;
    };
} // namespace sw::editor
