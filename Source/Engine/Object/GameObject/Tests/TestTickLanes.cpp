// TickLanesTest — 일괄 경로(파도 · 레인)가 기준 길(오브젝트 칸, 한 오브젝트 = 한 스레드)과 같은 상태를 내는지(서브틱 · 선행 조건 · 틱 중 서브틱
// 등록 · 해제 · 남의 오브젝트 쓰기를 섞어), 타입이 선언한 레인(`TickMode`)대로 가는지, 선행 조건 종류(순서만 · 쓰기를 봄)가 태스크 간선 · 레벨 장벽으로
// 갈리는지 본다(NEXT 5-3 1 · 1b · 1c).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/GameObject/Tests/SceneIterationTestAccess.h"
#include "Engine/Object/GameObject/Tests/TickLaneTestComponents.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"
#include "Engine/Object/GameObject/Tick/StructuralChangeBuffer.h"
#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"
#include "Engine/Scene/SceneObjectHash.h"

using namespace sw;

namespace
{
    constexpr utf8 kMoverBatchScopeName[] = "GT.Scene.tickBatch.TickLaneMoverTestComponent";

    /** @brief 무버 여럿(일부는 남의 오브젝트에 쓰고 일부는 팔로워가 기다린다)을 둔 씬입니다. 병렬 분배 임계값(16)을 넘게 둔다. */
    struct TickLaneRig
    {
        static constexpr uint32  kMoverCount = 40;
        static constexpr float32 kDeltaTime  = 1.0f / 60.0f;

        GameObjectManager                      _manager;
        vector<TickLaneMoverTestComponent*>    _listMover;
        vector<SceneComponent*>                _listMoverRoot;
        vector<TickLaneFollowerTestComponent*> _listFollower;

        TickLaneRig( const SceneEngineServices& services, bool bBatchPath )
            : _manager{ services }
        {
            _manager.getTickRegistry().setBatchPathEnabled( bBatchPath );
            SceneComponent* pShared = _manager.createGameObject( hashed_string( "Shared" ) )->addComponent<SceneComponent>();
            for ( uint32 index = 0; index < kMoverCount; ++index )
            {
                GameObject*                 pMoverObject = _manager.createGameObject( hashed_string( "Mover" ) );
                SceneComponent*             pMoverRoot   = pMoverObject->addComponent<SceneComponent>();
                TickLaneMoverTestComponent* pMover       = pMoverObject->addComponent<TickLaneMoverTestComponent>();
                pMover->_speed                           = 1.0f + static_cast<float32>( index ) * 0.125f;
                pMover->_pShared                         = ( index % 4 == 0 ) ? pShared : nullptr;
                _listMover.push_back( pMover );
                _listMoverRoot.push_back( pMoverRoot );
                if ( index % 5 != 0 )
                    continue;
                addFollower( index, ( index / 5 ) % 2 == 1 );
            }
            _manager.beginPlay();
        }

        /** @brief 무버 @p leaderIndex 를 기다리는 팔로워를 답니다. @p bAlt 면 다른 타입 · 다른 서브틱 id 다(스테이지에서 두 타입이 섞인다). 플레이 중이면 다음 틱이 beginPlay 한다. */
        void addFollower( uint32 leaderIndex, bool bAlt )
        {
            GameObject* pFollowerObject = _manager.createGameObject( hashed_string( "Follower" ) );
            (void)pFollowerObject->addComponent<SceneComponent>();
            TickLaneFollowerTestComponent* pFollower = bAlt ? pFollowerObject->addComponent<TickLaneFollowerAltTestComponent>()
                                                            : pFollowerObject->addComponent<TickLaneFollowerTestComponent>();
            pFollower->_pLeader                      = _listMover[leaderIndex];
            pFollower->_pLeaderRoot                  = _listMoverRoot[leaderIndex];
            _listFollower.push_back( pFollower );
        }

