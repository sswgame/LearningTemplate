/**
 * @file Engine/Presentation/InstanceSpriteFrame.h
 * @brief 인스턴스 하나의 스프라이트 프레임(아틀라스 UV 사각형) · 색 · 픽셀 스냅 단위입니다 — 컴포넌트 · 인스턴스 배치가 GPU 칸의 담는 법을 모르고 값을 듭니다.
 * @details 담는 일(UV unorm16 · 색 RGBA8 — `GPUSpriteInstanceData::make`)은 렌더러의 인스턴스 수집(`GPUSceneBuilder`)이 한다. `InstanceHighlight` 와 같은 갈림이다
 *          (언리얼 `UPrimitiveComponent` 가 값만 두고 GPU 칸은 프록시가 채우는 자리 — 기억). 머티리얼 인스턴스가 아니라 인스턴스에 싣는 까닭은 `GPUSpriteInstanceData` 머리말.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @struct InstanceSpriteFrame
     * @brief 기본값은 텍스처 전체 · 흰색 불투명 · 스냅 없음입니다.
     * @details UV 사각형은 머티리얼 uvRect 와 같은 (u, v, 폭, 높이) 이고 폭 · 높이가 음수면 뒤집기다. 꼭짓점은 담을 때 [0, 1] 로, 색은 [0, 1] 로 제한된다.
     */
    struct InstanceSpriteFrame
    {
        float4  _uvRect{ 0.0f, 0.0f, 1.0f, 1.0f }; ///< (u, v, 폭, 높이) 아틀라스 프레임
        float4  _tint{ 1.0f, 1.0f, 1.0f, 1.0f };   ///< 곱하는 색(RGBA)
        float32 _pixelSnap{ 0.0f };                ///< 픽셀 스냅 단위(자산 픽셀 하나의 월드 길이 = 1 / PPU). 0 이면 끈다 — 픽셀 퍼펙트 카메라가 정한다

        /** @brief 비트로 비교합니다(더티 판정 — 엡실론이면 작은 변화가 올라가지 않는다). */
        bool operator==( const InstanceSpriteFrame& other ) const
        {
            return Memory::compare( &_uvRect, &other._uvRect, sizeof( _uvRect ) ) == 0 && Memory::compare( &_tint, &other._tint, sizeof( _tint ) ) == 0 &&
                   Memory::compare( &_pixelSnap, &other._pixelSnap, sizeof( _pixelSnap ) ) == 0;
        }
        /** @brief operator== 의 부정입니다. */
        bool operator!=( const InstanceSpriteFrame& other ) const { return ( *this == other ) == false; }
    };
} // namespace sw
