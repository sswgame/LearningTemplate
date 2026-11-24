// ServerPoseOverlayTest — 서버 포즈 글의 읽기 · 쓰기(공백이 든 이름, 선택 묶음, 옛 형식, 망가진 줄), 이름으로 맞춘 차이의 차례, 차이 글, 로컬 포즈 모으기(서버 · 소켓 없음 — nogpu).
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/ServerPose/ServerPoseOverlay.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

namespace
{
    struct ServerPoseOverlayTestUtil
    {
        static AuthorityPose makePose( string_view name, const float3& position )
        {
            AuthorityPose pose;
            pose._name     = string( name );
            pose._position = position;
            return pose;
        }
    };
} // namespace

/** @brief [ServerPoseOverlayTest] 쓴 글을 되읽으면 이름(공백 포함) · 위치 · 회전 · 상자가 그대로고, 없는 묶음은 없는 채다 */
SW_TEST_CASE( ServerPoseOverlayTest, WrittenPosesReadBack )
{
    vector<AuthorityPose> listPose;
    listPose.push_back( ServerPoseOverlayTestUtil::makePose( "Plain", float3{ 1.0f, -2.0f, 3.5f } ) );
    AuthorityPose full  = ServerPoseOverlayTestUtil::makePose( "Guard Tower 2", float3{ 4.0f, 5.0f, 6.0f } );
    full._bHasRotation  = true;
    full._rotation      = float4{ 0.0f, 0.5f, 0.0f, 0.25f };
    full._bHasBox       = true;
    full._boxCenter     = float3{ 4.0f, 6.0f, 6.0f };
    full._boxHalfExtent = float3{ 0.5f, 1.0f, 0.5f };
    listPose.push_back( full );

    string text;
    ServerPoseOverlay::writePoses( listPose, text );
    vector<AuthorityPose> listRead;
    ServerPoseOverlay::parsePoses( text, listRead );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listRead.size() );
    SW_EXPECT_EQUAL( string( "Plain" ), listRead[0]._name );
    SW_EXPECT_NEAR( 3.5f, listRead[0]._position._z, 1.0e-4f );
    SW_EXPECT_FALSE( listRead[0]._bHasRotation );
    SW_EXPECT_FALSE( listRead[0]._bHasBox );
    SW_EXPECT_EQUAL( string( "Guard Tower 2" ), listRead[1]._name );
    SW_EXPECT_NEAR( 5.0f, listRead[1]._position._y, 1.0e-4f );
    SW_REQUIRE_TRUE( listRead[1]._bHasRotation );
    SW_EXPECT_NEAR( 0.5f, listRead[1]._rotation._y, 1.0e-4f );
    SW_EXPECT_NEAR( 0.25f, listRead[1]._rotation._w, 1.0e-4f );
    SW_REQUIRE_TRUE( listRead[1]._bHasBox );
    SW_EXPECT_NEAR( 6.0f, listRead[1]._boxCenter._y, 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, listRead[1]._boxHalfExtent._y, 1.0e-4f );
}

/** @brief [ServerPoseOverlayTest] 위치 셋이 없거나 이름이 없는 줄 · 빈 줄은 건너뛰고 나머지는 읽는다 */
SW_TEST_CASE( ServerPoseOverlayTest, BrokenLinesAreSkipped )
{
    vector<AuthorityPose> listRead;
    listRead.push_back( AuthorityPose{} ); // 지난 값은 비운다
    ServerPoseOverlay::parsePoses( "Crate 1 2\n\n1 2 3\nBarrel 7 8 9\r\nDoor x 2 3\n", listRead );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listRead.size() );
    SW_EXPECT_EQUAL( string( "Barrel" ), listRead[0]._name );
    SW_EXPECT_NEAR( 9.0f, listRead[0]._position._z, 1.0e-4f );
}

