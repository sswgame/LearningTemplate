#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/PackUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Graphics/Lighting/SceneVoxelScene.h"
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
#include "Engine/Renderer/Lighting/Tests/SceneVoxelReference.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/LightProbeVolumeComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"

// SceneVoxelGridTest · SceneVoxelizerTest · RealtimeProbeTest — 실시간 프로브 갱신의 CPU 쪽(결정 기록 5-5 R5 · R6). 디바이스 없음(nogpu).
//
// GPU 컴퓨트(scene_voxelize.hlsl · probe_update.hlsl)는 여기서 돌지 않는다. 대신 컴퓨트가 읽는 입력 행(칠할 일 · 프로브 갱신)을 CPU 기준 구현(SceneVoxelReference — 셰이더와 같은 식)으로
// 돌려 닫힌 값으로 본다.
// 컴퓨트의 갱신 결과(방 안이 밝아지는지)는 시나리오 realtimegi 가 네 백엔드에서 본다.

namespace
{
    /** @brief y = @p height 평면의 한 변 @p size 정사각형(x, z 가 0 .. size)을 삼각형 둘로 칠합니다. */
    void voxelizeHorizontalSquare( sw::test::SceneVoxelReference& inoutGrid, float32 height, float32 size )
    {
        inoutGrid.voxelizeTriangle( sw::float3{ 0.0f, height, 0.0f }, sw::float3{ size, height, 0.0f }, sw::float3{ 0.0f, height, size } );
        inoutGrid.voxelizeTriangle( sw::float3{ size, height, 0.0f }, sw::float3{ size, height, size }, sw::float3{ 0.0f, height, size } );
    }

    /** @brief x = @p position 평면의 한 변 @p size 정사각형(y, z 가 0 .. size)을 삼각형 둘로 칠합니다. */
    void voxelizeWallX( sw::test::SceneVoxelReference& inoutGrid, float32 position, float32 size )
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

    /** @brief 메시가 없는 복셀 장면입니다(격자 모양만 든다). */
    sw::shared_ptr<sw::SceneVoxelScene> makeEmptyScene( const sw::SceneVoxelGrid& grid )
    {
        sw::shared_ptr<sw::SceneVoxelScene> scene = sw::make_shared<sw::SceneVoxelScene>();
        scene->_grid                              = grid;
        return scene;
    }

    /** @brief 장면 하나를 처음부터 칠한 결과입니다(칠할 일을 CPU 기준 구현으로 돌린다). */
    sw::test::SceneVoxelReference paintWhole( const sw::SceneVoxelScene& scene )
    {
        sw::vector<uint32> words( scene._grid.getWordCount(), 0xFFFFFFFFu ); // 지우기가 먼저 돈다 — 앞 내용에 기대지 않는다
        words.back() = 0u;                                                   // 마지막 단어의 남는 비트는 칸이 아니다
        sw::test::SceneVoxelReference::executePlan( sw::SceneVoxelizer::planPaint( {
                                                        sw::SceneVoxelPaintTarget{ &scene, nullptr, 0u, sw::SceneVoxelizer::kNoEmission }
        } ),
                                                    words );
        return sw::test::SceneVoxelReference( scene._grid, words, 0u );
    }

    /** @brief 반투명 머티리얼입니다 — 반투명은 머티리얼이 정한다(메시 컴포넌트에는 블렌드 칸이 없다). */
    sw::shared_ptr<sw::Material> makeTranslucentMaterial()
    {
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        SW_EXPECT_TRUE( material->loadFromXML( "<MaterialDef formatVersion=\"0\" name=\"ProbeGlass\" shaderPath=\"engine/shaders/forward_lit.hlsl\" surfaceModel=\"Lit\" alphaMode=\"Blend\">"
                                               "<_permutations><_alwaysDefines><item>MATERIAL_BLEND_TRANSLUCENT</item></_alwaysDefines></_permutations></MaterialDef>" ) );
        return material;
    }

