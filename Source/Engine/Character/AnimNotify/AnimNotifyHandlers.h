/**
 * @file AnimNotifyHandlers.h
 * @brief 엔진 내장 알림 처리기의 등록과, 엔진이 모르는 쪽(카메라 · 게임)에 넘기는 요청 창구입니다.
 * @details 처리기 종류(표의 `handler` 이름 — 인자):
 *          | 이름 | 하는 일 | 인자 |
 *          |---|---|---|
 *          | `PlaySound` | 소켓 자리에서 소리 | sound(필수) · socket |
 *          | `SpawnPrefab` | 소켓 변환에 프리팹(이펙트 · 탄피) | prefab(필수) · socket · offset · attach |
 *          | `Footstep` | 소켓에서 아래로 쏜 광선이 맞은 물리 재질 = 바닥 종류, 소리 경로의 `{surface}` 를 그 이름(소문자)으로 | socket(필수) · distance · sound |
 *          | `HitWindow` | 구간 — 두 소켓 사이 칼날을 프레임마다 쓸어(지난 자리 → 지금) 맞은 오브젝트에 한 번씩 `onHitReceived` | socketA · socketB(필수) · radius · damage · impulse · samples |
 *          | `CameraShake` | 카메라 충격 요청(`getCameraShakeRequested` 를 듣는 카메라가 받는다) | amplitude · duration · frequency · radius · socket |
 *          | `GameplayEvent` | 같은 오브젝트의 컴포넌트에 `onAnimNotify`(구간이면 시작 · 끝) | event(필수) |
 *          | `MotionWarp` | 구간 — `MotionWarpingComponent` 에 워프 창을 열어 구간 끝에 이름 붙은 목표에 닿게 루트 모션을 휜다 | target(필수) · translation · rotation · vertical |
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/Math.h"

namespace sw
{
    class AnimNotifyHandlerRegistry;

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

namespace sw
{
    /** @brief 내장 처리기 등록과 요청 창구입니다(전부 static). */
    struct SW_API AnimNotifyHandlerUtil
    {
        /** @brief 내장 처리기를 @p registry 에 더합니다. 등록부의 기본 표가 처음 만들어질 때 부릅니다. */
        static bool registerBuiltInHandlers( AnimNotifyHandlerRegistry& registry );
        /**
         * @brief 카메라 흔들림 요청을 듣는 자리입니다(엔진 수명). 카메라 매니저가 플레이 시작에 구독하고 끝에 뗍니다.
         * @details 엔진은 카메라를 모릅니다 — 요청만 내고, 게임프레임워크의 카메라가 자기 충격 목록에 더합니다. 게임 스레드에서 불립니다.
         */
        static CameraShakeRequestDelegate& getCameraShakeRequested();
    };
} // namespace sw
