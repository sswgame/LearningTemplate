// SceneStreamingTest — 씬 스트리밍(감사 A-13): 시간 예산으로 나눈 짓기가 한 번에 지은 것과 같고, 내렸다 올린 셀이 바뀐 상태(런타임 칸 포함)를 지키며, 읽는 중에 내리면 취소되고 새지 않는다.
// 셀 경계(설계 "스트리밍 셀 경계 정합 · 저장"): 걸어 나간 오브젝트 · 런타임 스폰은 자리의 셀을 따르고, 세이브 몫은 비운 매니저 위에서 셀 상태를 되살린다.
#include "pch.h"

#include "Core/Concurrency/Futex.h"
#include "Core/Concurrency/atomic.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/File/ContentFileSource.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/Tests/StateHashTestComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/NetObjectTable.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

using namespace sw;

namespace
{
    struct SceneStreamingTestUtil
    {
        static constexpr uint32 kWholeBudgetMicroseconds = 1'000'000'000u; ///< 한 프레임에 모두 짓는 예산
        static constexpr uint32 kCellCount               = 3;
        static constexpr uint32 kEntitiesPerCell         = 2;

        /** @brief 씬 컴포넌트 하나를 가진 엔티티입니다. */
        static SceneDocument::SceneObjectNode makeNode( uint64 fileID, string_view name, string_view position )
        {
            SceneDocument::SceneObjectNode node{};
            node._fileID = fileID;
            node._name   = string( name );
            StringBuilder<constant::buffer::kMax512> xml;
            xml.appendFormat( "<GameObject _schemaVersion=\"0\" _name=\"%#\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"%#\" /></_listComponent></GameObject>", name, position );
            node._embeddedXML = string( xml.view() );
            return node;
        }

        /**
         * @brief 셀 (0,0) · (1,0) · … 에 엔티티 @p entitiesPerCell 개씩(파일 id 1 부터)인 스트리밍 문서입니다. 셀 한 변 10 m, 로딩 반경 6 m.
         * @details 셀 x 의 엔티티는 x*10+2 · x*10+8 · 그 뒤는 x*10+3..7 자리(z 5)다.
         */
        static SceneDocument makeDocument( uint32 budgetMicroseconds, uint32 cellCount = kCellCount, uint32 entitiesPerCell = kEntitiesPerCell )
        {
            SceneDocument doc{};
            doc._name                                        = "Streamed";
            doc._sceneSettings._bStreaming                   = true;
            doc._sceneSettings._cellGrid._cellSize           = 10.0f;
            doc._sceneSettings._cellGrid._loadingRadius      = 6.0f;
            doc._sceneSettings._unloadMargin                 = 0.0f;
            doc._sceneSettings._maxCellLoadsPerFrame         = 4;
            doc._sceneSettings._addToWorldBudgetMicroseconds = budgetMicroseconds;
            doc._bValid                                      = true;
            uint64 fileID                                    = 1;
            for ( int32 x = 0; x < static_cast<int32>( cellCount ); ++x )
            {
                SceneDocument::StreamingCell cell{};
                cell._cell = StreamingCellCoord{ x, 0 };
                for ( uint32 entityIndex = 0; entityIndex < entitiesPerCell; ++entityIndex )
                {
                    const int32                            offset = entityIndex == 0 ? 2 : ( entityIndex == 1 ? 8 : 3 + static_cast<int32>( entityIndex % 5 ) );
                    fixed_string<constant::buffer::kMax32> position;
                    formatstring( position.data(), position.capacity(), "%#,0,5", static_cast<int64>( x * 10 + offset ) );
                    fixed_string<constant::buffer::kMax32> name;
                    formatstring( name.data(), name.capacity(), "Crate%#", static_cast<int64>( fileID ) );
                    cell._listSceneObjectNode.push_back( makeNode( fileID, name, position ) );
                    fileID += 1;
                }
                doc._listStreamingCell.push_back( std::move( cell ) );
            }
            return doc;
        }

        /** @brief 파일 id @p fileID 의 살아 있는 오브젝트입니다. 없으면 nullptr 입니다. */
        static GameObject* findByFileID( const Scene& scene, uint64 fileID )
        {
            ObjectSavedIDMap mapSavedID;
            scene.collectSavedIDMap( mapSavedID );
            for ( const auto& [objectID, savedID] : mapSavedID )
            {
                GameObject* pObject = savedID == fileID ? scene.getObjectManager()->findGameObjectByID( objectID ) : nullptr;
                if ( pObject != nullptr && pObject->isPendingDestroy() == false )
                    return pObject;
            }
            return nullptr;
        }

        static float3 findPosition( const Scene& scene, uint64 fileID )
        {
            const GameObject*     pObject = findByFileID( scene, fileID );
            const SceneComponent* pScene  = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            SW_EXPECT_TRUE( pScene != nullptr );
            return pScene != nullptr ? pScene->getLocalPosition() : float3{};
        }

        static void setSource( SceneStreamingSubsystem& streaming, const float3& position )
        {
            const StreamingSource source{ position, streaming.getLoadingRadius(), StreamingSourcePriority::Normal };
            SW_EXPECT_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
        }

        /** @brief 이름이 @p pName 인(번호 붙은 겹침 포함) 살아 있는 오브젝트 수입니다 — 같은 엔티티가 두 번 서면 2 다. */
        static uint32 countNamed( GameObjectManager& objects, const utf8* pName )
        {
            objects.processDeferredDestruction();
            vector<GameObject*> listObject;
            objects.findGameObjectsByBaseName( pName, listObject );
            uint32 count = 0;
            for ( const GameObject* pObject : listObject )
            {
                count += pObject->isPendingDestroy() ? 0u : 1u;
            }
            return count;
        }
    };
    using Util = SceneStreamingTestUtil;