    /** @brief 씬 기본 자리의 불투명 머티리얼입니다 — 머티리얼을 비운 메시는 렌더러처럼 이것으로 그려진다(`Material::resolveDrawMaterial`). */
    sw::shared_ptr<sw::Material> makeOpaqueMaterial()
    {
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        SW_EXPECT_TRUE( material->loadFromXML( "<MaterialDef formatVersion=\"0\" name=\"ProbeWall\" shaderPath=\"engine/shaders/forward_lit.hlsl\" surfaceModel=\"Lit\"/>" ) );
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
    SW_EXPECT_EQUAL( ( wide.getCellCount() + 31u ) / 32u, wide.getWordCount() );

    const sw::SceneVoxelGrid small = makeFourMeterGrid();
    SW_EXPECT_EQUAL( 4u, small._dimX );
    SW_EXPECT_EQUAL( 64u, small.getCellCount() );
    SW_EXPECT_EQUAL( 2u, small.getWordCount() );

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
    sw::test::SceneVoxelReference grid( makeFourMeterGrid() );
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
    sw::test::SceneVoxelReference grid( makeFourMeterGrid() );
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
    sw::test::SceneVoxelReference grid( makeFourMeterGrid() );
    voxelizeHorizontalSquare( grid, 1.5f, 4.0f ); // 칸 층 y = 1 (1 .. 2 m)

    sw::test::SceneVoxelHit hit;
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
    sw::test::SceneVoxelReference grid( makeFourMeterGrid() );
    voxelizeWallX( grid, 3.5f, 4.0f ); // 칸 열 x = 3 (3 .. 4 m)

    const sw::float3        direction = sw::float3{ 1.0f, 0.3f, 0.2f }.normalize();
    sw::test::SceneVoxelHit hit;
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
    const sw::shared_ptr<sw::Material> glass       = makeTranslucentMaterial(); // 씬보다 먼저 만든다 — 메시가 원시 포인터로 든다
    const sw::shared_ptr<sw::Material> sceneOpaque = makeOpaqueMaterial();
    sw::Scene                          scene( "SceneVoxelizerOpaque" );
    sw::GameObjectManager*             pObjects = scene.getObjectManager();
    SW_REQUIRE_NOT_NULL( pObjects );
    const sw::AABB bounds{
        sw::float3{-2.0f, -2.0f, -2.0f},
        sw::float3{ 2.0f,  2.0f,  2.0f}
    };

    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Glass", sw::float3{ 0.0f, 0.0f, 0.0f }, glass.get(), true ) );
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Hidden", sw::float3{ 0.0f, 0.0f, 0.0f }, nullptr, false ) );
    const sw::test::SceneVoxelReference empty = paintWhole( sw::SceneVoxelizer::makeScene( pObjects->getPrimitiveRegistry(), sceneOpaque.get(), bounds, 0.25f ) );
    SW_REQUIRE_FALSE( empty._grid.isEmpty() );
    SW_EXPECT_EQUAL( 0u, empty.countOccupied() );

    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Wall", sw::float3{ 0.0f, 0.0f, 0.0f }, nullptr, true ) );
    const sw::test::SceneVoxelReference solid = paintWhole( sw::SceneVoxelizer::makeScene( pObjects->getPrimitiveRegistry(), sceneOpaque.get(), bounds, 0.25f ) );
    // 머티리얼을 비운 메시는 씬 기본으로 그려진다 — 기본이 반투명이면 렌더러처럼 가리개가 아니다(복셀러가 따로 짐작하지 않는다).
    const sw::test::SceneVoxelReference seeThrough = paintWhole( sw::SceneVoxelizer::makeScene( pObjects->getPrimitiveRegistry(), glass.get(), bounds, 0.25f ) );
    SW_EXPECT_EQUAL( 0u, seeThrough.countOccupied() );
    SW_EXPECT_TRUE( solid.countOccupied() > 0u );
    SW_EXPECT_TRUE( solid.isOccupied( 6, 8, 8 ) );  // x = -0.5 면 근처
    SW_EXPECT_FALSE( solid.isOccupied( 8, 8, 8 ) ); // 속은 비었다

    sw::test::SceneVoxelHit hit;
    SW_REQUIRE_TRUE( solid.traceRay( sw::float3{ -1.9f, 0.1f, 0.1f }, sw::float3{ 1.0f, 0.0f, 0.0f }, 100.0f, hit ) );
    SW_EXPECT_TRUE( hit._distance > 1.1f && hit._distance < 1.45f );
    SW_EXPECT_NEAR( -1.0f, hit._normal._x, 0.0f );
}

/**
 * @brief [SceneVoxelizerTest] 메시가 드나든 구간만 다시 칠한 격자가 처음부터 칠한 격자와 같고, 격자와 상관없는 칸(반투명 · 격자 밖)의 변화로는 장면이 바뀌지 않는다
 * @details 실시간 프로브 복셀 격자는 예전에 프리미티브 집합 세대가 오를 때마다(투사체 하나가 생기고 사라져도) 씬 전체를 다시 칠했다. 이제 집합 기록(`collectMeshSetChanges`)의
 *          칸만 다시 모으고(`SceneVoxelizer::updateScene`), 칠하기는 바뀐 원천이 덮던 · 덮는 구간만 지우고 다시 칠한다(`SceneVoxelizer::planPaint`).
 */
SW_TEST_CASE( SceneVoxelizerTest, RepaintsOnlyChangedOccluders )
{
    using sw::test::SceneVoxelReference;
    const sw::shared_ptr<sw::Material> glass       = makeTranslucentMaterial();
    const sw::shared_ptr<sw::Material> sceneOpaque = makeOpaqueMaterial();
    sw::Scene                          scene( "SceneVoxelizerUpdate" );
    sw::GameObjectManager*             pObjects = scene.getObjectManager();
    SW_REQUIRE_NOT_NULL( pObjects );
    const sw::PrimitiveRegistry& registry = pObjects->getPrimitiveRegistry();
    const sw::AABB               bounds{
        sw::float3{-2.0f, -2.0f, -2.0f},
        sw::float3{ 2.0f,  2.0f,  2.0f}
    };
    sw::MeshComponent* pWall = addCube( pObjects, "Wall", sw::float3{ -1.0f, 0.0f, 0.0f }, nullptr, true );
    SW_REQUIRE_NOT_NULL( pWall );
    sw::SceneVoxelScene painted    = sw::SceneVoxelizer::makeScene( registry, sceneOpaque.get(), bounds, 0.25f );
    sw::SceneVoxelScene current    = painted;
    sw::vector<uint32>  words      = paintWhole( painted )._listWord;
    uint64              generation = registry.getSetGeneration();
    sw::vector<uint32>  listChanged;

    // 반투명 큐브와 격자 밖 큐브 — 가리개가 아니거나 닿지 않으니 칠할 것이 없다.
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Glass", sw::float3{ 0.0f, 0.0f, 0.0f }, glass.get(), true ) );
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Far", sw::float3{ 50.0f, 0.0f, 0.0f }, nullptr, true ) );
    SW_REQUIRE_TRUE( registry.collectMeshSetChanges( generation, listChanged ) );
    SW_EXPECT_EQUAL( size_t( 2 ), listChanged.size() );
    SW_EXPECT_FALSE( sw::SceneVoxelizer::updateScene( current, registry, sceneOpaque.get(), listChanged ) );
    generation = registry.getSetGeneration();

    // 격자 안 불투명 큐브 — 그 구간만 칠해도 처음부터 칠한 것과 단어마다 같다.
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Inside", sw::float3{ 1.0f, 0.5f, 0.0f }, nullptr, true ) );
    SW_REQUIRE_TRUE( registry.collectMeshSetChanges( generation, listChanged ) );
    SW_EXPECT_TRUE( sw::SceneVoxelizer::updateScene( current, registry, sceneOpaque.get(), listChanged ) );
    const sw::SceneVoxelPaintPlan addPlan = sw::SceneVoxelizer::planPaint( {
        sw::SceneVoxelPaintTarget{ &current, &painted, 0u, sw::SceneVoxelizer::kNoEmission }
    } );
    SW_EXPECT_TRUE( addPlan._clearThreadCount < current._grid.getCellCount() ); // 격자 전체가 아니다
    SceneVoxelReference::executePlan( addPlan, words );
    const SceneVoxelReference afterAdd = paintWhole( sw::SceneVoxelizer::makeScene( registry, sceneOpaque.get(), bounds, 0.25f ) );
    SW_EXPECT_TRUE( words == afterAdd._listWord );
    SW_EXPECT_TRUE( afterAdd.countOccupied() > 0u );
    painted    = current;
    generation = registry.getSetGeneration();

    // 벽을 지운다 — 벽이 덮던 칸이 꺼지고 남은 큐브는 그대로다.
    SW_REQUIRE_NOT_NULL( pWall->getOwner() );
    pObjects->destroyGameObject( pWall->getOwner() );
    pObjects->processDeferredDestruction();
    SW_REQUIRE_TRUE( registry.collectMeshSetChanges( generation, listChanged ) );
    SW_EXPECT_TRUE( sw::SceneVoxelizer::updateScene( current, registry, sceneOpaque.get(), listChanged ) );
    SceneVoxelReference::executePlan( sw::SceneVoxelizer::planPaint( {
                                          sw::SceneVoxelPaintTarget{ &current, &painted, 0u, sw::SceneVoxelizer::kNoEmission }
    } ),
                                      words );
    const SceneVoxelReference afterRemove = paintWhole( sw::SceneVoxelizer::makeScene( registry, sceneOpaque.get(), bounds, 0.25f ) );
    SW_EXPECT_TRUE( words == afterRemove._listWord );
    SW_EXPECT_TRUE( afterRemove.countOccupied() < afterAdd.countOccupied() );

    // 같은 장면이면 할 일이 없다.
    SW_EXPECT_TRUE( sw::SceneVoxelizer::planPaint( {
                                                       sw::SceneVoxelPaintTarget{ &current, &current, 0u, sw::SceneVoxelizer::kNoEmission }
    } )
                        .isEmpty() );
}

/**
 * @brief [SceneVoxelizerTest] 움직인 가리개는 낡은 자리가 정적 사본으로 돌아가고 새 자리에 칠해져, 처음부터 칠한 격자와 같다. 두 번째 움직임부터는 정적 층을 다시 칠하지 않고, 움직인 것이 없으면 할 일이 없다
 * @details 변이: 되돌리기 일을 빼면 낡은 자리가 켜진 채 남고, 승격(정적 층에서 빼기)을 빼면 정적 사본이 낡은 자리를 되살린다.
 */
SW_TEST_CASE( SceneVoxelizerTest, MovedOccluderRepaintsOnlyItsLayer )
{
    using sw::test::SceneVoxelReference;
    const sw::shared_ptr<sw::Material> sceneOpaque = makeOpaqueMaterial();
    sw::Scene                          scene( "SceneVoxelizerMove" );
    sw::GameObjectManager*             pObjects = scene.getObjectManager();
    SW_REQUIRE_NOT_NULL( pObjects );
    sw::PrimitiveRegistry& registry = pObjects->getPrimitiveRegistry();
    const sw::AABB         bounds{
        sw::float3{-2.0f, -2.0f, -2.0f},
        sw::float3{ 2.0f,  2.0f,  2.0f}
    };
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Wall", sw::float3{ 0.75f, 0.0f, 0.0f }, nullptr, true ) );
    sw::MeshComponent* pDoor = addCube( pObjects, "Door", sw::float3{ -1.0f, 0.0f, 0.0f }, nullptr, true );
    SW_REQUIRE_NOT_NULL( pDoor );
    pObjects->flushSceneTransforms();
    sw::vector<uint32> listState;
    sw::vector<uint32> listTransform;
    registry.consumeDirty( listState, listTransform );
    sw::SceneVoxelScene painted    = sw::SceneVoxelizer::makeScene( registry, sceneOpaque.get(), bounds, 0.25f );
    uint64              generation = registry.getMeshChangeGeneration();
    const uint32        wordCount  = painted._grid.getWordCount();
    sw::vector<uint32>  words      = paintWhole( painted )._listWord;
    words.resize( wordCount * 2u, 0u );            // 뒤 절반은 정적 사본 자리
    const sw::SceneVoxelScene* pPainted = nullptr; // 정적 사본이 처음 생기는 프레임은 자리가 바뀌어 전체를 칠한다
    sw::SceneVoxelScene        current  = painted;

    for ( const float32 x : { -0.5f, 0.0f, 0.5f } )
    {
        pDoor->setLocalPosition( sw::float3{ x, 0.25f, 0.0f } );
        pObjects->flushSceneTransforms();
        registry.consumeDirty( listState, listTransform );
        sw::vector<uint32> listChanged;
        registry.appendMeshChanges( generation, listChanged );
        generation = registry.getMeshChangeGeneration();
        SW_REQUIRE_EQUAL( size_t( 1 ), listChanged.size() );
        painted = current;
        SW_REQUIRE_TRUE( sw::SceneVoxelizer::updateScene( current, registry, sceneOpaque.get(), listChanged ) );
        SW_REQUIRE_EQUAL( size_t( 1 ), current._listMovingSource.size() );
        const sw::SceneVoxelPaintPlan plan = sw::SceneVoxelizer::planPaint( {
            sw::SceneVoxelPaintTarget{ &current, pPainted, 0u, sw::SceneVoxelizer::kNoEmission, wordCount, sw::SceneVoxelizer::kNoEmission }
        } );
        SW_EXPECT_EQUAL( pPainted == nullptr, plan._clearThreadCount != 0 );
        SceneVoxelReference::executePlan( plan, words );
        const SceneVoxelReference fresh = paintWhole( sw::SceneVoxelizer::makeScene( registry, sceneOpaque.get(), bounds, 0.25f ) );
        SW_EXPECT_TRUE( sw::vector<uint32>( words.begin(), words.begin() + static_cast<ptrdiff_t>( wordCount ) ) == fresh._listWord );
        pPainted = &painted;
        painted  = current;
    }
    sw::vector<uint32> listChanged;
    registry.appendMeshChanges( generation, listChanged );
    SW_EXPECT_TRUE( listChanged.empty() );
}

/**
 * @brief [SceneVoxelizerTest] 노멀의 가장 큰 축으로 투영해 칠한 칸(셰이더의 식)이 상자 안 모든 칸을 분리축으로 본 칸(보수적 복셀화의 기준)과 같다 — 축 정렬 · 비스듬한 · 가는 삼각형
 * @details 변이: 투영 축을 하나로 고정하거나 층 범위에서 기울기 몫(spread)을 빼면 비스듬한 삼각형에서 빠지는 칸이 생겨 진다.
 */
SW_TEST_CASE( SceneVoxelizerTest, DominantAxisPaintMatchesSeparatingAxis )
{
    const sw::SceneVoxelGrid grid = sw::SceneVoxelGrid::make( sw::AABB{
                                                                  sw::float3{-3.0f, -3.0f, -3.0f},
                                                                  sw::float3{ 3.0f,  3.0f,  3.0f}
    },
                                                              0.375f );
    uint32                   seed = 0x9E3779B9u;
    const auto               next = [&seed]()
    {
        seed ^= seed << 13u;
        seed ^= seed >> 17u;
        seed ^= seed << 5u;
        return static_cast<float32>( seed >> 8u ) / 16777216.0f;
    };
    for ( uint32 caseIndex = 0; caseIndex < 200u; ++caseIndex )
    {
        sw::vector<sw::RHIVertex> listVertex( 3 );
        sw::float3                arrCorner[3];
        for ( uint32 corner = 0; corner < 3u; ++corner )
        {
            for ( uint32 axis = 0; axis < 3u; ++axis )
            {
                // 격자를 조금 넘는 자리까지 — 자르기도 본다. 다섯에 하나는 한 축을 눌러 축 정렬 면으로 만든다.
                const float32 value                   = ( next() - 0.5f ) * 7.0f;
                const bool    bFlat                   = caseIndex % 5u == 0u && axis == caseIndex % 3u && corner > 0u;
                listVertex[corner]._arrPosition[axis] = bFlat ? listVertex[0]._arrPosition[axis] : value;
            }
            arrCorner[corner] = sw::float3{ listVertex[corner]._arrPosition[0], listVertex[corner]._arrPosition[1], listVertex[corner]._arrPosition[2] };
        }
        sw::SceneVoxelScene scene;
        scene._grid = grid;
        sw::SceneVoxelSource source{};
        source._mesh = sw::Mesh::create( sw::MeshCPUAccess::Keep );
        SW_REQUIRE_TRUE( source._mesh->setGeometry( std::move( listVertex ), sw::vector<uint32>{ 0, 1, 2 } ) );
        source._box        = grid.getBounds();
        source._indexCount = 3u;
        scene.setStaticSources( sw::vector<sw::SceneVoxelSource>{ source } );
        sw::test::SceneVoxelReference expected( grid );
        expected.voxelizeTriangle( arrCorner[0], arrCorner[1], arrCorner[2] );
        SW_EXPECT_TRUE( paintWhole( scene )._listWord == expected._listWord );
    }
}

/**
 * @brief [SceneVoxelizerTest] 발광 칸은 순위를 쓰고 여러 면이 덮은 칸은 더 밝은 순위가 남는다. 발광 목록은 어두운 것부터이고 같은 값은 하나다
 */
SW_TEST_CASE( SceneVoxelizerTest, EmissionKeepsTheBrightestRank )
{
    const uint32        dim    = sw::PackUtil::packRGB9E5( sw::float3{ 0.5f, 0.5f, 0.5f } );
    const uint32        bright = sw::PackUtil::packRGB9E5( sw::float3{ 4.0f, 2.0f, 1.0f } );
    sw::SceneVoxelScene scene;
    scene._grid = makeFourMeterGrid(); // 64 칸 → 점유 단어 2 + 발광 단어 64
    sw::vector<sw::SceneVoxelSource> listSource;
    // 밝은 면을 먼저 · 어두운 면을 나중에 칠해도(원천 차례) 밝은 순위가 남는다. 셋째는 밝은 것과 같은 발광이다.
    for ( const uint32 packed : { bright, dim, bright } )
    {
        sw::vector<sw::RHIVertex> listVertex( 3 );
        const float32             height = 2.5f;
        listVertex[0]._arrPosition[0]    = 0.1f;
        listVertex[1]._arrPosition[0]    = 3.9f;
        listVertex[2]._arrPosition[0]    = 0.1f;
        listVertex[2]._arrPosition[2]    = 3.9f;
        for ( sw::RHIVertex& vertex : listVertex )
        {
            vertex._arrPosition[1] = height;
        }
        sw::SceneVoxelSource source{};
        source._mesh = sw::Mesh::create( sw::MeshCPUAccess::Keep );
        SW_REQUIRE_TRUE( source._mesh->setGeometry( std::move( listVertex ), sw::vector<uint32>{ 0, 1, 2 } ) );
        source._box            = scene._grid.getBounds();
        source._indexCount     = 3u;
        source._packedEmission = packed;
        listSource.push_back( source );
    }
    scene.setStaticSources( std::move( listSource ) );
    scene.rebuildEmissionPalette();
    SW_REQUIRE_EQUAL( size_t( 2 ), scene._listEmissionPalette.size() );
    SW_EXPECT_EQUAL( dim, scene._listEmissionPalette[0] );
    SW_EXPECT_EQUAL( 1u, scene.findEmissionRank( dim ) );
    SW_EXPECT_EQUAL( 2u, scene.findEmissionRank( bright ) );
    SW_EXPECT_EQUAL( 0u, scene.findEmissionRank( 0u ) );

    const uint32       emissionOffset = scene._grid.getWordCount();
    sw::vector<uint32> words( emissionOffset + scene._grid.getCellCount(), 0xFFFFFFFFu );
    sw::test::SceneVoxelReference::executePlan( sw::SceneVoxelizer::planPaint( {
                                                    sw::SceneVoxelPaintTarget{ &scene, nullptr, 0u, emissionOffset }
    } ),
                                                words );
    SW_EXPECT_EQUAL( 2u, words[emissionOffset + scene._grid.computeCellIndex( 0, 2, 0 )] );
    SW_EXPECT_EQUAL( 0u, words[emissionOffset + scene._grid.computeCellIndex( 0, 0, 0 )] ); // 발광하지 않는 칸
}

/**
 * @brief [SceneVoxelizerTest] 격자를 가로지르는 삼각형 몇 개는 줄마다 나눠 칠하고, 촘촘한 메시 · 구간이 작은 칠하기는 스레드를 늘리지 않는다
 */
SW_TEST_CASE( SceneVoxelizerTest, SliceCountFollowsTriangleSize )
{
    const sw::SceneVoxelGrid grid   = sw::SceneVoxelGrid::make( sw::AABB{
                                                                  sw::float3{  0.0f,  0.0f,   0.0f},
                                                                  sw::float3{128.0f, 16.0f, 128.0f}
    },
                                                                1.0f );
    const sw::AABB           bounds = grid.getBounds();
    SW_EXPECT_EQUAL( 128u, sw::SceneVoxelizer::computeSliceCount( grid, grid.getFullRange(), bounds, 1u ) );
    SW_EXPECT_EQUAL( 91u, sw::SceneVoxelizer::computeSliceCount( grid, grid.getFullRange(), bounds, 2u ) ); // 바닥판 — ceil( 128 / √2 )
    SW_EXPECT_EQUAL( 1u, sw::SceneVoxelizer::computeSliceCount( grid, grid.getFullRange(), bounds, 128u * 128u ) );
    sw::SceneVoxelGrid::CellRange range{};
    SW_REQUIRE_TRUE( grid.findCellRange( sw::AABB{
                                             sw::float3{4.5f, 4.5f, 4.5f},
                                             sw::float3{7.5f, 5.5f, 4.6f}
    },
                                         range ) );
    SW_EXPECT_EQUAL( 4u, sw::SceneVoxelizer::computeSliceCount( grid, range, bounds, 1u ) ); // 다시 칠할 구간의 줄만 나눈다
}

/**
 * @brief [SceneVoxelizerTest] 같은 메시를 쓰는 원천들은 정점 · 인덱스를 한 번만 싣고, 격자 모양 · 발광 목록이 달라지면 격자 전체를 다시 칠한다
 */
SW_TEST_CASE( SceneVoxelizerTest, PlanSharesMeshesAndRepaintsWholeOnShapeChange )
{
    sw::SceneVoxelScene scene;
    scene._grid                           = makeFourMeterGrid();
    const sw::shared_ptr<sw::Mesh>   cube = sw::MeshUtil::createUnitCube();
    sw::vector<sw::SceneVoxelSource> listSource;
    for ( uint32 index = 0; index < 2u; ++index )
    {
        sw::SceneVoxelSource source{};
        source._mesh       = cube;
        source._world      = sw::float4x4::makeTranslation( sw::float3{ 1.0f + static_cast<float32>( index ), 1.0f, 1.0f } );
        source._box        = scene._grid.getBounds();
        source._indexCount = cube->getIndexCount();
        listSource.push_back( source );
    }
    scene.setStaticSources( std::move( listSource ) );
    const sw::SceneVoxelPaintPlan plan = sw::SceneVoxelizer::planPaint( {
        sw::SceneVoxelPaintTarget{ &scene, nullptr, 0u, sw::SceneVoxelizer::kNoEmission }
    } );
    SW_EXPECT_EQUAL( size_t( 1 ), plan._listMesh.size() );
    SW_EXPECT_EQUAL( cube->getVertexCount(), plan._vertexCount );
    SW_EXPECT_EQUAL( cube->getIndexCount(), plan._indexCount );
    const uint32 cubeTriangleCount = cube->getIndexCount() / 3u;
    const uint32 sliceCount        = sw::SceneVoxelizer::computeSliceCount( scene._grid, scene._grid.getFullRange(), scene._grid.getBounds(), cubeTriangleCount );
    SW_EXPECT_EQUAL( 2u * cubeTriangleCount * sliceCount, plan._paintThreadCount );
    SW_EXPECT_EQUAL( size_t( 1u + 2u * sw::SceneVoxelizer::kJobRowCount ), plan._listPaintRow.size() );
    SW_EXPECT_EQUAL( scene._grid.getCellCount(), plan._clearThreadCount );

    sw::vector<sw::SceneVoxelGrid::CellRange> listRange;
    sw::SceneVoxelScene                       painted = scene;
    painted._listEmissionPalette.push_back( 1u ); // 순위의 뜻이 달라진다
    sw::SceneVoxelizer::collectDirtyRanges( painted, scene, listRange );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRange.size() );
    SW_EXPECT_EQUAL( scene._grid.getCellCount(), listRange[0].getCellCount() );
    painted                 = scene;
    painted._grid._cellSize = 2.0f;
    sw::SceneVoxelizer::collectDirtyRanges( painted, scene, listRange );
    SW_EXPECT_EQUAL( size_t( 1 ), listRange.size() );
}

