#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeInfo.h"

#include "GameFramework/Runtime/Gameplay/Severing/SeverResult.h"
#include "GameFramework/Runtime/Gameplay/Severing/SkeletalSeveringComponent.h"

using namespace sw;

namespace
{
    struct SkeletalSeveringTestUtil
    {
        /** @brief 띠 하나입니다 — 정점 0 ~ 3 은 영역 0, 4 · 5 는 영역 1 입니다. 삼각형 넷 중 마지막만 영역 1 의 정점이 둘입니다. */
        static AppearanceGeometry makeStrip()
        {
            AppearanceGeometry geometry;
            for ( uint32 row = 0; row < 3; ++row )
            {
                for ( uint32 column = 0; column < 2; ++column )
                {
                    geometry._listPosition.push_back( float3{ static_cast<float32>( column ), static_cast<float32>( row ), 0.0f } );
                    geometry._listNormal.push_back( float3{ 0.0f, 0.0f, -1.0f } );
                    geometry._listUV.push_back( float2{ 0.0f, 0.0f } );
                }
            }
            geometry._listIndex = { 0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4 };
            return geometry;
        }

        /** @brief 띠를 푼 스킨 메시입니다 — 정점 4 · 5 가 손 본이다. */
        static shared_ptr<Mesh> makeBodyMesh()
        {
            const AppearanceGeometry strip = makeStrip();
            vector<RHIVertex>        listVertex;
            vector<MeshSkinVertex>   listSkin;
            for ( const uint32 index : strip._listIndex )
            {
                RHIVertex vertex{};
                vertex._arrPosition[0] = strip._listPosition[index]._x;
                vertex._arrPosition[1] = strip._listPosition[index]._y;
                vertex._arrNormal[2]   = -1.0f;
                listVertex.push_back( vertex );
                MeshSkinVertex skin{};
                skin._arrJoint[0]  = index >= 4 ? uint16{ 1 } : uint16{ 0 };
                skin._arrWeight[0] = 1.0f;
                listSkin.push_back( skin );
            }
            shared_ptr<Mesh> mesh = Mesh::create( MeshCPUAccess::Keep );
            mesh->setVertices( std::move( listVertex ) );
            mesh->setSkin( std::move( listSkin ), MeshSkinBinding::makeIdentity( 2u ) );
            return mesh;
        }
    };
} // namespace

/** @brief [SkeletalSeveringTest] 정점 둘 이상이 잘라 낼 영역이면 그 삼각형이 떨어진 조각이고, 나머지는 남은 몸이다 */
SW_TEST_CASE( SkeletalSeveringTest, TriangleWithTwoSeveredVerticesGoesWithThePiece )
{
    const AppearanceGeometry strip         = SkeletalSeveringTestUtil::makeStrip();
    const uint16             arrRegion[6]  = { 0, 0, 0, 0, 1, 1 };
    const uint16             severedRegion = 1;
    SeverResult              result;
    SW_REQUIRE_TRUE( SeverResult::severRegions( strip, vector_reference<const uint16>{ arrRegion, 6 }, vector_reference<const uint16>{ &severedRegion, 1 }, result ) );
    SW_REQUIRE_EQUAL( size_t{ 4 }, result._listTriangleSevered.size() );
    SW_EXPECT_TRUE( result._listTriangleSevered[0] == SW_FALSE );
    SW_EXPECT_TRUE( result._listTriangleSevered[1] == SW_FALSE );
    SW_EXPECT_TRUE( result._listTriangleSevered[2] == SW_FALSE );
    SW_EXPECT_TRUE( result._listTriangleSevered[3] == SW_TRUE );
}

/** @brief [SkeletalSeveringTest] 잘라 낼 영역의 삼각형이 없으면 false 이고 남은 몸이 원본이다 */
SW_TEST_CASE( SkeletalSeveringTest, NothingToSeverReturnsFalse )
{
    const AppearanceGeometry strip         = SkeletalSeveringTestUtil::makeStrip();
    const uint16             arrRegion[6]  = { 0, 0, 0, 0, 1, 1 };
    const uint16             severedRegion = 7;
    SeverResult              result;
    SW_EXPECT_FALSE( SeverResult::severRegions( strip, vector_reference<const uint16>{ arrRegion, 6 }, vector_reference<const uint16>{ &severedRegion, 1 }, result ) );
    SW_EXPECT_EQUAL( strip._listIndex.size(), result._remaining._listIndex.size() );
}

