/**
 * @file UiStylePass.h
 * @brief 스타일 걷기 — 스타일 더러운 위젯만 계산된 스타일을 다시 정하고, 바뀐 칸 종류로 레이아웃 · 그리기 더러움을 고릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class UiStyleSet;
    class Widget;
    class WidgetTree;

    /**
     * @class UiStylePass
     * @brief 프레임 순서(애니메이션 → 바인딩 → **스타일** → 레이아웃 → 그리기)의 스타일 단계입니다(유니티 UI Toolkit 의 스타일 갱신 · Slate 는 없음).
     * @details `kStyle` 인 위젯(상태 · 클래스 · 이름 · 트리에 붙음)을 다시 맞춥니다. 자손은 그 위젯의 계산된 스타일이 바뀌었거나(상속) 그 위젯이 맞는
     *          조상 쪽 선택자 조각이 바뀌었을 때만(`ButtonWidget:hover TextWidget`) 내려가 다시 맞춥니다 — 호버 하나에 트리 전체가 돌지 않게.
     *          바뀐 칸이 레이아웃 칸이면 `kLayout`, 불투명도면 `kTransform`(자손 그림까지), 그 밖은 `kPaint` 입니다. 자기 `kStyle` 로 다시 맞춘 위젯은 그림도
     *          다시 칠합니다(스타일이 정하지 않은 상태 겉모습 — 버튼의 상태 브러시).
     */
    class SW_API UiStylePass
    {
    public:
        /** @brief 트리의 스타일 더러운 위젯을 다시 맞춥니다. 다시 계산한 위젯 수입니다. */
        static uint32 update( WidgetTree& tree, UiStyleSet& styleSet, bool bNavigationMode );

    private:
        /** @brief @p widget 을 다시 맞추고 필요한 자손으로 내려갑니다. */
        static uint32 restyle( Widget& widget, UiStyleSet& styleSet, bool bNavigationMode );
    };
} // namespace sw