        void run( uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kDeltaTime );
            }
        }

        /** @brief 트랜스폼(씬 해시)과 컴포넌트가 접은 값입니다. */
        uint64 computeHash() const
        {
            StateHashBuilder builder;
            const uint64     sceneHash = SceneObjectHash::compute( SceneIterationTestAccess::getKey(), _manager );
            builder.addBytes( &sceneHash, sizeof( sceneHash ) );
            for ( const TickLaneMoverTestComponent* pMover : _listMover )
            {
                builder.addFloat( pMover->_value );
            }
            for ( const TickLaneFollowerTestComponent* pFollower : _listFollower )
            {
                builder.addFloat( pFollower->_value );
            }
            return builder.getHash();
        }
    };

    /** @brief 그룹의 스테이지 중 항목이 있는 첫 스테이지입니다(테스트 씬은 팔로워가 모두 한 레벨 · 한 스테이지에 든다). 없으면 nullptr. */
    const TickStage* findFirstStage( const TickRegistry& registry, uint32 group )
    {
        for ( const TickStage& stage : registry.getStages() )
        {
            if ( stage._group == group && stage._listItem.empty() == false )
                return &stage;
        }
        return nullptr;
    }

    /** @brief 그룹의 파도 원소 수입니다. */
    uint32 countWaveElements( const TickRegistry& registry, uint32 group )
    {
        uint32 count = 0;
        for ( const TickWave& wave : registry.getWaves( group ) )
        {
            for ( const TickBatchList& batchList : wave._listBatch )
            {
                count += static_cast<uint32>( batchList._listElement.size() );
            }
        }
        return count;
    }
} // namespace

/**
 * @brief [TickLanesTest] 파도 길(서브틱 · 여러 항목 오브젝트까지 일괄)이 기준 길과 같은 상태를 낸다 — 서브틱 · 선행 조건 · 틱 중 등록 · 해제 · 남의 오브젝트 쓰기를 섞어
 */
SW_TEST_CASE( TickLanesTest, WavePathMatchesObjectEntryPath )
{
    const SceneEngineServices services = SceneEngineServices::makeFromBoundEngine();
    TickLaneRig               batched{ services, true };
    TickLaneRig               reference{ services, false };
    batched.run( 12 );
    reference.run( 12 );
    SW_EXPECT_EQUAL( reference.computeHash(), batched.computeHash() );

    // 일괄 길은 서브틱을 가진 무버도 파도로 돈다(칸으로 빠지지 않는다). 기준 길은 파도가 없다.
    const TickRegistry& registry = batched._manager.getTickRegistry();
    const uint32        group    = static_cast<uint32>( TickGroup::DuringPhysics );
    SW_EXPECT_TRUE( registry.getEntries( group ).empty() );
    SW_EXPECT_TRUE( countWaveElements( registry, group ) >= TickLaneRig::kMoverCount * 3 );
    SW_EXPECT_TRUE( reference._manager.getTickRegistry().getWaves( group ).empty() );
}

/**
 * @brief [TickLanesTest] 등록한 타입은 자기 일괄 함수로, 메인 레인 타입(과 등록 없는 파생)은 게임 스레드에서 돈다 — 상태는 기준 길과 같다
 */
