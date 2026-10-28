/**
 * @file ScheduleSkeletalAnimator.h
 * @brief 일정의 활동 애니메이션 훅(`IScheduleActivityAnimator`)을 스켈레탈 애니메이터(`SkeletalAnimatorComponent`)의 그래프 상태에 연결하는 구현입니다.
 * @details 활동이 시작되면 NPC 오브젝트(이름이 NPC id)의 애니메이터에서 상태를 재생한다. 상태 이름은 블록의 `animation` 이고, 없으면 활동 종류에 정해 둔 기본 상태다.
 *          활동이 끝나면 쉬는 상태(기본 `Idle`)로 돌아간다. 활동이 바로 이어지면 끝남 뒤에 시작이 불려 새 상태가 이긴다.
 *          일정은 화면 안(Near) NPC 에게만 이 훅을 부르므로, 화면 밖 NPC 의 애니메이터는 건드리지 않는다.
 *          그래프에 없는 상태는 재생하지 않고 횟수만 센다(`getFailedPlayCount`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ScheduleSkeletalAnimator
     * @brief 활동 신호를 NPC 오브젝트의 `SkeletalAnimatorComponent::play` 로 바꿉니다. 게임 스레드에서 일정이 부르는 훅입니다.
     */
    class SW_GF_API ScheduleSkeletalAnimator final : public IScheduleActivityAnimator
    {
    public:
        ScheduleSkeletalAnimator();

        /** @brief NPC 오브젝트를 찾을 씬의 오브젝트 매니저를 정합니다. 소유하지 않습니다. */
        void initialize( GameObjectManager* pManager );
        /** @brief 블록에 `animation` 이 없을 때 쓸 활동 종류의 기본 상태를 정합니다. 같은 활동을 다시 정하면 바꿉니다. */
        void setActivityState( const hashed_string& activity, const hashed_string& stateName );
        /** @brief 활동이 끝났을 때 돌아가는 상태입니다(기본 `Idle`). 비우면 돌아가지 않고 마지막 상태를 둡니다. */
        void setIdleState( const hashed_string& stateName ) { _idleState = stateName; }
        /** @brief 상태 사이를 섞는 시간(초)입니다. 음수면 애니메이터의 기본입니다. */
        void setBlendSeconds( float32 seconds ) { _blendSeconds = seconds; }

        void onActivityStarted( const ScheduleActivityCue& cue ) override;
        void onActivityEnded( const ScheduleActivityCue& cue ) override;

        /** @brief 신호가 고르는 상태입니다: 블록의 `animation`, 없으면 활동 종류의 기본 상태, 둘 다 없으면 빈 이름입니다. */
        hashed_string chooseState( const ScheduleActivityCue& cue ) const;

        /** @brief NPC 오브젝트나 애니메이터가 없거나 그래프에 없는 상태라 재생하지 못한 횟수입니다. */
        uint32 getFailedPlayCount() const { return _failedPlayCount; }
        /** @brief 재생을 시작한 횟수입니다. */
        uint32 getPlayCount() const { return _playCount; }

    private:
        /** @brief NPC 의 애니메이터에서 상태를 재생합니다. 성공하면 true 입니다. */
        bool playState( const hashed_string& npc, const hashed_string& stateName );

        /** @brief 활동 종류와 기본 상태의 짝입니다. */
        struct ActivityState
        {
            hashed_string _activity{};
            hashed_string _state{};
        };

        GameObjectManager*    _pManager;
        hashed_string         _idleState;
        vector<ActivityState> _listActivityState;
        float32               _blendSeconds;
        uint32                _playCount;
        uint32                _failedPlayCount;
    };
} // namespace sw
