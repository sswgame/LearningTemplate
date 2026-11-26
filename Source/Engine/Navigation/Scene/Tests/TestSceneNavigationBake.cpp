/**
 * @file TestSceneNavigationBake.cpp
 * @brief 바닥이 있는 씬에서 다시 베이크(`SceneNavigation::rebakeAllNavMeshes`, 에디터 `navigation.bake` 가 부른다)가 내비메시를 세우는지 봅니다. 장치 없이 돈다.
 */
#include "pch.h"

#include "Core/Delegate/Subscription.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Navigation/INavMeshTileStore.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

using namespace sw;

namespace
{
    /** @brief 원점에 놓인 16 m × 16 m 바닥 상자 하나를 냅니다. */
    class FloorGeometrySource final : public INavGeometrySource
    {
    public:
        explicit FloorGeometrySource( const GameObject* pOwner )
            : _pOwner{ pOwner }
        {
        }

        const GameObject* getNavGeometryOwner() const override { return _pOwner; }
        bool              shouldOverrideOwnerGeometry() const override { return true; }
        void              collectNavGeometry( NavMeshGeometry& outGeometry, uint8 area ) const override { outGeometry.addBox( float3{ 8.0f, 0.5f, 8.0f }, float4x4::makeTranslation( float3{} ), area ); }

    private:
        const GameObject* _pOwner;
    };

    struct SceneNavigationBakeTestUtil
    {
        static SceneDocument makeDocument()
        {
            SceneDocument doc{};
            doc._name   = "NavBake";
            doc._bValid = true;
            SceneDocument::SceneObjectNode node{};
            node._fileID      = 1;
            node._name        = "Ground";
            node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"Ground\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"0,0,0\" /></_listComponent></GameObject>";
            doc._listSceneObjectNode.push_back( std::move( node ) );
            return doc;
        }

        static NavMeshSettings makeSettings()
        {
            NavAgentTypeDef agentType;
            agentType._name = hashed_string{ "Walker" };
            NavAreaDef area;
            area._name = hashed_string{ "Walkable" };
            NavMeshSettings settings;
            settings._listAgentType.push_back( agentType );
            settings._listArea.push_back( area );
            settings._defaultAgentType = agentType._name;
            return settings;
        }
    };
    using Util = SceneNavigationBakeTestUtil;
} // namespace

SW_TEST_CASE( SceneNavigationBakeTest, RebakeOnFloorSceneBuildsNavMesh )
{
    const NavMeshSettings settings  = Util::makeSettings();
    const hashed_string   agentName = settings._listAgentType[0]._name;

    Scene scene{ "NavBake" };
    SW_REQUIRE_TRUE( scene.instantiate( Util::makeDocument() ) );
    GameObjectManager&  objects    = *scene.getObjectManager();
    SceneNavigation&    navigation = objects.getSubsystem<SceneNavigation>();
    FloorGeometrySource floor{ objects.findGameObjectByName( hashed_string{ "Ground" } ) };
    const Subscription  floorSubscription = navigation.registerGeometrySource( &floor );
    navigation.setSettings( settings );

    const INavMeshTileStore* pFirst = navigation.ensureNavMesh( agentName );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_EXPECT_TRUE( pFirst->getPolygonCount() > 0 );

    // 에디터 베이크 버튼과 같은 길 — 마련했던 종류를 버리고 다시 세운다.
    SW_EXPECT_EQUAL( 1u, navigation.rebakeAllNavMeshes() );
    navigation.flushTileBakes();
    const INavMeshTileStore* pRebaked = navigation.findNavMesh( agentName );
    SW_REQUIRE_NOT_NULL( pRebaked );
    SW_EXPECT_TRUE( pRebaked->getPolygonCount() > 0 );
    vector<hashed_string> listAgentType;
    navigation.collectBakedAgentTypes( listAgentType );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listAgentType.size() );
    SW_EXPECT_TRUE( listAgentType[0] == agentName );
}