    /** @brief 테스트 동안 결정적 실행의 "프레임마다 끝내기" 를 끈다 — 켜져 있으면 예산 없이 짓고 읽기를 기다린다. */
    class ScopedFreeRunningPolicy
    {
    public:
        ScopedFreeRunningPolicy()
            : _previous{ engine::getSubsystem<DeterministicRun>().getPolicy() }
        {
            DeterministicRunPolicy policy     = _previous;
            policy._bFinishAssetJobsEachFrame = SW_FALSE;
            engine::getSubsystem<DeterministicRun>().setPolicy( policy );
        }
        ~ScopedFreeRunningPolicy() { engine::getSubsystem<DeterministicRun>().setPolicy( _previous ); }
        ScopedFreeRunningPolicy( const ScopedFreeRunningPolicy& )            = delete;
        ScopedFreeRunningPolicy& operator=( const ScopedFreeRunningPolicy& ) = delete;

    private:
        DeterministicRunPolicy _previous;
    };

    /**
     * @brief 지금 콘텐츠 소스에 넘기되 이름이 @p fileName 으로 끝나는 파일의 구간 읽기는 완료를 문(`release`)이 열릴 때까지 붙든다 — 읽기가 "진행 중" 인 동안을 테스트가 정한다.
     * @details 붙든 완료는 IO · 태스크 워커에서 기다린다. 사라질 때 문을 열고 걸린 읽기가 모두 끝나기를 기다린다(완료가 이 객체를 가리킨다).
     */
    class LatchedContentFileSource final : public IContentFileSource
    {
    public:
        LatchedContentFileSource( const IContentFileSource& inner, string_view fileName )
            : _inner{ inner }
            , _fileName{ fileName }
            , _latch{ 0 }
        {
        }
        ~LatchedContentFileSource() override
        {
            release();
            engine::getSubsystem<AsyncFileIO>().waitIdle();
        }
        LatchedContentFileSource( const LatchedContentFileSource& )            = delete;
        LatchedContentFileSource& operator=( const LatchedContentFileSource& ) = delete;

        /** @brief 문을 엽니다 — 붙든 완료와 그 뒤의 완료가 지나간다. */
        void release()
        {
            _latch.store( 1 );
            Futex::wakeAll( _latch );
        }

