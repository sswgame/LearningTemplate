// InstancedMeshPackingTest — Packed Level(설계 "런타임 하위 레벨 · Packed Level · 원점 이동"): 정적 메시를 묶는 씬을 넣은 앵커의 멤버를 쿠커가 인스턴스 메시로 묶는다.
// 움직이는 메시 · 가리켜지는 메시 · 다른 컴포넌트를 가진 엔티티는 그대로 두고, 묶은 엔티티는 앵커에 붙어 같은 자리에 그린다. 원본 문서(에디터)는 묶지 않는다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "DevTools/Cook/SceneCooker.h"

#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Subsystem/SceneEngineSubsystems.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Visual/Component/InstancedMeshComponent.h"

using namespace sw;

namespace
{
    struct InstancedMeshPackingTestUtil
    {
        static string makeMesh( const utf8* pID, const utf8* pName, const utf8* pPosition, const utf8* pStatic, const utf8* pAttach = "_attachOwner=\"None\" _attachComponent=\"None\"" )
        {
            StringBuilder<constant::buffer::kMax1024> builder;
            builder.appendFormat( "<entity id=\"%#\" name=\"%#\"><GameObject _schemaVersion=\"0\" _name=\"%#\" _bActive=\"true\"><_listComponent><MeshComponent _localID=\"1\" _localPosition=\"%#\" _localRotation=\"0,0,0\" "
                                  "_localScale=\"1,1,1\" %# _meshID=\"Cube\" _bStatic=\"%#\" /></_listComponent></GameObject></entity>",
                                  pID, pName, pName, pPosition, pAttach, pStatic );
            return string( builder.view() );
        }

        /** @brief 엔티티 글 @p listEntity 를 차례로 담은 씬입니다. */
        static string makeScene( const utf8* pName, const utf8* pPacking, std::initializer_list<string_view> listEntity )
        {
            StringBuilder<constant::buffer::kMax4096> builder;
            builder.appendFormat( "<Scene formatVersion=\"0\" name=\"%#\"><SceneSettings _bStreaming=\"false\" _unloadMargin=\"0\" _maxCellLoadsPerFrame=\"0\" _addToWorldBudgetMicroseconds=\"0\" %#><_cellGrid "
                                  "_cellSize=\"0\" _loadingRadius=\"0\" /></SceneSettings><entities>",
                                  pName, pPacking );
            for ( const string_view entity : listEntity )
            {
                builder.append( entity );
            }
            builder.append( "</entities></Scene>" );
            return string( builder.view() );
        }

        /** @brief 묶는 내부 씬 — 정적 Rock1 · Rock2(묶인다), 움직이는 Lamp, 정적이지만 Lid 가 붙은 Base(가리켜진다)와 Lid(부모가 앵커가 아니다). */
        static void writeScenes( const test::ScopedScratchDomain& domain )
        {
            const string inner = makeScene( "Props", "_packing=\"StaticMeshes\"",
                                            { makeMesh( "1", "Rock1", "1,0,0", "true" ), makeMesh( "2", "Rock2", "2,0,0", "true" ), makeMesh( "3", "Lamp", "3,0,0", "false" ),
                                              makeMesh( "4", "Base", "4,0,0", "true" ),
                                              makeMesh( "5", "Lid", "0,1,0", "true", "_attachOwner=\"Base\" _attachOwnerID=\"4\" _attachComponent=\"#0000000000000001\"" ) } );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "props.scene.xml" ), inner ) );
            StringBuilder<constant::buffer::kMax1024> anchor;
            anchor.appendFormat( "<entity id=\"1\" name=\"Village\" scene=\"%#\"><GameObject _schemaVersion=\"0\" _name=\"Village\" _bActive=\"true\"><_listComponent><SceneComponent _localPosition=\"10,0,0\" "
                                 "/></_listComponent></GameObject></entity>",
                                 test::ScopedScratchDomain::makeID( "props.scene.xml" ).str() );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "outer.scene.xml" ), makeScene( "Outer", "", { anchor.view() } ) ) );
        }

        static bool hasNode( const SceneDocument& doc, string_view name )
        {
            for ( const SceneDocument::SceneObjectNode& node : doc._listSceneObjectNode )
            {
                if ( node._name == name )
                    return true;
            }
            return false;
        }
    };
    using Util = InstancedMeshPackingTestUtil;
} // namespace