/** @brief [SkeletalSeveringTest] 자른 적이 없는 몸(갓 스폰)은 되돌려도 몸 메시 · 잘린 영역 · 조각이 그대로다 — 풀이 처음 만든 인스턴스로 계약을 확인할 때 아무것도 바꾸지 않는다 */
SW_TEST_CASE( SkeletalSeveringTest, IntactBodyIsUnchangedBySpawnStateReset )
{
    GameObjectManager          manager;
    GameObject*                pObject = manager.createGameObject( hashed_string( "Skeleton" ) );
    SkeletalMeshComponent*     pUnit   = pObject->addComponent<SkeletalMeshComponent>();
    SkeletalSeveringComponent* pCut    = pObject->addComponent<SkeletalSeveringComponent>();
    SW_REQUIRE_TRUE( pUnit != nullptr && pCut != nullptr );
    const Mesh* pBody = pUnit->getMesh().get();

    SW_EXPECT_TRUE( pCut->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_TRUE( pUnit->getMesh().get() == pBody );
    SW_EXPECT_TRUE( pCut->getSeveredRegions().empty() );
    SW_EXPECT_TRUE( pCut->getPieces().empty() );
}

/** @brief [SkeletalSeveringTest] 결함: 자른 뒤 외형이 몸 메시를 다시 조립하면(옷 갈아입기) 잘린 팔이 되살아나고, 풀 반환이 옛 외형의 메시를 걸었다 — 새 메시에서도 잘려 있고, 되돌리면 새 메시다 */
SW_TEST_CASE( SkeletalSeveringTest, NewBodyMeshStaysSevered )
{
    SW_EXPECT_LOG( "SkeletalSevering.AppearanceSurfaceMissing" ); // 외형 없는 몸이다(피는 이 테스트의 일이 아니다)
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "body.fit.xml" ), R"(<FitTables><Region name="Hand" bones="hand" minWeight="0.5"/></FitTables>)" ) );
    shared_ptr<Skeleton> skeleton = make_shared<Skeleton>();
    (void)skeleton->addBone( hashed_string( "spine" ), -1, BoneTransform{} );
    (void)skeleton->addBone( hashed_string( "hand" ), 0, BoneTransform{} );

    GameObjectManager          manager;
    GameObject*                pBody     = manager.createGameObject( hashed_string( "Body" ) );
    SkeletalMeshComponent*     pUnit     = pBody->addComponent<SkeletalMeshComponent>();
    SkeletalSeveringComponent* pSevering = pBody->addComponent<SkeletalSeveringComponent>();
    pSevering->setRegionTablePath( test::ScopedScratchDomain::makeID( "body.fit.xml" ).str() );
    manager.beginPlay();
    pUnit->setSkeleton( skeleton );
    pUnit->setSkinSourceMesh( SkeletalSeveringTestUtil::makeBodyMesh() );
    const uint32 wholeCount = pUnit->getMesh()->getIndexCount();
    SW_REQUIRE_TRUE( pSevering->severRegion( hashed_string( "Hand" ), float3{} ) );
    const uint32 severedCount = pUnit->getMesh()->getIndexCount();
    SW_REQUIRE_TRUE( severedCount != wholeCount );

    const shared_ptr<Mesh> dressed = SkeletalSeveringTestUtil::makeBodyMesh();
    pUnit->setSkinSourceMesh( dressed ); // 옷을 갈아입어 몸을 다시 조립했다
    SW_EXPECT_TRUE( pUnit->getMesh() != dressed );
    SW_EXPECT_EQUAL( severedCount, pUnit->getMesh()->getIndexCount() );
    SW_EXPECT_EQUAL( size_t{ 1 }, pSevering->getPieces().size() ); // 조각을 다시 띄우지 않는다

    SW_EXPECT_TRUE( pSevering->resetToSpawnState() == SpawnStateReset::Reset );
    SW_EXPECT_TRUE( pUnit->getMesh() == dressed ); // 옛 외형이 아니다
    manager.endPlay();
}

