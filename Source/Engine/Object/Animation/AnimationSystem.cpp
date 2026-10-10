#include "pch.h"

#include "Engine/Object/Animation/AnimationSystem.h"

#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/AnimPlayer.h"
#include "Engine/Common/EngineParallel.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationSystem" );

    /**
     * @brief `-gv_animationLod=0` — 애니메이션 LOD(가시성 · 갱신 주기 · 본 LOD · 예산)를 끕니다. 비교 측정 · 진단용입니다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_animationLod, 1, "Animation LOD: frustum visibility, update rate, bone LOD and budget (0 = every unit every frame)" );

    /**
     * @brief `-gv_animationForceVertexAnimation=1` — 군중 공유를 켠 모든 유닛을 거리와 상관없이 VAT 로 그립니다(VAT 경로 검증 · 측정용).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_animationForceVertexAnimation, 0, "Draw every crowd-shared unit with vertex animation regardless of distance (verification)" );

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
                    {
                        _ppUnit[index]->runAnimationPhase( _phase );
                    }
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
        , _listLODClient{}
        , _listLODView{}
        , _listScratchLODState{}
        , _listScratchBudgetItem{}
        , _lodSettings{}
        , _crowd{}
#if SW_ANIMATION_REWIND_ENABLED
        , _rewind{}
        , _rewindScratchPose{}
        , _bRewindApplied{ SW_FALSE }
#endif
        , _pManager{ nullptr }
        , _lodViewPosition{}
        , _frameIndex{ 0 }
        , _deltaSeconds{ 0.0f }
        , _averageEvaluationMicroseconds{ 0.0f }
        , _expectedEvaluationMicroseconds{ 0.0f }
        , _activeUnitCount{ 0 }
        , _poseEvaluatedUnitCount{ 0 }
        , _bOrderDirty{ SW_FALSE }
        , _bCycle{ SW_FALSE }
        , _bLODViewsSet{ SW_FALSE }
        , _bLODSettingsReady{ SW_FALSE }
        , _bLODApplied{ SW_FALSE }
        , _bCrowdSettingsReady{ SW_FALSE }
        , _bHasLODViewPosition{ SW_FALSE }
    {
    }

    void AnimationSystem::setLODViewPosition( const float3& position )
    {
        _lodViewPosition     = position;
        _bHasLODViewPosition = SW_TRUE;
    }

    bool AnimationSystem::findLODViewPosition( float3& outPosition ) const
    {
        if ( _bHasLODViewPosition == SW_FALSE )
            return false;
        outPosition = _lodViewPosition;
        return true;
    }

    void AnimationSystem::setCrowdSettings( const AnimationCrowdSettings& settings )
    {
        _crowd.setSettings( settings );
        _bCrowdSettingsReady = SW_TRUE;
    }

    void AnimationSystem::updateCrowd()
    {
        bool bAnyShared = false;
        for ( const SkeletalMeshComponent* pUnit : _listActive )
        {
            bAnyShared = bAnyShared || pUnit->isShareCrowdPose() || pUnit->getCrowdMode() != AnimationCrowdMode::Own;
        }
        if ( bAnyShared == false && _crowd.getBuckets().empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Animation.crowd" );
        if ( _bCrowdSettingsReady == SW_FALSE )
        {
            _bCrowdSettingsReady = SW_TRUE;
            AnimationCrowdSettings settings;
            if ( ResourceUtil::hasResource( AnimationCrowdSettings::kResourcePath ) && settings.loadFromResource( AnimationCrowdSettings::kResourcePath ) )
                _crowd.setSettings( settings );
        }
        // 묶음 · 사본 · VAT 를 정한다(게임 스레드 — 메시를 갈아 끼운다). 나눈 유닛은 이번 프레임 포즈 단계를 돌지 않는다.
        uint32 arrModeCount[4] = { 0, 0, 0, 0 };
        for ( SkeletalMeshComponent* pUnit : _listActive )
        {
            if ( pUnit->isShareCrowdPose() || pUnit->getCrowdMode() != AnimationCrowdMode::Own )
                pUnit->updateCrowdMembership( _crowd );
            ++arrModeCount[static_cast<uint32>( pUnit->getCrowdMode() )];
        }
        _crowd.evaluateBuckets();
        SW_PROFILE_COUNT( "GT.Animation.crowdSharedUnits", arrModeCount[static_cast<uint32>( AnimationCrowdMode::Shared )] );
        SW_PROFILE_COUNT( "GT.Animation.crowdSoloUnits", arrModeCount[static_cast<uint32>( AnimationCrowdMode::Solo )] );
        SW_PROFILE_COUNT( "GT.Animation.crowdVertexAnimationUnits", arrModeCount[static_cast<uint32>( AnimationCrowdMode::VertexAnimation )] );
        SW_PROFILE_COUNT( "GT.Animation.crowdBucketCount", static_cast<uint32>( _crowd.getBuckets().size() ) );
        SW_PROFILE_COUNT( "GT.Animation.crowdEvaluatedBuckets", _crowd.getEvaluatedBucketCount() );
    }

    void AnimationSystem::registerLODClient( IAnimationLODClient* pClient )
    {
        if ( pClient == nullptr || std::find( _listLODClient.begin(), _listLODClient.end(), pClient ) != _listLODClient.end() )
            return;
        _listLODClient.push_back( pClient );
    }

    void AnimationSystem::unregisterLODClient( IAnimationLODClient* pClient )
    {
        const auto it = std::find( _listLODClient.begin(), _listLODClient.end(), pClient );
        if ( it != _listLODClient.end() )
            _listLODClient.erase( it );
    }

    void AnimationSystem::setLODViews( const vector<AnimationLODView>& listView )
    {
        _listLODView  = listView;
        _bLODViewsSet = SW_TRUE;
        // 거리 LOD(스프링 본 `lod_distance`)의 기준점도 주 시점이다 — 뷰를 넣는 쪽이 따로 넣지 않아도 같은 카메라를 본다.
        if ( listView.empty() == false )
            setLODViewPosition( listView.front()._position );
    }

    void AnimationSystem::clearLODViews()
    {
        _listLODView.clear();
        _bLODViewsSet = SW_FALSE;
    }

    void AnimationSystem::setLODSettings( const AnimationLODSettings& settings )
    {
        _lodSettings       = settings;
        _bLODSettingsReady = SW_TRUE;
    }

    void AnimationSystem::updateLOD()
    {
        if ( _listLODClient.empty() )
            return;
        SW_PROFILE_SCOPE( "GT.Animation.lod" );
        const size_t clientCount = _listLODClient.size();
        // 뷰가 없거나(시험 · 서버) 꺼져 있으면 판정하지 않는다 — 켜져 있다가 꺼진 첫 프레임에만 지난 판정을 되돌린다(가시성 훅을 직접 부르는 쪽을 덮지 않게).
        if ( gv_animationLod == 0 || _bLODViewsSet == SW_FALSE )
        {
            if ( _bLODApplied == SW_TRUE )
            {
                const AnimationLODState fullState{};
                for ( IAnimationLODClient* pClient : _listLODClient )
                {
                    pClient->applyAnimationLOD( fullState );
                }
            }
            _bLODApplied                    = SW_FALSE;
            _expectedEvaluationMicroseconds = 0.0f;
            return;
        }
        _bLODApplied = SW_TRUE;
        if ( _bLODSettingsReady == SW_FALSE )
        {
            // 표가 없으면 단계 없는 기본(가시성만)이다. 있는데 틀리면 오류를 남기고 기본으로 간다.
            _bLODSettingsReady = SW_TRUE;
            if ( ResourceUtil::hasResource( AnimationLODSettings::kResourcePath ) )
                (void)_lodSettings.loadFromResource( AnimationLODSettings::kResourcePath ); // 틀리면 loadFromResource 가 오류를 남기고 기본으로 돌린다
        }

        _listScratchLODState.resize( clientCount );
        _listScratchBudgetItem.clear();
        for ( size_t clientIndex = 0; clientIndex < clientCount; ++clientIndex )
        {
            const IAnimationLODClient* pClient = _listLODClient[clientIndex];
            float3                     center{};
            float32                    radius     = 0.0f;
            float32                    screenSize = 1.0f;
            bool                       bVisible   = true;
            if ( pClient->findAnimationLODBounds( center, radius ) )
            {
                screenSize = 0.0f;
                for ( const AnimationLODView& view : _listLODView )
                {
                    screenSize = MathUtil::max( screenSize, AnimationLODUtil::computeScreenSize( view, center, radius ) );
                }
                bVisible = screenSize > 0.0f;
            }
            AnimationLODState& state = _listScratchLODState[clientIndex];
            state                    = AnimationLODUtil::makeState( _lodSettings, screenSize, bVisible, pClient->findBoneLOD() );
            // 예산은 포즈를 만드는 것만 센다 — 화면 밖에서 포즈를 건너뛰는 것은 이미 0 이다.
            if ( state._bVisible == SW_TRUE || _lodSettings._offscreenUpdateRateDivisor > 0 )
                _listScratchBudgetItem.push_back( AnimationBudgetItem{ state._significance, state._updateRateDivisor } );
        }

        _expectedEvaluationMicroseconds = AnimationLODUtil::allocateBudget( _listScratchBudgetItem.data(), static_cast<uint32>( _listScratchBudgetItem.size() ),
                                                                            _averageEvaluationMicroseconds, _lodSettings._budgetMilliseconds * 1000.0f,
                                                                            _lodSettings._maxUpdateRateDivisor );
        size_t budgetIndex              = 0;
        for ( size_t clientIndex = 0; clientIndex < clientCount; ++clientIndex )
        {
            AnimationLODState& state = _listScratchLODState[clientIndex];
            if ( gv_animationForceVertexAnimation != 0 )
                state._bVertexAnimation = SW_TRUE;
            if ( state._bVisible == SW_TRUE || _lodSettings._offscreenUpdateRateDivisor > 0 )
            {
                const uint32 budgeted = _listScratchBudgetItem[budgetIndex++]._updateRateDivisor;
                // 예산이 늘린 주기는 보간을 켠다 — 덜 중요한 유닛이 뚝뚝 끊기지 않게(언리얼 예산 배분기의 보간과 같은 자리).
                if ( budgeted > state._updateRateDivisor )
                    state._bInterpolate = SW_TRUE;
                state._updateRateDivisor = budgeted;
            }
            _listLODClient[clientIndex]->applyAnimationLOD( state );
        }
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
        {
            mapIndex.emplace( _listUnit[unitIndex], static_cast<uint32>( unitIndex ) );
        }

        vector<vector<uint32>> listDependent( unitCount );
        vector<uint32>         listPendingCount( unitCount, 0 );
        for ( SkeletalMeshComponent* pUnit : _listUnit )
        {
            pUnit->setHasAnimationDependents( false );
        }
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
                // 다른 유닛이 이 유닛의 포즈를 읽는다(리더 · 부착) — VAT 로 넘기면 CPU 포즈가 멈춰 따르는 쪽이 굳는다.
                _listUnit[it->second]->setHasAnimationDependents( true );
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
#if SW_ANIMATION_REWIND_ENABLED
        // 되감기: 요청(콘솔 · 패널 · -gv_animationRewind)을 따르고, 되감는 동안에는 평가 대신 기록된 포즈를 건다.
        _rewind.syncWithRequest();
        if ( _rewind.isScrubbing() )
        {
            applyRewindScrub();
            return;
        }
        if ( _bRewindApplied == SW_TRUE )
        {
            // 되감기가 끝났다 — 걸어 둔 포즈를 지금 상태로 다시 만든다(쉬던 유닛도).
            _bRewindApplied = SW_FALSE;
            for ( SkeletalMeshComponent* pUnit : _listUnit )
            {
                pUnit->markPoseDirty();
            }
        }
#endif
        updateLOD();
        if ( _bOrderDirty == SW_TRUE || ( _listLevel.empty() && _listUnit.empty() == false ) )
            rebuildLevels();
        ++_frameIndex;
        _deltaSeconds = deltaSeconds;
        _crowd.beginFrame( deltaSeconds );

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
        _activeUnitCount        = static_cast<uint32>( _listActive.size() );
        _poseEvaluatedUnitCount = 0;
        if ( _listActive.empty() )
        {
            _crowd.endFrame();
#if SW_ANIMATION_REWIND_ENABLED
            recordRewindFrame();
            _rewind.endFrame( deltaSeconds );
#endif
            return;
        }

        runPhase( AnimationPhase::Time );
        synchronizeGroups();
        updateCrowd();

        // 포즈 단계의 벽시계 시간을 잰다 — 예산 배분이 "유닛 하나에 얼마" 를 이 평균으로 본다(언리얼 예산 배분기도 전체 시간을 재서 나눈다).
        uint32 poseUnitCount = 0;
        for ( const SkeletalMeshComponent* pUnit : _listActive )
        {
            poseUnitCount += pUnit->isPoseNeededThisFrame() ? 1u : 0u;
        }
        const int64 poseStart = MonotonicClock::nowNanoseconds();
        runPhase( AnimationPhase::BasePose );
        runPhase( AnimationPhase::Attachment );
        runPhase( AnimationPhase::PostProcess );
        runPhase( AnimationPhase::SkinPalette );
        _poseEvaluatedUnitCount = poseUnitCount;
        SW_PROFILE_COUNT( "GT.Animation.activeUnits", _activeUnitCount );
        SW_PROFILE_COUNT( "GT.Animation.poseUnits", poseUnitCount );
        if ( poseUnitCount > 0 )
        {
            const float32 sample           = static_cast<float32>( MonotonicClock::nowNanoseconds() - poseStart ) * 0.001f / static_cast<float32>( poseUnitCount );
            _averageEvaluationMicroseconds = ( _averageEvaluationMicroseconds <= 0.0f ) ? sample : ( _averageEvaluationMicroseconds * 0.9f + sample * 0.1f );
        }

        for ( SkeletalMeshComponent* pUnit : _listActive )
        {
            pUnit->finishAnimationFrame();
        }
        _crowd.endFrame();
#if SW_ANIMATION_REWIND_ENABLED
        recordRewindFrame();
        _rewind.endFrame( deltaSeconds );
#endif
    }

#if SW_ANIMATION_REWIND_ENABLED
    void AnimationSystem::recordRewindFrame()
    {
        if ( _rewind.isEnabled() == false )
            return;
        SW_PROFILE_SCOPE( "GT.Animation.rewindRecord" );
        for ( const SkeletalMeshComponent* pUnit : _listActive )
        {
            _rewind.recordUnit( *pUnit, _frameIndex );
        }
        AnimationDebugState state;
        for ( const IAnimationLODClient* pClient : _listLODClient )
        {
            const Component* pTarget = pClient->findRewindTarget();
            if ( pTarget == nullptr )
                continue;
            state.reset();
            pClient->collectDebugState( state );
            _rewind.recordState( *pTarget, AnimationRewindKind::Sprite, state, _frameIndex );
        }
    }

    void AnimationSystem::applyRewindScrub()
    {
        const float64 scrubTime = _rewind.getScrubTime();
        for ( SkeletalMeshComponent* pUnit : _listUnit )
        {
            const AnimationRewindTrack* pTrack = _rewind.findTrack( pUnit->getHandle() );
            const AnimationRewindFrame* pFrame = ( pTrack != nullptr ) ? pTrack->findFrame( scrubTime ) : nullptr;
            if ( pFrame == nullptr || pFrame->_boneCount == 0 )
                continue;
            AnimationRewindRecorder::decodePose( *pFrame, _rewindScratchPose );
            if ( pUnit->applyRewindPose( _rewindScratchPose ) )
                _bRewindApplied = SW_TRUE;
        }
        for ( IAnimationLODClient* pClient : _listLODClient )
        {
            const Component*            pTarget = pClient->findRewindTarget();
            const AnimationRewindTrack* pTrack  = ( pTarget != nullptr ) ? _rewind.findTrack( pTarget->getHandle() ) : nullptr;
            const AnimationRewindFrame* pFrame  = ( pTrack != nullptr ) ? pTrack->findFrame( scrubTime ) : nullptr;
            if ( pFrame != nullptr )
                pClient->applyRewindState( pFrame->_state );
        }
    }
#endif

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
        {
            pUnit->collectSyncPlayers( listPlayer, listWeight, listGroup );
        }
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
