/**
 * @file CanvasPanel.h
 * @brief 앵커 · 오프셋으로 자식을 놓는 패널입니다(UMG Canvas Panel · Godot 앵커/오프셋 · 유니티 position: absolute).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/PanelWidget.h"

namespace sw
{
    /**
     * @class CanvasPanel
     * @brief 자식 슬롯의 앵커 · 오프셋으로 변을 정합니다 — 변 = 앵커 × 패널 크기 + 오프셋(Godot 방식).
     * @details 앵커가 같으면 점에 붙고(크기 = 오프셋 차), 다르면 늘어납니다. 자동 크기(`_bAutoSize`)면 원하는 크기를 커지는 쪽(`_grow*`)으로 씁니다.
     *          원하는 크기 = 앵커가 모두 (0,0) 인 자식들의 오른쪽 아래 끝 최대(나머지는 패널 크기에 기대므로 넣지 않는다 — UMG 와 같다).
     *          z 순서(`_zOrder`)가 같으면 자식 순서입니다(`collectPaintOrder`).
     */
    REFLECT( Category = "Layout", DisplayName = "Canvas Panel", Tooltip = "Places children by anchors and offsets" )
    class SW_API CanvasPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        CanvasPanel();
        ~CanvasPanel() override;

        const TypeInfo* getTypeInfo() const override;

        bool hasCustomPaintOrder() const override { return true; }
        /** @brief 그리는 순서의 자식 자리를 담습니다 — z 순서 오름차순, 같으면 자식 순서(안정). 히트 테스트는 그 역순입니다. */
        void collectPaintOrder( vector<uint32>& outListIndex ) const override;

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;
    };
} // namespace sw
