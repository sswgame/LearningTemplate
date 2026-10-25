#include "pch.h"

#include "Engine/Object/SceneAnimation/AnimationSystem.h"

#include "Core/Delegate/Delegate.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Common/EngineParallel.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"

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

            /** @brief union-find 에서 @p unitIndex 의 대표 유닛 번호를 찾습니다(경로를 반으로 줄이며 오른다). */
            static uint32 findComponentRoot( vector<uint32>& inoutListRoot, uint32 unitIndex )
            {
                while ( inoutListRoot[unitIndex] != unitIndex )
                {
                    inoutListRoot[unitIndex] = inoutListRoot[inoutListRoot[unitIndex]];
                    unitIndex                = inoutListRoot[unitIndex];
                }
                return unitIndex;
            }

            /** @brief 두 유닛의 성분을 합칩니다. 대표는 더 작은 번호이므로 성분의 대표는 늘 그 성분에서 등록 순서가 가장 앞선 유닛입니다. */
            static void uniteComponents( vector<uint32>& inoutListRoot, uint32 leftIndex, uint32 rightIndex )
            {
                const uint32 leftRoot  = findComponentRoot( inoutListRoot, leftIndex );
                const uint32 rightRoot = findComponentRoot( inoutListRoot, rightIndex );
                if ( leftRoot < rightRoot )
                    inoutListRoot[rightRoot] = leftRoot;
                else if ( rightRoot < leftRoot )
                    inoutListRoot[leftRoot] = rightRoot;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_SCENE_SERVICE( AnimationSystem, Animation )

    AnimationSystem::AnimationSystem( GameObjectManager& manager )
        : _unitList{}
        , _uniqueLevelRemoval{}
        , _listLevel{}
        , _componentAll{}
        , _componentActive{}
        , _listLevelSlot{}
        , _listActive{}
        , _listLevelStart{}
        , _lodClientList{}
        , _listLODView{}
        , _pendingLODView{}
        , _listScratchLODState{}
        , _listScratchBudgetItem{}
        , _listScratchEdge{}
        , _listScratchPendingCount{}
        , _listScratchDependentStart{}
        , _listScratchDependentCursor{}
        , _listScratchDependent{}
        , _listScratchReady{}
        , _listScratchNext{}
        , _listScratchOrder{}
        , _listScratchComponentRoot{}
        , _listScratchComponentOf{}
        , _listScratchComponentCursor{}
        , _listScratchSlotOfUnit{}
        , _listScratchSlotRemap{}
        , _listScratchActiveFlag{}
        , _lodSettings{}
        , _crowd{}
#if SW_ANIMATION_REWIND_ENABLED
        , _rewind{}
        , _rewindScratchPose{}
        , _bRewindApplied{ SW_FALSE }
#endif
        , _pManager{ &manager }
        , _lodViewPosition{}
        , _frameIndex{ 0 }
        , _deltaSeconds{ 0.0f }
        , _averageEvaluationMicroseconds{ 0.0f }
        , _expectedEvaluationMicroseconds{ 0.0f }
        , _activeUnitCount{ 0 }
        , _poseEvaluatedUnitCount{ 0 }
        , _arrCrowdModeCount{}
        , _bOrderDirty{ SW_FALSE }
        , _bCycle{ SW_FALSE }
        , _bLODViewsSet{ SW_FALSE }
        , _bLODSettingsReady{ SW_FALSE }
        , _bLODSettingsFailed{ SW_FALSE }
        , _bLODApplied{ SW_FALSE }
        , _bCrowdSettingsReady{ SW_FALSE }
        , _bCrowdSettingsFailed{ SW_FALSE }
        , _bHasLODViewPosition{ SW_FALSE }
        , _bHasPendingLODView{ SW_FALSE }
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
        for ( uint32& count : _arrCrowdModeCount )
        {
            count = 0;
        }
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
            // 군중 표는 엔진 필수 데이터다. 못 읽으면 단언하고 이 시스템은 군중을 묶지 않는다(코드 기본 표로 묶지 않는다).
            _bCrowdSettingsReady = SW_TRUE;
            AnimationCrowdSettings settings;
            _bCrowdSettingsFailed = settings.loadFromResource( AnimationCrowdSettings::kResourcePath ) ? SW_FALSE : SW_TRUE;
            SW_LOG_ASSERT( _bCrowdSettingsFailed == SW_FALSE, "Animation crowd settings '%#' could not be loaded", AnimationCrowdSettings::kResourcePath );
            if ( _bCrowdSettingsFailed == SW_FALSE )
                _crowd.setSettings( settings );
        }
        if ( _bCrowdSettingsFailed == SW_TRUE )
            return;
        // 묶음 · 사본 · VAT 를 정한다(게임 스레드 — 메시를 갈아 끼운다). 나눈 유닛은 이번 프레임 포즈 단계를 돌지 않는다.
        for ( SkeletalMeshComponent* pUnit : _listActive )
        {
            if ( pUnit->isShareCrowdPose() || pUnit->getCrowdMode() != AnimationCrowdMode::Own )
                pUnit->updateCrowdMembership( _crowd );
            ++_arrCrowdModeCount[static_cast<uint32>( pUnit->getCrowdMode() )];
        }
        _crowd.evaluateBuckets();
        SW_PROFILE_COUNT( "GT.Animation.crowdSharedUnits", getCrowdModeCount( AnimationCrowdMode::Shared ) );
        SW_PROFILE_COUNT( "GT.Animation.crowdSoloUnits", getCrowdModeCount( AnimationCrowdMode::Solo ) );
        SW_PROFILE_COUNT( "GT.Animation.crowdVertexAnimationUnits", getCrowdModeCount( AnimationCrowdMode::VertexAnimation ) );
        SW_PROFILE_COUNT( "GT.Animation.crowdBucketCount", static_cast<uint32>( _crowd.getBuckets().size() ) );
        SW_PROFILE_COUNT( "GT.Animation.crowdEvaluatedBuckets", _crowd.getEvaluatedBucketCount() );
    }

    void AnimationSystem::registerLODClient( IAnimationLODClient* pClient )
    {
        if ( pClient == nullptr )
            return;
        (void)_lodClientList.add( pClient ); // 이미 있으면 아무것도 하지 않는다
    }

    void AnimationSystem::unregisterLODClient( IAnimationLODClient* pClient )
    {
        (void)_lodClientList.remove( pClient ); // 없으면 아무것도 하지 않는다
    }

    void AnimationSystem::setLODViews( const vector<AnimationLODView>& listView )
    {
        _listLODView  = listView;
        _bLODViewsSet = SW_TRUE;
        // 주 시점 위치도 함께 기록한다. 뷰를 넣는 쪽이 따로 넣지 않아도 같은 카메라를 본다.
        if ( listView.empty() == false )
            setLODViewPosition( listView.front()._position );
    }

    void AnimationSystem::clearLODViews()
    {
        _listLODView.clear();
        _bLODViewsSet = SW_FALSE;
    }

    void AnimationSystem::setPendingLODView( const AnimationLODView& view )
    {
        _pendingLODView     = view;
        _bHasPendingLODView = SW_TRUE;
    }

    void AnimationSystem::clearPendingLODView()
    {
        _bHasPendingLODView = SW_FALSE;
    }

    void AnimationSystem::setLODSettings( const AnimationLODSettings& settings )
    {
        _lodSettings       = settings;
        _bLODSettingsReady = SW_TRUE;
    }

    void AnimationSystem::updateLOD()
    {
        if ( _lodClientList.getCount() == 0 )
            return;
        SW_PROFILE_SCOPE( "GT.Animation.lod" );
        // 판정 단계는 훅 없이 읽기만 하므로 접힌 목록을 한 번 잡아 쓴다. 적용 훅(등록 · 해제를 부를 수 있음)은 아래에서 `forEach` 로 부른다.
        const vector<IAnimationLODClient*>& listLODClient = _lodClientList.getItems();
        const size_t                        clientCount   = listLODClient.size();
        // 뷰가 없거나(테스트 · 서버) 꺼져 있으면 판정하지 않는다 — 켜져 있다가 꺼진 첫 프레임에만 지난 판정을 되돌린다(가시성 훅을 직접 부르는 쪽을 덮지 않게).
        if ( gv_animationLod == 0 || _bLODViewsSet == SW_FALSE )
        {
            if ( _bLODApplied == SW_TRUE )
            {
                const AnimationLODState fullState{};
                // 적용 훅이 등록 · 해제를 불러도 안전하도록 `forEach` 로 돈다(해제된 클라이언트는 건너뛴다).
                _lodClientList.forEach( [&fullState]( uint32, IAnimationLODClient* pClient )
                { pClient->applyAnimationLOD( fullState ); } );
            }
            _bLODApplied                    = SW_FALSE;
            _expectedEvaluationMicroseconds = 0.0f;
            return;
        }
        if ( _bLODSettingsReady == SW_FALSE )
        {
            // LOD 표는 엔진 필수 데이터다. 못 읽으면 단언하고 이 시스템은 LOD 를 판정하지 않는다(모든 유닛이 매 프레임 — 코드 기본 표로 판정하지 않는다).
            _bLODSettingsReady  = SW_TRUE;
            _bLODSettingsFailed = _lodSettings.loadFromResource( AnimationLODSettings::kResourcePath ) ? SW_FALSE : SW_TRUE;
            SW_LOG_ASSERT( _bLODSettingsFailed == SW_FALSE, "Animation LOD settings '%#' could not be loaded", AnimationLODSettings::kResourcePath );
        }
        if ( _bLODSettingsFailed == SW_TRUE )
            return;
        _bLODApplied = SW_TRUE;

        _listScratchLODState.resize( clientCount );
        _listScratchBudgetItem.clear();
        for ( size_t clientIndex = 0; clientIndex < clientCount; ++clientIndex )
        {
            const IAnimationLODClient* pClient = listLODClient[clientIndex];
            float3                     center{};
            float32                    radius     = 0.0f;
            float32                    screenSize = 1.0f;
            bool                       bVisible   = true;
            // 숨긴 유닛(1인칭에서 숨긴 몸)은 뷰에 들어도 화면 밖과 같다 — 그려지지 않는 포즈를 만들지 않는다.
            if ( pClient->isRendered() == false )
            {
                screenSize = 0.0f;
                bVisible   = false;
            }
            else if ( pClient->findAnimationLODBounds( center, radius ) )
            {
                screenSize = 0.0f;
                for ( const AnimationLODView& view : _listLODView )
                {
                    screenSize = MathUtil::max( screenSize, AnimationLODUtil::computeScreenSize( view, center, radius ) );
                }
                // 대기 뷰(곧 넘어갈 컷)에서 보일 유닛도 지금부터 그 크기로 판정한다 — 컷 첫 프레임에 낮은 LOD · 지난 포즈로 보이지 않게.
                if ( _bHasPendingLODView == SW_TRUE )
                    screenSize = MathUtil::max( screenSize, AnimationLODUtil::computeScreenSize( _pendingLODView, center, radius ) );
                bVisible = screenSize > 0.0f;
            }
            AnimationLODState&     state    = _listScratchLODState[clientIndex];
            const SkeletonBoneLOD* pBoneLOD = pClient->findBoneLOD();
            state                           = AnimationLODUtil::makeState( _lodSettings, screenSize, bVisible, pBoneLOD );
            // 메시가 LOD 를 가지면 본 LOD 는 그 메시 단계를 따른다(화면 크기 임계값의 정본이 메시 단계 하나 — 본은 안 보이는 단계에서 먼저 빠지지 않는다).
            const int32 meshLODIndex   = pClient->findMeshLODIndex();
            const bool  bHasBoneLevels = pBoneLOD != nullptr && pBoneLOD->getLevelCount() > 0;
            if ( bHasBoneLevels && meshLODIndex >= 0 )
                state._boneLODLevel = static_cast<uint8>( MathUtil::min( static_cast<uint32>( meshLODIndex ), pBoneLOD->getLevelCount() - 1 ) );
            // 예산은 포즈를 만드는 것만 센다 — 화면 밖에서 포즈를 건너뛰는 것은 이미 0 이다.
            if ( state._bVisible == SW_TRUE || _lodSettings._offscreenUpdateRateDivisor > 0 )
                _listScratchBudgetItem.push_back( AnimationBudgetItem{ state._significance, state._updateRateDivisor } );
        }

        _expectedEvaluationMicroseconds = AnimationLODUtil::allocateBudget( _listScratchBudgetItem.data(), static_cast<uint32>( _listScratchBudgetItem.size() ),
                                                                            _averageEvaluationMicroseconds, _lodSettings._budgetMilliseconds * constant::kMicrosecondsPerMillisecond,
                                                                            _lodSettings._maxUpdateRateDivisor );
        size_t budgetIndex              = 0;
        // 예산을 칸마다 풀어 두는 단계 — 훅이 없으니 목록이 그대로다. 예산 순번은 여기서만 센다(아래 순회가 해제된 칸을 건너뛰어도 어긋나지 않게).
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
        }
        // 적용 단계 — 훅이 등록 · 해제를 불러도 안전하도록 `forEach` 로 돈다. 위치는 위 판정과 같고(접지 않는다), 해제된 칸은 건너뛴다.
        // 위 `listLODClient` 참조는 여기서 쓰지 않는다 — 훅이 더하면 저장소가 다시 잡힌다.
        _lodClientList.forEach( [this]( uint32 clientIndex, IAnimationLODClient* pClient )
        { pClient->applyAnimationLOD( _listScratchLODState[clientIndex] ); } );
    }

    void AnimationSystem::registerUnit( SkeletalMeshComponent* pUnit )
    {
        if ( pUnit == nullptr || _unitList.add( pUnit ) == false )
            return;
        _bOrderDirty = SW_TRUE;
    }

    void AnimationSystem::unregisterUnit( SkeletalMeshComponent* pUnit )
    {
        if ( pUnit == nullptr || _unitList.remove( pUnit ) == false )
            return;
        // 레벨이 맞고(더럽지 않고 고리가 없음) 이 유닛이 의존을 가지지도 받지도 않으면 레벨 0 에만 있다 — 다른 유닛의 레벨은 그대로이므로 레벨 0 에서 빼는 것으로 끝난다
        // (전체 재구성과 같은 결과다: 레벨 0 은 의존 없는 유닛을 등록 순서로 모은 것이다). 평가가 시작될 때 모아서 한 번에 뺀다.
        const bool bIsolated = _bOrderDirty == SW_FALSE && _bCycle == SW_FALSE && pUnit->hasAnimationDependents() == false && pUnit->getAnimationDependencies().empty();
        if ( bIsolated )
        {
            _uniqueLevelRemoval.insert( pUnit );
            return;
        }
        _bOrderDirty = SW_TRUE;
        _uniqueLevelRemoval.clear();
        // 지은 레벨과 성분에 남은 죽은 포인터를 다음 평가 전에 쓰지 않게 바로 비운다.
        _listLevel.clear();
        _listLevelSlot.clear();
        _componentAll.clear();
    }

    void AnimationSystem::flushLevelRemoval()
    {
        if ( _uniqueLevelRemoval.empty() )
            return;
        // 레벨을 다시 지을 예정이면(더럽거나 아직 안 지음) 지을 때 해제된 유닛이 빠진다.
        if ( _bOrderDirty == SW_FALSE && _listLevel.empty() == false )
        {
            vector<SkeletalMeshComponent*>& levelZero     = _listLevel.front();
            const size_t                    levelZeroSize = levelZero.size();
            // 레벨 0 은 `_listLevelSlot` 의 앞 구간이다. 빼는 유닛은 외톨이라 유닛 하나짜리 성분이므로, 그 칸을 표시해 두었다가 성분 목록에서도 뺀다.
            _listScratchSlotRemap.assign( _componentAll.getUnitCount(), 0 );
            size_t keepCount = 0;
            for ( size_t position = 0; position < levelZeroSize; ++position )
            {
                SkeletalMeshComponent*       pUnit = levelZero[position];
                const SkeletalMeshComponent* pKey  = pUnit;
                if ( _uniqueLevelRemoval.contains( pKey ) )
                {
                    _listScratchSlotRemap[_listLevelSlot[position]] = invalid_index::kUint32;
                    continue;
                }
                levelZero[keepCount]      = pUnit;
                _listLevelSlot[keepCount] = _listLevelSlot[position];
                ++keepCount;
            }
            levelZero.resize( keepCount );
            // 유닛이 모두 빠졌거나 레벨 0 이 비었으면 레벨을 비운다 — 남은 유닛이 있으면 다음 평가가 다시 짓는다.
            if ( keepCount == 0 )
            {
                _listLevel.clear();
                _listLevelSlot.clear();
                _componentAll.clear();
            }
            else
            {
                _listLevelSlot.erase( _listLevelSlot.begin() + static_cast<ptrdiff_t>( keepCount ), _listLevelSlot.begin() + static_cast<ptrdiff_t>( levelZeroSize ) );
                _componentAll.removeMarkedSingletons( _listScratchSlotRemap );
                for ( uint32& slot : _listLevelSlot )
                {
                    slot = _listScratchSlotRemap[slot];
                }
            }
        }
        _uniqueLevelRemoval.clear();
    }

    void AnimationSystem::rebuildLevels()
    {
        _bOrderDirty = SW_FALSE;
        _bCycle      = SW_FALSE;
        _uniqueLevelRemoval.clear();
        const vector<SkeletalMeshComponent*>& listUnit  = _unitList.getItems();
        const size_t                          unitCount = listUnit.size();
        if ( unitCount == 0 )
        {
            _listLevel.clear();
            _listLevelSlot.clear();
            _componentAll.clear();
            return;
        }

        // Kahn 위상 정렬을 레벨 단위로 한다: 레벨 n 의 유닛은 의존이 모두 레벨 n 보다 앞이다.
        // 간선(위 유닛 → 아래 유닛)을 모아 위 유닛별 구간(CSR)으로 정렬한다 — 유닛마다 벡터를 할당하지 않고, 임시 버퍼는 멤버로 재사용한다.
        // 위 유닛의 번호는 `_unitList` 의 위치 색인으로 O(1) 에 찾는다.
        _listScratchEdge.clear();
        _listScratchOrder.clear();
        _listScratchPendingCount.assign( unitCount, 0 );
        _listScratchDependentStart.assign( unitCount + 1, 0 );
        for ( SkeletalMeshComponent* pUnit : listUnit )
        {
            pUnit->setHasAnimationDependents( false );
        }
        for ( size_t unitIndex = 0; unitIndex < unitCount; ++unitIndex )
        {
            for ( const TComponentHandle<SkeletalMeshComponent> handle : listUnit[unitIndex]->getAnimationDependencies() )
            {
                const SkeletalMeshComponent* pUpstream     = ( _pManager != nullptr ) ? _pManager->resolveComponent( handle ) : nullptr;
                const uint32                 upstreamIndex = _unitList.findIndex( pUpstream );
                if ( upstreamIndex == invalid_index::kUint32 )
                    continue; // 사라졌거나 다른 씬의 유닛 — 순서를 강제하지 않는다
                _listScratchEdge.push_back( AnimationDependencyEdge{ upstreamIndex, static_cast<uint32>( unitIndex ) } );
                ++_listScratchPendingCount[unitIndex];
                ++_listScratchDependentStart[upstreamIndex + 1];
                // 다른 유닛이 이 유닛의 포즈를 읽는다(리더 · 부착) — VAT 로 넘기면 CPU 포즈가 멈춰 따르는 쪽이 굳는다.
                listUnit[upstreamIndex]->setHasAnimationDependents( true );
            }
        }
        for ( size_t unitIndex = 0; unitIndex < unitCount; ++unitIndex )
        {
            _listScratchDependentStart[unitIndex + 1] += _listScratchDependentStart[unitIndex];
        }
        // 위 유닛별 구간 안의 순서는 간선을 모은 순서(아래 유닛 번호 순, 같은 유닛 안은 의존 등록 순)이다 — 옛 벡터 방식과 같다.
        _listScratchDependent.resize( _listScratchEdge.size() );
        _listScratchDependentCursor.assign( _listScratchDependentStart.begin(), _listScratchDependentStart.end() - 1 );
        for ( const AnimationDependencyEdge& edge : _listScratchEdge )
        {
            _listScratchDependent[_listScratchDependentCursor[edge._upstream]++] = edge._dependent;
        }

        _listScratchReady.clear();
        for ( uint32 unitIndex = 0; unitIndex < static_cast<uint32>( unitCount ); ++unitIndex )
        {
            if ( _listScratchPendingCount[unitIndex] == 0 )
                _listScratchReady.push_back( unitIndex );
        }
        // 레벨 벡터는 지난 번의 용량을 재사용한다(`_listLevel` 의 앞 `levelCount` 개만 이번 결과이고 남는 뒤쪽은 마지막에 자른다).
        size_t placedCount = 0;
        size_t levelCount  = 0;
        while ( _listScratchReady.empty() == false )
        {
            if ( levelCount == _listLevel.size() )
                _listLevel.emplace_back();
            vector<SkeletalMeshComponent*>& listLevelUnit = _listLevel[levelCount++];
            listLevelUnit.clear();
            listLevelUnit.reserve( _listScratchReady.size() );
            _listScratchNext.clear();
            for ( const uint32 unitIndex : _listScratchReady )
            {
                listLevelUnit.push_back( listUnit[unitIndex] );
                _listScratchOrder.push_back( unitIndex );
                for ( uint32 dependentSlot = _listScratchDependentStart[unitIndex]; dependentSlot < _listScratchDependentStart[unitIndex + 1]; ++dependentSlot )
                {
                    const uint32 dependentIndex = _listScratchDependent[dependentSlot];
                    if ( --_listScratchPendingCount[dependentIndex] == 0 )
                        _listScratchNext.push_back( dependentIndex );
                }
            }
            placedCount += listLevelUnit.size();
            _listScratchReady.swap( _listScratchNext );
        }

        if ( placedCount != unitCount )
        {
            // 고리 — 데이터 오류다. 알리고, 남은 유닛은 등록 순서로 한 유닛씩 따로 레벨을 둔다.
            // 레벨 안은 병렬로 돈다(runPhase). 고리의 유닛은 서로의 포즈를 읽고 쓰므로 한 레벨에 모으면 데이터 레이스다 — "레벨 안의 유닛은 서로 의존하지 않는다" 를 고리에서도 지킨다.
            _bCycle = SW_TRUE;
            for ( size_t unitIndex = 0; unitIndex < unitCount; ++unitIndex )
            {
                if ( _listScratchPendingCount[unitIndex] > 0 )
                {
                    if ( levelCount == _listLevel.size() )
                        _listLevel.emplace_back();
                    vector<SkeletalMeshComponent*>& listCycleLevel = _listLevel[levelCount++];
                    listCycleLevel.clear();
                    listCycleLevel.push_back( listUnit[unitIndex] );
                    _listScratchOrder.push_back( static_cast<uint32>( unitIndex ) );
                    const GameObject* pOwner = listUnit[unitIndex]->getOwner();
                    SW_LOG_ERROR( "Animation dependency cycle includes unit on '%#'", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
                }
            }
        }
        _listLevel.resize( levelCount );
        rebuildComponents();
    }

    void AnimationSystem::rebuildComponents()
    {
        const vector<SkeletalMeshComponent*>& listUnit  = _unitList.getItems();
        const uint32                          unitCount = static_cast<uint32>( listUnit.size() );

        // 간선으로 이어진 유닛을 한 성분으로 묶는다(union-find). 대표는 성분에서 가장 작은 번호이므로 성분 번호는 "등록 순서가 가장 앞선 유닛" 순으로 매겨진다.
        _listScratchComponentRoot.resize( unitCount );
        for ( uint32 unitIndex = 0; unitIndex < unitCount; ++unitIndex )
        {
            _listScratchComponentRoot[unitIndex] = unitIndex;
        }
        for ( const AnimationDependencyEdge& edge : _listScratchEdge )
        {
            AnimationSystemInternal::uniteComponents( _listScratchComponentRoot, edge._upstream, edge._dependent );
        }
        _listScratchComponentOf.resize( unitCount );
        uint32 componentCount = 0;
        for ( uint32 unitIndex = 0; unitIndex < unitCount; ++unitIndex )
        {
            const uint32 root = AnimationSystemInternal::findComponentRoot( _listScratchComponentRoot, unitIndex );
            // 대표는 성분에서 가장 작은 번호라 자기 번호보다 앞서므로 성분 번호는 이미 정해져 있다.
            _listScratchComponentOf[unitIndex] = ( root == unitIndex ) ? componentCount++ : _listScratchComponentOf[root];
        }

        // 성분별 구간(CSR)을 센다.
        vector<uint32>& listStart = _componentAll._listStart;
        listStart.assign( componentCount + 1, 0 );
        for ( uint32 unitIndex = 0; unitIndex < unitCount; ++unitIndex )
        {
            ++listStart[_listScratchComponentOf[unitIndex] + 1];
        }
        for ( uint32 componentIndex = 0; componentIndex < componentCount; ++componentIndex )
        {
            listStart[componentIndex + 1] += listStart[componentIndex];
        }

        // 레벨 순서(위상 순서)로 훑으며 성분 구간에 차례로 놓는다 — 구간 안은 위 유닛이 먼저이고 고리의 남은 유닛은 등록 순서로 뒤에 온다.
        // 레벨 순서의 칸마다 놓인 칸 번호를 `_listLevelSlot` 에 남겨, 레벨을 도는 쪽이 같은 유닛을 성분에서 찾게 한다.
        _listScratchComponentCursor.assign( listStart.begin(), listStart.end() - 1 );
        _componentAll._listUnit.resize( unitCount );
        _listScratchSlotOfUnit.resize( unitCount );
        _listLevelSlot.resize( _listScratchOrder.size() );
        for ( size_t position = 0; position < _listScratchOrder.size(); ++position )
        {
            const uint32 unitIndex = _listScratchOrder[position];
            const uint32 slot      = _listScratchComponentCursor[_listScratchComponentOf[unitIndex]]++;

            _componentAll._listUnit[slot]     = listUnit[unitIndex];
            _listScratchSlotOfUnit[unitIndex] = slot;
            _listLevelSlot[position]          = slot;
        }
        _componentAll.updateMaxComponentSize();
#if defined( SW_DEBUG )
        // 간선의 뜻("위 유닛이 먼저")이 성분 안 순서에 지켜졌는지 본다. 고리가 있으면 그 유닛들은 순서를 강제하지 않는다.
        if ( _bCycle == SW_FALSE )
        {
            for ( const AnimationDependencyEdge& edge : _listScratchEdge )
            {
                SW_ASSERT( _listScratchSlotOfUnit[edge._upstream] < _listScratchSlotOfUnit[edge._dependent] );
            }
        }
#endif
    }

    void AnimationSystem::collectActiveUnits( float32 deltaSeconds )
    {
        _listActive.clear();
        _listLevelStart.clear();
        _listScratchActiveFlag.assign( _componentAll.getUnitCount(), 0 );
        size_t position = 0;
        for ( const vector<SkeletalMeshComponent*>& level : _listLevel )
        {
            _listLevelStart.push_back( static_cast<uint32>( _listActive.size() ) );
            for ( SkeletalMeshComponent* pUnit : level )
            {
                const uint32 slot = _listLevelSlot[position++];
                if ( pUnit->beginAnimationFrame( deltaSeconds, _frameIndex ) )
                {
                    _listActive.push_back( pUnit );
                    _listScratchActiveFlag[slot] = 1;
                }
            }
        }
        _listLevelStart.push_back( static_cast<uint32>( _listActive.size() ) );
        SW_ASSERT( position == _listLevelSlot.size() );

        // 같은 유닛을 성분별로 다시 모은다 — 일하는 유닛이 없는 성분은 빠진다.
        _componentActive.clear();
        const uint32 componentCount = _componentAll.getComponentCount();
        for ( uint32 componentIndex = 0; componentIndex < componentCount; ++componentIndex )
        {
            const uint32 activeStart = static_cast<uint32>( _componentActive._listUnit.size() );
            for ( uint32 slot = _componentAll.getComponentStart( componentIndex ); slot < _componentAll.getComponentEnd( componentIndex ); ++slot )
            {
                if ( _listScratchActiveFlag[slot] != 0 )
                    _componentActive._listUnit.push_back( _componentAll._listUnit[slot] );
            }
            if ( _componentActive._listUnit.size() > activeStart )
                _componentActive._listStart.push_back( activeStart );
        }
        if ( _componentActive._listStart.empty() == false )
            _componentActive._listStart.push_back( static_cast<uint32>( _componentActive._listUnit.size() ) );
        _componentActive.updateMaxComponentSize();
        _activeUnitCount        = static_cast<uint32>( _listActive.size() );
        _poseEvaluatedUnitCount = 0;
    }

    void AnimationSystem::runPosePhases()
    {
        // 포즈 단계의 벽시계 시간을 측정한다 — 예산 배분이 "유닛 하나에 얼마" 를 이 평균으로 본다(언리얼 예산 배분기도 전체 시간을 측정해서 나눈다).
        uint32 poseUnitCount = 0;
        for ( const SkeletalMeshComponent* pUnit : _listActive )
        {
            poseUnitCount += pUnit->isPoseNeededThisFrame() ? 1u : 0u;
        }
        const int64 poseStart = MonotonicClock::nowNanoseconds();
        {
            SW_PROFILE_SCOPE( "GT.Animation.phaseBasePose" );
            runPhase( AnimationPhase::BasePose );
        }
        {
            SW_PROFILE_SCOPE( "GT.Animation.phaseAttachment" );
            runPhase( AnimationPhase::Attachment );
        }
        {
            SW_PROFILE_SCOPE( "GT.Animation.phasePostProcess" );
            runPhase( AnimationPhase::PostProcess );
        }
        {
            SW_PROFILE_SCOPE( "GT.Animation.phaseSkinPalette" );
            runPhase( AnimationPhase::SkinPalette );
        }
        _poseEvaluatedUnitCount = poseUnitCount;
        SW_PROFILE_COUNT( "GT.Animation.activeUnits", _activeUnitCount );
        SW_PROFILE_COUNT( "GT.Animation.poseUnits", poseUnitCount );
        // 레벨 수는 단계마다 전체 대기를 하는 횟수의 기준이다(대기 = 단계 수 × 이 값). 성분 수와 가장 큰 성분은 성분 단위로 옮길 때의 병렬도와 직렬 길이다.
        uint32 levelRunCount = 0;
        for ( size_t levelIndex = 0; levelIndex + 1 < _listLevelStart.size(); ++levelIndex )
        {
            levelRunCount += ( _listLevelStart[levelIndex] < _listLevelStart[levelIndex + 1] ) ? 1u : 0u;
        }
        SW_PROFILE_COUNT( "GT.Animation.levelCount", levelRunCount );
        SW_PROFILE_COUNT( "GT.Animation.componentCount", _componentActive.getComponentCount() );
        SW_PROFILE_COUNT( "GT.Animation.maxComponentSize", _componentActive.getMaxComponentSize() );
        if ( poseUnitCount > 0 )
        {
            const float32 sample           = static_cast<float32>( MonotonicClock::nowNanoseconds() - poseStart ) * 0.001f / static_cast<float32>( poseUnitCount );
            _averageEvaluationMicroseconds = ( _averageEvaluationMicroseconds <= 0.0f ) ? sample : ( _averageEvaluationMicroseconds * 0.9f + sample * 0.1f );
        }
    }

    void AnimationSystem::endEvaluationFrame( float32 deltaSeconds )
    {
        _crowd.endFrame();
#if SW_ANIMATION_REWIND_ENABLED
        recordRewindFrame();
        _rewind.endFrame( deltaSeconds );
#else
        (void)deltaSeconds;
#endif
    }

    void AnimationSystem::evaluate( float32 deltaSeconds )
    {
        SW_PROFILE_SCOPE( "GT.Animation.evaluate" );
        flushLevelRemoval();
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
            for ( SkeletalMeshComponent* pUnit : _unitList.getItems() )
            {
                pUnit->markPoseDirty();
            }
        }
#endif
        updateLOD();
        if ( _bOrderDirty == SW_TRUE || ( _listLevel.empty() && _unitList.getCount() > 0 ) )
            rebuildLevels();
        ++_frameIndex;
        _deltaSeconds = deltaSeconds;
        _crowd.beginFrame( deltaSeconds );

        collectActiveUnits( deltaSeconds );
        if ( _listActive.empty() )
        {
            endEvaluationFrame( deltaSeconds );
            return;
        }

        // 단계마다 프로파일 구간을 둔다 — 비용 곡선(계획 AnimationAdvancedFeatures A1)이 샘플 · 블렌드(BasePose) · 리그 노드(PostProcess) · 팔레트(SkinPalette)를 따로 읽는다.
        {
            SW_PROFILE_SCOPE( "GT.Animation.phaseTime" );
            runPhase( AnimationPhase::Time );
        }
        synchronizeGroups();
        updateCrowd();
        runPosePhases();

        for ( SkeletalMeshComponent* pUnit : _listActive )
        {
            pUnit->finishAnimationFrame();
        }
        endEvaluationFrame( deltaSeconds );
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
        for ( const IAnimationLODClient* pClient : _lodClientList.getItems() )
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
        for ( SkeletalMeshComponent* pUnit : _unitList.getItems() )
        {
            const AnimationRewindTrack* pTrack = _rewind.findTrack( pUnit->getHandle() );
            const AnimationRewindFrame* pFrame = ( pTrack != nullptr ) ? pTrack->findFrame( scrubTime ) : nullptr;
            if ( pFrame == nullptr || pFrame->_boneCount == 0 )
                continue;
            AnimationRewindRecorder::decodePose( *pFrame, _rewindScratchPose );
            if ( pUnit->applyRewindPose( _rewindScratchPose ) )
                _bRewindApplied = SW_TRUE;
        }
        for ( IAnimationLODClient* pClient : _lodClientList.getItems() )
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
            // 유닛 하나가 수 us 라 고정 개수로는 나눌 값이 갈린다(캐릭터 20 개는 직렬이, 100 개 이상은 병렬이 이긴다) — 측정한 비용으로 고른다.
            engine::runParallelByCost( end - start, SW_DELEGATE_METHOD( ParallelBlockDelegate, &AnimationSystemInternal::PhaseJob::runRange, &job ) );
        }
    }

    void AnimationSystem::synchronizeGroups()
    {
        vector<AnimationPlayer*> listPlayer;
        vector<float32>          listWeight;
        vector<hashed_string>    listGroup;
        for ( SkeletalMeshComponent* pUnit : _listActive )
        {
            pUnit->collectSyncPlayers( listPlayer, listWeight, listGroup );
        }
        if ( listPlayer.size() < 2 )
            return;

        // 그룹 이름 → 그룹 번호(처음 나온 순서)를 해시로 잡고, 번호별로 안정 정렬(계수 정렬)해 그룹마다 연속 구간으로 모은다 — 플레이어 P 개 · 그룹 G 개에 O(P) (옛 방식은 O(P·G)).
        // 그룹 안 순서는 등록 순서 그대로이고 그룹은 처음 나온 순서로 처리한다(옛 방식과 같다).
        const size_t                         playerCount = listPlayer.size();
        unordered_map<hashed_string, uint32> mapGroupIndex;
        vector<uint32>                       listGroupOfPlayer( playerCount );
        vector<uint32>                       listGroupStart{ 0 }; // 그룹마다 시작 위치(누적 전에는 개수를 센다)
        for ( size_t playerIndex = 0; playerIndex < playerCount; ++playerIndex )
        {
            const auto   inserted   = mapGroupIndex.emplace( listGroup[playerIndex], static_cast<uint32>( listGroupStart.size() - 1 ) );
            const uint32 groupIndex = inserted.first->second;
            if ( inserted.second )
                listGroupStart.push_back( 0 );
            listGroupOfPlayer[playerIndex] = groupIndex;
            ++listGroupStart[groupIndex + 1];
        }
        const size_t groupCount = listGroupStart.size() - 1;
        for ( size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            listGroupStart[groupIndex + 1] += listGroupStart[groupIndex];
        }
        vector<AnimationPlayer*> listSortedPlayer( playerCount );
        vector<float32>          listSortedWeight( playerCount );
        vector<uint32>           listCursor( listGroupStart.begin(), listGroupStart.end() - 1 );
        for ( size_t playerIndex = 0; playerIndex < playerCount; ++playerIndex )
        {
            const uint32 slot      = listCursor[listGroupOfPlayer[playerIndex]]++;
            listSortedPlayer[slot] = listPlayer[playerIndex];
            listSortedWeight[slot] = listWeight[playerIndex];
        }
        for ( size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            const uint32 start = listGroupStart[groupIndex];
            const uint32 count = listGroupStart[groupIndex + 1] - start;
            if ( count > 1 )
                (void)AnimationSyncGroup::synchronize( listSortedPlayer.data() + start, listSortedWeight.data() + start, count );
        }
    }
} // namespace sw
