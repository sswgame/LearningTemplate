// ScenePersistentObjectsTest — 씬 전환을 넘는 오브젝트(`SceneManager::markPersistent`): PROPERTY 밖 상태와 절대 자리를 옮기고 파생 자식은 옮기지 않으며,
// 스트리밍 셀이 내려가도 남고, 게임이 끝나면 다음 게임으로 옮기지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/ComponentStateCodec.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

using namespace sw;

namespace
{
    /** @brief 테스트 동안 `SceneComponent` 에 상태 코덱을 겁니다 — 뜬 상태는 값 하나이고, 되돌린 값을 센다. */
    class ScopedSceneComponentCodec
    {
    public:
        static constexpr uint8 kStateByte = 42;

        static inline uint32 s_restoredCount = 0;
        static inline uint8  s_restoredByte  = 0;

        ScopedSceneComponentCodec()
            : _codec{ "SceneComponent", &SceneComponent::StaticType, &ScopedSceneComponentCodec::write, &ScopedSceneComponentCodec::restore }
        {
            s_restoredCount = 0;
            s_restoredByte  = 0;
            SW_EXPECT_TRUE( ComponentStateCodec::add( &_codec ) );
        }
        ~ScopedSceneComponentCodec() { ComponentStateCodec::remove( &_codec ); }
        ScopedSceneComponentCodec( const ScopedSceneComponentCodec& )            = delete;
        ScopedSceneComponentCodec& operator=( const ScopedSceneComponentCodec& ) = delete;

    private:
        static void write( Component& /*component*/, vector<uint8>& outBytes ) { outBytes.assign( 1, kStateByte ); }
        static void restore( Component& /*component*/, vector<uint8>&& bytes )
        {
            ++s_restoredCount;
            s_restoredByte = bytes.empty() ? uint8{ 0 } : bytes[0];
        }

    private:
        ComponentStateCodec _codec;
    };

    struct ScenePersistentObjectsTestUtil
    {
        /** @brief 셀 (0,0) 에 엔티티 하나(파일 id 1, 자리 2,0,5)인 스트리밍 문서입니다. 셀 한 변 10 m, 로딩 반경 6 m. */
        static SceneDocument makeStreamedDocument()
        {
            SceneDocument doc{};
            doc._name                                        = "Streamed";
            doc._sceneSettings._bStreaming                   = true;
            doc._sceneSettings._cellGrid._cellSize           = 10.0f;
            doc._sceneSettings._cellGrid._loadingRadius      = 6.0f;
            doc._sceneSettings._unloadMargin                 = 0.0f;
            doc._sceneSettings._maxCellLoadsPerFrame         = 4;
            doc._sceneSettings._addToWorldBudgetMicroseconds = 1'000'000'000u;
            doc._bValid                                      = true;
            SceneDocument::SceneObjectNode node{};
            node._fileID      = 1;
            node._name        = "Music";
            node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"Music\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"2,0,5\" /></_listComponent></GameObject>";
            SceneDocument::StreamingCell cell{};
            cell._cell = StreamingCellCoord{ 0, 0 };
            cell._listSceneObjectNode.push_back( std::move( node ) );
            doc._listStreamingCell.push_back( std::move( cell ) );
            return doc;
        }

        static void streamAround( Scene& scene, const float3& position )
        {
            SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
            const StreamingSource    source{ position, streaming.getLoadingRadius(), StreamingSourcePriority::Normal };
            SW_EXPECT_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
            streaming.update( 0.0f );
            scene.getObjectManager()->mergePendingAdds();
            scene.getObjectManager()->processDeferredDestruction();
        }
    };
    using Util = ScenePersistentObjectsTestUtil;
} // namespace