/** @brief [ServerPoseOverlayTest] 차이는 이름이 같은 쌍만, 거리가 큰 순서이고, 회전 · 상자 차이는 두 쪽에 다 있을 때만 잰다 */
SW_TEST_CASE( ServerPoseOverlayTest, DeltasPairByNameFarthestFirst )
{
    vector<AuthorityPose> listLocal;
    listLocal.push_back( ServerPoseOverlayTestUtil::makePose( "Near", float3{} ) );
    listLocal.push_back( ServerPoseOverlayTestUtil::makePose( "Far", float3{} ) );
    listLocal.push_back( ServerPoseOverlayTestUtil::makePose( "OnlyLocal", float3{} ) );
    listLocal[1]._bHasRotation = true; // 단위 회전

    vector<AuthorityPose> listServer;
    listServer.push_back( ServerPoseOverlayTestUtil::makePose( "Near", float3{ 1.0f, 0.0f, 0.0f } ) );
    listServer.push_back( ServerPoseOverlayTestUtil::makePose( "Far", float3{ 0.0f, 3.0f, 4.0f } ) );
    listServer.push_back( ServerPoseOverlayTestUtil::makePose( "OnlyServer", float3{} ) );
    listServer[0]._bHasRotation = true; // 로컬에 회전이 없다
    listServer[1]._bHasRotation = true;
    listServer[1]._rotation     = float4{ 0.0f, 0.70710678f, 0.0f, 0.70710678f }; // Y 90 도
    listServer[1]._bHasBox      = true;

    vector<AuthorityDelta> listDelta;
    ServerPoseOverlay::computeDeltas( listLocal, listServer, listDelta );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listDelta.size() );
    SW_EXPECT_EQUAL( string( "Far" ), listDelta[0]._name );
    SW_EXPECT_NEAR( 5.0f, listDelta[0]._distance, 1.0e-4f );
    SW_EXPECT_NEAR( 90.0f, listDelta[0]._angleDegrees, 0.05f );
    SW_EXPECT_TRUE( listDelta[0]._bHasServerBox );
    SW_EXPECT_FALSE( listDelta[0]._bHasBoxPair );
    SW_EXPECT_EQUAL( string( "Near" ), listDelta[1]._name );
    SW_EXPECT_NEAR( 1.0f, listDelta[1]._distance, 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, listDelta[1]._angleDegrees, 1.0e-4f );

    string report;
    ServerPoseOverlay::writeReport( listDelta, report );
    SW_EXPECT_TRUE( StringUtil::startsWith( report, "Far " ) );
    SW_EXPECT_TRUE( StringUtil::contains( report, "\nNear " ) );
    SW_EXPECT_TRUE( StringUtil::contains( report, " server " ) );
}

/** @brief [ServerPoseOverlayTest] 로컬 포즈는 트랜스폼이 있는 이름 있는 오브젝트만 이름 순이고, 서버 포즈를 받기 전의 알림은 없다 */
SW_TEST_CASE( ServerPoseOverlayTest, CollectsNamedTransformsInNameOrder )
{
    GameObjectManager manager;
    manager.createGameObject( hashed_string( "Zed" ) )->addComponent<SceneComponent>()->setLocalPosition( float3{ 2.0f, 0.0f, 0.0f } );
    manager.createGameObject( hashed_string( "Alpha" ) )->addComponent<SceneComponent>()->setLocalPosition( float3{ 1.0f, 0.0f, 0.0f } );
    (void)manager.createGameObject( hashed_string( "Logic" ) ); // 트랜스폼이 없다
    manager.beginPlay();

    vector<AuthorityPose> listPose;
    ServerPoseOverlay::collectPoses( manager, listPose );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listPose.size() );
    SW_EXPECT_EQUAL( string( "Alpha" ), listPose[0]._name );
    SW_EXPECT_EQUAL( string( "Zed" ), listPose[1]._name );
    SW_EXPECT_NEAR( 2.0f, listPose[1]._position._x, 1.0e-4f );
    SW_EXPECT_TRUE( listPose[1]._bHasRotation );

    const ServerPoseOverlay overlay;
    string                  text;
    SW_EXPECT_FALSE( overlay.describe( text ) );
}