        AsyncFileHandle open( string_view path ) const override { return _inner.open( path ); }
        bool            isFile( string_view path ) const override { return _inner.isFile( path ); }
        bool            isFolder( string_view path ) const override { return _inner.isFolder( path ); }
        bool            collectFiles( string_view folder, vector<string>& outListPath ) const override { return _inner.collectFiles( folder, outListPath ); }
        bool            readFile( string_view path, vector<uint8>& outBytes ) const override { return _inner.readFile( path, outBytes ); }
        bool            readTextFile( string_view path, string& outText ) const override { return _inner.readTextFile( path, outText ); }
        AsyncReadHandle readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const override
        {
            return _inner.readFileAsync( io, path, priority, onComplete );
        }
        AsyncReadHandle readRangeAsync( AsyncFileIO& io, string_view path, uint64 offset, uint64 size, AsyncIOPriority priority,
                                        const AsyncReadCompleteDelegate& onComplete ) const override
        {
            const bool bLatched = path.size() >= _fileName.size() && path.substr( path.size() - _fileName.size() ) == _fileName;
            if ( bLatched == false )
                return _inner.readRangeAsync( io, path, offset, size, priority, onComplete );
            return _inner.readRangeAsync( io, path, offset, size, priority, SW_DELEGATE_LAMBDA( AsyncReadCompleteDelegate, [this, onComplete]( AsyncReadResult& result )
            {
                while ( _latch.load() == 0 )
                    Futex::wait( _latch, 0 );
                if ( onComplete.isBound() )
                    onComplete( result );
            } ) );
        }

    private:
        const IContentFileSource& _inner;
        string                    _fileName;
        mutable atomic<uint32>    _latch; ///< 0 이면 붙든다
    };

    /** @brief 가짜 소스를 지금 소스로 걸고, 사라질 때 이전 소스로 되돌립니다. */
    class ScopedContentFileSource
    {
    public:
        explicit ScopedContentFileSource( const IContentFileSource& source )
            : _pPrevious{ ContentFileSource::bindCurrent( &source ) }
        {
        }
        ~ScopedContentFileSource() { (void)ContentFileSource::bindCurrent( _pPrevious ); }
        ScopedContentFileSource( const ScopedContentFileSource& )            = delete;
        ScopedContentFileSource& operator=( const ScopedContentFileSource& ) = delete;

