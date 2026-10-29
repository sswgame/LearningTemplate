/**
 * @file CameraShakeChannel.h
 * @brief 카메라 흔들림 요청 값과, 씬의 카메라 레지스트리가 소유하는 요청 채널입니다.
 * @details 요청을 내는 쪽(애니메이션 알림의 `CameraShake` 처리기)은 카메라를 모르고, 받는 쪽(게임 프레임워크의 카메라 매니저)은 알림을 모른다.
 *          둘은 씬(`GameObjectManager::getCameraRegistry`)의 채널 하나로 만난다 — 엔진 수명 정적 델리게이트를 두면 씬이 여럿일 때(에디터 미리보기 ·
 *          분할 월드) 다른 씬의 카메라까지 흔들리고, 등록 목록을 지키는 것이 없다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/LockedMulticastDelegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /** @brief 알림이 낸 카메라 흔들림 요청입니다(카메라 쪽 충격 서술과 같은 필드이며 크기, 길이, 진동수, 거리 감쇠를 담습니다). */
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
} // namespace sw

namespace sw
{
    /**
     * @brief 카메라 흔들림 요청을 듣는 쪽의 목록입니다. `add` 가 돌려주는 구독이 등록을 소유합니다.
     * @details 공용 스레드 안전 멀티캐스트(`LockedMulticastDelegate`)다 — 알림 처리기(워커)가 방송하고 카메라 매니저(게임 스레드)가 등록 · 해제한다. 방송은 잠금 밖의
     *          불변 사본으로 부르고 해제는 앞선 방송이 끝날 때까지 기다리므로, 풀린 듣는 쪽이 불리는 일과 듣는 쪽 안의 재진입(등록 · 해제 · 방송)이 형식으로 안전하다.
     *          손으로 짠 재진입 잠금 · 번호 카운터 · 전달 깊이 목록을 대신한다.
     */
    using CameraShakeChannel = LockedMulticastDelegate<void( const CameraShakeRequest& )>;
} // namespace sw
