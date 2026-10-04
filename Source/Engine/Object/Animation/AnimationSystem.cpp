#include "pch.h"

#include "Engine/Object/Animation/AnimationSystem.h"

#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Log/Logger.h"

#include "Engine/Animation/AnimPlayer.h"
#include "Engine/Common/EngineParallel.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationSystem" );

    namespace
    {
        struct AnimationSystemInternal
        {
            /** @brief 한 레벨의 유닛 구간에 단계 하나를 돌리는 잡입니다. 워커는 포인터만 받습니다. */
            struct PhaseJob
            {
                SkeletalMeshComponent* const* _ppUnit{ nullptr };
                AnimationPhase                _phase{ AnimationPhase::Time };

                void runRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                        _ppUnit[index]->runAnimationPhase( _phase );
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    AnimationSystem::AnimationSystem()
        : _listUnit{}
        , _listLevel{}
        , _listActive{}
        , _listLevelStart{}
        , _pManager{ nullptr }
        , _lodViewPosition{}
        , _frameIndex{ 0 }
        , _deltaSeconds{ 0.0f }
        , _activeUnitCount{ 0 }
        , _bOrderDirty{ SW_FALSE }
        , _bCycle{ SW_FALSE }
        , _bHasLodViewPosition{ SW_FALSE }
    {
    }

    void AnimationSystem::setLodViewPosition( const float3& position )
    {
        _lodViewPosition     = position;
        _bHasLodViewPosition = SW_TRUE;
    }

    bool AnimationSystem::findLodViewPosition( float3& outPosition ) const
    {
        if ( _bHasLodViewPosition == SW_FALSE )
            return false;
        outPosition = _lodViewPosition;
        return true;
    }

    void AnimationSystem::registerUnit( SkeletalMeshComponent* pUnit )
    {
        if ( pUnit == nullptr || std::find( _listUnit.begin(), _listUnit.end(), pUnit ) != _listUnit.end() )
            return;
        _listUnit.push_back( pUnit );
        _bOrderDirty = SW_TRUE;
    }

    void AnimationSystem::unregisterUnit( SkeletalMeshComponent* pUnit )
    {
        const auto it = std::find( _listUnit.begin(), _listUnit.end(), pUnit );
        if ( it == _listUnit.end() )
            return;
        _listUnit.erase( it );
        _bOrderDirty = SW_TRUE;
        // 지은 레벨에 남은 죽은 포인터를 다음 평가 전에 쓰지 않게 바로 비운다.
        _listLevel.clear();
    }

    void AnimationSystem::rebuildLevels()
    {
        _bOrderDirty = SW_FALSE;
        _bCycle      = SW_FALSE;
        _listLevel.clear();
        const size_t unitCount = _listUnit.size();
        if ( unitCount == 0 )
            return;

        // Kahn 위상 정렬을 레벨 단위로 한다: 레벨 n 의 유닛은 의존이 모두 레벨 n 보다 앞이다.
        unordered_map<const SkeletalMeshComponent*, uint32> mapIndex;
        for ( size_t unitIndex = 0; unitIndex < unitCount; ++unitIndex )
            mapIndex.emplace( _listUnit[unitIndex], static_cast<uint32>( unitIndex ) );

        vector<vector<uint32>> listDependent( unitCount );
        vector<uint32>         listPendingCount( unitCount, 0 );
        for ( size_t unitIndex = 0; unitIndex < unitCount; ++unitIndex )
        {
            for ( const ComponentHandle handle : _listUnit[unitIndex]->getAnimationDependencies() )
            {
                Component* pComponent = ( _pManager != nullptr ) ? _pManager->resolveComponent( handle ) : nullptr;
                const auto it         = mapIndex.find( castTo<SkeletalMeshComponent>( pComponent ) );
                if ( it == mapIndex.end() )
                    continue; // 사라졌거나 다른 씬의 유닛 — 순서를 강제하지 않는다
                listDependent[it->second].push_back( static_cast<uint32>( unitIndex ) );
                ++listPendingCount[unitIndex];
            }
        }

        vector<uint32> listReady;
        for ( uint32 unitIndex = 0; unitIndex < static_cast<uint32>( unitCount ); ++unitIndex )
        {
            if ( listPendingCount[unitIndex] == 0 )
                listReady.push_back( unitIndex );
        }
        size_t placedCount = 0;
        while ( listReady.empty() == false )
        {
            vector<SkeletalMeshComponent*> listLevelUnit;
            vector<uint32>                 listNext;
            for ( const uint32 unitIndex : listReady )
            {
                listLevelUnit.push_back( _listUnit[unitIndex] );
                for ( const uint32 dependentIndex : listDependent[unitIndex] )
                {
                    if ( --listPendingCount[dependentIndex] == 0 )
                        listNext.push_back( dependentIndex );
                }
            }
            placedCount += listLevelUnit.size();
            _listLevel.push_back( std::move( listLevelUnit ) );
            listReady.swap( listNext );
        }

        if ( placedCount == unitCount )
            return;
        // 고리 — 데이터 오류다. 알리고, 남은 유닛은 순서 없이 마지막 레벨에서 돈다(서로의 지난 프레임 포즈를 읽는다).
        _bCycle = SW_TRUE;
        vector<SkeletalMeshComponent*> listRemaining;
        for ( size_t unitIndex = 0; unitIndex < unitCount; ++unitIndex )
        {
            if ( listPendingCount[unitIndex] > 0 )
            {
                listRemaining.push_back( _listUnit[unitIndex] );
                const GameObject* pOwner = _listUnit[unitIndex]->getOwner();
                SW_LOG_ERROR( "Animation dependency cycle includes unit on '%#'", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
            }
        }
        _listLevel.push_back( std::move( listRemaining ) );
    }

    void AnimationSystem::evaluate( float32 deltaSeconds )
    {
        SW_PROFILE_SCOPE( "GT.Animation.evaluate" );
        if ( _bOrderDirty == SW_TRUE || ( _listLevel.empty() && _listUnit.empty() == false ) )
            rebuildLevels();
        ++_frameIndex;
        _deltaSeconds = deltaSeconds;

        // 이번 프레임에 일하는 유닛만 레벨 순서로 모은다 — 쉬는 유닛은 여기서 빠져 단계를 돌지 않는다.
        _listActive.clear();
        _listLevelStart.clear();
        for ( const vector<SkeletalMeshComponent*>& level : _listLevel )
        {
            _listLevelStart.push_back( static_cast<uint32>( _listActive.size() ) );
            for ( SkeletalMeshComponent* pUnit : level )
            {
                if ( pUnit->beginAnimationFrame( deltaSeconds, _frameIndex ) )
                    _listActive.push_back( pUnit );
            }
        }
        _listLevelStart.push_back( static_cast<uint32>( _listActive.size() ) );
        _activeUnitCount = static_cast<uint32>( _listActive.size() );
        if ( _listActive.empty() )
            return;

        runPhase( AnimationPhase::Time );
        synchronizeGroups();
        runPhase( AnimationPhase::BasePose );
        runPhase( AnimationPhase::Attachment );
        runPhase( AnimationPhase::PostProcess );
        runPhase( AnimationPhase::SkinPalette );

        for ( SkeletalMeshComponent* pUnit : _listActive )
            pUnit->finishAnimationFrame();
    }

    void AnimationSystem::runPhase( AnimationPhase phase )
    {
        for ( size_t levelIndex = 0; levelIndex + 1 < _listLevelStart.size(); ++levelIndex )
        {
            const uint32 start = _listLevelStart[levelIndex];
            const uint32 end   = _listLevelStart[levelIndex + 1];
            if ( start == end )
                continue;
            AnimationSystemInternal::PhaseJob job{};
            job._ppUnit = _listActive.data() + start;
            job._phase  = phase;
            engine::runParallel( end - start, kParallelUnitCount,
                                 SW_DELEGATE_METHOD( ParallelBlockDelegate, &AnimationSystemInternal::PhaseJob::runRange, &job ) );
        }
    }

    void AnimationSystem::synchronizeGroups()
    {
        vector<AnimPlayer*>   listPlayer;
        vector<float32>       listWeight;
        vector<hashed_string> listGroup;
        for ( SkeletalMeshComponent* pUnit : _listActive )
            pUnit->collectSyncPlayers( listPlayer, listWeight, listGroup );
        if ( listPlayer.size() < 2 )
            return;

        vector<AnimPlayer*> listMemberPlayer;
        vector<float32>     listMemberWeight;
        vector<uint8>       listDone( listPlayer.size(), SW_FALSE );
        for ( size_t first = 0; first < listPlayer.size(); ++first )
        {
            if ( listDone[first] == SW_TRUE )
                continue;
            listMemberPlayer.clear();
            listMemberWeight.clear();
            for ( size_t other = first; other < listPlayer.size(); ++other )
            {
                if ( listDone[other] == SW_TRUE || listGroup[other] != listGroup[first] )
                    continue;
                listDone[other] = SW_TRUE;
                listMemberPlayer.push_back( listPlayer[other] );
                listMemberWeight.push_back( listWeight[other] );
            }
            if ( listMemberPlayer.size() > 1 )
                (void)AnimSyncGroup::synchronize( listMemberPlayer.data(), listMemberWeight.data(), static_cast<uint32>( listMemberPlayer.size() ) );
        }
    }
} // namespace sw