    private:
        const IContentFileSource* _pPrevious;
    };
} // namespace

/**
 * @brief [SceneStreamingTest] 예산이 작으면 오브젝트 많은 셀 하나의 짓기가 엔티티 단위로 여러 프레임에 나뉘고, 다 짓기 전에는 드러나지 않으며, 결과(이름 · 자리)는 한 번에 지은 것과 같다
 */
SW_TEST_CASE( SceneStreamingTest, SmallBudgetSplitsCellAcrossFramesHiddenUntilDone )
{
    constexpr uint32              kEntityCount = 24;
    const ScopedFreeRunningPolicy policy;
    const float3                  source{ 5.0f, 0.0f, 5.0f }; // 셀 (0,0) 하나뿐이다
    const StreamingCellCoord      cell{ 0, 0 };

    Scene sliced{ "Sliced" };
    SW_REQUIRE_TRUE( sliced.instantiate( Util::makeDocument( 1u, 1, kEntityCount ) ) ); // 엔티티 하나가 1 us 보다 오래 걸린다 — 프레임마다 하나씩 짓는다
    SceneStreamingSubsystem& slicedStreaming = sliced.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( slicedStreaming, source );
    slicedStreaming.update( 0.0f );
    SW_EXPECT_FALSE( slicedStreaming.isCellLoaded( cell ) );
    SW_EXPECT_TRUE( Util::findByFileID( sliced, 1 ) == nullptr ); // 지었지만 드러나지 않았다(순회 밖)
    uint32 frameCount = 1;
    while ( slicedStreaming.isCellLoaded( cell ) == false && frameCount < kEntityCount + 2 )
    {
        slicedStreaming.update( 0.0f );
        frameCount += 1;
    }
    SW_REQUIRE_TRUE( slicedStreaming.isCellLoaded( cell ) );
    SW_EXPECT_TRUE( frameCount > 1 );

    Scene whole{ "Whole" };
    SW_REQUIRE_TRUE( whole.instantiate( Util::makeDocument( Util::kWholeBudgetMicroseconds, 1, kEntityCount ) ) );
    SceneStreamingSubsystem& wholeStreaming = whole.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( wholeStreaming, source );
    wholeStreaming.update( 0.0f );
    SW_REQUIRE_TRUE( wholeStreaming.isCellLoaded( cell ) );

    for ( uint64 fileID = 1; fileID <= kEntityCount; ++fileID )
    {
        const GameObject* pSliced = Util::findByFileID( sliced, fileID );
        const GameObject* pWhole  = Util::findByFileID( whole, fileID );
        SW_REQUIRE_TRUE( pSliced != nullptr && pWhole != nullptr );
        SW_EXPECT_TRUE( pSliced->getName() == pWhole->getName() );
        const float3 slicedPosition = Util::findPosition( sliced, fileID );
        const float3 wholePosition  = Util::findPosition( whole, fileID );
        SW_EXPECT_EQUAL( wholePosition._x, slicedPosition._x );
        SW_EXPECT_EQUAL( wholePosition._z, slicedPosition._z );
    }
}

/** @brief [SceneStreamingTest] 내렸다 다시 올린 셀은 플레이 중 바뀐 상태(자리)를 지키고, 플레이 중 지운 오브젝트는 다시 생기지 않는다 */
SW_TEST_CASE( SceneStreamingTest, ReloadedCellKeepsChangedState )
{
    const ScopedFreeRunningPolicy policy;
    Scene                         scene{ "Kept" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument( Util::kWholeBudgetMicroseconds ) ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    streaming.update( 0.0f );
    SW_REQUIRE_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );

    GameObject* pMoved = Util::findByFileID( scene, 1 );
    GameObject* pGone  = Util::findByFileID( scene, 2 );
    SW_REQUIRE_TRUE( pMoved != nullptr && pGone != nullptr && pMoved->getPrimarySceneComponent() != nullptr );
    pMoved->getPrimarySceneComponent()->setLocalPosition( float3{ 1.0f, 2.0f, 3.0f } );
    objects.destroyGameObject( pGone );

    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, streaming.getLoadedCellCount() );
    SW_EXPECT_TRUE( Util::findByFileID( scene, 1 ) == nullptr );
    objects.processDeferredDestruction(); // 내린 오브젝트의 이름을 비운다 — 다시 지은 오브젝트가 같은 이름을 받는다

    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    streaming.update( 0.0f );
    SW_REQUIRE_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    const float3 position = Util::findPosition( scene, 1 );
    SW_EXPECT_EQUAL( 1.0f, position._x );
    SW_EXPECT_EQUAL( 2.0f, position._y );
    SW_EXPECT_EQUAL( 3.0f, position._z );
    SW_EXPECT_TRUE( Util::findByFileID( scene, 2 ) == nullptr ); // 지운 것은 지운 채다
    SW_EXPECT_TRUE( Util::findByFileID( scene, 3 ) != nullptr ); // 다른 셀(반경 안의 (1,0))은 문서 그대로 다시 올랐다
}

