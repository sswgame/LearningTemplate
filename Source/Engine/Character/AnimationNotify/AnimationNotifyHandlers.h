/**
 * @file AnimationNotifyHandlers.h
 * @brief 엔진 내장 알림 처리기의 등록과, 엔진이 모르는 쪽(카메라, 게임)에 넘기는 요청 인터페이스입니다.
 * @details 처리기 종류(테이블의 `handler` 이름과 인자)는 다음과 같다.
 *          | 이름 | 하는 일 | 인자 |
 *          |---|---|---|
 *          | `PlaySound` | 소켓 위치에서 소리를 낸다 | sound(필수), socket |
 *          | `SpawnPrefab` | 소켓 변환에 프리팹(이펙트, 탄피)을 만든다 | prefab(필수), socket, offset, attach |
 *          | `Footstep` | 소켓에서 아래로 쏜 광선이 맞은 물리 재질을 바닥 종류로 보고, 소리 경로의 `{surface}` 를 그 이름(소문자)으로 바꾼다 | socket(필수), distance, sound |
 *          | `HitWindow` | 구간 알림이다. 두 소켓 사이 칼날을 프레임마다 쓸어(지난 위치에서 현재 위치까지, `MeleeHitWindowUtil::sweep`) 맞은 오브젝트에 한 번씩 `onHitReceived` 를 호출한다. 클라이언트는 기록만 한다 | socketA, socketB(필수), radius, damage, impulse, samples |
 *          | `CameraShake` | 카메라 충격을 요청한다(`getCameraShakeRequested` 를 듣는 카메라가 받는다) | amplitude, duration, frequency, radius, socket |
 *          | `GameplayEvent` | 같은 오브젝트의 컴포넌트에 `onAnimationNotify` 를 호출한다(구간이면 시작과 끝) | event(필수) |
 *          | `MotionWarp` | 구간 알림이다. `MotionWarpingComponent` 에 워프 창을 열어, 구간 끝에 이름 붙은 목표에 닿게 루트 모션을 휜다 | target(필수), translation, rotation, vertical |
 */
#pragma once
#include "Core/Common/Macros.h"

#include "Engine/Character/AnimationNotify/CameraShakeRequest.h"

namespace sw
{
    class AnimationNotifyHandlerRegistry;
} // namespace sw

namespace sw
{
    /** @brief 내장 처리기 등록과 요청 함수 모음입니다(전부 static). */
    struct SW_API AnimationNotifyHandlerUtil
    {
        /** @brief 내장 처리기를 @p registry 에 추가합니다. 레지스트리의 기본 테이블이 처음 만들어질 때 호출합니다. */
        static bool registerBuiltInHandlers( AnimationNotifyHandlerRegistry& registry );
        /**
         * @brief 카메라 흔들림 요청을 받는 리스너입니다(엔진 수명). 카메라 매니저가 플레이 시작에 구독하고 끝에 해제합니다.
         * @details 엔진은 카메라를 모른다. 요청만 내고, 게임 프레임워크의 카메라가 자기 충격 목록에 추가한다. 게임 스레드에서 호출한다.
         */
        static CameraShakeRequestDelegate& getCameraShakeRequested();
    };
} // namespace sw