SW_TEST_CASE( TickLanesTest, DeclaredLanesRunWhereDeclared )
{
    TickBatchRegistry        tickBatchRegistry;
    TickBatchRegistry::Entry moverEntry{ &TickLaneMoverTestComponent::StaticType, &TickBatchOf<TickLaneMoverTestComponent, kMoverBatchScopeName>::tickRange,
                                         TickMode::Batch };
    TickBatchRegistry::Entry mainEntry{ &TickLaneMainTestComponent::StaticType, nullptr, TickMode::Main };
    SW_REQUIRE_TRUE( tickBatchRegistry.registerEntry( moverEntry ) );
    SW_REQUIRE_TRUE( tickBatchRegistry.registerEntry( mainEntry ) );

    SceneEngineServices services = SceneEngineServices::makeFromBoundEngine();
    services._pTickBatchRegistry = &tickBatchRegistry;
    {
        TickLaneRig                       batched{ services, true };
        TickLaneMainTestComponent*        pMain    = batched._manager.createGameObject( hashed_string( "Main" ) )->addComponent<TickLaneMainTestComponent>();
        TickLaneMainDerivedTestComponent* pDerived = batched._manager.createGameObject( hashed_string( "Derived" ) )->addComponent<TickLaneMainDerivedTestComponent>();
        batched.run( 6 );

        const TickRegistry& registry = batched._manager.getTickRegistry();
        const uint32        group    = static_cast<uint32>( TickGroup::DuringPhysics );
        SW_EXPECT_EQUAL( size_t{ 2 }, registry.getMainEntries( group ).size() );
        SW_EXPECT_TRUE( pMain->_tickCount > 0 && pMain->_tickThread == std::this_thread::get_id() );
        SW_EXPECT_TRUE( pDerived->_tickCount > 0 && pDerived->_tickThread == std::this_thread::get_id() );

        // 무버의 주 틱 · 서브틱 목록은 등록한 함수를 든다(가상 호출 함수가 아니다).
        bool bTypedList = false;
        for ( const TickWave& wave : registry.getWaves( group ) )
        {
            for ( const TickBatchList& batchList : wave._listBatch )
            {
                bTypedList = bTypedList || batchList._pFunction == &TickBatchOf<TickLaneMoverTestComponent, kMoverBatchScopeName>::tickRange;
            }
        }
        SW_EXPECT_TRUE( bTypedList );

        TickLaneRig reference{ SceneEngineServices::makeFromBoundEngine(), false };
        (void)reference._manager.createGameObject( hashed_string( "Main" ) )->addComponent<TickLaneMainTestComponent>();
        (void)reference._manager.createGameObject( hashed_string( "Derived" ) )->addComponent<TickLaneMainDerivedTestComponent>();
        reference.run( 6 );
        SW_EXPECT_EQUAL( reference.computeHash(), batched.computeHash() );
    }
    tickBatchRegistry.unregisterEntry( mainEntry );
    tickBatchRegistry.unregisterEntry( moverEntry );
}

/**
 * @brief [TickLanesTest] 미룬 일은 (디스패치 순번, 쓴 오브젝트, 넣은 순서) 순으로 돈다 — 틱 밖의 일이 맨 앞, 같은 디스패치 안은 쓴 오브젝트 id 순(워커 순이 아니다)
 */
SW_TEST_CASE( TickLanesTest, DeferredQueueMergesThreadBuffersByKey )
{
    GameObjectManager     manager;
    const GameObject*     pFirst  = manager.createGameObject( hashed_string( "First" ) );
    const GameObject*     pSecond = manager.createGameObject( hashed_string( "Second" ) );
    DeferredDelegateQueue queue;
    vector<uint32>        listRun;
    const auto            pushAs = [&queue, &listRun]( const GameObject* pWriter, uint32 dispatchOrdinal, uint32 tag )
    {
        TickThreadState&      threadState = StructuralChangeBuffer::getThreadState();
        const TickThreadState saved       = threadState;
        threadState._pTickWriter          = pWriter;
        threadState._dispatchOrdinal      = dispatchOrdinal;
        queue.push( SW_DELEGATE_LAMBDA( DeferredDelegateQueue::Callback, [&listRun, tag]()
        { listRun.push_back( tag ); } ) );
        threadState = saved;
    };
    pushAs( pSecond, 2, 4 );
    pushAs( pFirst, 2, 3 );
    pushAs( pSecond, 1, 2 );
    pushAs( nullptr, 0, 1 );
    pushAs( pSecond, 2, 5 );
    queue.flush();
    SW_EXPECT_TRUE( ( listRun == vector<uint32>{ 1, 2, 3, 4, 5 } ) );
    SW_EXPECT_TRUE( queue.isEmpty() );
}

namespace
{
    /** @brief 잡 컨텍스트로 도는 레인 시스템입니다 — 씬을 바꾸는 일은 틱 뒤 큐로만 낸다. */
    class LaneProbeSystem final : public ISceneLaneFrameSystem
    {
    public:
        uint32 _runCount      = 0;
        uint32 _deferredCount = 0;

        explicit LaneProbeSystem( bool bWritesStructure )
            : _bWritesStructure{ bWritesStructure }
        {
        }

        void runLane( SceneJobContext& context ) override
        {
            ++_runCount;
            uint32* pDeferredCount = &_deferredCount;
            context.deferPostTick( SW_DELEGATE_LAMBDA( SceneJobContext::Callback, [pDeferredCount]()
            { ++*pDeferredCount; } ) );
        }

        SceneFrameAccess getFrameAccess() const override
        {
            SceneFrameAccess access{};
            if ( _bWritesStructure == SW_TRUE )
                access._writes = { SceneDataKind::Structure };
            return access;
        }