/** @brief [SceneStreamingTest] 쿠킹본의 셀은 색인만 읽고 내용은 비동기로 읽는다 — 읽는 중에 내리면 읽기가 취소되고 짓지 않으며, 다시 다가가면 끝까지 읽어 짓는다 */
SW_TEST_CASE( SceneStreamingTest, UnloadWhileReadingCancelsWithoutLeak )
{
    constexpr const utf8*           kFileName = "streamcancel.scene.bin";
    const ScopedFreeRunningPolicy   policy;
    const test::ScopedScratchDomain domain;
    const optional<AbsolutePath>    binPath = FileUtil::requireAbsolutePath( domain.makeDiskPath( kFileName ) );
    SW_REQUIRE_TRUE( binPath.has_value() );
    SW_REQUIRE_TRUE( Util::makeDocument( Util::kWholeBudgetMicroseconds ).saveBinary( *binPath ) );

    SceneDocument cooked{};
    SW_REQUIRE_TRUE( cooked.loadBinary( test::ScopedScratchDomain::makeID( kFileName ) ) );
    SW_REQUIRE_EQUAL( size_t{ Util::kCellCount }, cooked._listStreamingCell.size() );
    SW_EXPECT_TRUE( cooked._listStreamingCell[0].hasFileContent() );
    SW_EXPECT_TRUE( cooked._listStreamingCell[0]._listSceneObjectNode.empty() ); // 색인만 상주한다

    LatchedContentFileSource      latched{ ContentFileSource::getCurrent(), kFileName };
    const ScopedContentFileSource bind{ latched };
    Scene                         scene{ "Cooked" };
    SW_REQUIRE_TRUE( scene.instantiate( cooked ) );
    SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();

    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } ); // (0,0) · (1,0) 이 반경 안이다
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( 2u, streaming.getLoadingCellCount() ); // 완료를 문이 붙들고 있다
    SW_EXPECT_EQUAL( 0u, streaming.getLoadedCellCount() );

    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, streaming.getLoadingCellCount() );
    latched.release();
    engine::getSubsystem<AsyncFileIO>().waitIdle(); // 취소한 읽기의 완료까지 끝났다 — 결과는 완료가 쥔 사본과 함께 사라졌다
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        streaming.update( 0.0f );
    }
    SW_EXPECT_EQUAL( 0u, streaming.getLoadedCellCount() );
    SW_EXPECT_TRUE( Util::findByFileID( scene, 1 ) == nullptr ); // 늦게 온 결과로 짓지 않았다

    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    SW_REQUIRE_TRUE( test::waitUntil( [&streaming]()
    {
        streaming.update( 0.0f );
        return streaming.getLoadedCellCount() == 2u;
    } ) );
    const float3 position = Util::findPosition( scene, 1 );
    SW_EXPECT_EQUAL( 2.0f, position._x );
    SW_EXPECT_EQUAL( 5.0f, position._z );
}

/**
 * @brief [SceneStreamingTest] 셀 A 의 오브젝트가 셀 B 자리로 가면 — B 가 올라 있으면 A 를 내려도 그대로 살아 B 에 들고, B 가 내려가 있으면 B 의 떠 둔 상태에 합쳐 B 가 오를 때 선다(어느 쪽이든 하나뿐)
 * @details 문서: 셀 (0,0) · (1,0) · (2,0), 한 변 10 m, 반경 6 m, 여유 0. 셀 x 의 엔티티는 x*10+2 · x*10+8 자리(파일 id 1 부터).
 */
SW_TEST_CASE( SceneStreamingTest, MovedObjectFollowsTheCellOfItsPosition )
{
    const ScopedFreeRunningPolicy policy;
    Scene                         scene{ "Moved" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument( Util::kWholeBudgetMicroseconds ) ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } ); // (0,0) · (1,0)
    streaming.update( 0.0f );
    SW_REQUIRE_EQUAL( 2u, streaming.getLoadedCellCount() );

    // (0,0) 의 Crate1 이 올라 있는 (1,0) 자리로 걷는다 — (0,0) 을 내려도 같은 오브젝트가 (1,0) 에 들어 산다.
    GameObject* pMover = Util::findByFileID( scene, 1 );
    SW_REQUIRE_TRUE( pMover != nullptr && pMover->getPrimarySceneComponent() != nullptr );
    const uint64 moverID = pMover->getObjectID();
    pMover->getPrimarySceneComponent()->setLocalPosition( float3{ 15.0f, 0.0f, 5.0f } );
    Util::setSource( streaming, float3{ 25.0f, 0.0f, 5.0f } ); // (0,0) 은 15 m 밖 — 내린다. (1,0) · (2,0) 은 반경 안
    streaming.update( 0.0f );
    SW_EXPECT_FALSE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    const GameObject* pAfter = Util::findByFileID( scene, 1 );
    SW_REQUIRE_TRUE( pAfter != nullptr );
    SW_EXPECT_EQUAL( moverID, pAfter->getObjectID() ); // 다시 지은 것이 아니라 그 오브젝트다
    SW_EXPECT_TRUE( streaming.isCellObject( moverID ) );
    SW_EXPECT_TRUE( Util::findByFileID( scene, 2 ) == nullptr ); // 남은 (0,0) 엔티티는 셀과 함께 내렸다

    // (1,0) 의 Crate3 이 내려간 (0,0) 자리로 간다 — (1,0) 을 내릴 때 (0,0) 의 떠 둔 상태에 합친다.
    GameObject* pCrate3 = Util::findByFileID( scene, 3 );
    SW_REQUIRE_TRUE( pCrate3 != nullptr && pCrate3->getPrimarySceneComponent() != nullptr );
    pCrate3->getPrimarySceneComponent()->setLocalPosition( float3{ 4.0f, 0.0f, 6.0f } );
    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, streaming.getLoadedCellCount() );
    SW_EXPECT_EQUAL( 0u, Util::countNamed( objects, "Crate1" ) );
    SW_EXPECT_EQUAL( 0u, Util::countNamed( objects, "Crate3" ) );

    // 두 셀을 함께 올린다 — 옮긴 둘은 새 자리에 하나씩만 선다.
    const StreamingSource arrSource[] = {
        StreamingSource{ float3{ 5.0f, 0.0f, 5.0f }, streaming.getLoadingRadius(), StreamingSourcePriority::Normal},
        StreamingSource{float3{ 15.0f, 0.0f, 5.0f }, streaming.getLoadingRadius(), StreamingSourcePriority::Normal}
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( arrSource, 2 ) ) );
    streaming.update( 0.0f );
    SW_REQUIRE_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) && streaming.isCellLoaded( StreamingCellCoord{ 1, 0 } ) );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Crate1" ) );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Crate3" ) );
    SW_EXPECT_EQUAL( 15.0f, Util::findPosition( scene, 1 )._x );
    SW_EXPECT_EQUAL( 4.0f, Util::findPosition( scene, 3 )._x );
    SW_EXPECT_EQUAL( 6.0f, Util::findPosition( scene, 3 )._z );
}

