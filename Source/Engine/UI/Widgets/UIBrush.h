/**
 * @file UIBrush.h
 * @brief 위젯 칸에 적는 겉모습 하나입니다(리플렉션 — 문서 · 인스펙터가 쓴다). 칠할 때 캔버스 브러시로 바꿉니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Texture2D;

    /**
     * @struct UIBrush
     * @brief 사각형 하나의 겉모습입니다 — 색 · 테두리 · 둥근 모서리 · 9-슬라이스 여백(언리얼 FSlateBrush · 유니티 배경 스타일 · Godot StyleBoxFlat).
     * @details 길이는 UI 단위입니다. 그림은 위젯이 따로 들고(`ImageWidget::setImage`) 칠할 때 `makeCanvasBrush` 에 넘깁니다.
     */
    REFLECT()
    struct SW_API UIBrush
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Color", Tooltip = "Fill color (straight RGBA); multiplies the image" )
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( DisplayName = "Border Color" )
        float4 _borderColor{};
        PROPERTY( DisplayName = "Corner Radius", Tooltip = "Top-left, top-right, bottom-right, bottom-left", Meta = "Units=ui" )
        float4 _cornerRadius{};
        PROPERTY( DisplayName = "Nine Slice Margin", Tooltip = "Fraction of the image (left, top, right, bottom, 0..1); 0 stretches" )
        float4 _nineSliceMargin{};
        PROPERTY( DisplayName = "Border Width", Min = 0.0, Meta = "Units=ui" )
        float32 _borderWidth{ 0.0f };

        /** @brief 다 투명이라 칠할 것이 없으면 true 입니다(색 · 테두리 알파가 0). */
        bool isInvisible() const { return _color._w <= 0.0f && ( _borderWidth <= 0.0f || _borderColor._w <= 0.0f ); }
        /** @brief 캔버스 브러시로 바꿉니다. @p image 가 있으면 그림 브러시(색을 곱한다)입니다. */
        CanvasBrush makeCanvasBrush( const shared_ptr<const Texture2D>& image = {} ) const;
        /** @brief 단색 · 둥근 모서리 브러시를 만듭니다(코드로 짓는 위젯 · 시험 화면). */
        static UIBrush makeSolid( const float4& color, float32 cornerRadius = 0.0f );
    };
} // namespace sw
