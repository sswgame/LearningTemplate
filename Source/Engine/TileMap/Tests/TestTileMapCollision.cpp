// TileMapCollisionTest — 타일맵 충돌(감사 P0-14): 청크마다 정적 바디 하나, 칠한 칸의 청크만 다시 만들기, 오너 변환 따라가기. 디바이스 없음(nogpu).
// 유니티 TilemapCollider2D(바뀐 타일만 늦게 반영) · Godot TileMapLayer(사분면 바디가 노드 변환을 따른다)와 같은 계약을 본다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Physics/Collision/PhysicsContact.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/TileMap/Component/TileMapRendererComponent.h"
#include "Engine/TileMap/TileMapXML.h"
#include "Engine/TileMap/TileSetAsset.h"

using namespace sw;

namespace
{
    struct TestTileMapCollisionInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 단단한 벽 하나만 든 타일셋(칸 한 변 1 미터). */
        static constexpr const utf8* kTileSetXML = "<TileSet atlas=\"engine/textures/test/quadrants.dds\" columns=\"4\" rows=\"4\" tileSize=\"1\">"
                                                   "  <Tile name=\"wall\" cell=\"0\" solid=\"true\"/>"
                                                   "</TileSet>";

        /**
         * @brief 장면 컴포넌트를 가진 오브젝트 하나에 타일맵을 붙이고 시작한다. @p listWallRow 의 행은 모두 벽으로 칠한다.
         * @details 요구 검사(`SW_REQUIRE_*`)는 케이스 본문에서만 쓸 수 있어, 준비가 끝났는지는 `_pScene` 으로 알린다(케이스가 확인한다).
         */
        struct Rig
        {
            GameObjectManager         _manager;
            GameObject*               _pMap{ nullptr };
            SceneComponent*           _pRoot{ nullptr };
            TileMapRendererComponent* _pTiles{ nullptr };
            IPhysicsScene2D*          _pScene{ nullptr }; ///< 준비가 끝났으면 바디가 든 2D 물리 씬

            Rig( int32 width, int32 height, const vector<int32>& listWallRow )
            {
                shared_ptr<TileSetAsset> pTileSet = make_shared<TileSetAsset>();
                TileMapXMLData           map;
                if ( pTileSet->loadFromXMLText( kTileSetXML, "<test>" ) == false || map.resetTiles( width, height ) == false )
                    return;
                map._tileSetPath = "<test>";
                for ( const int32 row : listWallRow )
                {
                    for ( int32 x = 0; x < width; ++x )
                    {
                        (void)map.setTileBrush( x, row, "wall" ); // 칸은 맵 안이다
                    }
                }
                _pMap = _manager.createGameObject( hashed_string( "Tiles" ) );
                if ( _pMap == nullptr )
                    return;
                _pRoot  = _pMap->addComponent<SceneComponent>();
                _pTiles = _pMap->addComponent<TileMapRendererComponent>();
                if ( _pRoot == nullptr || _pTiles == nullptr )
                    return;
                _pTiles->setTileMapData( map, std::move( pTileSet ) );
                _manager.beginPlay();
                _pScene = _manager.getSubsystem<ScenePhysics>().findScene2D();
            }

            /** @brief 맵 원점 기준 자리(칸 단위, y 는 위가 +)를 지금 오너 월드 행렬로 옮긴다. */
            float2 toWorld( float32 localX, float32 localY ) const
            {
                const float3 world = float3::transform( float3{ localX, localY, 0.0f }, _pRoot->getWorldMatrix() );
                return float2{ world._x, world._y };
            }

            /** @brief @p world 에 작은 상자를 두면 겹치는 바디들. */
            vector<PhysicsBodyHandle> overlapAt( const float2& world )
            {
                PhysicsShapeDesc2D probe;
                probe._type        = PhysicsShapeType2D::Box;
                probe._halfExtents = float2{ 0.05f, 0.05f };
                vector<PhysicsBodyHandle> listBody;
                (void)_pScene->overlapShape( probe, world, 0.0f, PhysicsQueryFilter{}, listBody );
                return listBody;
            }

            void update( uint32 frameCount )
            {
                for ( uint32 frame = 0; frame < frameCount; ++frame )
                {
                    _manager.update( kFrame );
                }
            }
        };

        static bool contains( const vector<PhysicsBodyHandle>& listBody, PhysicsBodyHandle body )
        {
            for ( const PhysicsBodyHandle& candidate : listBody )
            {
                if ( candidate == body )
                    return true;
            }
            return false;
        }

        static bool isBetween( const PhysicsContactEvent2D& event, PhysicsBodyHandle bodyA, PhysicsBodyHandle bodyB )
        {
            return ( event._bodyA == bodyA && event._bodyB == bodyB ) || ( event._bodyA == bodyB && event._bodyB == bodyA );
        }
    };
} // namespace