/**
 * @brief [SceneStreamingTest] 셀에 들지 않은 런타임 스폰은 반경 밖이 되면 자리의 셀에 들어 함께 내리고 그 셀이 오를 때 다시 선다 — 늘 올라 있음 표시가 있으면 셀에 들지 않는다
 */
SW_TEST_CASE( SceneStreamingTest, RuntimeSpawnJoinsTheCellOfItsPosition )
{
    const ScopedFreeRunningPolicy policy;
    Scene                         scene{ "Spawned" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument( Util::kWholeBudgetMicroseconds ) ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    streaming.update( 0.0f );
    SW_REQUIRE_EQUAL( 2u, streaming.getLoadedCellCount() );

    GameObject* pSpawned  = objects.createGameObject( hashed_string( "Spawned" ) );
    GameObject* pResident = objects.createGameObject( hashed_string( "Resident" ) );
    SW_REQUIRE_TRUE( pSpawned != nullptr && pResident != nullptr );
    SceneComponent* pSpawnedRoot  = pSpawned->addComponent<SceneComponent>();
    SceneComponent* pResidentRoot = pResident->addComponent<SceneComponent>();
    SW_REQUIRE_TRUE( pSpawnedRoot != nullptr && pResidentRoot != nullptr );
    pSpawnedRoot->setLocalPosition( float3{ 15.0f, 0.0f, 5.0f } );
    pResidentRoot->setLocalPosition( float3{ 15.0f, 0.0f, 5.0f } );
    pResident->setAlwaysLoaded( true );
    GameObject* pReplicated = objects.createGameObject( hashed_string( "Replicated" ) );
    SW_REQUIRE_NOT_NULL( pReplicated );
    SceneComponent* pReplicatedRoot = pReplicated->addComponent<SceneComponent>();
    SW_REQUIRE_NOT_NULL( pReplicatedRoot );
    pReplicatedRoot->setLocalPosition( float3{ 15.0f, 0.0f, 5.0f } );
    objects.mergePendingAdds();
    SW_REQUIRE_TRUE( objects.getSubsystem<NetObjectTable>().registerSpawned( *pReplicated ) != 0 ); // 서버가 복제하는 스폰
    SW_EXPECT_FALSE( streaming.isCellObject( pSpawned->getObjectID() ) );                           // 반경 안에서는 셀 밖이다

    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } );
    streaming.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, Util::countNamed( objects, "Spawned" ) );    // (1,0) 에 들어 함께 내렸다
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Resident" ) );   // 늘 올라 있음 표시는 셀에 들지 않는다
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Replicated" ) ); // 복제 스폰은 셀에 들지 않는다 — 번호가 클라이언트와 이어져 있다

    Util::setSource( streaming, float3{ 15.0f, 0.0f, 5.0f } );
    streaming.update( 0.0f );
    SW_REQUIRE_EQUAL( 1u, Util::countNamed( objects, "Spawned" ) );
    const GameObject* pBack = objects.findGameObjectByName( hashed_string( "Spawned" ) );
    SW_REQUIRE_TRUE( pBack != nullptr && pBack->getPrimarySceneComponent() != nullptr );
    SW_EXPECT_EQUAL( 15.0f, pBack->getPrimarySceneComponent()->getLocalPosition()._x );
    SW_EXPECT_TRUE( streaming.isCellObject( pBack->getObjectID() ) );
    // 셀에 들 때 파일 id 를 받았다 — 셀 엔티티(1..6) 위의 새 id 다.
    SW_EXPECT_TRUE( scene.findFileID( pBack->getObjectID() ) > 6 );
}

