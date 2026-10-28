/**
 * @file GenreGimmickUtil.h
 * @brief 장르 기믹 컴포넌트가 함께 쓰는 도구입니다. "game" 채널 이벤트, 오브젝트의 몸(콜라이더와 그래픽)을 켜고 끄는 함수, 고정 스텝 수를 세는 함수가 있습니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Event/EventType.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/FixedStepTimer.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Component;
    class GameObject;

    /** @brief 발사대나 스프링이 오브젝트를 쏘았음을 알립니다. 캐릭터 컨트롤러와 이동 몸이 이 이벤트를 받아 속도를 바꿉니다. */
    struct SW_GF_API GimmickLaunchEvent final : IEvent
    {
        GameObjectHandle _target{};
        GameObjectHandle _source{};
        float3           _velocity{};
        SW_DECLARE_GAMEPLAY_EVENT( GimmickLaunchEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 부스트 패드를 밟았음을 알립니다. 차량 몸이 이 이벤트를 받아 부스트를 적용합니다. */
    struct SW_GF_API GimmickBoostEvent final : IEvent
    {
        GameObjectHandle _target{};
        float32          _duration{ 0.0f };
        float32          _strength{ 0.0f };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickBoostEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템을 얻었음을 알립니다. 아이템 상자와 채집 지점에서 냅니다. 인벤토리 규칙이 이 이벤트를 받습니다. */
    struct SW_GF_API GimmickItemEvent final : IEvent
    {
        GameObjectHandle _target{};
        GameObjectHandle _source{};
        hashed_string    _item{};
        int32            _count{ 0 };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickItemEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 연출 신호입니다. 공포 트리거, 폭발, 파괴 단계에서 냅니다. 사운드, 카메라 흔들림, 시퀀서가 이름으로 이 신호를 받습니다. */
    struct SW_GF_API GimmickCueEvent final : IEvent
    {
        GameObjectHandle _source{};
        GameObjectHandle _target{};
        hashed_string    _cue{};
        float3           _position{};
        float32          _magnitude{ 0.0f };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickCueEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 소리를 냈음을 알립니다. 잠입 게임에서 반경 안의 AI 가 이 소리를 듣습니다(`AIStimulus::_noiseRadius`). */
    struct SW_GF_API GimmickNoiseEvent final : IEvent
    {
        GameObjectHandle _source{};
        float3           _position{};
        float32          _radius{ 0.0f };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickNoiseEvent );
    };
} // namespace sw

namespace sw
{
    /** @struct GenreGimmickUtil */
    struct SW_GF_API GenreGimmickUtil
    {
        /** @brief 기믹이 스텝 수를 세는 고정 스텝(60 Hz)입니다. */
        static constexpr float32 kStepTime = 1.0f / 60.0f;

        /** @brief 초를 스텝 수로 바꿉니다. 반올림하며 최소 @p minSteps 입니다. */
        static int32 toSteps( float32 seconds, int32 minSteps );
        /** @brief 기믹 컴포넌트가 쓰는 시계입니다. 60 Hz 이고 한 프레임에 최대 0.25 초까지 진행합니다. */
        static FixedStepTimer makeClock() { return FixedStepTimer( kStepTime, 0.25f ); }
        /**
         * @brief 오브젝트의 몸(기믹 논리 컴포넌트와 @p pKeep 을 뺀 모든 컴포넌트)을 켜고 끕니다. 무너진 발판이나 먹은 아이템 상자를 숨기고 복원할 때 씁니다.
         * @details 꺼진 콜라이더는 겹침에 포함되지 않고, 꺼진 그래픽은 그려지지 않는다. 틱 중이면 틱 뒤로 미룬다.
         */
        static void setBodyActive( GameObject& object, bool bActive, const Component* pKeep );
        /** @brief 오브젝트의 `InteractableComponent` 를 켜고 끕니다. 다 쓴 채집 지점에 씁니다. 상호작용하는 쪽이 병렬 틱에서 읽으므로 틱 중이면 틱 뒤로 미룹니다. */
        static void setInteractableEnabled( GameObject& object, bool bEnabled );
    };
} // namespace sw
