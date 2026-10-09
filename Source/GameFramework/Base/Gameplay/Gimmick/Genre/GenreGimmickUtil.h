/**
 * @file GenreGimmickUtil.h
 * @brief 장르 기믹 컴포넌트가 함께 쓰는 것 — "game" 채널 이벤트들과, 오브젝트의 몸(콜라이더 · 그림)을 켜고 끄기 · 고정 걸음 수 세기입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Event/EventType.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Component;
    class GameObject;

    /** @brief 발사대 · 스프링이 오브젝트를 쏘았습니다. 캐릭터 컨트롤러 · 이동 몸이 받아 속도를 바꿉니다. */
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
    /** @brief 부스트 패드를 밟았습니다. 차량 몸이 받아 부스트를 겁니다. */
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
    /** @brief 아이템을 얻었습니다(아이템 상자 · 채집 지점). 인벤토리 규칙이 받습니다. */
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
    /** @brief 연출 신호(공포 트리거 · 폭발 · 파괴 단계)입니다 — 사운드 · 카메라 흔들림 · 시퀀서가 이름으로 받습니다. */
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
    /** @brief 소리를 냈습니다(잠입 — 반경 안의 AI 가 듣는다: `AiStimulus::_noiseRadius`). */
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
        /** @brief 기믹이 걸음을 세는 고정 스텝(60 Hz)입니다. */
        static constexpr float32 kStepTime = 1.0f / 60.0f;

        /** @brief 초 → 걸음 수(반올림, 최소 @p minSteps)입니다. */
        static int32 toSteps( float32 seconds, int32 minSteps );
        /** @brief 기믹 컴포넌트가 쓰는 시계입니다(60 Hz, 한 프레임 최대 0.25 초). */
        static FixedStepTimer makeClock() { return FixedStepTimer( kStepTime, 0.25f ); }
        /**
         * @brief 오브젝트의 몸(기믹 논리 컴포넌트 · @p pKeep 을 뺀 모든 컴포넌트)을 켜고 끕니다 — 무너진 발판 · 먹은 아이템 상자를 숨기고 되살린다.
         * @details 꺼진 콜라이더는 겹침에 들지 않고 꺼진 그림은 그리지 않습니다. 틱 중이면 틱 뒤로 미룹니다.
         */
        static void setBodyActive( GameObject& object, bool bActive, const Component* pKeep );
        /** @brief 오브젝트의 `InteractableComponent` 를 켜고 끕니다(다 쓴 채집 지점). 틱 중이면 틱 뒤로 미룹니다 — 하는 쪽이 병렬 틱에서 읽는다. */
        static void setInteractableEnabled( GameObject& object, bool bEnabled );
    };
} // namespace sw