/**
 * @brief [RealtimeProbeTest] 요청들의 버퍼 배치는 앞 요청들의 복셀 단어 · 프로브 수를 누적하고, 기록 구간은 할당기가 정체마다 매긴 자리다(처음은 이어 붙는다)
 */
SW_TEST_CASE( RealtimeProbeTest, PlanLayoutAccumulatesOffsets )
{
    sw::vector<sw::RealtimeProbeRequest> listRequest( 2 );
    listRequest[0]._volumeID    = 1;
    listRequest[1]._volumeID    = 2;
    listRequest[0]._probeCount  = 18;
    listRequest[0]._pVoxelScene = makeEmptyScene( makeFourMeterGrid() ); // 64 칸 → 단어 2
    listRequest[1]._probeCount  = 5;
    listRequest[1]._pVoxelScene = makeEmptyScene( sw::SceneVoxelGrid::make( sw::AABB{
                                                                                sw::float3{},
                                                                                sw::float3{ 8.0f, 8.0f, 8.0f }
    },
                                                                            1.0f ) ); // 512 칸 → 단어 16

    sw::RealtimeProbeHistoryAllocator history;
    SW_EXPECT_TRUE( history.assign( listRequest ) ); // 처음 — 용량이 생긴다
    const sw::RealtimeProbeLayout layout = sw::RealtimeProbeUpdater::planLayout( listRequest, history );
    SW_REQUIRE_EQUAL( size_t( 2 ), layout._listFirstThread.size() );
    SW_EXPECT_EQUAL( 0u, layout._listFirstThread[0] );
    SW_EXPECT_EQUAL( 18u, layout._listFirstThread[1] );
    SW_EXPECT_EQUAL( 0u, layout._listVoxelWordOffset[0] );
    SW_EXPECT_EQUAL( 2u, layout._listVoxelWordOffset[1] );
    SW_EXPECT_EQUAL( 0u, layout._listHistoryOffset[0] );
    SW_EXPECT_EQUAL( 18u * sw::RealtimeProbeHistoryAllocator::kFloat4PerProbe, layout._listHistoryOffset[1] );
    SW_EXPECT_EQUAL( 23u, layout._totalProbes );
    SW_EXPECT_EQUAL( 18u, layout._totalVoxelWords );
    SW_EXPECT_EQUAL( history.getCapacity(), layout._totalHistoryElements );
    SW_EXPECT_TRUE( layout._totalHistoryElements >= 23u * sw::RealtimeProbeHistoryAllocator::kFloat4PerProbe );
}

