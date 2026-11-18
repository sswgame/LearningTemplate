// SceneStreamingTest — 씬 스트리밍(감사 A-13): 시간 예산으로 나눈 짓기가 한 번에 지은 것과 같고, 내렸다 올린 셀이 바뀐 상태를 지키며, 읽는 중에 내리면 취소되고 새지 않는다.
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
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
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
        static SceneDocument::SceneObjectNode makeNode( uint64 fileID, const string& name, const string& position )
        {
            SceneDocument::SceneObjectNode node{};
            node._fileID      = fileID;
            node._name        = name;
            node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"" + name + "\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"" + position +
                                "\" /></_listComponent></GameObject>";
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
                    const int32  offset   = entityIndex == 0 ? 2 : ( entityIndex == 1 ? 8 : 3 + static_cast<int32>( entityIndex % 5 ) );
                    const string position = sw::to_string( static_cast<int64>( x * 10 + offset ) ) + ",0,5";
                    cell._listSceneObjectNode.push_back( makeNode( fileID, "Crate" + sw::to_string( static_cast<int64>( fileID ) ), position ) );
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

        static void setSource( SceneStreamingSubsystem& streaming, const float3& source ) { streaming.setExternalSources( span<const float3>( &source, 1 ) ); }
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
    objects.destroyGameObject( pGone, true );

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
