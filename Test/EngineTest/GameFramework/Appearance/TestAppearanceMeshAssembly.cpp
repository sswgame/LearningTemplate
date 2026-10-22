#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/Character/Fit/FitPartData.h"
#include "Engine/Character/Fit/FitSolver.h"
#include "Engine/Character/Fit/FitTables.h"
#include "Engine/Graphics/Mesh/Mesh.h"

#include "EngineTest/CharacterTestUtil.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceMeshAssembly.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"

#include "TestFramework/TestFramework.h"

// 외형 메시 조립 — 메시 ↔ 형상 다리(위치로 잇기 · 굽기), 피팅이 덮은 몸 삼각형 · 해석의 숨김 영역을 빼고 체형 모프를 레스트에 굽는다.

using namespace sw;

namespace
{
    struct AppearanceMeshAssemblyTestInternal
    {
        static constexpr const utf8* kTablesXML = "<FitTables>"
                                                  "  <Layer name='Body' order='0'/>"
                                                  "  <Layer name='Bracelet' order='40'/>"
                                                  "  <Region name='LowerArm' bones='lowerarm hand' minWeight='0.5'/>"
                                                  "  <Profile name='Skin' rigidity='0' coverageDistance='0.03'/>"
                                                  "  <Profile name='Rigid' rigidity='1'/>"
                                                  "</FitTables>";

        static RHIVertex makeVertex( const float3& position, const float3& normal )
        {
            RHIVertex vertex{};
            vertex._arrPosition[0] = position._x;
            vertex._arrPosition[1] = position._y;
            vertex._arrPosition[2] = position._z;
            vertex._arrNormal[0]   = normal._x;
            vertex._arrNormal[1]   = normal._y;
            vertex._arrNormal[2]   = normal._z;
            for ( float32& channel : vertex._arrColor )
            {
                channel = 1.0f;
            }
            return vertex;
        }

        /** @brief 형상(인덱스)을 인덱스 없는 스킨드 메시로 펼친다(임포트한 메시와 같은 꼴). */
        static shared_ptr<Mesh> makeMesh( const AppearanceGeometry& geometry, uint32 boneCount )
        {
            vector<RHIVertex>      listVertex;
            vector<MeshSkinVertex> listSkin;
            for ( const uint32 index : geometry._listIndex )
            {
                listVertex.push_back( makeVertex( geometry._listPosition[index], geometry._listNormal[index] ) );
                MeshSkinVertex skin;
                if ( geometry.isSkinned() )
                {
                    for ( uint32 slot = 0; slot < 4; ++slot )
                    {
                        skin._arrJoint[slot]  = geometry._listSkin[index]._arrJoint[slot];
                        skin._arrWeight[slot] = geometry._listSkin[index]._arrWeight[slot];
                    }
                }
                listSkin.push_back( skin );
            }
            shared_ptr<Mesh> mesh = Mesh::create();
            mesh->setVertices( std::move( listVertex ) );
            mesh->setSkin( std::move( listSkin ), boneCount );
            return mesh;
        }

        /** @brief 두 삼각형짜리 사각형 — (0,0)-(1,0)-(1,1) 과 (0,0)-(1,1)-(0,1). 모서리 0 · 3 과 2 · 4 가 같은 자리다. 구간 A · B 가 삼각형 하나씩. */
        static shared_ptr<Mesh> makeQuad()
        {
            const float3      normal{ 0.0f, 0.0f, 1.0f };
            vector<RHIVertex> listVertex{ makeVertex( float3{ 0.0f, 0.0f, 0.0f }, normal ), makeVertex( float3{ 1.0f, 0.0f, 0.0f }, normal ),
                                          makeVertex( float3{ 1.0f, 1.0f, 0.0f }, normal ), makeVertex( float3{ 0.0f, 0.0f, 0.0f }, normal ),
                                          makeVertex( float3{ 1.0f, 1.0f, 0.0f }, normal ), makeVertex( float3{ 0.0f, 1.0f, 0.0f }, normal ) };
            shared_ptr<Mesh>  mesh = Mesh::create();
            mesh->setVertices( std::move( listVertex ) );
            vector<MeshMorphTarget> listTarget( 2 );
            listTarget[0]._name = hashed_string( "Wide" );
            listTarget[0]._listDelta.push_back( MeshMorphDelta{
                1, float3{ 0.5f, 0.0f, 0.0f },
                float3{}
            } );
            listTarget[1]._name = hashed_string( "Smile" );
            listTarget[1]._listDelta.push_back( MeshMorphDelta{
                5, float3{ 0.0f, 0.2f, 0.0f },
                float3{}
            } );
            mesh->setMorphTargets( std::move( listTarget ) );
            SW_EXPECT_TRUE( mesh->setSections( {
                MeshSection{hashed_string( "A" ), 0, 3, MeshPersonVisibility::Both},
                MeshSection{hashed_string( "B" ), 3, 3, MeshPersonVisibility::Both}
            } ) );
            return mesh;
        }