/**
 * @brief [RealtimeProbeTest] 볼륨 하나가 빠지고 하나가 들어와도 서 있는 볼륨은 기록 구간 · 누적 프레임을 지키고, 새 볼륨만 0 에서 시작해 빠진 볼륨의 구간을 다시 쓴다
 * @details 예전 열쇠는 (볼륨 번호, 프로브 수) 목록을 순서째 견줘 하나만 드나들어도 모든 기록이 0 으로 돌아갔다.
 */
SW_TEST_CASE( RealtimeProbeTest, HistoryKeepsStandingVolumesWhenOneLeaves )
{
    sw::vector<sw::RealtimeProbeRequest> listRequest( 3 );
    for ( uint32 index = 0; index < 3; ++index )
    {
        listRequest[index]._volumeID   = 100u + index;
        listRequest[index]._probeCount = 4;
    }
    sw::RealtimeProbeHistoryAllocator history;
    SW_REQUIRE_TRUE( history.assign( listRequest ) );
    const uint32 offsetB  = history.getOffset( 1 );
    const uint32 offsetC  = history.getOffset( 2 );
    const uint32 offsetA  = history.getOffset( 0 );
    const uint32 capacity = history.getCapacity();
    history.advanceFrame();
    history.advanceFrame();

    // A(100) 가 빠지고 D(200) 가 들어온다 — 차례도 바뀐다.
    sw::vector<sw::RealtimeProbeRequest> listNext( 3 );
    listNext[0]             = listRequest[2];
    listNext[1]             = listRequest[1];
    listNext[2]._volumeID   = 200u;
    listNext[2]._probeCount = 4;
    SW_EXPECT_FALSE( history.assign( listNext ) ); // 빠진 구간으로 들어가 용량이 그대로다
    SW_EXPECT_EQUAL( offsetC, history.getOffset( 0 ) );
    SW_EXPECT_EQUAL( offsetB, history.getOffset( 1 ) );
    SW_EXPECT_EQUAL( offsetA, history.getOffset( 2 ) );
    SW_EXPECT_FALSE( history.isFresh( 0 ) );
    SW_EXPECT_FALSE( history.isFresh( 1 ) );
    SW_EXPECT_TRUE( history.isFresh( 2 ) );
    SW_EXPECT_EQUAL( 2u, history.getFramesSinceReset( 0 ) );
    SW_EXPECT_EQUAL( 0u, history.getFramesSinceReset( 2 ) );
    SW_EXPECT_EQUAL( capacity, history.getCapacity() );

    // 프로브 수가 바뀐 볼륨은 새 볼륨과 같다.
    listNext[1]._probeCount = 2;
    SW_EXPECT_FALSE( history.assign( listNext ) ); // 돌려받은 구간 안에 든다
    SW_EXPECT_TRUE( history.isFresh( 1 ) );
    SW_EXPECT_FALSE( history.isFresh( 0 ) );
}