    private:
        uint8 _bWritesStructure : 1;
    };
} // namespace

/**
 * @brief [TickLanesTest] 레인 시스템은 잡 컨텍스트로 돌고 씬을 바꾸는 일은 틱 뒤 큐로 간다 — 구조를 쓴다고 선언한 레인 시스템은 붙지 않는다
 */
SW_TEST_CASE( TickLanesTest, LaneFrameSystemRunsThroughJobContext )
{
    GameObjectManager manager;
    SW_EXPECT_LOG( "FrameSystem.LaneStructure" );
    SW_EXPECT_TRUE( manager.addFrameSystem( hashed_string( "LaneStructure" ), make_unique<LaneProbeSystem>( true ) ) == false );
    SW_REQUIRE_TRUE( manager.addFrameSystem( hashed_string( "LaneProbe" ), make_unique<LaneProbeSystem>( false ) ) );
    (void)manager.createGameObject( hashed_string( "Anchor" ) );
    manager.beginPlay();
    manager.update( 1.0f / 60.0f );
    const LaneProbeSystem* pProbe = static_cast<const LaneProbeSystem*>( manager.findFrameSystem( hashed_string( "LaneProbe" ) ) );
    SW_REQUIRE_TRUE( pProbe != nullptr );
    SW_EXPECT_EQUAL( uint32{ 1 }, pProbe->_runCount );
    SW_EXPECT_EQUAL( uint32{ 1 }, pProbe->_deferredCount ); // 같은 프레임의 `ApplyPrePhysics` 가 돌렸다
}

/**
 * @brief [TickLanesTest] 선행 조건 스테이지는 같은 (타입, 서브틱) 이웃을 한 일괄 목록으로 묶고 옛 순서(순서 키, 오브젝트 id — 기준 길)와 같은 상태를 낸다 —
 *        전체 재구축 뒤에도, 플레이 중 스폰한 팔로워를 증분으로 끼운 뒤(`flushStageEdits`)에도 같은 규칙이다(NEXT 5-3 1b)
 */
SW_TEST_CASE( TickLanesTest, StageNeighborBatchesMatchOldOrder )
{
    const SceneEngineServices services = SceneEngineServices::makeFromBoundEngine();
    TickLaneRig               batched{ services, true };
    TickLaneRig               reference{ services, false };
    batched.run( 4 );
    reference.run( 4 );
    SW_EXPECT_EQUAL( reference.computeHash(), batched.computeHash() );

    // 팔로워 두 타입이 오브젝트 id 순으로 번갈아 든다 — 새 자리 키는 두 목록, 옛 순서는 번갈아 끊긴다.
    const uint32     group           = static_cast<uint32>( TickGroup::DuringPhysics );
    const TickStage* pBatchedStage   = findFirstStage( batched._manager.getTickRegistry(), group );
    const TickStage* pReferenceStage = findFirstStage( reference._manager.getTickRegistry(), group );
    SW_REQUIRE_TRUE( pBatchedStage != nullptr && pReferenceStage != nullptr );
    SW_EXPECT_EQUAL( pReferenceStage->_listItem.size(), pBatchedStage->_listItem.size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, pBatchedStage->_listBatch.size() );
    SW_EXPECT_TRUE( pReferenceStage->_listBatch.size() > 2 );

    // 플레이 중 스폰한 팔로워는 말단이라 스테이지를 다시 짓지 않고 끼운다 — 끼운 자리도 같은 규칙이라 목록이 늘지 않는다.
    for ( const uint32 leaderIndex : { 1u, 2u, 3u, 4u } )
    {
        batched.addFollower( leaderIndex, leaderIndex % 2 == 0 );
        reference.addFollower( leaderIndex, leaderIndex % 2 == 0 );
    }
    batched.run( 4 );
    reference.run( 4 );
    SW_EXPECT_EQUAL( reference.computeHash(), batched.computeHash() );
    pBatchedStage = findFirstStage( batched._manager.getTickRegistry(), group );
    SW_REQUIRE_TRUE( pBatchedStage != nullptr );
    SW_EXPECT_EQUAL( batched._listFollower.size(), pBatchedStage->_listItem.size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, pBatchedStage->_listBatch.size() );
}