/** @brief [SkeletalSeveringTest] 잘린 영역은 상태에 실리고, 다시 지은 몸은 메시가 서는 대로 잘려 있다 — 조각은 다시 띄우지 않고, 떨어진 조각은 저장되지 않는다 */
SW_TEST_CASE( SkeletalSeveringTest, RestoredBodyIsSeveredAgain )
{
    SW_EXPECT_LOG( "SkeletalSevering.AppearanceSurfaceMissing" ); // 외형 없는 몸이다(피는 이 테스트의 일이 아니다)
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "body.fit.xml" ), R"(<FitTables><Region name="Hand" bones="hand" minWeight="0.5"/></FitTables>)" ) );
    shared_ptr<Skeleton> skeleton = make_shared<Skeleton>();
    (void)skeleton->addBone( hashed_string( "spine" ), -1, BoneTransform{} );
    (void)skeleton->addBone( hashed_string( "hand" ), 0, BoneTransform{} );

    GameObjectManager          manager;
    GameObject*                pBody     = manager.createGameObject( hashed_string( "Body" ) );
    SkeletalMeshComponent*     pUnit     = pBody->addComponent<SkeletalMeshComponent>();
    SkeletalSeveringComponent* pSevering = pBody->addComponent<SkeletalSeveringComponent>();
    const PropertyInfo*        pChannel  = SkeletalSeveringComponent::StaticType()->findProperty( hashed_string( "_severedSurfaceChannel" ) ); // 뜬 상태를 되돌릴 때 필수 칸을 본다
    SW_REQUIRE_NOT_NULL( pChannel );
    *pChannel->getValuePtr<hashed_string>( static_cast<void*>( pSevering ) ) = hashed_string( "Blood" );
    pSevering->setRegionTablePath( test::ScopedScratchDomain::makeID( "body.fit.xml" ).str() );
    manager.beginPlay();
    pUnit->setSkeleton( skeleton );
    pUnit->setSkinSourceMesh( SkeletalSeveringTestUtil::makeBodyMesh() );
    SW_REQUIRE_TRUE( pSevering->severRegion( hashed_string( "Hand" ), float3{} ) );
    const uint32 severedCount = pUnit->getMesh()->getIndexCount();
    SW_REQUIRE_EQUAL( size_t{ 1 }, pSevering->getPieces().size() );
    SW_EXPECT_TRUE( manager.resolveGameObject( pSevering->getPieces()[0] )->isTransient() );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( ObjectStateSerializer::saveToBinaryBuffer( pBody, bytes, ObjectSaveOptions{ SerializePurpose::Snapshot } ) );

    GameObject*       pRestored = manager.createGameObject( hashed_string( "Restored" ) );
    ObjectLoadContext context{ SerializePurpose::Snapshot };
    SW_REQUIRE_TRUE( ObjectStateSerializer::loadFromBinaryBuffer( pRestored, bytes.data(), bytes.size(), context ) != 0 );
    SkeletalMeshComponent*           pRestoredUnit = pRestored->getComponent<SkeletalMeshComponent>();
    const SkeletalSeveringComponent* pRestoredCut  = pRestored->getComponent<SkeletalSeveringComponent>();
    SW_REQUIRE_TRUE( pRestoredUnit != nullptr && pRestoredCut != nullptr );
    manager.update( 1.0f / 60.0f ); // 시작한다(표를 읽는다) — 몸 메시는 아직 없다
    pRestoredUnit->setSkeleton( skeleton );
    pRestoredUnit->setSkinSourceMesh( SkeletalSeveringTestUtil::makeBodyMesh() );
    manager.update( 1.0f / 60.0f );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( severedCount, pRestoredUnit->getMesh()->getIndexCount() );
    SW_EXPECT_EQUAL( size_t{ 1 }, pRestoredCut->getSeveredRegions().size() );
    SW_EXPECT_TRUE( pRestoredCut->getPieces().empty() );
    manager.endPlay();
}