        static AppearanceGeometry makeArm( float32 radius, float32 yMin, float32 yMax, uint32 ringCount )
        {
            test::CharacterTestUtil::CylinderDesc desc;
            desc._radius                = radius;
            desc._yMin                  = yMin;
            desc._yMax                  = yMax;
            desc._segmentCount          = 24;
            desc._ringCount             = ringCount;
            AppearanceGeometry geometry = test::CharacterTestUtil::makeCylinder( desc );
            test::CharacterTestUtil::skinBySplit( geometry, 0.5f, 1, 2 );
            return geometry;
        }

        /** @brief 삼각형 하나라도 아래 팔(y ≥ 0.5)에 다 든 것이 남아 있으면 true 입니다. */
        static bool keepsLowerArmTriangle( const Mesh& mesh )
        {
            const vector<RHIVertex>& listVertex = mesh.getVertices();
            for ( size_t corner = 0; corner + 2 < listVertex.size(); corner += 3 )
            {
                const float32 minY = MathUtil::min( listVertex[corner]._arrPosition[1], MathUtil::min( listVertex[corner + 1]._arrPosition[1], listVertex[corner + 2]._arrPosition[1] ) );
                if ( minY >= 0.5f - 1.0e-5f )
                    return true;
            }
            return false;
        }
    };
} // namespace

/**
 * @brief [CharacterMeshUtilTest] 위치로 이은 형상 — 같은 자리의 모서리는 한 정점, 삼각형 순서는 그대로, 모프는 형상 정점마다 촘촘하게
 */
SW_TEST_CASE( CharacterMeshUtilTest, WeldedGeometryJoinsCornersAndKeepsMorphs )
{
    using Internal                = AppearanceMeshAssemblyTestInternal;
    const shared_ptr<Mesh> source = Internal::makeQuad();
    AppearanceGeometry     welded;
    CharacterMeshUtil::makeWeldedGeometry( *source, welded );
    SW_ASSERT_EQUAL( 4u, welded.getVertexCount() );
    SW_ASSERT_EQUAL( size_t( 6 ), welded._listIndex.size() );
    SW_EXPECT_EQUAL( welded._listIndex[0], welded._listIndex[3] );
    SW_EXPECT_EQUAL( welded._listIndex[2], welded._listIndex[4] );
    SW_EXPECT_TRUE( welded.isValid() );
    const GeometryMorphTarget* pWide = welded.findMorph( "Wide" );
    SW_ASSERT_NOT_NULL( pWide );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pWide->_listPositionDelta[welded._listIndex[1]]._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pWide->_listPositionDelta[welded._listIndex[0]].getLength(), 1.0e-6f );
}

/**
 * @brief [CharacterMeshUtilTest] 굽기 — 모프를 레스트에 더하고 그 타깃을 빼며, 숨긴 삼각형을 빼고 남은 구간 · 모프를 새 번호로 옮긴다(빈 구간은 이름만 남는다)
 */
