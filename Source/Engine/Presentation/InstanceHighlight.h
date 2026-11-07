/**
 * @file Engine/Presentation/InstanceHighlight.h
 * @brief 메시 강조(외곽선)의 방식과 색입니다 — 게임 코드가 GPU 인스턴스 칸의 담는 법(`GPUInstanceHighlight::pack`)을 모르고 강조를 정합니다.
 * @details 담는 일(색 RGB8 + 방식 바이트)은 렌더러의 인스턴스 수집(`GPUSceneBuilder`)이 한다. 언리얼 `UPrimitiveComponent::SetRenderCustomDepth` 가 값만 두고
 *          GPU 칸은 프록시가 채우는 갈림과 같다(기억).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief 강조 방식입니다. 값은 셰이더 `kHighlightStyle*`(instance_data.hlsli) 와 같습니다(`GPUInstanceHighlight` 가 상위 바이트로 담는다). */
    enum class InstanceHighlightStyle : uint8
    {
        None = 0, ///< 강조 없음
        Outline,  ///< 보이는 곳에만 외곽선(깊이 테스트 — 상호작용 대상)
        Sense,    ///< 벽 너머에서도 보이는 외곽선(깊이 테스트 없음 — 감각 모드)
    };

    /** @struct InstanceHighlight @brief 강조 하나입니다. 방식이 None 이면 색과 무관하게 강조가 없습니다. */
    struct InstanceHighlight
    {
        InstanceHighlightStyle _style{ InstanceHighlightStyle::None };
        float3                 _color{};

        /** @brief 보이는 결과가 같으면 같습니다(방식이 None 이면 색은 보지 않는다). */
        bool operator==( const InstanceHighlight& other ) const
        {
            if ( _style != other._style )
                return false;
            return _style == InstanceHighlightStyle::None || ( _color._x == other._color._x && _color._y == other._color._y && _color._z == other._color._z );
        }
        bool operator!=( const InstanceHighlight& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw
