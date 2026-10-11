/**
 * @file CameraShakeRequest.h
 * @brief 애니메이션 알림이 내는 카메라 흔들림 요청 값입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/Math.h"

namespace sw
{
    /** @brief 알림이 낸 카메라 흔들림 요청입니다(카메라 쪽 충격 서술과 같은 칸 — 크기 · 길이 · 진동수 · 거리 감쇠). */
    struct CameraShakeRequest
    {
        float3  _origin{};
        uint64  _sourceObjectID{ 0 };
        float32 _amplitude{ 0.1f };
        float32 _duration{ 0.3f };
        float32 _frequency{ 30.0f };
        float32 _falloffRadius{ 0.0f };
    };

    using CameraShakeRequestCallback = Delegate<void( const CameraShakeRequest& )>;
    using CameraShakeRequestDelegate = MulticastDelegate<void( const CameraShakeRequest& )>;
} // namespace sw