SW_TEST_CASE( CharacterMeshUtilTest, BakedMeshFoldsMorphsAndDropsHiddenTriangles )
{
    using Internal                = AppearanceMeshAssemblyTestInternal;
    const shared_ptr<Mesh> source = Internal::makeQuad();
    AppearanceGeometry     welded;
    CharacterMeshUtil::makeWeldedGeometry( *source, welded );
    FitPartResult fit;
    fit.initialize( 2, welded.getVertexCount() );
    fit.setTriangleVisible( 1, false );
    fit._listVertexDelta[welded._listIndex[0]] = float3{ 0.0f, 0.0f, 0.1f };

    const BodyMorphWeight arrWeight[] = {
        BodyMorphWeight{ hashed_string( "Wide" ), 1.0f }
    };
    const shared_ptr<Mesh> baked = CharacterMeshUtil::createBakedMesh( *source, arrWeight, &welded, &fit );
    SW_ASSERT_NOT_NULL( baked.get() );
    SW_ASSERT_EQUAL( 3u, baked->getVertexCount() );
    SW_EXPECT_NEAR_EQUAL( 1.5f, baked->getVertices()[1]._arrPosition[0], 1.0e-6f ); // 구운 모프
    SW_EXPECT_NEAR_EQUAL( 0.1f, baked->getVertices()[0]._arrPosition[2], 1.0e-6f ); // 피팅 델타
    SW_EXPECT_TRUE( baked->hasSkin() == false );
    SW_EXPECT_EQUAL( -1, baked->findMorphTargetIndex( "Wide" ) ); // 구운 타깃은 빠진다
    const int32 smileIndex = baked->findMorphTargetIndex( "Smile" );
    SW_ASSERT_TRUE( smileIndex >= 0 );
    SW_EXPECT_TRUE( baked->getMorphTargets()[static_cast<size_t>( smileIndex )]._listDelta.empty() ); // 그 정점의 삼각형이 빠졌다
    SW_ASSERT_EQUAL( size_t( 2 ), baked->getSections().size() );
    SW_EXPECT_EQUAL( 3u, baked->getSections()[0]._vertexCount );
    SW_EXPECT_EQUAL( 0u, baked->getSections()[1]._vertexCount );

    // 형상만 있고 결과가 없으면(또는 크기가 다르면) 짓지 않는다.
    SW_TEST_DEFENSIVE_SCOPE( "a baked mesh refuses a fit that does not match its mesh" );
    SW_EXPECT_NULL( CharacterMeshUtil::createBakedMesh( *source, arrWeight, &welded, nullptr ).get() );
    FitPartResult wrong;
    wrong.initialize( 5, 9 );
    SW_EXPECT_NULL( CharacterMeshUtil::createBakedMesh( *source, arrWeight, &welded, &wrong ).get() );
}

/**
 * @brief [CharacterMeshUtilTest] 몸 → 장비 전이(임포트 단계) — 장비 정점이 가까운 몸 삼각형의 스킨 · 체형 모프를 받는다. 멀리 떨어진 정점 수를 알린다
 */
SW_TEST_CASE( CharacterMeshUtilTest, BodySurfaceTransfersSkinAndMorphsToAGarment )
{
    using Internal                 = AppearanceMeshAssemblyTestInternal;
    const CharacterBoneArray bones = test::CharacterTestUtil::makeArmBones();
    const shared_ptr<Mesh>   body  = Internal::makeMesh( Internal::makeArm( 0.04f, 0.0f, 1.0f, 20 ), bones.getBoneCount() );
    vector<MeshMorphTarget>  listTarget( 1 );
    listTarget[0]._name = hashed_string( "Heavy" );
    for ( uint32 vertex = 0; vertex < body->getVertexCount(); ++vertex )
    {
        const RHIVertex& corner = body->getVertices()[vertex];
        const float3     radial = float3{ corner._arrPosition[0], 0.0f, corner._arrPosition[2] }.normalize();
        listTarget[0]._listDelta.push_back( MeshMorphDelta{ vertex, radial * 0.01f, float3{} } );
    }
    body->setMorphTargets( std::move( listTarget ) );

    test::CharacterTestUtil::CylinderDesc desc;
    desc._radius              = 0.05f;
    desc._yMin                = 0.1f;
    desc._yMax                = 0.9f;
    desc._segmentCount        = 30;
    desc._ringCount           = 10;
    AppearanceGeometry sleeve = test::CharacterTestUtil::makeCylinder( desc );
    sleeve._listSkin.clear();
    shared_ptr<Mesh> garment = Internal::makeMesh( sleeve, 0 );
    garment->setSkin( {}, 0 );
    SW_ASSERT_FALSE( garment->hasSkin() );

    SW_EXPECT_EQUAL( 0u, CharacterMeshUtil::transferBodySurface( *body, *garment, 0.05f ) );
    SW_ASSERT_TRUE( garment->hasSkin() );
    SW_EXPECT_EQUAL( bones.getBoneCount(), garment->getSkinBoneCount() );
    const int32 heavyIndex = garment->findMorphTargetIndex( "Heavy" );
    SW_ASSERT_TRUE( heavyIndex >= 0 );
    SW_EXPECT_EQUAL( size_t( garment->getVertexCount() ), garment->getMorphTargets()[static_cast<size_t>( heavyIndex )]._listDelta.size() );
    for ( uint32 vertex = 0; vertex < garment->getVertexCount(); ++vertex )
    {
        const float32 y        = garment->getVertices()[vertex]._arrPosition[1];
        const uint16  expected = y >= 0.5f ? uint16( 2 ) : uint16( 1 );
        if ( MathUtil::abs( y - 0.5f ) > 0.06f ) // 경계 근처는 섞인다
            SW_EXPECT_EQUAL( expected, garment->getSkinVertices()[vertex]._arrJoint[0] );
    }

    // 몸에서 너무 먼 장비 — 묶이지 않은 수를 알린다.
    shared_ptr<Mesh> far = Internal::makeMesh( sleeve, 0 );
    SW_EXPECT_EQUAL( far->getVertexCount(), CharacterMeshUtil::transferBodySurface( *body, *far, 0.001f ) );
}

