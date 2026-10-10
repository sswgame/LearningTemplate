/**
 * @file UiBindingDesc.h
 * @brief UI 문서의 바인딩 식 하나 — 문서 속성 값이 `{` 로 시작하면 위젯 칸에 넣지 않고 이것으로 뗍니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    /**
     * @struct UiBindingDesc
     * @brief 문서에서 뗀 바인딩 식 하나입니다(`_value="{bind:_health}"`). 식을 푸는 것은 바인딩 단계(뷰모델 · 설정 · 현지화 글)의 일입니다.
     * @details 문서를 읽을 때 값으로 파싱하지 않고 떼어 두므로, 그 칸의 값 형식과 상관없이 식을 적을 수 있습니다(값 파싱 오류가 나지 않는다).
     *          문서 노드에서는 `_widget` 이 무효이고, 화면을 지을 때(`UiDocumentLoader::instantiate`) 그 위젯 번호가 채워집니다.
     */
    struct UiBindingDesc
    {
        string   _propertyPath{};             ///< 위젯 기준 프로퍼티 경로 — `_text`, 구조체 칸이면 `_slot._offsetMin`
        string   _expression{};               ///< 원문 그대로(`{bind:_health, mode=TwoWay}`)
        WidgetID _widget{ kInvalidWidgetID }; ///< 이 식이 붙은 위젯(인스턴스에서만)
        uint32   _sourceLine{ 0 };            ///< 문서의 줄(오류 문구)
    };
} // namespace sw
