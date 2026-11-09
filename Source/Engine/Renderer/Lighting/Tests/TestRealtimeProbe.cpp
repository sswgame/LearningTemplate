#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/RealtimeProbeUpdater.h"
#include "Engine/Renderer/Lighting/SceneVoxelizer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/LightProbeVolumeComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"

// SceneVoxelGridTest · SceneVoxelizerTest · RealtimeProbeTest — 실시간 프로브 갱신의 CPU 쪽(결정 기록 5-5 R5 · R6). 디바이스 없음(nogpu).
//
// GPU 컴퓨트(probe_update.hlsl)는 여기서 돌지 않는다. 대신 컴퓨트가 읽는 두 입력 - 점유 복셀(식은 셰이더 traceVoxelRay 와 같다)과 입력 행 배치 - 를 CPU 에서 닫힌 값으로 본다.
// 컴퓨트의 갱신 결과(방 안이 밝아지는지)는 시나리오 realtimegi 가 네 백엔드에서 본다.

namespace
{
    /** @brief y = @p height 평면의 한 변 @p size 정사각형(x, z 가 0 .. size)을 삼각형 둘로 칠합니다. */
    void voxelizeHorizontalSquare( sw::SceneVoxelGrid& inoutGrid, float32 height, float32 size )
    {
        inoutGrid.voxelizeTriangle( sw::float3{ 0.0f, height, 0.0f }, sw::float3{ size, height, 0.0f }, sw::float3{ 0.0f, height, size } );
        inoutGrid.voxelizeTriangle( sw::float3{ size, height, 0.0f }, sw::float3{ size, height, size }, sw::float3{ 0.0f, height, size } );
    }

    /** @brief x = @p position 평면의 한 변 @p size 정사각형(y, z 가 0 .. size)을 삼각형 둘로 칠합니다. */
    void voxelizeWallX( sw::SceneVoxelGrid& inoutGrid, float32 position, float32 size )
    {
        inoutGrid.voxelizeTriangle( sw::float3{ position, 0.0f, 0.0f }, sw::float3{ position, size, 0.0f }, sw::float3{ position, 0.0f, size } );
        inoutGrid.voxelizeTriangle( sw::float3{ position, size, 0.0f }, sw::float3{ position, size, size }, sw::float3{ position, 0.0f, size } );
    }

    /** @brief 0 .. 4 m 상자를 한 변 1 m 칸으로 나눈 빈 격자입니다. */
    sw::SceneVoxelGrid makeFourMeterGrid()
    {
        return sw::SceneVoxelGrid::make( sw::AABB{
                                             sw::float3{0.0f, 0.0f, 0.0f},
                                             sw::float3{4.0f, 4.0f, 4.0f}
        },
                                         1.0f );
    }

    /** @brief 반투명 머티리얼입니다 — 반투명은 머티리얼이 정한다(메시 컴포넌트에는 블렌드 칸이 없다). */
    sw::shared_ptr<sw::Material> makeTranslucentMaterial()
    {
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        SW_EXPECT_TRUE( material->loadFromXML( "<MaterialDef formatVersion=\"0\" name=\"ProbeGlass\" shaderPath=\"engine/shaders/forward_lit.hlsl\" alphaMode=\"Blend\">"
                                               "<_permutations><_alwaysDefines><item>MATERIAL_BLEND_TRANSLUCENT</item></_alwaysDefines></_permutations></MaterialDef>" ) );
        return material;
    }

