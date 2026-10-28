/**
 * @file UIStylePass.h
 * @brief 스타일 순회를 정의합니다. 스타일이 더티한 위젯만 계산된 스타일을 다시 정하고, 바뀐 필드 종류에 따라 레이아웃이나 그리기 더티로 표시합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class UIStyleSet;
    class Widget;
    class WidgetTree;

    /**
     * @class UIStylePass
     * @brief 프레임 순서(애니메이션, 바인딩, **스타일**, 레이아웃, 그리기) 중 스타일 단계입니다(유니티 UI Toolkit 의 스타일 갱신에 해당하며 Slate 에는 없습니다).
     * @details `kStyle` 인 위젯(상태, 클래스, 이름이 바뀌었거나 트리에 붙은 경우)을 다시 맞춘다. 자손은 그 위젯의 계산된 스타일이 바뀌었거나(상속) 그 위젯이 맞는
     *          조상 쪽 선택자 조각이 바뀌었을 때만(`ButtonWidget:hover TextWidget`) 내려가 다시 맞춘다. 호버 하나에 트리 전체를 돌지 않기 위해서다.
     *          바뀐 필드가 레이아웃 필드이면 `kLayout`, 불투명도이면 `kTransform`(자손 그리기까지), 그 밖은 `kPaint` 로 표시한다. 자기 `kStyle` 로 다시 맞춘 위젯은 그리기도
     *          다시 한다(스타일이 정하지 않은 상태 겉모습이 있기 때문이며, 예로 버튼의 상태 브러시가 있다).
     */
    class SW_API UIStylePass
    {
    public:
        /** @brief 트리의 스타일 더러운 위젯을 다시 맞춥니다. 다시 계산한 위젯 수입니다. */
        static uint32 update( WidgetTree& tree, UIStyleSet& styleSet, bool bNavigationMode );
        /** @brief 바뀐 필드 비트 @p changedFields 의 무효화 이유를 반환합니다(레이아웃 필드는 kLayout, 불투명도는 kTransform, 그 밖은 kPaint). 없으면 kNone 입니다. */
        static uint32 makeDirtyReason( uint32 changedFields );

    private:
        /** @brief @p widget 을 다시 맞추고 필요한 자손으로 내려갑니다. */
        static uint32 restyle( Widget& widget, UIStyleSet& styleSet, bool bNavigationMode );
    };
} // namespace sw
