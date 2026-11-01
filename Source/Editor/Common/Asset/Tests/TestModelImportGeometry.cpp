// ModelImportGeometry — 임포트 메시를 고유 정점 + 인덱스로 만들기: 노멀이 있는 원본 정점은 한 번만 싣고, 노멀이 없는 정점은 삼각형마다 나눠 면 노멀을 준다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Model/ModelImportGeometry.h"
#include "Editor/Common/Asset/Model/ModelImportUtil.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 정점 넷(사각형)을 삼각형 둘이 나눠 쓰는 원본입니다. 정점 3 만 노멀이 없고, 모프 타깃 하나가 정점 2 만 옮깁니다. */
    ModelImportUtil::MergedMesh makeQuad()
    {
        ModelImportUtil::MergedMesh mesh;
        const float3                arrPosition[4] = {
            float3{0.0f, 0.0f, 0.0f},
            float3{1.0f, 0.0f, 0.0f},
            float3{1.0f, 0.0f, 1.0f},
            float3{0.0f, 0.0f, 1.0f}
        };
        for ( uint32 index = 0; index < 4; ++index )
        {
            ModelImportUtil::ImportVertex vertex;
            vertex._position   = arrPosition[index];
            vertex._normal     = float3::UnitY;
            vertex._bHasNormal = index != 3;
            mesh._listVertex.push_back( vertex );
        }
        mesh._listIndex = vector<uint32>{ 0, 2, 1, 0, 3, 2 };
        mesh._listMorphName.push_back( hashed_string( "Lift" ) );
        mesh._listMorphPosition.push_back( vector<float3>{
            float3{},
            float3{},
            float3{ 0.0f, 1.0f, 0.0f },
            float3{}
        } );
        mesh._listMorphNormal.push_back( vector<float3>( 4, float3{} ) );
        return mesh;
    }
} // namespace

/**
 * @brief [ModelImportGeometryTest] 노멀이 있는 원본 정점은 처음 쓰인 순서로 한 번씩 실리고 노멀이 없는 정점(3)은 쓰인 삼각형마다 나뉘는지, 모프 차이가 원본 정점을 따르는지 검증
 */
SW_TEST_CASE( ModelImportGeometryTest, SharesVerticesWithNormalsInFirstUseOrder )
{
    const ModelImportUtil::MergedMesh source = makeQuad();
    MeshAssetData                     data;
    ModelImportGeometry::makeMeshDataInOrder( source, 0, data );
    // 삼각형 (0, 2, 1) → 정점 0 · 1 · 2 가 원본 0 · 2 · 1. 삼각형 (0, 3, 2) → 원본 0 · 2 는 나눠 쓰고 원본 3 은 노멀이 없어 새 정점 3 이다.
    SW_REQUIRE_EQUAL( size_t{ 4 }, data._listVertex.size() );
    SW_EXPECT_TRUE( data._listIndex == ( vector<uint32>{ 0, 1, 2, 0, 3, 1 } ) );
    SW_EXPECT_NEAR( 1.0f, data._listVertex[1]._arrPosition[0], 1e-6f ); // 출력 1 = 원본 2 (1, 0, 1)
    SW_EXPECT_NEAR( 1.0f, data._listVertex[1]._arrPosition[2], 1e-6f );
    // 노멀 없는 정점은 그 삼각형의 면 노멀(+Y — 감김이 위를 본다)을 받는다.
    SW_EXPECT_NEAR( 1.0f, MathUtil::abs( data._listVertex[3]._arrNormal[1] ), 1e-5f );
    SW_REQUIRE_EQUAL( size_t{ 1 }, data._listMorphTarget.size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, data._listMorphTarget[0]._listDelta.size() );
    SW_EXPECT_EQUAL( 1u, data._listMorphTarget[0]._listDelta[0]._vertexIndex ); // 원본 2 의 출력 정점
}