    /** @brief 단위 큐브 메시를 든 오브젝트를 씬에 올립니다. */
    sw::MeshComponent* addCube( sw::GameObjectManager* pObjects, const utf8* pName, const sw::float3& position, sw::Material* pMaterial, bool bVisible )
    {
        sw::GameObject* pObject = pObjects->createGameObject( sw::hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
        if ( pMesh == nullptr )
            return nullptr;
        pMesh->setMesh( sw::MeshUtil::createUnitCube() );
        pMesh->setLocalPosition( position );
        pMesh->setMaterial( pMaterial ); // nullptr 이면 씬 기본(불투명)
        pMesh->setVisible( bVisible );
        return pMesh;
    }
} // namespace

/**
 * @brief [SceneVoxelGridTest] 칸 수는 축마다 상한(128)을 넘지 않고, 넘을 상자는 칸 크기를 키워 맞춘다. 잘못된 입력은 빈 격자다
 */
SW_TEST_CASE( SceneVoxelGridTest, MakeCapsCellsPerAxisAndRejectsBadInput )
{
    const sw::SceneVoxelGrid wide = sw::SceneVoxelGrid::make( sw::AABB{
                                                                  sw::float3{   0.0f,  0.0f,  0.0f},
                                                                  sw::float3{1000.0f, 10.0f, 10.0f}
    },
                                                              1.0f );
    SW_EXPECT_FALSE( wide.isEmpty() );
    SW_EXPECT_TRUE( wide._dimX <= sw::SceneVoxelGrid::kMaxCellsPerAxis );
    SW_EXPECT_TRUE( wide._cellSize * static_cast<float32>( wide._dimX ) >= 1000.0f - 1.0e-3f ); // 상자를 덮는다
    SW_EXPECT_EQUAL( size_t( ( wide.getCellCount() + 31u ) / 32u ), wide._listWord.size() );

    const sw::SceneVoxelGrid small = makeFourMeterGrid();
    SW_EXPECT_EQUAL( 4u, small._dimX );
    SW_EXPECT_EQUAL( 64u, small.getCellCount() );
    SW_EXPECT_EQUAL( 0u, small.countOccupied() );

    SW_EXPECT_TRUE( sw::SceneVoxelGrid::make( sw::AABB{
                                                  sw::float3{1.0f, 1.0f, 1.0f},
                                                  sw::float3{0.0f, 0.0f, 0.0f}
    },
                                              1.0f )
                        .isEmpty() );
    SW_EXPECT_TRUE( sw::SceneVoxelGrid::make( sw::AABB{
                                                  sw::float3{0.0f, 0.0f, 0.0f},
                                                  sw::float3{1.0f, 1.0f, 1.0f}
    },
                                              0.0f )
                        .isEmpty() );
}

/**
 * @brief [SceneVoxelGridTest] 수평 삼각형은 자기 높이의 칸 층에만 켜지고, 삼각형 밖 칸은 켜지지 않는다
 */
SW_TEST_CASE( SceneVoxelGridTest, VoxelizesHorizontalTriangleIntoOneLayer )
{
    sw::SceneVoxelGrid grid = makeFourMeterGrid();
    grid.voxelizeTriangle( sw::float3{ 0.1f, 1.5f, 0.1f }, sw::float3{ 3.9f, 1.5f, 0.1f }, sw::float3{ 0.1f, 1.5f, 3.9f } );
    SW_EXPECT_TRUE( grid.countOccupied() > 0u );
    for ( uint32 cellZ = 0; cellZ < 4; ++cellZ )
    {
        for ( uint32 cellY = 0; cellY < 4; ++cellY )
        {
            for ( uint32 cellX = 0; cellX < 4; ++cellX )
            {
                if ( grid.isOccupied( cellX, cellY, cellZ ) )
                    SW_EXPECT_EQUAL( 1u, cellY );
            }
        }
    }
    SW_EXPECT_TRUE( grid.isOccupied( 0, 1, 0 ) );
    SW_EXPECT_FALSE( grid.isOccupied( 3, 1, 3 ) ); // 빗변(x + z = 4) 너머
    SW_EXPECT_FALSE( grid.isOccupied( 9, 1, 0 ) ); // 범위 밖은 켜지지 않는다
}

/**
 * @brief [SceneVoxelGridTest] 칸을 대각선으로 가르는 가는 삼각형도 지나는 칸을 빠짐없이 켠다(보수적 복셀화)
 */
SW_TEST_CASE( SceneVoxelGridTest, VoxelizesThinDiagonalTriangle )
{
    sw::SceneVoxelGrid grid = makeFourMeterGrid();
    grid.voxelizeTriangle( sw::float3{ 0.01f, 0.01f, 0.01f }, sw::float3{ 3.99f, 3.99f, 3.99f }, sw::float3{ 3.99f, 3.99f, 3.9f } );
    for ( uint32 cell = 0; cell < 4; ++cell )
    {
        SW_EXPECT_TRUE( grid.isOccupied( cell, cell, cell ) );
    }
}

/**
 * @brief [SceneVoxelGridTest] 위에서 내려오는 광선은 켜진 층의 윗면에서 거리 · 노멀(+y)로 닿는다. 최대 거리 밖 · 반대 방향 · 평행 광선은 닿지 않는다
 */
SW_TEST_CASE( SceneVoxelGridTest, TraceRayHitsTopFace )
{
    sw::SceneVoxelGrid grid = makeFourMeterGrid();
    voxelizeHorizontalSquare( grid, 1.5f, 4.0f ); // 칸 층 y = 1 (1 .. 2 m)

    sw::SceneVoxelHit hit;
    SW_REQUIRE_TRUE( grid.traceRay( sw::float3{ 2.5f, 3.5f, 2.5f }, sw::float3{ 0.0f, -1.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_NEAR( 1.5f, hit._distance, 1.0e-4f ); // 3.5 → 2.0
    SW_EXPECT_NEAR( 1.0f, hit._normal._y, 0.0f );
    SW_EXPECT_NEAR( 0.0f, hit._normal._x, 0.0f );

    SW_EXPECT_FALSE( grid.traceRay( sw::float3{ 2.5f, 3.5f, 2.5f }, sw::float3{ 0.0f, -1.0f, 0.0f }, 1.0f, hit ) );  // 최대 거리 안에 닿지 못한다
    SW_EXPECT_FALSE( grid.traceRay( sw::float3{ 2.5f, 3.5f, 2.5f }, sw::float3{ 0.0f, 1.0f, 0.0f }, 100.0f, hit ) ); // 위로 간다
    SW_EXPECT_FALSE( grid.traceRay( sw::float3{ 2.5f, 3.5f, 2.5f }, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, hit ) ); // 층 위를 평행하게 지난다

    // 격자 밖에서 들어오는 광선: (2.5, 10, 2.5) 에서 내려오면 격자 윗면(4 m)에서 6 m 를 지나 층 윗면(2 m)까지 8 m.
    SW_REQUIRE_TRUE( grid.traceRay( sw::float3{ 2.5f, 10.0f, 2.5f }, sw::float3{ 0.0f, -1.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_NEAR( 8.0f, hit._distance, 1.0e-4f );
}

/**
 * @brief [SceneVoxelGridTest] 비스듬한 광선은 벽의 앞면(-x)에서 닿고, 켜진 칸 안에서 시작한 광선은 거리 0 으로 닿는다
 */
SW_TEST_CASE( SceneVoxelGridTest, TraceRayHitsWallAndInsideStart )
{
    sw::SceneVoxelGrid grid = makeFourMeterGrid();
    voxelizeWallX( grid, 3.5f, 4.0f ); // 칸 열 x = 3 (3 .. 4 m)

    const sw::float3  direction = sw::float3{ 1.0f, 0.3f, 0.2f }.normalize();
    sw::SceneVoxelHit hit;
    SW_REQUIRE_TRUE( grid.traceRay( sw::float3{ 0.5f, 0.5f, 0.5f }, direction, 100.0f, hit ) );
    SW_EXPECT_NEAR( 2.5f / direction._x, hit._distance, 1.0e-3f );
    SW_EXPECT_NEAR( -1.0f, hit._normal._x, 0.0f );

    SW_REQUIRE_TRUE( grid.traceRay( sw::float3{ 3.5f, 1.0f, 1.0f }, sw::float3{ 0.0f, 1.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_NEAR( 0.0f, hit._distance, 1.0e-4f );
}

/**
 * @brief [SceneVoxelizerTest] 불투명하고 보이는 메시만 칠하고, 반투명 · 숨긴 메시는 빛을 가리지 않는다. 큐브는 속이 빈 껍질이다
 */
SW_TEST_CASE( SceneVoxelizerTest, VoxelizesOpaqueVisibleMeshesOnly )
{
    const sw::shared_ptr<sw::Material> glass = makeTranslucentMaterial(); // 씬보다 먼저 만든다 — 메시가 원시 포인터로 든다
    sw::Scene                          scene( "SceneVoxelizerOpaque" );
    sw::GameObjectManager*             pObjects = scene.getObjectManager();
    SW_REQUIRE_NOT_NULL( pObjects );
    const sw::AABB bounds{
        sw::float3{-2.0f, -2.0f, -2.0f},
        sw::float3{ 2.0f,  2.0f,  2.0f}
    };

    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Glass", sw::float3{ 0.0f, 0.0f, 0.0f }, glass.get(), true ) );
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Hidden", sw::float3{ 0.0f, 0.0f, 0.0f }, nullptr, false ) );
    const sw::SceneVoxelGrid empty = sw::SceneVoxelizer::makeGrid( pObjects->getPrimitiveRegistry(), bounds, 0.25f );
    SW_REQUIRE_FALSE( empty.isEmpty() );
    SW_EXPECT_EQUAL( 0u, empty.countOccupied() );

    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Wall", sw::float3{ 0.0f, 0.0f, 0.0f }, nullptr, true ) );
    const sw::SceneVoxelGrid solid = sw::SceneVoxelizer::makeGrid( pObjects->getPrimitiveRegistry(), bounds, 0.25f );
    SW_EXPECT_TRUE( solid.countOccupied() > 0u );
    SW_EXPECT_TRUE( solid.isOccupied( 6, 8, 8 ) );  // x = -0.5 면 근처
    SW_EXPECT_FALSE( solid.isOccupied( 8, 8, 8 ) ); // 속은 비었다

    sw::SceneVoxelHit hit;
    SW_REQUIRE_TRUE( solid.traceRay( sw::float3{ -1.9f, 0.1f, 0.1f }, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_TRUE( hit._distance > 1.1f && hit._distance < 1.45f );
    SW_EXPECT_NEAR( -1.0f, hit._normal._x, 0.0f );
}

/**
 * @brief [RealtimeProbeTest] 요청들의 버퍼 배치는 앞 요청들의 복셀 단어 · 기록 원소 · 프로브 수를 누적한다
 */
SW_TEST_CASE( RealtimeProbeTest, PlanLayoutAccumulatesOffsets )
{
    sw::vector<sw::RealtimeProbeRequest> listRequest( 2 );
    listRequest[0]._probeCount = 18;
    listRequest[0]._pVoxelGrid = sw::make_shared<sw::SceneVoxelGrid>( makeFourMeterGrid() ); // 64 칸 → 단어 2
    listRequest[1]._probeCount = 5;
    listRequest[1]._pVoxelGrid = sw::make_shared<sw::SceneVoxelGrid>( sw::SceneVoxelGrid::make( sw::AABB{
                                                                                                    sw::float3{},
                                                                                                    sw::float3{ 8.0f, 8.0f, 8.0f }
    },
                                                                                                1.0f ) ); // 512 칸 → 단어 16

    const sw::RealtimeProbeLayout layout = sw::RealtimeProbeUpdater::planLayout( listRequest );
    SW_REQUIRE_EQUAL( size_t( 2 ), layout._listFirstThread.size() );
    SW_EXPECT_EQUAL( 0u, layout._listFirstThread[0] );
    SW_EXPECT_EQUAL( 18u, layout._listFirstThread[1] );
    SW_EXPECT_EQUAL( 0u, layout._listVoxelWordOffset[0] );
    SW_EXPECT_EQUAL( 2u, layout._listVoxelWordOffset[1] );
    SW_EXPECT_EQUAL( 0u, layout._listHistoryOffset[0] );
    SW_EXPECT_EQUAL( 18u * sw::SH9Color::kGPUFloat4Count, layout._listHistoryOffset[1] );
    SW_EXPECT_EQUAL( 23u, layout._totalProbes );
    SW_EXPECT_EQUAL( 18u, layout._totalVoxelWords );
    SW_EXPECT_EQUAL( 23u * sw::SH9Color::kGPUFloat4Count, layout._totalHistoryElements );
}

/**
 * @brief [RealtimeProbeTest] 한 색 하늘의 SH 투영은 L0 계수만 2√π × 색이다. 하늘이 없으면 0 이다(폴백 없음)
 */
SW_TEST_CASE( RealtimeProbeTest, ProjectSkyConstantRadiance )
{
    sw::SkyGradient sky{};
    sky._zenithColor  = sw::float3{ 0.5f, 0.5f, 0.5f };
    sky._horizonColor = sw::float3{ 0.5f, 0.5f, 0.5f };
    sky._groundColor  = sw::float3{ 0.5f, 0.5f, 0.5f };
    sky._intensity    = 1.0f;

    const sw::SH9Color projected = sw::RealtimeProbeUpdater::projectSky( sky, true );
    const float32      expected  = 0.5f * 2.0f * sw::MathUtil::sqrt( sw::MathUtil::kPi );
    SW_EXPECT_NEAR( expected, projected._arrCoefficient[0], 1.0e-3f );
    SW_EXPECT_NEAR( expected, projected._arrCoefficient[2], 1.0e-3f );
    const sw::float3 up = projected.evaluateRadiance( sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_EXPECT_NEAR( 0.5f, up._x, 0.02f );

    const sw::SH9Color none = sw::RealtimeProbeUpdater::projectSky( sky, false );
    for ( float32 coefficient : none._arrCoefficient )
    {
        SW_EXPECT_NEAR( 0.0f, coefficient, 0.0f );
    }
}

/**
 * @brief [RealtimeProbeTest] 입력 행은 probe_update.hlsl 의 배치대로 머리 · 하늘 SH · 볼륨 일곱 행으로 짜인다
 */
SW_TEST_CASE( RealtimeProbeTest, BuildParamRowsFollowShaderLayout )
{
    sw::RealtimeProbeRequest request{};
    request._volumeElement._colorIntensity = sw::float4{ 1.0f, 2.0f, 3.0f, 0.0f };
    request._volumeElement._directionType  = sw::float4{ 2.0f, 2.0f, 2.0f, static_cast<float32>( sw::shaderslot::kLightTypeProbeVolume ) };
    request._volumeElement._params         = sw::float4{ 3.0f, 2.0f, 3.0f, 5.0f }; // 프로브 3 × 2 × 3, 첫 프로브 번호 5
    request._probeCount                    = 18;
    request._rayCount                      = 64;
    request._updatePeriod                  = 8;
    request._blend                         = 0.25f;
    request._bounceAlbedo                  = 0.5f;
    request._pVoxelGrid                    = sw::make_shared<sw::SceneVoxelGrid>( makeFourMeterGrid() );
    sw::vector<sw::RealtimeProbeRequest> listRequest;
    listRequest.push_back( request );

    const sw::RealtimeProbeLayout layout = sw::RealtimeProbeUpdater::planLayout( listRequest );
    const sw::SH9Color            sky    = sw::SH9Color::makeConstant( sw::float3{ 1.0f, 2.0f, 3.0f } );
    const sw::vector<uint32>      warmup{ 3u };
    const sw::vector<sw::float4>  rows = sw::RealtimeProbeUpdater::makeParamRows( listRequest, layout, warmup, sky, 9u, 4u );

    SW_REQUIRE_EQUAL( size_t( sw::RealtimeProbeUpdater::kHeaderRowCount + sw::RealtimeProbeUpdater::kVolumeRowCount ), rows.size() );
    SW_EXPECT_EQUAL( 1u, sw::BitUtil::floatToBits( rows[0]._x ) ); // 볼륨 수
    SW_EXPECT_EQUAL( 9u, sw::BitUtil::floatToBits( rows[0]._y ) ); // 프레임 번호
    SW_EXPECT_EQUAL( 4u, sw::BitUtil::floatToBits( rows[0]._z ) ); // 라이트 수
    SW_EXPECT_NEAR( sky._arrCoefficient[0], rows[1]._x, 0.0f );    // 하늘 SH 의 원시 계수
    SW_EXPECT_NEAR( sky._arrCoefficient[26], rows[7]._z, 0.0f );
    SW_EXPECT_NEAR( 0.0f, rows[7]._w, 0.0f ); // 28 번째 칸은 0

    const uint32 first = sw::RealtimeProbeUpdater::kHeaderRowCount;
    SW_EXPECT_NEAR( 3.0f, rows[first]._z, 0.0f );      // 시작점 z
    SW_EXPECT_NEAR( 0.5f, rows[first]._w, 0.0f );      // 반사율
    SW_EXPECT_NEAR( 0.25f, rows[first + 1]._w, 0.0f ); // 섞는 비율
    SW_EXPECT_EQUAL( 3u, sw::BitUtil::floatToBits( rows[first + 2]._x ) );
    SW_EXPECT_EQUAL( 5u * sw::SH9Color::kGPUFloat4Count, sw::BitUtil::floatToBits( rows[first + 2]._w ) ); // 프로브 버퍼 안의 첫 float4
    SW_EXPECT_EQUAL( 4u, sw::BitUtil::floatToBits( rows[first + 4]._x ) );                                 // 복셀 칸 수
    SW_EXPECT_EQUAL( 0u, sw::BitUtil::floatToBits( rows[first + 4]._w ) );                                 // 복셀 단어 시작
    SW_EXPECT_EQUAL( 18u, sw::BitUtil::floatToBits( rows[first + 5]._z ) );                                // 프로브 수
    SW_EXPECT_EQUAL( 64u, sw::BitUtil::floatToBits( rows[first + 5]._w ) );                                // 광선 수
    SW_EXPECT_EQUAL( 8u, sw::BitUtil::floatToBits( rows[first + 6]._x ) );                                 // 갱신 주기
    SW_EXPECT_EQUAL( 3u, sw::BitUtil::floatToBits( rows[first + 6]._y ) );                                 // 쌓인 갱신 횟수
}

/**
 * @brief [SceneVoxelGridTest] 발광 복사휘도는 공유 지수(R9G9B9E5)로 접혀 상대 오차 안에서 되돌아오고, 0 · 음수는 0 이다
 */
SW_TEST_CASE( SceneVoxelGridTest, EmissionPackRoundTrips )
{
    const sw::float3 radiance{ 3.5f, 0.25f, 12.0f };
    const sw::float3 back = sw::SceneVoxelGrid::unpackEmission( sw::SceneVoxelGrid::packEmission( radiance ) );
    SW_EXPECT_NEAR( 3.5f, back._x, 3.5f * 0.01f + 0.02f );
    SW_EXPECT_NEAR( 0.25f, back._y, 0.03f ); // 가장 큰 채널의 지수를 나눠 쓰므로 작은 채널은 그 단위(12 / 512)로 둥글린다
    SW_EXPECT_NEAR( 12.0f, back._z, 12.0f * 0.01f );
    SW_EXPECT_EQUAL( 0u, sw::SceneVoxelGrid::packEmission( sw::float3{} ) );
    SW_EXPECT_EQUAL( 0u, sw::SceneVoxelGrid::packEmission( sw::float3{ -1.0f, -2.0f, -3.0f } ) );
}

/**
 * @brief [SceneVoxelGridTest] 발광 삼각형은 칸을 켜고 칸의 발광을 둔다. 발광 없는 격자는 발광 배열이 비어 있다(칸 수만큼 0 을 두지 않는다)
 */
SW_TEST_CASE( SceneVoxelGridTest, EmissiveTriangleWritesCellEmission )
{
    sw::SceneVoxelGrid grid = makeFourMeterGrid();
    grid.voxelizeTriangle( sw::float3{ 0.1f, 0.5f, 0.1f }, sw::float3{ 3.9f, 0.5f, 0.1f }, sw::float3{ 0.1f, 0.5f, 3.9f } );
    SW_EXPECT_TRUE( grid._listEmission.empty() );
    grid.voxelizeEmissiveTriangle( sw::float3{ 0.1f, 2.5f, 0.1f }, sw::float3{ 3.9f, 2.5f, 0.1f }, sw::float3{ 0.1f, 2.5f, 3.9f }, sw::float3{ 4.0f, 2.0f, 1.0f } );
    SW_REQUIRE_EQUAL( size_t( grid.getCellCount() ), grid._listEmission.size() );
    SW_EXPECT_TRUE( grid.isOccupied( 0, 2, 0 ) );
    SW_EXPECT_NEAR( 4.0f, grid.findEmission( 0, 2, 0 )._x, 0.05f );
    SW_EXPECT_NEAR( 0.0f, grid.findEmission( 0, 0, 0 )._x, 0.0f ); // 발광하지 않는 면의 칸
}

/**
 * @brief [RealtimeProbeTest] 발광 칸은 그 볼륨의 점유 단어 바로 뒤에 놓이고, 갱신 행에 발광 단어 번호 · 배율이 실린다. 발광 없는 볼륨은 kNoEmission 이다
 */
SW_TEST_CASE( RealtimeProbeTest, EmissionWordsFollowOccupancy )
{
    sw::SceneVoxelGrid emissive = makeFourMeterGrid(); // 64 칸 → 점유 단어 2 + 발광 단어 64
    emissive.voxelizeEmissiveTriangle( sw::float3{ 0.1f, 2.5f, 0.1f }, sw::float3{ 3.9f, 2.5f, 0.1f }, sw::float3{ 0.1f, 2.5f, 3.9f }, sw::float3{ 1.0f, 1.0f, 1.0f } );
    sw::vector<sw::RealtimeProbeRequest> listRequest( 2 );
    listRequest[0]._probeCount        = 1;
    listRequest[0]._emissiveIntensity = 2.5f;
    listRequest[0]._pVoxelGrid        = sw::make_shared<sw::SceneVoxelGrid>( std::move( emissive ) );
    listRequest[1]._probeCount        = 1;
    listRequest[1]._pVoxelGrid        = sw::make_shared<sw::SceneVoxelGrid>( makeFourMeterGrid() );

    const sw::RealtimeProbeLayout layout = sw::RealtimeProbeUpdater::planLayout( listRequest );
    SW_REQUIRE_EQUAL( size_t( 2 ), layout._listEmissionWordOffset.size() );
    SW_EXPECT_EQUAL( 2u, layout._listEmissionWordOffset[0] );
    SW_EXPECT_EQUAL( 66u, layout._listVoxelWordOffset[1] );
    SW_EXPECT_EQUAL( sw::RealtimeProbeUpdater::kNoEmission, layout._listEmissionWordOffset[1] );
    SW_EXPECT_EQUAL( 68u, layout._totalVoxelWords );

    const sw::vector<sw::float4> rows     = sw::RealtimeProbeUpdater::makeParamRows( listRequest, layout, {}, sw::SH9Color{}, 0u, 0u );
    const uint32                 schedule = sw::RealtimeProbeUpdater::kHeaderRowCount + 6u;
    SW_EXPECT_EQUAL( 2u, sw::BitUtil::floatToBits( rows[schedule]._z ) );
    SW_EXPECT_NEAR( 2.5f, rows[schedule]._w, 0.0f );
    SW_EXPECT_EQUAL( sw::RealtimeProbeUpdater::kNoEmission, sw::BitUtil::floatToBits( rows[schedule + sw::RealtimeProbeUpdater::kVolumeRowCount]._z ) );
}

/**
 * @brief [RealtimeProbeTest] 하늘 가림 · 발광 배율의 기본값은 0 이다 — 설정을 적지 않은 씬은 이전 그림 그대로다. 볼륨 원소의 colorIntensity.w 가 하늘 가림 세기를 싣는다
 */
SW_TEST_CASE( RealtimeProbeTest, GIExtrasDefaultToOff )
{
    sw::Scene              scene( "RealtimeProbeGIExtras" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_REQUIRE_NOT_NULL( pObjects );
    sw::GameObject* pVolumeObject = pObjects->createGameObject( sw::hashed_string( "Volume" ) );
    SW_REQUIRE_NOT_NULL( pVolumeObject );
    sw::LightProbeVolumeComponent* pVolume = pVolumeObject->addComponent<sw::LightProbeVolumeComponent>();
    SW_REQUIRE_NOT_NULL( pVolume );
    SW_EXPECT_NEAR( 0.0f, pVolume->getRealtimeSkyOcclusion(), 0.0f );
    SW_EXPECT_NEAR( 0.0f, pVolume->getRealtimeEmissiveIntensity(), 0.0f );
    pVolume->setExtent( sw::float3{ 4.0f, 2.0f, 4.0f } );
    pVolume->setSpacing( 2.0f );
    pVolume->setRealtime( true );
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Wall", sw::float3{ 0.0f, 0.0f, 0.0f }, nullptr, true ) );

    sw::vector<sw::GPULight>             listLight;
    sw::LightProbeElementBuilder         probeElement;
    sw::vector<sw::RealtimeProbeRequest> listRequest;
    collectSceneLights( &scene, listLight, nullptr, &probeElement, nullptr, &listRequest );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRequest.size() );
    SW_EXPECT_NEAR( 0.0f, listRequest[0]._volumeElement._colorIntensity._w, 0.0f );
    SW_EXPECT_NEAR( 0.0f, listRequest[0]._emissiveIntensity, 0.0f );
    SW_EXPECT_TRUE( listRequest[0]._pVoxelGrid->_listEmission.empty() ); // 발광 머티리얼이 없다
}

/**
 * @brief [RealtimeProbeTest] 실시간 볼륨은 0 으로 채운 프로브 자리와 볼륨 원소 · 갱신 요청으로 모이고, 복셀 격자는 프리미티브 집합이 그대로면 캐시를 돌려준다
 * @details 요청 목록을 받지 않는 수집(미리보기 · 베이크)은 실시간 볼륨을 넣지 않는다 - 볼륨 원소가 빈 프로브 버퍼를 가리키지 않게.
 */
SW_TEST_CASE( RealtimeProbeTest, CollectsRealtimeVolumeWithPlaceholderAndRequest )
{
    sw::Scene              scene( "RealtimeProbeCollect" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_REQUIRE_NOT_NULL( pObjects );
    sw::GameObject* pVolumeObject = pObjects->createGameObject( sw::hashed_string( "Volume" ) );
    SW_REQUIRE_NOT_NULL( pVolumeObject );
    sw::LightProbeVolumeComponent* pVolume = pVolumeObject->addComponent<sw::LightProbeVolumeComponent>();
    SW_REQUIRE_NOT_NULL( pVolume );
    pVolume->setExtent( sw::float3{ 4.0f, 2.0f, 4.0f } );
    pVolume->setSpacing( 2.0f );
    pVolume->setRealtime( true );
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Wall", sw::float3{ 0.0f, 0.0f, 0.0f }, nullptr, true ) );

    sw::vector<sw::GPULight>             listLight;
    sw::LightProbeElementBuilder         probeElement;
    sw::vector<sw::RealtimeProbeRequest> listRequest;
    collectSceneLights( &scene, listLight, nullptr, &probeElement, nullptr, &listRequest );
    SW_REQUIRE_EQUAL( size_t( 1 ), listLight.size() );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRequest.size() );
    SW_EXPECT_EQUAL( static_cast<uint32>( listLight[0]._directionType._w ), sw::shaderslot::kLightTypeProbeVolume );
    const size_t                                       probeCount = static_cast<size_t>( pVolume->computeLayout().getProbeCount() );
    const sw::shared_ptr<const sw::vector<sw::float4>> pProbe     = probeElement.getElements();
    SW_REQUIRE_NOT_NULL( pProbe.get() );
    SW_EXPECT_EQUAL( probeCount * sw::shaderslot::kLightProbeFloat4Count, pProbe->size() );
    for ( const sw::float4& element : *pProbe )
    {
        SW_EXPECT_NEAR( 0.0f, element._x + element._y + element._z + element._w, 0.0f ); // 자리는 0 - 값은 컴퓨트가 쓴다
    }
    const sw::RealtimeProbeRequest& request = listRequest[0];
    SW_EXPECT_EQUAL( static_cast<uint32>( probeCount ), request._probeCount );
    SW_EXPECT_EQUAL( pVolume->getRealtimeRayCount(), request._rayCount );
    SW_REQUIRE_NOT_NULL( request._pVoxelGrid.get() );
    SW_EXPECT_TRUE( request._pVoxelGrid->countOccupied() > 0u );
    SW_EXPECT_EQUAL( listLight[0]._params._x, request._volumeElement._params._x );

    // 같은 집합이면 같은 격자(캐시), 메시가 하나 늘면 새 격자.
    // 프로브 원소 묶음도 내용(자리 수)이 그대로면 같은 포인터다 - 렌더 스레드가 다시 올리지 않는다.
    sw::vector<sw::RealtimeProbeRequest> again;
    collectSceneLights( &scene, listLight, nullptr, &probeElement, nullptr, &again );
    SW_REQUIRE_EQUAL( size_t( 1 ), again.size() );
    SW_EXPECT_TRUE( again[0]._pVoxelGrid == request._pVoxelGrid );
    SW_EXPECT_TRUE( probeElement.getElements() == pProbe );
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Second", sw::float3{ 1.0f, 0.0f, 0.0f }, nullptr, true ) );
    collectSceneLights( &scene, listLight, nullptr, &probeElement, nullptr, &again );
    SW_REQUIRE_EQUAL( size_t( 1 ), again.size() );
    SW_EXPECT_TRUE( again[0]._pVoxelGrid != request._pVoxelGrid );

    // 요청 목록을 받지 않는 수집은 실시간 볼륨을 넣지 않는다.
    collectSceneLights( &scene, listLight, nullptr, &probeElement );
    SW_EXPECT_EQUAL( size_t( 0 ), listLight.size() );
    SW_EXPECT_TRUE( probeElement.getElements() == nullptr );
}