/**
 * @brief [TileMapCollisionTest] 오너를 옮기고 돌리면 청크 바디가 그 자리 · 회전을 따른다 — 만들 때의 월드 자리에 남지 않고, 회전한 맵이 AABB 상자가 되지 않는다
 * @details 맵 4 × 1 벽 한 줄. 45° 돌리면 띠의 AABB 는 띠 위쪽 빈 자리(로컬 (1.5, 1.0))까지 덮는다 — 거기에 바디가 없어야 셰이프가 회전한 상자다.
 */
SW_TEST_CASE( TileMapCollisionTest, BodiesFollowOwnerTransform )
{
    using Internal = TestTileMapCollisionInternal;
    Internal::Rig rig( 4, 1, { 0 } );
    SW_REQUIRE_NOT_NULL( rig._pScene );
    SW_REQUIRE_EQUAL( size_t{ 1 }, rig._pTiles->getChunkBodies().size() );
    const PhysicsBodyHandle body = rig._pTiles->getChunkBodies()[0];
    SW_REQUIRE_TRUE( rig._pScene->isBodyValid( body ) );
    const float2 oldEnd = rig.toWorld( 3.5f, -0.5f );
    SW_EXPECT_TRUE( Internal::contains( rig.overlapAt( oldEnd ), body ) );

    rig._pRoot->setLocalPosition( float3{ 10.0f, 5.0f, 0.0f } );
    rig._pRoot->setLocalRotation( float3{ 0.0f, 0.0f, MathUtil::kPi * 0.25f } );
    rig.update( 1 );

    SW_EXPECT_TRUE( body == rig._pTiles->getChunkBodies()[0] ); // 옮기기만 한다 — 다시 만들지 않는다
    float2  position{};
    float32 rotation{ 0.0f };
    SW_REQUIRE_TRUE( rig._pScene->getBodyTransform( body, position, rotation ) );
    SW_EXPECT_NEAR( 10.0f, position._x, 1e-4f );
    SW_EXPECT_NEAR( 5.0f, position._y, 1e-4f );
    SW_EXPECT_NEAR( MathUtil::kPi * 0.25f, MathUtil::abs( rotation ), 1e-4f );

    SW_EXPECT_FALSE( Internal::contains( rig.overlapAt( oldEnd ), body ) );                    // 옛 자리에 남지 않았다
    SW_EXPECT_TRUE( Internal::contains( rig.overlapAt( rig.toWorld( 0.5f, -0.5f ) ), body ) ); // 첫 칸
    SW_EXPECT_TRUE( Internal::contains( rig.overlapAt( rig.toWorld( 3.5f, -0.5f ) ), body ) ); // 끝 칸
    SW_EXPECT_FALSE( Internal::contains( rig.overlapAt( rig.toWorld( 1.5f, 1.0f ) ), body ) ); // AABB 안 · 띠 밖
}

/**
 * @brief [TileMapCollisionTest] 칸 하나를 칠하면 그 청크의 바디만 다음 틱에 한 번 다시 만들고, 다른 청크의 바디는 핸들까지 그대로다
 * @details 맵 64 × 1(청크 32 칸 둘) 벽 한 줄. 청크 0 의 칸 둘을 같은 프레임에 지우면 청크 0 만 새 핸들이고 병합 사각형은 셋이다.
 */
SW_TEST_CASE( TileMapCollisionTest, SetTileRebuildsOnlyItsChunk )
{
    using Internal = TestTileMapCollisionInternal;
    Internal::Rig rig( 64, 1, { 0 } );
    SW_REQUIRE_NOT_NULL( rig._pScene );
    SW_REQUIRE_EQUAL( size_t{ 2 }, rig._pTiles->getChunkBodies().size() );
    const PhysicsBodyHandle oldChunk0 = rig._pTiles->getChunkBodies()[0];
    const PhysicsBodyHandle oldChunk1 = rig._pTiles->getChunkBodies()[1];
    SW_REQUIRE_TRUE( rig._pScene->isBodyValid( oldChunk0 ) && rig._pScene->isBodyValid( oldChunk1 ) );
    const uint32 bodyCount = rig._pScene->getBodyCount();

    SW_REQUIRE_TRUE( rig._pTiles->setTileBrush( 3, 0, "" ) );
    SW_REQUIRE_TRUE( rig._pTiles->setTileBrush( 10, 0, "" ) );
    SW_EXPECT_TRUE( oldChunk0 == rig._pTiles->getChunkBodies()[0] ); // 칠하는 순간에는 표시만 — 반영은 틱에서 한 번
    rig.update( 1 );

    const PhysicsBodyHandle newChunk0 = rig._pTiles->getChunkBodies()[0];
    SW_EXPECT_TRUE( newChunk0 != oldChunk0 );
    SW_EXPECT_TRUE( rig._pScene->isBodyValid( newChunk0 ) );
    SW_EXPECT_FALSE( rig._pScene->isBodyValid( oldChunk0 ) );
    SW_EXPECT_TRUE( oldChunk1 == rig._pTiles->getChunkBodies()[1] );
    SW_EXPECT_TRUE( rig._pScene->isBodyValid( oldChunk1 ) );
    SW_EXPECT_EQUAL( bodyCount, rig._pScene->getBodyCount() );
    SW_EXPECT_EQUAL( size_t{ 3 }, rig._pTiles->getSolidRects().size() ); // [0, 3) · [4, 10) · [11, 64)
    SW_EXPECT_TRUE( rig.overlapAt( rig.toWorld( 3.5f, -0.5f ) ).empty() );
    SW_EXPECT_TRUE( Internal::contains( rig.overlapAt( rig.toWorld( 4.5f, -0.5f ) ), newChunk0 ) );
}

