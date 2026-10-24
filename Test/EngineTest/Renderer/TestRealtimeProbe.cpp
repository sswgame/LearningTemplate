#include "pch.h"

#include "Core/Common/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/LightProbeVolumeComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Renderer/Light/RealtimeProbeUpdater.h"
#include "Engine/Renderer/Light/SceneVoxelizer.h"
#include "Engine/Scene/Scene.h"

#include "TestFramework/TestFramework.h"

// SceneVoxelGridTest · SceneVoxelizerTest · RealtimeProbeTest — 실시간 프로브 갱신의 CPU 쪽(RealtimeGlobalIllumination.md R5 · R6). 디바이스 없음(nogpu).
//
// GPU 컴퓨트(probeupdate.hlsl)는 여기서 돌지 않는다. 대신 컴퓨트가 읽는 두 입력 - 점유 복셀(식은 셰이더 traceVoxelRay 와 같다)과 입력 행 배치 - 를 CPU 에서 닫힌 값으로 본다.
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

    /** @brief 단위 큐브 메시를 든 오브젝트를 씬에 올립니다. */
    sw::MeshComponent* addCube( sw::GameObjectManager* pObjects, const utf8* pName, const sw::float3& position, sw::RHIBlendMode blend, bool bVisible )
    {
        sw::GameObject* pObject = pObjects->createGameObject( sw::hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
        if ( pMesh == nullptr )
            return nullptr;
        pMesh->setMesh( sw::MeshUtil::createUnitCube() );
        pMesh->setLocalPosition( position );
        pMesh->setBlendMode( blend );
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
    SW_ASSERT_TRUE( grid.traceRay( sw::float3{ 2.5f, 3.5f, 2.5f }, sw::float3{ 0.0f, -1.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_NEAR_EQUAL( 1.5f, hit._distance, 1.0e-4f ); // 3.5 → 2.0
    SW_EXPECT_NEAR_EQUAL( 1.0f, hit._normal._y, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, hit._normal._x, 0.0f );

    SW_EXPECT_FALSE( grid.traceRay( sw::float3{ 2.5f, 3.5f, 2.5f }, sw::float3{ 0.0f, -1.0f, 0.0f }, 1.0f, hit ) );  // 최대 거리 안에 닿지 못한다
    SW_EXPECT_FALSE( grid.traceRay( sw::float3{ 2.5f, 3.5f, 2.5f }, sw::float3{ 0.0f, 1.0f, 0.0f }, 100.0f, hit ) ); // 위로 간다
    SW_EXPECT_FALSE( grid.traceRay( sw::float3{ 2.5f, 3.5f, 2.5f }, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, hit ) ); // 층 위를 평행하게 지난다

    // 격자 밖에서 들어오는 광선: (2.5, 10, 2.5) 에서 내려오면 격자 윗면(4 m)에서 6 m 를 지나 층 윗면(2 m)까지 8 m.
    SW_ASSERT_TRUE( grid.traceRay( sw::float3{ 2.5f, 10.0f, 2.5f }, sw::float3{ 0.0f, -1.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_NEAR_EQUAL( 8.0f, hit._distance, 1.0e-4f );
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
    SW_ASSERT_TRUE( grid.traceRay( sw::float3{ 0.5f, 0.5f, 0.5f }, direction, 100.0f, hit ) );
    SW_EXPECT_NEAR_EQUAL( 2.5f / direction._x, hit._distance, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, hit._normal._x, 0.0f );

    SW_ASSERT_TRUE( grid.traceRay( sw::float3{ 3.5f, 1.0f, 1.0f }, sw::float3{ 0.0f, 1.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, hit._distance, 1.0e-4f );
}

/**
 * @brief [SceneVoxelizerTest] 불투명하고 보이는 메시만 칠하고, 반투명 · 숨긴 메시는 빛을 가리지 않는다. 큐브는 속이 빈 껍질이다
 */
SW_TEST_CASE( SceneVoxelizerTest, VoxelizesOpaqueVisibleMeshesOnly )
{
    sw::Scene              scene( "SceneVoxelizerOpaque" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );
    const sw::AABB bounds{
        sw::float3{-2.0f, -2.0f, -2.0f},
        sw::float3{ 2.0f,  2.0f,  2.0f}
    };

    SW_ASSERT_NOT_NULL( addCube( pObjects, "Glass", sw::float3{ 0.0f, 0.0f, 0.0f }, sw::RHIBlendMode::Transparent, true ) );
    SW_ASSERT_NOT_NULL( addCube( pObjects, "Hidden", sw::float3{ 0.0f, 0.0f, 0.0f }, sw::RHIBlendMode::Opaque, false ) );
    const sw::SceneVoxelGrid empty = sw::SceneVoxelizer::makeGrid( pObjects->getPrimitiveRegistry(), bounds, 0.25f );
    SW_ASSERT_FALSE( empty.isEmpty() );
    SW_EXPECT_EQUAL( 0u, empty.countOccupied() );

    SW_ASSERT_NOT_NULL( addCube( pObjects, "Wall", sw::float3{ 0.0f, 0.0f, 0.0f }, sw::RHIBlendMode::Opaque, true ) );
    const sw::SceneVoxelGrid solid = sw::SceneVoxelizer::makeGrid( pObjects->getPrimitiveRegistry(), bounds, 0.25f );
    SW_EXPECT_TRUE( solid.countOccupied() > 0u );
    SW_EXPECT_TRUE( solid.isOccupied( 6, 8, 8 ) );  // x = -0.5 면 근처
    SW_EXPECT_FALSE( solid.isOccupied( 8, 8, 8 ) ); // 속은 비었다

    sw::SceneVoxelHit hit;
    SW_ASSERT_TRUE( solid.traceRay( sw::float3{ -1.9f, 0.1f, 0.1f }, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_TRUE( hit._distance > 1.1f && hit._distance < 1.45f );
    SW_EXPECT_NEAR_EQUAL( -1.0f, hit._normal._x, 0.0f );
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
    SW_ASSERT_EQUAL( size_t( 2 ), layout._listFirstThread.size() );
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
    SW_EXPECT_NEAR_EQUAL( expected, projected._arrCoefficient[0], 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( expected, projected._arrCoefficient[2], 1.0e-3f );
    const sw::float3 up = projected.evaluateRadiance( sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.5f, up._x, 0.02f );

    const sw::SH9Color none = sw::RealtimeProbeUpdater::projectSky( sky, false );
    for ( float32 coefficient : none._arrCoefficient )
    {
        SW_EXPECT_NEAR_EQUAL( 0.0f, coefficient, 0.0f );
    }
}

/**
 * @brief [RealtimeProbeTest] 입력 행은 probeupdate.hlsl 의 배치대로 머리 · 하늘 SH · 볼륨 일곱 행으로 짜인다
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

    SW_ASSERT_EQUAL( size_t( sw::RealtimeProbeUpdater::kHeaderRowCount + sw::RealtimeProbeUpdater::kVolumeRowCount ), rows.size() );
    SW_EXPECT_EQUAL( 1u, sw::BitUtil::floatToBits( rows[0]._x ) );    // 볼륨 수
    SW_EXPECT_EQUAL( 9u, sw::BitUtil::floatToBits( rows[0]._y ) );    // 프레임 번호
    SW_EXPECT_EQUAL( 4u, sw::BitUtil::floatToBits( rows[0]._z ) );    // 라이트 수
    SW_EXPECT_NEAR_EQUAL( sky._arrCoefficient[0], rows[1]._x, 0.0f ); // 하늘 SH 의 원시 계수
    SW_EXPECT_NEAR_EQUAL( sky._arrCoefficient[26], rows[7]._z, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, rows[7]._w, 0.0f ); // 28 번째 칸은 0

    const uint32 first = sw::RealtimeProbeUpdater::kHeaderRowCount;
    SW_EXPECT_NEAR_EQUAL( 3.0f, rows[first]._z, 0.0f );      // 시작점 z
    SW_EXPECT_NEAR_EQUAL( 0.5f, rows[first]._w, 0.0f );      // 반사율
    SW_EXPECT_NEAR_EQUAL( 0.25f, rows[first + 1]._w, 0.0f ); // 섞는 비율
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
 * @brief [RealtimeProbeTest] 실시간 볼륨은 0 으로 채운 프로브 자리와 볼륨 원소 · 갱신 요청으로 모이고, 복셀 격자는 프리미티브 집합이 그대로면 캐시를 돌려준다
 * @details 요청 목록을 받지 않는 수집(미리보기 · 베이크)은 실시간 볼륨을 넣지 않는다 - 볼륨 원소가 빈 프로브 버퍼를 가리키지 않게.
 */
SW_TEST_CASE( RealtimeProbeTest, CollectsRealtimeVolumeWithPlaceholderAndRequest )
{
    sw::Scene              scene( "RealtimeProbeCollect" );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );
    sw::GameObject* pVolumeObject = pObjects->createGameObject( sw::hashed_string( "Volume" ) );
    SW_ASSERT_NOT_NULL( pVolumeObject );
    sw::LightProbeVolumeComponent* pVolume = pVolumeObject->addComponent<sw::LightProbeVolumeComponent>();
    SW_ASSERT_NOT_NULL( pVolume );
    pVolume->setExtent( sw::float3{ 4.0f, 2.0f, 4.0f } );
    pVolume->setSpacing( 2.0f );
    pVolume->setRealtime( true );
    SW_ASSERT_NOT_NULL( addCube( pObjects, "Wall", sw::float3{ 0.0f, 0.0f, 0.0f }, sw::RHIBlendMode::Opaque, true ) );

    sw::vector<sw::GPULight>             listLight;
    sw::vector<sw::float4>               listProbe;
    sw::vector<sw::RealtimeProbeRequest> listRequest;
    collectSceneLights( &scene, listLight, nullptr, &listProbe, nullptr, &listRequest );
    SW_ASSERT_EQUAL( size_t( 1 ), listLight.size() );
    SW_ASSERT_EQUAL( size_t( 1 ), listRequest.size() );
    SW_EXPECT_EQUAL( static_cast<uint32>( listLight[0]._directionType._w ), sw::shaderslot::kLightTypeProbeVolume );
    const size_t probeCount = static_cast<size_t>( pVolume->computeLayout().getProbeCount() );
    SW_EXPECT_EQUAL( probeCount * sw::shaderslot::kLightProbeFloat4Count, listProbe.size() );
    for ( const sw::float4& element : listProbe )
    {
        SW_EXPECT_NEAR_EQUAL( 0.0f, element._x + element._y + element._z + element._w, 0.0f ); // 자리는 0 - 값은 컴퓨트가 쓴다
    }
    const sw::RealtimeProbeRequest& request = listRequest[0];
    SW_EXPECT_EQUAL( static_cast<uint32>( probeCount ), request._probeCount );
    SW_EXPECT_EQUAL( pVolume->getRealtimeRayCount(), request._rayCount );
    SW_ASSERT_NOT_NULL( request._pVoxelGrid.get() );
    SW_EXPECT_TRUE( request._pVoxelGrid->countOccupied() > 0u );
    SW_EXPECT_EQUAL( listLight[0]._params._x, request._volumeElement._params._x );

    // 같은 집합이면 같은 격자(캐시), 메시가 하나 늘면 새 격자.
    sw::vector<sw::RealtimeProbeRequest> again;
    collectSceneLights( &scene, listLight, nullptr, &listProbe, nullptr, &again );
    SW_ASSERT_EQUAL( size_t( 1 ), again.size() );
    SW_EXPECT_TRUE( again[0]._pVoxelGrid == request._pVoxelGrid );
    SW_ASSERT_NOT_NULL( addCube( pObjects, "Second", sw::float3{ 1.0f, 0.0f, 0.0f }, sw::RHIBlendMode::Opaque, true ) );
    collectSceneLights( &scene, listLight, nullptr, &listProbe, nullptr, &again );
    SW_ASSERT_EQUAL( size_t( 1 ), again.size() );
    SW_EXPECT_TRUE( again[0]._pVoxelGrid != request._pVoxelGrid );

    // 요청 목록을 받지 않는 수집은 실시간 볼륨을 넣지 않는다.
    collectSceneLights( &scene, listLight, nullptr, &listProbe );
    SW_EXPECT_EQUAL( size_t( 0 ), listLight.size() );
    SW_EXPECT_EQUAL( size_t( 0 ), listProbe.size() );
}