/** @brief [InstancedMeshPackingTest] 묶는 씬을 넣은 앵커의 정적 메시 멤버는 인스턴스 메시 하나로 묶이고, 움직이는 · 가리켜지는 · 앵커에 붙지 않은 멤버는 그대로 남는다 */
SW_TEST_CASE( InstancedMeshPackingTest, PackedAnchorMergesStaticMeshMembers )
{
    const test::ScopedScratchDomain domain;
    Util::writeScenes( domain );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "outer.scene.xml" ) ) );
    SW_REQUIRE_TRUE( doc._listSceneObjectNode.empty() == false );
    SW_EXPECT_TRUE( doc._listSceneObjectNode[0]._instancePacking == SceneInstancePacking::StaticMeshes ); // 내부 씬의 설정이 앵커에 실렸다
    SW_REQUIRE_EQUAL( size_t{ 6 }, doc._listSceneObjectNode.size() );

    SW_REQUIRE_TRUE( SceneCooker::packStaticMeshes( SceneEngineSubsystems::makeFromBoundEngine(), doc, "outer.scene.xml" ) );
    SW_EXPECT_FALSE( Util::hasNode( doc, "Village/Rock1" ) );
    SW_EXPECT_FALSE( Util::hasNode( doc, "Village/Rock2" ) );
    SW_EXPECT_TRUE( Util::hasNode( doc, "Village/Lamp" ) ); // 움직인다
    SW_EXPECT_TRUE( Util::hasNode( doc, "Village/Base" ) ); // Lid 가 가리킨다
    SW_EXPECT_TRUE( Util::hasNode( doc, "Village/Lid" ) );  // 부모가 앵커가 아니다
    SW_EXPECT_TRUE( Util::hasNode( doc, "Village/Packed0" ) );
    SW_EXPECT_EQUAL( size_t{ 5 }, doc._listSceneObjectNode.size() );

    Scene scene{ "Outer" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager& objects  = *scene.getObjectManager();
    const GameObject*  pVillage = objects.findGameObjectByName( hashed_string( "Village" ) );
    const GameObject*  pPacked  = objects.findGameObjectByName( hashed_string( "Village/Packed0" ) );
    SW_REQUIRE_TRUE( pVillage != nullptr && pPacked != nullptr );
    SW_EXPECT_TRUE( pPacked->getParent() == pVillage ); // 앵커에 붙어 앵커와 함께 움직인다
    const InstancedMeshComponent* pInstanced = pPacked->getComponent<InstancedMeshComponent>();
    SW_REQUIRE_NOT_NULL( pInstanced );
    SW_REQUIRE_EQUAL( size_t{ 2 }, pInstanced->getInstances().size() );
    SW_EXPECT_EQUAL( string( "Cube" ), pInstanced->getMeshID() );
    SW_EXPECT_EQUAL( 1.0f, pInstanced->getInstances()[0]._position._x );
    SW_EXPECT_EQUAL( 2.0f, pInstanced->getInstances()[1]._position._x );
    SW_EXPECT_EQUAL( 10.0f, pInstanced->getWorldPosition()._x );
}

/** @brief [InstancedMeshPackingTest] 묶지 않는 씬을 넣은 앵커는 그대로이고(쿠커가 문서를 바꾸지 않는다), 원본 문서를 지으면 멤버가 엔티티 그대로 선다(에디터는 원본을 편집한다) */
SW_TEST_CASE( InstancedMeshPackingTest, UnpackedSourceKeepsMemberEntities )
{
    const test::ScopedScratchDomain domain;
    Util::writeScenes( domain );
    SceneDocument doc{};
    SW_REQUIRE_TRUE( doc.loadXMLFromDiskFile( domain.makeDiskPath( "outer.scene.xml" ) ) );
    Scene scene{ "Outer" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    SW_EXPECT_TRUE( scene.getObjectManager()->findGameObjectByName( hashed_string( "Village/Rock1" ) ) != nullptr );
    SW_EXPECT_TRUE( scene.getObjectManager()->findGameObjectByName( hashed_string( "Village/Packed0" ) ) == nullptr );

    doc._listSceneObjectNode[0]._instancePacking = SceneInstancePacking::Separate;
    SW_REQUIRE_TRUE( SceneCooker::packStaticMeshes( SceneEngineSubsystems::makeFromBoundEngine(), doc, "outer.scene.xml" ) );
    SW_EXPECT_EQUAL( size_t{ 6 }, doc._listSceneObjectNode.size() );
}

/** @brief [InstancedMeshPackingTest] 인스턴스 행렬은 스케일 → 회전 → 이동이다(행 벡터) — 원점은 위치로, 단위 X 는 스케일만큼 늘어 Y 축 회전을 받는다 */
SW_TEST_CASE( InstancedMeshPackingTest, LocalMatrixIsScaleRotateTranslate )
{
    InstancedMeshTransform transform{};
    transform._position   = float3{ 5.0f, 0.0f, 0.0f };
    transform._scale      = float3{ 2.0f, 2.0f, 2.0f };
    const float4x4 matrix = InstancedMeshComponent::makeLocalMatrix( transform );
    const float3   origin = float3::transform( float3{}, matrix );
    SW_EXPECT_EQUAL( 5.0f, origin._x );
    const float3 unitX = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, matrix );
    SW_EXPECT_EQUAL( 7.0f, unitX._x );
}