/**
 * @brief [TileMapCollisionTest] 다른 청크의 칸을 칠해도 이 청크 위에 놓인 바디의 접촉에는 끝 → 시작이 거짓으로 나가지 않는다(유지만 이어진다)
 * @details 맵 64 × 2, 아래 행이 벽. 청크 1 위(x = 48)에 상자를 떨어뜨려 유지가 나올 때까지 돌리고, 청크 0 의 위 행 칸을 칠한다.
 *          접촉 추적기의 재시작(지운 바디가 같은 간격에 다시 닿으면 끝 + 시작)은 핸들 쌍 단위라 청크 0 의 새 바디와 상관이 없어야 한다.
 */
SW_TEST_CASE( TileMapCollisionTest, SetTileKeepsUnrelatedContacts )
{
    using Internal = TestTileMapCollisionInternal;
    Internal::Rig rig( 64, 2, { 1 } );
    SW_REQUIRE_NOT_NULL( rig._pScene );
    SW_REQUIRE_EQUAL( size_t{ 2 }, rig._pTiles->getChunkBodies().size() );
    const PhysicsBodyHandle chunk0 = rig._pTiles->getChunkBodies()[0];
    const PhysicsBodyHandle chunk1 = rig._pTiles->getChunkBodies()[1];

    PhysicsBodyDesc2D crateDesc;
    crateDesc._type     = PhysicsBodyType::Dynamic;
    crateDesc._position = rig.toWorld( 48.5f, -0.55f ); // 벽 윗면(y = -1) 바로 위
    crateDesc._userData = 0xC0A7Eull;                   // 유지는 opt-in 이다(감사 S-1) — 상자의 사용자 값이 듣는다
    rig._pScene->setContactStayListener( crateDesc._userData, true );
    PhysicsShapeDesc2D crateShape;
    crateShape._type        = PhysicsShapeType2D::Box;
    crateShape._halfExtents = float2{ 0.4f, 0.4f };
    crateDesc._listShape.push_back( crateShape );
    const PhysicsBodyHandle crate = rig._pScene->createBody( crateDesc );
    SW_REQUIRE_TRUE( rig._pScene->isBodyValid( crate ) );

    bool bResting = false;
    for ( uint32 frame = 0; frame < 120 && bResting == false; ++frame )
    {
        rig.update( 1 );
        for ( const PhysicsContactEvent2D& event : rig._manager.getSubsystem<ScenePhysics>().getFrameEvents2D() )
        {
            bResting = bResting || ( Internal::isBetween( event, crate, chunk1 ) && event._phase == PhysicsContactPhase::Stay );
        }
    }
    SW_REQUIRE_TRUE( bResting );

    SW_REQUIRE_TRUE( rig._pTiles->setTileBrush( 5, 0, "wall" ) );
    bool   bFalseEdge = false;
    uint32 stayCount  = 0;
    for ( uint32 frame = 0; frame < 3; ++frame )
    {
        rig.update( 1 );
        for ( const PhysicsContactEvent2D& event : rig._manager.getSubsystem<ScenePhysics>().getFrameEvents2D() )
        {
            if ( Internal::isBetween( event, crate, chunk1 ) == false )
                continue;
            bFalseEdge = bFalseEdge || event._phase != PhysicsContactPhase::Stay;
            stayCount += event._phase == PhysicsContactPhase::Stay ? 1u : 0u;
        }
    }
    SW_EXPECT_TRUE( rig._pTiles->getChunkBodies()[0] != chunk0 ); // 칠한 청크는 다시 만들었다
    SW_EXPECT_TRUE( rig._pTiles->getChunkBodies()[1] == chunk1 );
    SW_EXPECT_FALSE( bFalseEdge );
    SW_EXPECT_TRUE( stayCount > 0 );
}