/**
 * @brief [SceneStreamingTest] 쿠킹본 머리는 셀 엔티티의 가장 큰 파일 id 를 싣고, 셀을 하나도 읽지 않은 씬이 새 오브젝트에 주는 파일 id 는 그 위다 — 내려간 셀의 엔티티와 겹치지 않는다
 */
SW_TEST_CASE( SceneStreamingTest, NewFileIDSkipsUnreadCellEntities )
{
    constexpr const utf8*           kFileName = "streammaxid.scene.bin";
    const ScopedFreeRunningPolicy   policy;
    const test::ScopedScratchDomain domain;
    const optional<AbsolutePath>    binPath = FileUtil::requireAbsolutePath( domain.makeDiskPath( kFileName ) );
    SW_REQUIRE_TRUE( binPath.has_value() );
    SW_REQUIRE_TRUE( Util::makeDocument( Util::kWholeBudgetMicroseconds ).saveBinary( *binPath ) );

    SceneDocument cooked{};
    SW_REQUIRE_TRUE( cooked.loadBinary( test::ScopedScratchDomain::makeID( kFileName ) ) );
    SW_EXPECT_EQUAL( uint64{ Util::kCellCount * Util::kEntitiesPerCell }, cooked._maxCellFileID ); // 셀 셋 × 엔티티 둘 — 파일 id 1..6

    Scene scene{ "MaxID" };
    SW_REQUIRE_TRUE( scene.instantiate( cooked ) ); // 원점이 없다 — 셀을 읽지 않는다
    GameObject* pFresh = scene.getObjectManager()->createGameObject( hashed_string( "Fresh" ) );
    SW_REQUIRE_NOT_NULL( pFresh );
    scene.getObjectManager()->mergePendingAdds();
    SW_EXPECT_EQUAL( uint64{ Util::kCellCount * Util::kEntitiesPerCell + 1 }, scene.getOrAssignFileID( pFresh->getObjectID() ) );
}