/**
 * @brief [AppearanceMeshAssemblyTest] 장갑의 숨김 영역이 팔의 아래 반을 빼고, 해석의 숨김 영역(몸 수정)도 같은 길로 빠진다. 표에 없는 영역 · 표 없는 피팅은 오류다
 */
SW_TEST_CASE( AppearanceMeshAssemblyTest, FittedPartsAndHiddenRegionsCutTheBody )
{
    using Internal = AppearanceMeshAssemblyTestInternal;
    const FitSolver solver;
    FitTables       tables;
    SW_ASSERT_TRUE( tables.loadFromXMLText( Internal::kTablesXML, "assembly.fit.xml", solver.getOperatorRegistry() ) );
    FitPartData bodyFit;
    SW_ASSERT_TRUE( bodyFit.loadFromXMLText( "<PartFit layer='Body' profile='Skin'/>", "body.partfit.xml" ) );
    FitPartData gloveFit;
    SW_ASSERT_TRUE( gloveFit.loadFromXMLText( "<PartFit layer='Bracelet' profile='Rigid'><Hide region='LowerArm'/></PartFit>", "glove.partfit.xml" ) );

    const CharacterBoneArray bones = test::CharacterTestUtil::makeArmBones();
    const shared_ptr<Mesh>   body  = Internal::makeMesh( Internal::makeArm( 0.04f, 0.0f, 1.0f, 50 ), bones.getBoneCount() );
    const shared_ptr<Mesh>   glove = Internal::makeMesh( Internal::makeArm( 0.06f, 0.5f, 1.0f, 10 ), bones.getBoneCount() );
    SW_ASSERT_TRUE( Internal::keepsLowerArmTriangle( *body ) );

    const AppearanceMeshPart arrPart[] = {
        AppearanceMeshPart{ hashed_string( "Body" ),          hashed_string{},  body.get(),  &bodyFit},
        AppearanceMeshPart{hashed_string( "Glove" ), hashed_string( "Hands" ), glove.get(), &gloveFit}
    };
    ResolvedAppearance       resolved;
    vector<shared_ptr<Mesh>> listMesh;
    string                   report;
    SW_ASSERT_TRUE_MSG( AppearanceMeshAssembly::assemble( resolved, arrPart, &tables, bones, listMesh, report ), report.c_str() );
    SW_ASSERT_EQUAL( size_t( 2 ), listMesh.size() );
    SW_ASSERT_NOT_NULL( listMesh[0].get() );
    SW_EXPECT_TRUE( listMesh[0]->getVertexCount() < body->getVertexCount() );
    SW_EXPECT_FALSE( Internal::keepsLowerArmTriangle( *listMesh[0] ) );
    SW_EXPECT_NOT_NULL( listMesh[1].get() );

    // 장갑 없이 몸 수정의 숨김 영역만 — 같은 규칙으로 빠진다.
    resolved._listHiddenRegion.push_back( hashed_string( "LowerArm" ) );
    SW_ASSERT_TRUE_MSG( AppearanceMeshAssembly::assemble( resolved, vector_reference<const AppearanceMeshPart>( arrPart, 1 ), &tables, bones, listMesh, report ),
                        report.c_str() );
    SW_ASSERT_NOT_NULL( listMesh[0].get() );
    SW_EXPECT_FALSE( Internal::keepsLowerArmTriangle( *listMesh[0] ) );

    // 표에 없는 영역 · 표 없이 피팅 데이터 — 조립하지 않고 까닭을 남긴다.
    resolved._listHiddenRegion.push_back( hashed_string( "Tail" ) );
    report.clear();
    SW_EXPECT_FALSE( AppearanceMeshAssembly::assemble( resolved, vector_reference<const AppearanceMeshPart>( arrPart, 1 ), &tables, bones, listMesh, report ) );
    SW_EXPECT_TRUE( report.find( "'Tail'" ) != string::npos );
    report.clear();
    SW_EXPECT_FALSE( AppearanceMeshAssembly::assemble( resolved, arrPart, nullptr, bones, listMesh, report ) );
    SW_EXPECT_TRUE( report.find( "no fit tables" ) != string::npos );
    SW_EXPECT_TRUE( listMesh.empty() );
}