/** @brief [ScenePersistentObjectsTest] 옮겨 심은 오브젝트는 같은 id · 같은 절대 자리로 서고 PROPERTY 밖 상태를 돌려받으며, 파생 자식은 옮기지 않는다 */
SW_TEST_CASE( ScenePersistentObjectsTest, CarryKeepsStateAndAbsolutePlace )
{
    const ScopedSceneComponentCodec codec;
    SceneManager                    sceneManager;
    Scene* const                    pFrom = sceneManager.createEmptyActiveScene( "From" );
    SW_REQUIRE_TRUE( pFrom != nullptr );
    GameObject* const     pRoot      = pFrom->getObjectManager()->createGameObject( hashed_string( "Keeper" ) );
    SceneComponent* const pRootScene = pRoot->addComponent<SceneComponent>();
    GameObject* const     pPart      = pFrom->getObjectManager()->createGameObject( hashed_string( "KeeperPart" ) );
    SceneComponent* const pPartScene = pPart->addComponent<SceneComponent>();
    SW_REQUIRE_TRUE( pPartScene->attachToComponent( pRootScene ) );
    pPart->setTransient( true );
    pRootScene->setLocalPosition( float3{ 10.0f, 0.0f, 3.0f } );
    pFrom->getObjectManager()->mergePendingAdds();
    pFrom->getObjectManager()->flushSceneTransforms();
    SW_REQUIRE_TRUE( pFrom->shiftWorldOrigin( double3{ 1000.0, 0.0, 0.0 } ) );
    SW_EXPECT_NEAR( -990.0f, pRootScene->getLocalPosition()._x, 0.01f );

    sceneManager.setWorldPlaying( true );
    sceneManager.markPersistent( pRoot );
    const uint64 rootID = pRoot->getObjectID();
    const uint64 partID = pPart->getObjectID();
    Scene* const pTo    = sceneManager.createEmptyActiveScene( "To" );
    SW_REQUIRE_TRUE( pTo != nullptr );

    const GameObject* const pCarried = pTo->getObjectManager()->findGameObjectByID( rootID );
    SW_REQUIRE_TRUE( pCarried != nullptr );
    SW_EXPECT_NEAR( 10.0f, pCarried->getPrimarySceneComponent()->getLocalPosition()._x, 0.01f ); // 들어온 씬의 원점은 0 이다
    SW_EXPECT_EQUAL( 1u, ScopedSceneComponentCodec::s_restoredCount );
    SW_EXPECT_EQUAL( uint32{ ScopedSceneComponentCodec::kStateByte }, uint32{ ScopedSceneComponentCodec::s_restoredByte } );
    SW_EXPECT_TRUE( pTo->getObjectManager()->findGameObjectByID( partID ) == nullptr );
    sceneManager.setWorldPlaying( false );
}

/** @brief [ScenePersistentObjectsTest] 셀에 든 오브젝트를 표시하면 셀이 내려가도 같은 오브젝트로 남는다 */
SW_TEST_CASE( ScenePersistentObjectsTest, MarkedObjectStaysWhenItsCellUnloads )
{
    SceneManager sceneManager;
    Scene* const pScene = sceneManager.createEmptyActiveScene( "Streamed" );
    SW_REQUIRE_TRUE( pScene != nullptr && pScene->instantiate( Util::makeStreamedDocument() ) );
    Util::streamAround( *pScene, float3{ 2.0f, 0.0f, 5.0f } );
    GameObject* const pMusic = pScene->getObjectManager()->findGameObjectByName( hashed_string( "Music" ) );
    SW_REQUIRE_TRUE( pMusic != nullptr );
    const uint64 musicID = pMusic->getObjectID();

    sceneManager.setWorldPlaying( true );
    sceneManager.markPersistent( pMusic );
    Util::streamAround( *pScene, float3{ 500.0f, 0.0f, 5.0f } );
    SW_EXPECT_EQUAL( 0u, pScene->getObjectManager()->getSubsystem<SceneStreamingSubsystem>().getLoadedCellCount() );
    const GameObject* pLeft = pScene->getObjectManager()->findGameObjectByID( musicID );
    SW_EXPECT_TRUE( pLeft != nullptr && pLeft->isPendingDestroy() == false );

    // 다시 오른 셀이 같은 엔티티를 또 세우지 않는다.
    Util::streamAround( *pScene, float3{ 2.0f, 0.0f, 5.0f } );
    vector<GameObject*> listMusic;
    pScene->getObjectManager()->findGameObjectsByBaseName( "Music", listMusic );
    SW_EXPECT_EQUAL( size_t{ 1 }, listMusic.size() );
    sceneManager.setWorldPlaying( false );
}

/** @brief [ScenePersistentObjectsTest] 하던 게임이 끝나면(타이틀로) 표시를 잊는다 — 다음 씬으로 옮기지 않는다 */
SW_TEST_CASE( ScenePersistentObjectsTest, EndedGameInstanceForgetsTheMarks )
{
    SceneManager sceneManager;
    Scene* const pFrom = sceneManager.createEmptyActiveScene( "From" );
    SW_REQUIRE_TRUE( pFrom != nullptr );
    GameObject* const pRoot = pFrom->getObjectManager()->createGameObject( hashed_string( "Keeper" ) );
    sceneManager.setWorldPlaying( true );
    sceneManager.markPersistent( pRoot );
    SW_EXPECT_TRUE( sceneManager.isPersistent( pRoot ) );
    const uint64 rootID = pRoot->getObjectID();

    sceneManager.resetGameInstanceState( GameInstanceBoundary::Ended );
    Scene* const pTo = sceneManager.createEmptyActiveScene( "To" );
    SW_REQUIRE_TRUE( pTo != nullptr );
    SW_EXPECT_TRUE( pTo->getObjectManager()->findGameObjectByID( rootID ) == nullptr );
    sceneManager.setWorldPlaying( false );
}
