/**
 * @file AnimNotifyListener.h
 * @brief 애니메이터(3D 스켈레탈 · 2D 스프라이트)가 한 프레임에 울린 알림을 받는 쪽의 창구(`IAnimNotifyListener` — 알림 디스패치가 구현)와, 루트 모션을
 *        오브젝트에 옮기기 전에 고치는 쪽의 창구(`IRootMotionModifier` — 모션 워핑이 구현)입니다.
 * @details 애니메이터는 받는 쪽이 무엇을 하는지(소리 · 스폰 · 판정) 모릅니다. 같은 오브젝트의 받는 쪽 하나를 걸고(`setNotifyListener`), 알림을 모은 뒤 한 번
 *          넘깁니다. 3D 는 애니메이션 시스템의 게임 스레드 마무리(`finishAnimationFrame`, 루트 모션 적용 전)에서, 2D 는 스프라이트 애니메이터의 틱
 *          (병렬 워커 — `_bFromTick`)에서 부릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Math/Math.h"

#include "Engine/Animation/AnimPlayback.h"

namespace sw
{
    /**
     * @struct AnimNotifyFrame
     * @brief 한 프레임의 알림 묶음입니다. 가리키는 배열은 이 호출 동안만 유효합니다.
     * @details `_listActivePlayable` 은 지금 재생 중인(섞이는 칸 · 레이어 포함) 재생할 것입니다 — 열린 구간 알림의 클립이 여기 없으면 그 구간은 끊긴 것이라
     *          받는 쪽이 `End` 를 대신 냅니다(상태가 바뀌어 클립이 빠졌다).
     */
    struct AnimNotifyFrame
    {
        vector_reference<const AnimFiredNotify>      _listFired;
        vector_reference<const IAnimPlayable* const> _listActivePlayable;
        float32                                      _deltaSeconds{ 0.0f };
        uint8                                        _bFromTick{ SW_FALSE }; ///< 병렬 틱 워커에서 불렸다 — 월드를 바꾸는 일은 틱 뒤로 미룬다
    };
} // namespace sw

namespace sw
{
    /** @brief 알림을 받는 쪽입니다. 애니메이터가 `setNotifyListener` 로 빌립니다(받는 쪽이 사라지기 전에 떼야 합니다). */
    class SW_API IAnimNotifyListener
    {
    public:
        IAnimNotifyListener()                                        = default;
        virtual ~IAnimNotifyListener()                               = default;
        IAnimNotifyListener( const IAnimNotifyListener& )            = delete;
        IAnimNotifyListener& operator=( const IAnimNotifyListener& ) = delete;

        /** @brief 이번 프레임의 알림이 울렸습니다(울린 것이 없어도 열린 구간의 틱을 위해 매 프레임 불립니다). */
        virtual void onAnimNotifiesFired( const AnimNotifyFrame& frame ) = 0;
    };
} // namespace sw

namespace sw
{
    class SkeletalAnimatorComponent;

    /**
     * @struct RootMotionFrame
     * @brief 한 프레임의 루트 모션 — 오브젝트에 옮기기 직전의 값입니다. 고치는 쪽이 이것을 바꿉니다.
     * @details 이동은 **월드** 공간(오브젝트의 지금 회전 · 배율로 돌린 것), 회전은 오브젝트 로컬 회전 뒤에 곱할 차이(`로컬 × 차이`)입니다.
     *          `_previousClipTime` → `_clipTime` 은 지금 칸 클립이 이번 프레임에 지나간 시각입니다(반복으로 감겼으면 `_bClipWrapped`).
     */
    struct RootMotionFrame
    {
        float3               _worldTranslation{};
        quaternion           _rotation{};
        const IAnimPlayable* _pClip{ nullptr }; ///< 지금 칸의 재생할 것
        float32              _previousClipTime{ 0.0f };
        float32              _clipTime{ 0.0f };
        float32              _deltaSeconds{ 0.0f };
        uint8                _bClipWrapped{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 루트 모션을 오브젝트에 옮기기 전에 고치는 쪽입니다(모션 워핑). 게임 스레드에서 불립니다. 애니메이터가 빌립니다. */
    class SW_API IRootMotionModifier
    {
    public:
        IRootMotionModifier()                                        = default;
        virtual ~IRootMotionModifier()                               = default;
        IRootMotionModifier( const IRootMotionModifier& )            = delete;
        IRootMotionModifier& operator=( const IRootMotionModifier& ) = delete;

        /** @brief 이번 프레임의 루트 모션을 고칩니다. @p animator 는 그 애니메이터, @p inoutFrame 의 이동 · 회전을 바꿉니다. */
        virtual void modifyRootMotion( const SkeletalAnimatorComponent& animator, RootMotionFrame& inoutFrame ) = 0;
    };
} // namespace sw