/**
 * @brief [AppearanceMeshAssemblyTest] 피팅 없이 체형 모프만 — 몸 모프는 몸과 그 이름을 가진 부품에 굽고, 걸 것이 없는 부품은 원본(nullptr) 그대로다
 */
SW_TEST_CASE( AppearanceMeshAssemblyTest, BodyMorphsAreBakedWithoutFitting )
{
    using Internal                     = AppearanceMeshAssemblyTestInternal;
    const shared_ptr<Mesh>   quad      = Internal::makeQuad();
    const CharacterBoneArray bones     = test::CharacterTestUtil::makeArmBones();
    const shared_ptr<Mesh>   arm       = Internal::makeMesh( Internal::makeArm( 0.04f, 0.0f, 1.0f, 4 ), bones.getBoneCount() );
    const AppearanceMeshPart arrPart[] = {
        AppearanceMeshPart{  hashed_string( "Body" ),          hashed_string{}, quad.get(), nullptr},
        AppearanceMeshPart{hashed_string( "Sleeve" ), hashed_string( "Torso" ),  arm.get(), nullptr}
    };
    ResolvedAppearance resolved;
    resolved._listMorph.push_back( ResolvedMorph{ hashed_string{}, hashed_string( "Wide" ), 0.5f } );
    vector<shared_ptr<Mesh>> listMesh;
    string                   report;
    SW_ASSERT_TRUE_MSG( AppearanceMeshAssembly::assemble( resolved, arrPart, nullptr, bones, listMesh, report ), report.c_str() );
    SW_ASSERT_NOT_NULL( listMesh[0].get() );
    SW_EXPECT_NEAR_EQUAL( 1.25f, listMesh[0]->getVertices()[1]._arrPosition[0], 1.0e-6f );
    SW_EXPECT_NULL( listMesh[1].get() );
}

/**
 * @brief [AppearanceMeshAssemblyTest] 구운 메시 캐시 — 같은 키는 같은 객체를 나누고(원본 부품은 nullptr 그대로), 그리는 쪽이 모두 놓으면 사라진다
 */
SW_TEST_CASE( AppearanceMeshAssemblyTest, BakedMeshCacheSharesWhileSomeoneDraws )
{
    using Internal         = AppearanceMeshAssemblyTestInternal;
    constexpr uint64 kKey  = 0x5157A11ull;
    shared_ptr<Mesh> baked = Internal::makeQuad();
    AppearanceBakedMeshCache::store( kKey, { baked, nullptr } );
    vector<shared_ptr<Mesh>> listShared;
    SW_ASSERT_TRUE( AppearanceBakedMeshCache::tryGet( kKey, listShared ) );
    SW_ASSERT_EQUAL( size_t( 2 ), listShared.size() );
    SW_EXPECT_TRUE( listShared[0] == baked );
    SW_EXPECT_NULL( listShared[1].get() );

    listShared.clear();
    baked.reset();
    SW_EXPECT_FALSE( AppearanceBakedMeshCache::tryGet( kKey, listShared ) );
    SW_EXPECT_FALSE( AppearanceBakedMeshCache::tryGet( kKey + 1, listShared ) );
}
