/**
 * @file GPUInstanceHighlight.h
 * @brief 인스턴스 강조(상호작용 외곽선 · 감각 모드)를 셰이더 레이아웃 그대로 담는 32비트 값입니다.
 * @details HLSL 쪽은 `Content/engine/shaders/instance_data.hlsli` 의 `highlight` 이고, 읽는 셰이더는 `highlight_outline.hlsl` 입니다.
 *          컴포넌트 층(Object)이 렌더러(Renderer)를 include 할 수 없어 `GPUSpriteInstanceData` 처럼 렌더러 아래의 계약 폴더에 둡니다.
 *          배치 키가 아니라 인스턴스 내용이라, 강조를 켜고 꺼도 배치를 다시 나누지 않고 그 인스턴스만 다시 올라갑니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Presentation/InstanceHighlight.h"

namespace sw
{
    // 방식(`InstanceHighlightStyle`)은 표현 폴더에 있다 — 게임 코드가 GPU 칸을 모르고 강조를 정한다(`Engine/Presentation/InstanceHighlight.h`).

    /**
     * @struct GPUInstanceHighlight
     * @brief 하위 3 바이트가 색(RGB8, r 이 하위), 상위 바이트가 방식(`InstanceHighlightStyle`)입니다. 0 이면 강조가 없습니다.
     */
    struct GPUInstanceHighlight
    {
        /** @brief 방식과 색으로 담습니다. 방식이 None 이면 색과 무관하게 0 입니다. */
        static uint32 pack( InstanceHighlightStyle style, const float3& color )
        {
            if ( style == InstanceHighlightStyle::None )
                return 0u;
            const uint32 red   = static_cast<uint32>( MathUtil::saturate( color._x ) * 255.0f + 0.5f );
            const uint32 green = static_cast<uint32>( MathUtil::saturate( color._y ) * 255.0f + 0.5f );
            const uint32 blue  = static_cast<uint32>( MathUtil::saturate( color._z ) * 255.0f + 0.5f );
            return red | ( green << 8 ) | ( blue << 16 ) | ( static_cast<uint32>( style ) << 24 );
        }
        /** @brief 강조 하나를 담습니다(렌더러의 인스턴스 수집이 부른다). */
        static uint32 pack( const InstanceHighlight& highlight ) { return pack( highlight._style, highlight._color ); }
        /** @brief 담긴 방식입니다. */
        static InstanceHighlightStyle getStyle( uint32 packed ) { return static_cast<InstanceHighlightStyle>( packed >> 24 ); }
    };
} // namespace sw
