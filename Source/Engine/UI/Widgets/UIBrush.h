/**
 * @file UIBrush.h
 * @brief 위젯 필드에 적는 겉모습 하나입니다(리플렉션을 쓰므로 문서와 인스펙터가 씁니다). 칠할 때 캔버스 브러시로 바꿉니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Texture2D;

    struct CanvasBrush;

    /**
     * @struct UIBrush
     * @brief 사각형 하나의 겉모습입니다. 색, 테두리, 둥근 모서리, 9-슬라이스 여백을 가집니다(언리얼 FSlateBrush, 유니티 배경 스타일, Godot StyleBoxFlat 에 해당).
     * @details 길이는 UI 단위다. 이미지는 위젯이 따로 가지고(`ImageWidget::setImage`) 칠할 때 `makeCanvasBrush` 에 넘긴다.
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

        /** @brief 모두 투명이라 칠할 것이 없으면 true 입니다(색과 테두리 알파가 0). */
        bool isInvisible() const { return _color._w <= 0.0f && ( _borderWidth <= 0.0f || _borderColor._w <= 0.0f ); }
        /** @brief 캔버스 브러시로 바꿉니다. @p image 가 있으면 이미지 브러시이며 색을 곱합니다. */
        CanvasBrush makeCanvasBrush( const shared_ptr<const Texture2D>& image = {} ) const;
        /** @brief 단색 둥근 모서리 브러시를 만듭니다(코드로 만드는 위젯과 테스트 화면용). */
        static UIBrush makeSolid( const float4& color, float32 cornerRadius = 0.0f );
    };
} // namespace sw
