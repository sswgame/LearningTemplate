#include "pch.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleSkeletalAnimator.h"

#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    ScheduleSkeletalAnimator::ScheduleSkeletalAnimator()
        : _pManager{ nullptr }
        , _idleState{ "Idle" }
        , _listActivityState{}
        , _blendSeconds{ -1.0f }
        , _playCount{ 0 }
        , _failedPlayCount{ 0 }
    {
    }

    void ScheduleSkeletalAnimator::initialize( GameObjectManager* pManager )
    {
        _pManager        = pManager;
        _playCount       = 0;
        _failedPlayCount = 0;
    }

    void ScheduleSkeletalAnimator::setActivityState( const hashed_string& activity, const hashed_string& stateName )
    {
        for ( ActivityState& entry : _listActivityState )
        {
            if ( entry._activity == activity )
            {
                entry._state = stateName;
                return;
            }
        }
        _listActivityState.push_back( ActivityState{ activity, stateName } );
    }

    hashed_string ScheduleSkeletalAnimator::chooseState( const ScheduleActivityCue& cue ) const
    {
        if ( cue._animation.empty() == false )
            return cue._animation;
        for ( const ActivityState& entry : _listActivityState )
        {
            if ( entry._activity == cue._activity )
                return entry._state;
        }
        return hashed_string{};
    }

    bool ScheduleSkeletalAnimator::playState( const hashed_string& npc, const hashed_string& stateName )
    {
        GameObject*                pObject   = _pManager != nullptr ? _pManager->findGameObjectByName( npc ) : nullptr;
        SkeletalAnimatorComponent* pAnimator = pObject != nullptr ? pObject->getComponent<SkeletalAnimatorComponent>() : nullptr;
        if ( pAnimator == nullptr || pAnimator->play( stateName, true, _blendSeconds ) == false )
        {
            ++_failedPlayCount;
            return false;
        }
        ++_playCount;
        return true;
    }

    void ScheduleSkeletalAnimator::onActivityStarted( const ScheduleActivityCue& cue )
    {
        const hashed_string state = chooseState( cue );
        if ( state.empty() )
            return;                         // 이 활동에 정한 상태가 없다 — 애니메이터를 건드리지 않는다
        (void)playState( cue._npc, state ); // 못 재생하면 getFailedPlayCount 에 센다. 일정은 애니메이션 없이도 간다
    }

    void ScheduleSkeletalAnimator::onActivityEnded( const ScheduleActivityCue& cue )
    {
        if ( _idleState.empty() )
            return;
        (void)playState( cue._npc, _idleState ); // 쉬는 상태가 그래프에 없으면 센다
    }
} // namespace sw