namespace
{
    /**
     * @brief 사슬 고리를 둔 씬입니다 — 머리(0) ← 1 ← 2 ← 3 ←(쓰기를 봄) 4 ← 5 ← 6 ← 7, 그리고 2 번 오브젝트의 둘째 고리(2b ← 3). 4 번만 `ReadsWrites` 이고 나머지는 순서만이다.
     * @details 레벨 0 은 {1, 2, 3} 한 스테이지(간선 둘)와 같은 오브젝트라 칸이 갈린 {2b}, 레벨 1 은 {4..7} 한 스테이지(간선 셋, 적용 먼저)가 된다.
     */
    struct TickChainRig
    {
        static constexpr uint32  kLinkCount       = 8;
        static constexpr uint32  kReadsWritesLink = 4;
        static constexpr float32 kDeltaTime       = 1.0f / 60.0f;

        GameObjectManager                   _manager;
        vector<TickLaneChainTestComponent*> _listLink;
        TickLaneChainTestComponent*         _pSecond{ nullptr }; ///< 2 번 오브젝트의 둘째 고리(3 번을 기다린다)

        TickChainRig( const SceneEngineServices& services, bool bBatchPath )
            : _manager{ services }
        {
            _manager.getTickRegistry().setBatchPathEnabled( bBatchPath );
            for ( uint32 index = 0; index < kLinkCount; ++index )
            {
                TickLaneChainTestComponent* pLink = _manager.createGameObject( hashed_string( "Link" ) )->addComponent<TickLaneChainTestComponent>();
                pLink->_gain                      = 1.0f + static_cast<float32>( index ) * 0.25f;
                pLink->_pPrev                     = index > 0 ? _listLink[index - 1] : nullptr;
                _listLink.push_back( pLink );
            }
            _pSecond         = _listLink[2]->getOwner()->addComponent<TickLaneChainTestComponent>();
            _pSecond->_gain  = 7.0f;
            _pSecond->_pPrev = _listLink[3];
            _manager.beginPlay();
            // 선행 조건은 고리마다 서브틱 핸들이 생긴 뒤(beginPlay) 건다.
            for ( uint32 index = 1; index < kLinkCount; ++index )
            {
                const TickPrerequisiteKind kind = index == kReadsWritesLink ? TickPrerequisiteKind::ReadsWrites : TickPrerequisiteKind::OrderOnly;
                SW_EXPECT_TRUE( _listLink[index]->addSubTickPrerequisite( TickLaneChainTestComponent::kChainSubTick, _listLink[index - 1]->_chainHandle, kind ) );
            }
            SW_EXPECT_TRUE( _pSecond->addSubTickPrerequisite( TickLaneChainTestComponent::kChainSubTick, _listLink[3]->_chainHandle, TickPrerequisiteKind::OrderOnly ) );
        }

        void run( uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                _manager.update( kDeltaTime );
            }
        }

        /** @brief 이번 프레임 값이 사슬 순서대로 계산된 값과 같은지 봅니다(앞 고리보다 먼저 돈 고리는 지난 프레임 값을 읽어 어긋난다). */
        void expectChainValues() const
        {
            float32 expected = static_cast<float32>( _listLink[0]->_frame ) * 0.5f + _listLink[0]->_gain + kDeltaTime;
            SW_EXPECT_NEAR( expected, _listLink[0]->_value, 1e-4f );
            for ( uint32 index = 1; index < kLinkCount; ++index )
            {
                expected = expected * 0.5f + _listLink[index]->_gain + kDeltaTime;
                SW_EXPECT_NEAR( expected, _listLink[index]->_value, 1e-4f );
            }
            SW_EXPECT_NEAR( _listLink[3]->_value * 0.5f + _pSecond->_gain + kDeltaTime, _pSecond->_value, 1e-4f );
        }
    };
} // namespace

/**
 * @brief [TickLanesTest] 순서만 선행 조건은 같은 레벨 스테이지의 태스크 간선(`runAfter`)이 되고 쓰기를 보는 선행 조건만 레벨 장벽 · 적용을 넘는다 —
 *        순서는 사슬 그대로이고 일괄 길과 기준 길이 같은 값을 낸다(NEXT 5-3 선행 조건 간선 종류)
 */