/**
 * @brief [RealtimeProbeTest] 갱신을 중간에 그만둔 프레임(버퍼 할당 실패) 뒤에도 용량 늘기와 새 구간을 다시 알린다 — `advanceFrame` 이 온 뒤에만 이어진다
 * @details 변이: 한 번만 알리면 다음 프레임이 0 으로 채우지 않은 구간 · 옛 크기 버퍼를 쓴다(예전 — 복셀 버퍼 할당이 실패한 프레임).
 */
SW_TEST_CASE( RealtimeProbeTest, HistoryReportsAgainUntilTheFrameIsUpdated )
{
    sw::vector<sw::RealtimeProbeRequest> listRequest( 1 );
    listRequest[0]._volumeID   = 100u;
    listRequest[0]._probeCount = 4;
    sw::RealtimeProbeHistoryAllocator history;
    SW_EXPECT_TRUE( history.assign( listRequest ) );
    SW_EXPECT_TRUE( history.assign( listRequest ) ); // 앞 프레임이 그만뒀다 — 버퍼는 아직 없다
    SW_EXPECT_TRUE( history.isFresh( 0 ) );
    history.advanceFrame();
    SW_EXPECT_FALSE( history.assign( listRequest ) );
    SW_EXPECT_FALSE( history.isFresh( 0 ) );

    // 새 볼륨이 들어온 프레임도 같다 — 서 있는 볼륨은 이어지고 새 볼륨만 다시 알린다.
    sw::vector<sw::RealtimeProbeRequest> listSwapped( 1 );
    listSwapped[0]._volumeID   = 200u;
    listSwapped[0]._probeCount = 4;
    SW_EXPECT_FALSE( history.assign( listSwapped ) ); // 빠진 구간에 든다
    SW_EXPECT_TRUE( history.isFresh( 0 ) );
    SW_EXPECT_FALSE( history.assign( listSwapped ) );
    SW_EXPECT_TRUE( history.isFresh( 0 ) );
    history.advanceFrame();
    SW_EXPECT_FALSE( history.assign( listSwapped ) );
    SW_EXPECT_FALSE( history.isFresh( 0 ) );
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
 * @brief [RealtimeProbeTest] 입력 행은 probe_update.hlsl 의 배치대로 머리 · 하늘 SH · 볼륨 여덟 행으로 짜인다
 */
SW_TEST_CASE( RealtimeProbeTest, BuildParamRowsFollowShaderLayout )
{
    sw::RealtimeProbeRequest request{};
    request._volumeElement._colorIntensity = sw::float4{ 1.0f, 2.0f, 3.0f, 0.0f };
    request._volumeElement._directionType  = sw::float4{ 2.0f, 2.0f, 2.0f, static_cast<float32>( sw::shaderslot::kLightTypeProbeVolume ) };
    request._volumeElement._params         = sw::float4{ 3.0f, 2.0f, 3.0f, 5.0f };   // 프로브 3 × 2 × 3, 첫 프로브 번호 5
    request._volumeElement._extra          = sw::float4{ 161.0f, 1.0f, 0.0f, 0.0f }; // 가시성 모먼트의 첫 원소 161, 모먼트 있음
    request._probeCount                    = 18;
    request._rayCount                      = 64;
    request._updatePeriod                  = 8;
    request._blend                         = 0.25f;
    request._bounceAlbedo                  = 0.5f;
    request._pVoxelScene                   = makeEmptyScene( makeFourMeterGrid() );
    sw::vector<sw::RealtimeProbeRequest> listRequest;
    listRequest.push_back( request );

    sw::RealtimeProbeHistoryAllocator history;
    (void)history.assign( listRequest );
    const sw::RealtimeProbeLayout layout = sw::RealtimeProbeUpdater::planLayout( listRequest, history );
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
    SW_EXPECT_EQUAL( 161u, sw::BitUtil::floatToBits( rows[first + 7]._x ) );                               // 가시성 모먼트의 첫 float4
}

/**
 * @brief [SceneVoxelGridTest] 발광 복사휘도는 공유 지수(R9G9B9E5)로 접혀 상대 오차 안에서 되돌아오고, 0 · 음수는 0 이다
 */
SW_TEST_CASE( SceneVoxelGridTest, EmissionPackRoundTrips )
{
    const sw::float3 radiance{ 3.5f, 0.25f, 12.0f };
    const sw::float3 back = sw::PackUtil::unpackRGB9E5( sw::PackUtil::packRGB9E5( radiance ) );
    SW_EXPECT_NEAR( 3.5f, back._x, 3.5f * 0.01f + 0.02f );
    SW_EXPECT_NEAR( 0.25f, back._y, 0.03f ); // 가장 큰 채널의 지수를 나눠 쓰므로 작은 채널은 그 단위(12 / 512)로 둥글린다
    SW_EXPECT_NEAR( 12.0f, back._z, 12.0f * 0.01f );
    SW_EXPECT_EQUAL( 0u, sw::PackUtil::packRGB9E5( sw::float3{} ) );
    SW_EXPECT_EQUAL( 0u, sw::PackUtil::packRGB9E5( sw::float3{ -1.0f, -2.0f, -3.0f } ) );
}

/**
 * @brief [RealtimeProbeTest] 발광 칸은 그 볼륨의 점유 단어 바로 뒤에 놓이고, 갱신 행에 발광 단어 번호 · 배율이 실린다. 발광 없는 볼륨은 kNoEmission 이다
 */
SW_TEST_CASE( RealtimeProbeTest, EmissionWordsFollowOccupancy )
{
    const sw::shared_ptr<sw::SceneVoxelScene> emissive = makeEmptyScene( makeFourMeterGrid() ); // 64 칸 → 점유 단어 2 + 발광 단어 64
    emissive->_listEmissionPalette                     = { 11u, 22u, 33u, 44u, 55u };           // 다섯 → 입력 행 둘
    sw::vector<sw::RealtimeProbeRequest> listRequest( 2 );
    listRequest[0]._probeCount        = 1;
    listRequest[0]._emissiveIntensity = 2.5f;
    listRequest[0]._pVoxelScene       = emissive;
    listRequest[1]._probeCount        = 1;
    listRequest[1]._pVoxelScene       = makeEmptyScene( makeFourMeterGrid() );
    listRequest[0]._volumeID          = 1;
    listRequest[1]._volumeID          = 2;

    sw::RealtimeProbeHistoryAllocator history;
    (void)history.assign( listRequest );
    const sw::RealtimeProbeLayout layout = sw::RealtimeProbeUpdater::planLayout( listRequest, history );
    SW_REQUIRE_EQUAL( size_t( 2 ), layout._listEmissionWordOffset.size() );
    SW_EXPECT_EQUAL( 2u, layout._listEmissionWordOffset[0] );
    SW_EXPECT_EQUAL( 66u, layout._listVoxelWordOffset[1] );
    SW_EXPECT_EQUAL( sw::SceneVoxelizer::kNoEmission, layout._listEmissionWordOffset[1] );
    SW_EXPECT_EQUAL( 68u, layout._totalVoxelWords );

    const sw::vector<sw::float4> rows     = sw::RealtimeProbeUpdater::makeParamRows( listRequest, layout, {}, sw::SH9Color{}, 0u, 0u );
    const uint32                 schedule = sw::RealtimeProbeUpdater::kHeaderRowCount + 6u;
    SW_EXPECT_EQUAL( 2u, sw::BitUtil::floatToBits( rows[schedule]._z ) );
    SW_EXPECT_NEAR( 2.5f, rows[schedule]._w, 0.0f );
    SW_EXPECT_EQUAL( sw::SceneVoxelizer::kNoEmission, sw::BitUtil::floatToBits( rows[schedule + sw::RealtimeProbeUpdater::kVolumeRowCount]._z ) );
    // 발광 목록은 볼륨 행들 뒤에 행마다 넷씩 이어진다 — 순위 5 는 둘째 행의 첫 칸이다.
    const uint32 paletteRow = sw::RealtimeProbeUpdater::kHeaderRowCount + 2u * sw::RealtimeProbeUpdater::kVolumeRowCount;
    SW_EXPECT_EQUAL( paletteRow, sw::BitUtil::floatToBits( rows[schedule + 1u]._y ) );
    SW_EXPECT_EQUAL( paletteRow + 2u, sw::BitUtil::floatToBits( rows[schedule + 1u + sw::RealtimeProbeUpdater::kVolumeRowCount]._y ) );
    SW_REQUIRE_EQUAL( size_t( paletteRow + 2u ), rows.size() );
    SW_EXPECT_EQUAL( 11u, sw::BitUtil::floatToBits( rows[paletteRow]._x ) );
    SW_EXPECT_EQUAL( 55u, sw::BitUtil::floatToBits( rows[paletteRow + 1u]._x ) );
}

/**
 * @brief [RealtimeProbeTest] 기록에 남은 옛 값의 몫은 갱신마다 (1 − 섞는 비율)을 곱한 값이고, 볼륨이 여럿이면 가장 큰 값이다(사진 앞 정착 조건 `RenderRealtimeGI`)
 */
SW_TEST_CASE( RealtimeProbeTest, ErrorDecaysPerUpdate )
{
    sw::vector<sw::RealtimeProbeRequest> listRequest( 1 );
    listRequest[0]._updatePeriod = 4;
    listRequest[0]._blend        = 0.25f;
    SW_EXPECT_NEAR( 1.0f, sw::RealtimeProbeUpdater::computeError( listRequest, 3u ), 0.0f ); // 아직 한 바퀴도 돌지 않았다
    SW_EXPECT_NEAR( 0.75f, sw::RealtimeProbeUpdater::computeError( listRequest, 4u ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.5625f, sw::RealtimeProbeUpdater::computeError( listRequest, 11u ), 1.0e-6f );
    SW_EXPECT_TRUE( sw::RealtimeProbeUpdater::computeError( listRequest, 119u ) > 0.0002f ); // 문턱 — 30 번째 갱신에서 선다
    SW_EXPECT_TRUE( sw::RealtimeProbeUpdater::computeError( listRequest, 120u ) <= 0.0002f );
    const sw::RealtimeProbeRequest slower = listRequest[0];
    listRequest.push_back( slower );
    listRequest[1]._updatePeriod = 8;
    SW_EXPECT_NEAR( 0.75f, sw::RealtimeProbeUpdater::computeError( listRequest, 8u ), 1.0e-6f ); // 느린 볼륨이 정한다
    SW_EXPECT_NEAR( 0.0f, sw::RealtimeProbeUpdater::computeError( {}, 8u ), 0.0f );
}

/**
 * @brief [RealtimeProbeTest] 정착 진척은 되돌림부터 세고, 변화가 이어지는 동안은 시작한 뒤, 멈추면 마지막 변화 뒤부터 센다
 * @details 변이: 변화마다 처음부터 세면 도는 물체 · 깜빡이는 등이 있는 장면이 끝내 정착하지 않고, 변화를 세지 않으면 막 멈춘 문의 옛 그늘이 남은 채 사진을 찍는다.
 */
SW_TEST_CASE( RealtimeProbeTest, AccumulationCountsFromChange )
{
    sw::RealtimeProbeAccumulation accumulation;
    accumulation.advance( true, true, true );
    for ( uint32 frame = 0; frame < 9u; ++frame )
    {
        accumulation.advance( false, false, false );
    }
    SW_EXPECT_EQUAL( 10u, accumulation.getAccumulatedFrames() );

    // 정착한 뒤의 변화는 1 부터, 이어지는 동안 는다.
    accumulation.advance( false, true, true );
    SW_EXPECT_EQUAL( 1u, accumulation.getAccumulatedFrames() );
    for ( uint32 frame = 0; frame < 20u; ++frame )
    {
        accumulation.advance( false, true, false );
    }
    SW_EXPECT_EQUAL( 21u, accumulation.getAccumulatedFrames() );

    // 멈추면 바로 마지막 변화 뒤부터 센다.
    accumulation.advance( false, false, false );
    SW_EXPECT_EQUAL( 2u, accumulation.getAccumulatedFrames() );
    for ( uint32 frame = 0; frame < 10u; ++frame )
    {
        accumulation.advance( false, false, false );
    }
    SW_EXPECT_EQUAL( 12u, accumulation.getAccumulatedFrames() );

    // 정착하기 전의 다음 사건(문이 닫힌다)은 처음부터고, 멈춘 뒤에는 앞 사건과의 틈에 기대지 않는다.
    for ( uint32 frame = 0; frame < 3u; ++frame )
    {
        accumulation.advance( false, true, false );
        SW_EXPECT_EQUAL( frame + 1u, accumulation.getAccumulatedFrames() );
    }
    accumulation.advance( false, false, false );
    SW_EXPECT_EQUAL( 2u, accumulation.getAccumulatedFrames() );

    // 되돌림은 언제나 처음부터다.
    accumulation.advance( true, true, false );
    SW_EXPECT_EQUAL( 1u, accumulation.getAccumulatedFrames() );
}

/**
 * @brief [RealtimeProbeTest] 드물게 되풀이하는 변화(틈이 정착 프레임보다 짧다)도 이어진 변화다 — 주기와 무관하게 정착하고 틈에서도 정착해 있다. 되풀이가 끝나면 다시 마지막 변화 뒤부터 센다
 * @details 변이: 정한 틈(갱신 주기)만 이어진 것으로 치면 10 프레임마다 움직이는 물체가 있는 장면이 영영 정착하지 않는다.
 */
SW_TEST_CASE( RealtimeProbeTest, AccumulationKeepsRareRepeatedChange )
{
    constexpr uint32 kStabilityFrames = 120u;
    for ( const uint32 period : { 2u, 10u, 60u, 119u } )
    {
        sw::RealtimeProbeAccumulation accumulation;
        uint32                        frame      = 0;
        const auto                    advanceOne = [&accumulation, &frame]( bool bChanged )
        {
            accumulation.advance( frame == 0u, bChanged, accumulation._framesSinceChange >= kStabilityFrames );
            ++frame;
        };
        // 틈을 배우고(주기 하나) 이어진 변화가 시작한 뒤 정착 프레임이 지나고 다음 주기까지면 언제나 정착해 있다.
        while ( frame < kStabilityFrames + period * 3u )
        {
            advanceOne( frame % period == 0u );
        }
        for ( uint32 step = 0; step < period * 2u; ++step )
        {
            advanceOne( frame % period == 0u );
            SW_EXPECT_TRUE( accumulation.getAccumulatedFrames() >= kStabilityFrames );
        }
        // 멈추면 배운 틈을 넘는 프레임부터 마지막 변화 뒤를 센다.
        while ( frame % period != 1u )
        {
            advanceOne( frame % period == 0u );
        }
        for ( uint32 still = 0; still < period; ++still )
        {
            advanceOne( false );
        }
        SW_EXPECT_EQUAL( period + 1u, accumulation.getAccumulatedFrames() );
    }

    // 틈이 들쭉날쭉해도 끝내 정착한다 — 바뀐 프레임은 이어진 변화다.
    sw::RealtimeProbeAccumulation mixed;
    uint32                        changeCount = 0;
    for ( uint32 frame = 0; frame < kStabilityFrames * 4u; ++frame )
    {
        const bool bChanged = frame % 7u == 0u || frame % 11u == 0u;
        mixed.advance( frame == 0u, bChanged, mixed._framesSinceChange >= kStabilityFrames );
        if ( bChanged && frame >= kStabilityFrames * 2u )
        {
            ++changeCount;
            SW_EXPECT_TRUE( mixed.getAccumulatedFrames() >= kStabilityFrames );
        }
    }
    SW_EXPECT_TRUE( changeCount > 0u );
}

/**
 * @brief [RealtimeProbeTest] 입력 행은 빛(자리 · 세기 · 색 · 원뿔 · 켜짐) · 하늘 · 볼륨 배율이 바뀌면 달라지고, 광선이 읽지 않는 값(그림자 칸 · 2D 빛 · 상한 너머의 빛)에는 그대로다
 */
SW_TEST_CASE( RealtimeProbeTest, InputRowsFollowLightsAndSky )
{
    sw::vector<sw::RealtimeProbeRequest> listRequest( 1 );
    sw::vector<sw::GPULight>             listLight( sw::shaderslot::kProbeMaxDirectLights + 1u );
    for ( sw::GPULight& light : listLight )
    {
        light._positionRadius = sw::float4{ 1.0f, 2.0f, 3.0f, 8.0f };
        light._colorIntensity = sw::float4{ 1.0f, 1.0f, 1.0f, 2.0f };
        light._directionType  = sw::float4{ 0.0f, -1.0f, 0.0f, static_cast<float32>( sw::shaderslot::kLightTypePoint ) };
    }
    listLight[1]._directionType._w = static_cast<float32>( sw::shaderslot::kLightTypePoint2D );
    sw::float3 skyColor{ 0.1f, 0.2f, 0.3f };
    const auto makeRows = [&listRequest, &skyColor]( const sw::vector<sw::GPULight>& lights )
    { return sw::RealtimeProbeUpdater::makeInputRows( listRequest, sw::SH9Color::makeConstant( skyColor ), sw::vector_reference<const sw::GPULight>( lights.data(), lights.size() ) ); };
    const sw::vector<sw::float4> rows   = makeRows( listLight );
    const auto                   isSame = [&rows]( const sw::vector<sw::float4>& other )
    { return rows.size() == other.size() && sw::Memory::compare( rows.data(), other.data(), rows.size() * sizeof( sw::float4 ) ) == 0; };

    sw::vector<sw::GPULight> listChanged  = listLight;
    listChanged[0]._params._x             = 1.0f; // 그림자 칸
    listChanged[0]._params._w             = 5.0f;
    listChanged[0]._extra                 = sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f };
    listChanged[1]._positionRadius._x     = 9.0f; // 2D 빛
    listChanged.back()._colorIntensity._w = 9.0f; // 상한 너머
    SW_EXPECT_TRUE( isSame( makeRows( listChanged ) ) );

    listChanged                       = listLight;
    listChanged[0]._positionRadius._y = 2.5f;
    SW_EXPECT_FALSE( isSame( makeRows( listChanged ) ) );
    listChanged                       = listLight;
    listChanged[0]._colorIntensity._w = 1.0f;
    SW_EXPECT_FALSE( isSame( makeRows( listChanged ) ) );
    listChanged                       = listLight;
    listChanged[0]._colorIntensity._x = 0.5f;
    SW_EXPECT_FALSE( isSame( makeRows( listChanged ) ) );
    listChanged               = listLight;
    listChanged[0]._params._y = 0.5f; // 스폿 원뿔
    SW_EXPECT_FALSE( isSame( makeRows( listChanged ) ) );
    listChanged = listLight;
    listChanged.resize( 3 ); // 꺼진 빛은 목록에서 빠진다
    SW_EXPECT_FALSE( isSame( makeRows( listChanged ) ) );

    skyColor._z = 0.4f;
    SW_EXPECT_FALSE( isSame( makeRows( listLight ) ) );
    skyColor._z                       = 0.3f;
    listRequest[0]._emissiveIntensity = 1.0f;
    SW_EXPECT_FALSE( isSame( makeRows( listLight ) ) );
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
    SW_EXPECT_TRUE( listRequest[0]._pVoxelScene->_listEmissionPalette.empty() ); // 발광 머티리얼이 없다
}

/**
 * @brief [RealtimeProbeTest] 실시간 볼륨은 0 으로 채운 프로브 자리와 볼륨 원소 · 갱신 요청으로 모이고, 복셀 격자는 프리미티브 집합이 그대로면 캐시를 돌려준다
 * @details 요청 목록을 받지 않는 수집(미리보기 · 베이크)은 실시간 볼륨을 넣지 않는다 - 볼륨 원소가 빈 프로브 버퍼를 가리키지 않게.
 */
SW_TEST_CASE( RealtimeProbeTest, CollectsRealtimeVolumeWithPlaceholderAndRequest )
{
    // 벽 머티리얼은 씬보다 먼저 만든다(메시가 원시 포인터로 든다) — 이 씬에는 기본 머티리얼이 없어 머티리얼을 비운 메시는 그려지지도 가리지도 않는다(`Material::resolveDrawMaterial`).
    const sw::shared_ptr<sw::Material> wall = makeOpaqueMaterial();
    sw::Scene                          scene( "RealtimeProbeCollect" );
    sw::GameObjectManager*             pObjects = scene.getObjectManager();
    SW_REQUIRE_NOT_NULL( pObjects );
    sw::GameObject* pVolumeObject = pObjects->createGameObject( sw::hashed_string( "Volume" ) );
    SW_REQUIRE_NOT_NULL( pVolumeObject );
    sw::LightProbeVolumeComponent* pVolume = pVolumeObject->addComponent<sw::LightProbeVolumeComponent>();
    SW_REQUIRE_NOT_NULL( pVolume );
    pVolume->setExtent( sw::float3{ 4.0f, 2.0f, 4.0f } );
    pVolume->setSpacing( 2.0f );
    pVolume->setRealtime( true );
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Wall", sw::float3{ 0.0f, 0.0f, 0.0f }, wall.get(), true ) );

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
    SW_EXPECT_EQUAL( size_t( sw::LightProbeElementBuilder::computeRealtimeElementCount( static_cast<uint32>( probeCount ) ) ), pProbe->size() ); // SH9 자리 + 가시성 모먼트 자리
    for ( const sw::float4& element : *pProbe )
    {
        SW_EXPECT_NEAR( 0.0f, element._x + element._y + element._z + element._w, 0.0f ); // 자리는 0 - 값은 컴퓨트가 쓴다
    }
    const sw::RealtimeProbeRequest& request = listRequest[0];
    SW_EXPECT_EQUAL( static_cast<uint32>( probeCount ), request._probeCount );
    SW_EXPECT_EQUAL( pVolume->getRealtimeRayCount(), request._rayCount );
    SW_REQUIRE_NOT_NULL( request._pVoxelScene.get() );
    SW_EXPECT_TRUE( paintWhole( *request._pVoxelScene ).countOccupied() > 0u );
    SW_EXPECT_EQUAL( listLight[0]._params._x, request._volumeElement._params._x );

    // 같은 집합이면 같은 격자(캐시), 메시가 하나 늘면 새 격자.
    // 프로브 원소 묶음도 내용(자리 수)이 그대로면 같은 포인터다 - 렌더 스레드가 다시 올리지 않는다.
    sw::vector<sw::RealtimeProbeRequest> again;
    collectSceneLights( &scene, listLight, nullptr, &probeElement, nullptr, &again );
    SW_REQUIRE_EQUAL( size_t( 1 ), again.size() );
    SW_EXPECT_TRUE( again[0]._pVoxelScene == request._pVoxelScene );
    SW_EXPECT_TRUE( probeElement.getElements() == pProbe );
    SW_REQUIRE_NOT_NULL( addCube( pObjects, "Second", sw::float3{ 1.0f, 0.0f, 0.0f }, wall.get(), true ) );
    collectSceneLights( &scene, listLight, nullptr, &probeElement, nullptr, &again );
    SW_REQUIRE_EQUAL( size_t( 1 ), again.size() );
    SW_EXPECT_TRUE( again[0]._pVoxelScene != request._pVoxelScene );

    // 요청 목록을 받지 않는 수집은 실시간 볼륨을 넣지 않는다.
    collectSceneLights( &scene, listLight, nullptr, &probeElement );
    SW_EXPECT_EQUAL( size_t( 0 ), listLight.size() );
    SW_EXPECT_TRUE( probeElement.getElements() == nullptr );
}