/** @brief [SceneStreamingTest] 내렸다 다시 올린 셀은 런타임 칸(`RuntimeState`)까지 지킨다 — 떠 둔 상태의 목적이 스냅숏이다(저작 목적이면 처음 값으로 돌아온다) */
SW_TEST_CASE( SceneStreamingTest, ReloadedCellKeepsRuntimeState )
{
    const ScopedFreeRunningPolicy policy;
    Scene                         scene{ "Runtime" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument( Util::kWholeBudgetMicroseconds ) ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    streaming.update( 0.0f );

    GameObject* pCrate = Util::findByFileID( scene, 1 );
    SW_REQUIRE_TRUE( pCrate != nullptr );
    StateHashTestComponent* pState = pCrate->addComponent<StateHashTestComponent>();
    SW_REQUIRE_NOT_NULL( pState );
    pState->_steps = 7;
    pState->_phase = "opened";

    Util::setSource( streaming, float3{ 500.0f, 0.0f, 500.0f } );
    streaming.update( 0.0f );
    objects.processDeferredDestruction();
    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    streaming.update( 0.0f );

    const GameObject* pBack = Util::findByFileID( scene, 1 );
    SW_REQUIRE_TRUE( pBack != nullptr );
    const StateHashTestComponent* pBackState = pBack->getComponent<StateHashTestComponent>();
    SW_REQUIRE_NOT_NULL( pBackState );
    SW_EXPECT_EQUAL( 7, pBackState->_steps );
    SW_EXPECT_TRUE( pBackState->_phase == "opened" );
}

/**
 * @brief [SceneStreamingTest] 세이브 몫은 내려간 셀의 떠 둔 상태와 올라 있는 셀의 지금 상태를 싣고, 비운 매니저(복원) 위에서 셀이 그 상태로 다시 선다 — 저장 뒤에 바꾼 것은 덮인다
 * @details 복원 흐름(`GameStateSnapshot::readEnvelope`)과 같다: 매니저를 비우면 조각이 문서 내용으로 돌아가되 월드는 남고, 세이브의 상태를 얹은 뒤 다음 갱신이 짓는다.
 */
SW_TEST_CASE( SceneStreamingTest, SaveStateRestoresCellsOverAClearedManager )
{
    const ScopedFreeRunningPolicy policy;
    Scene                         scene{ "Saved" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument( Util::kWholeBudgetMicroseconds ) ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    Util::setSource( streaming, float3{ 5.0f, 0.0f, 5.0f } );
    streaming.update( 0.0f );

    GameObject* pCrate1 = Util::findByFileID( scene, 1 );
    GameObject* pCrate3 = Util::findByFileID( scene, 3 );
    SW_REQUIRE_TRUE( pCrate1 != nullptr && pCrate3 != nullptr );
    pCrate1->getPrimarySceneComponent()->setLocalPosition( float3{ 3.0f, 0.0f, 7.0f } );
    pCrate3->getPrimarySceneComponent()->setLocalPosition( float3{ 13.0f, 0.0f, 7.0f } );
    Util::setSource( streaming, float3{ 25.0f, 0.0f, 5.0f } ); // (0,0) 은 떠 두고 내린다 · (1,0) 은 올라 있다
    streaming.update( 0.0f );
    SW_REQUIRE_FALSE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) );
    SW_REQUIRE_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 1, 0 } ) );

    vector<uint8> bytes;
    streaming.writeSaveState( bytes );
    SceneStreamingSubsystem::SaveState state;
    SW_REQUIRE_TRUE( SceneStreamingSubsystem::readSaveState( bytes.data(), bytes.size(), state ) );
    SW_EXPECT_TRUE( state._listCell.empty() == false );

    // 저장 뒤에 바꾼다 — 복원이 덮는다.
    pCrate3 = Util::findByFileID( scene, 3 );
    SW_REQUIRE_TRUE( pCrate3 != nullptr );
    pCrate3->getPrimarySceneComponent()->setLocalPosition( float3{ 18.0f, 0.0f, 7.0f } );

    objects.clear();
    scene.rebindRestoredFileIDs( {} );
    SW_EXPECT_TRUE( streaming.isStreaming() ); // 월드는 남는다
    SW_EXPECT_EQUAL( 0u, streaming.getLoadedCellCount() );
    streaming.applySaveState( std::move( state ) );

    const StreamingSource arrSource[] = {
        StreamingSource{ float3{ 5.0f, 0.0f, 5.0f }, streaming.getLoadingRadius(), StreamingSourcePriority::Normal},
        StreamingSource{float3{ 15.0f, 0.0f, 5.0f }, streaming.getLoadingRadius(), StreamingSourcePriority::Normal}
    };
    SW_REQUIRE_TRUE( streaming.setExternalSources( span<const StreamingSource>( arrSource, 2 ) ) );
    streaming.update( 0.0f );
    SW_REQUIRE_TRUE( streaming.isCellLoaded( StreamingCellCoord{ 0, 0 } ) && streaming.isCellLoaded( StreamingCellCoord{ 1, 0 } ) );
    SW_EXPECT_EQUAL( 3.0f, Util::findPosition( scene, 1 )._x ); // 내려가 있던 셀의 떠 둔 상태
    SW_EXPECT_EQUAL( 7.0f, Util::findPosition( scene, 1 )._z );
    SW_EXPECT_EQUAL( 13.0f, Util::findPosition( scene, 3 )._x ); // 올라 있던 셀은 저장 때 뜬 상태(뒤에 바꾼 18 이 아니다)
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Crate1" ) );
    SW_EXPECT_EQUAL( 1u, Util::countNamed( objects, "Crate3" ) );
}