SW_TEST_CASE( TickLanesTest, OrderOnlyPrerequisitesBecomeTaskEdges )
{
    const SceneEngineServices services = SceneEngineServices::makeFromBoundEngine();
    TickChainRig              batched{ services, true };
    TickChainRig              reference{ services, false };
    batched.run( 6 );
    reference.run( 6 );
    batched.expectChainValues();
    reference.expectChainValues();
    for ( uint32 index = 0; index < TickChainRig::kLinkCount; ++index )
    {
        SW_EXPECT_EQUAL( reference._listLink[index]->_value, batched._listLink[index]->_value );
    }
    SW_EXPECT_EQUAL( reference._pSecond->_value, batched._pSecond->_value );

    // 스테이지 모양: 레벨 0 = {1, 2, 3}(간선 둘) + {2b}(같은 오브젝트라 다음 칸, 간선 없음), 레벨 1 = {4..7}(간선 셋, 돌기 전에 적용).
    const uint32      group = static_cast<uint32>( TickGroup::DuringPhysics );
    vector<TickStage> listStage; // 복사본 — 아래에서 종류를 바꾸면 레지스트리가 다시 짓는다
    for ( const TickStage& stage : batched._manager.getTickRegistry().getStages() )
    {
        if ( stage._group == group && stage._listItem.empty() == false )
            listStage.push_back( stage );
    }
    SW_REQUIRE_EQUAL( size_t{ 3 }, listStage.size() );
    SW_EXPECT_EQUAL( uint32{ 0 }, listStage[0]._level );
    SW_EXPECT_EQUAL( size_t{ 3 }, listStage[0]._listItem.size() );
    SW_EXPECT_EQUAL( size_t{ 2 }, listStage[0]._listEdge.size() );
    SW_EXPECT_EQUAL( size_t{ 3 }, listStage[0]._listBatch.size() ); // 간선 있는 스테이지는 항목마다 목록 하나
    SW_EXPECT_EQUAL( uint32{ 0 }, listStage[1]._level );
    SW_EXPECT_EQUAL( size_t{ 1 }, listStage[1]._listItem.size() );
    SW_EXPECT_TRUE( listStage[1]._listEdge.empty() );
    SW_EXPECT_EQUAL( uint32{ 1 }, listStage[2]._level );
    SW_EXPECT_EQUAL( size_t{ 4 }, listStage[2]._listItem.size() );
    SW_EXPECT_EQUAL( size_t{ 3 }, listStage[2]._listEdge.size() );
    SW_EXPECT_TRUE( listStage[0]._bApplyBefore == SW_FALSE ); // 순서만 — 머리의 쓰기를 기다리지 않는다
    SW_EXPECT_TRUE( listStage[2]._bApplyBefore == SW_TRUE );  // 쓰기를 보는 간선이 넘는 레벨

    // 종류를 바꾸면 스테이지가 다시 갈린다 — 모두 쓰기를 보면 고리마다 레벨 하나(간선 없는 스테이지, 2b 는 4 와 같은 레벨).
    for ( uint32 index = 1; index < TickChainRig::kLinkCount; ++index )
    {
        SW_REQUIRE_TRUE( batched._listLink[index]->addSubTickPrerequisite( TickLaneChainTestComponent::kChainSubTick, batched._listLink[index - 1]->_chainHandle,
                                                                           TickPrerequisiteKind::ReadsWrites ) );
    }
    SW_REQUIRE_TRUE( batched._pSecond->addSubTickPrerequisite( TickLaneChainTestComponent::kChainSubTick, batched._listLink[3]->_chainHandle, TickPrerequisiteKind::ReadsWrites ) );
    batched.run( 2 );
    batched.expectChainValues();
    uint32 maxLevel = 0;
    for ( const TickStage& stage : batched._manager.getTickRegistry().getStages() )
    {
        if ( stage._group != group || stage._listItem.empty() )
            continue;
        SW_EXPECT_TRUE( stage._listEdge.empty() );
        maxLevel = stage._level > maxLevel ? stage._level : maxLevel;
    }
    SW_EXPECT_EQUAL( TickChainRig::kLinkCount - 2, maxLevel ); // 고리 1..7 이 레벨 0..6
}
