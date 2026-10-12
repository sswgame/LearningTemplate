#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Physics/Collision/PhysicsMeshShapeCache.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Physics/PhysicsSystem.h"

#include "TestFramework/TestFramework.h"

// 볼록 껍질 · 삼각 메시 셰이프를 `.mesh` 에서 채우는 길(`PhysicsMeshShapeCache`) — 합치기, 실제 메시 파일 읽기, 두 셰이프 종류, 실패.

namespace
{
    constexpr float32 kMeshShapeStep = 1.0f / 60.0f;

    /** @brief 반 크기 0.5 인 정육면체의 삼각형 목록(36 위치, 모서리 8 개를 삼각형마다 복사 — 임포터가 쓰는 모양)입니다. */
    sw::vector<sw::float3> makeCubeTriangleList()
    {
        const sw::float3 arrCorner[8] = {
            sw::float3{-0.5f, -0.5f, -0.5f},
            sw::float3{ 0.5f, -0.5f, -0.5f},
            sw::float3{ 0.5f,  0.5f, -0.5f},
            sw::float3{-0.5f,  0.5f, -0.5f},
            sw::float3{-0.5f, -0.5f,  0.5f},
            sw::float3{ 0.5f, -0.5f,  0.5f},
            sw::float3{ 0.5f,  0.5f,  0.5f},
            sw::float3{-0.5f,  0.5f,  0.5f},
        };
        // 면마다 두 삼각형. 감기는 엔진 앞면(왼손 좌표계에서 시계 방향)이다 — (b - a) × (c - a) 가 바깥을 향하므로 Jolt 에서도 앞면이다.
        const uint32           arrIndex[36] = { 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5 };
        sw::vector<sw::float3> listPosition;
        for ( const uint32 cornerIndex : arrIndex )
        {
            listPosition.push_back( arrCorner[cornerIndex] );
        }
        return listPosition;
    }

    /** @brief 엔진 조립 지점이 넣을 읽기 함수와 같은 일을 합니다 — `.mesh` 정점(삼각형 목록)의 위치만 꺼냅니다. */
    [[nodiscard]] bool loadMeshPositions( sw::string_view path, sw::vector<sw::float3>& outListPosition )
    {
        sw::vector<sw::RHIVertex> listVertex;
        if ( sw::MeshAssetFormat::loadFromResource( path, listVertex ) == false )
            return false;
        outListPosition.clear();
        outListPosition.reserve( listVertex.size() );
        for ( const sw::RHIVertex& vertex : listVertex )
        {
            outListPosition.push_back( sw::float3{ vertex._arrPosition[0], vertex._arrPosition[1], vertex._arrPosition[2] } );
        }
        return true;
    }

    /** @brief 정육면체 `.mesh` 를 임시 폴더에 쓰고 그 경로를 돌려줍니다. */
    sw::string writeCubeMesh()
    {
        sw::vector<sw::RHIVertex> listVertex;
        for ( const sw::float3& position : makeCubeTriangleList() )
        {
            sw::RHIVertex vertex{};
            vertex._arrPosition[0] = position._x;
            vertex._arrPosition[1] = position._y;
            vertex._arrPosition[2] = position._z;
            vertex._arrNormal[1]   = 1.0f;
            listVertex.push_back( vertex );
        }
        const sw::string path = test::makeTempPath( "cube.mesh" );
        SW_EXPECT_TRUE( sw::MeshAssetFormat::saveToFile( path, listVertex ) );
        return path;
    }

    /** @brief 테스트가 끝나면 읽기 함수와 표를 처음 상태로 돌립니다(같은 프로세스의 다른 테스트에 남기지 않는다). */
    struct ScopedMeshLoader
    {
        explicit ScopedMeshLoader( sw::PhysicsMeshShapeCache::LoadTriangleListFunction pfnLoad )
        {
            sw::PhysicsMeshShapeCache::clear();
            sw::PhysicsMeshShapeCache::setLoadFunction( pfnLoad );
        }
        ~ScopedMeshLoader()
        {
            sw::PhysicsMeshShapeCache::setLoadFunction( nullptr );
            sw::PhysicsMeshShapeCache::clear();
        }
        ScopedMeshLoader( const ScopedMeshLoader& )            = delete;
        ScopedMeshLoader& operator=( const ScopedMeshLoader& ) = delete;
    };

    sw::unique_ptr<sw::IPhysicsScene3D> makeScene()
    {
        sw::PhysicsSettings settings;
        settings.ensureDefaults();
        return sw::engine::getPhysicsSystem().createScene3D( settings );
    }

    sw::PhysicsShapeDesc3D makeMeshShape( sw::PhysicsShapeType3D type, const sw::string& meshPath )
    {
        sw::PhysicsShapeDesc3D shape;
        shape._type     = type;
        shape._meshPath = meshPath;
        return shape;
    }

    sw::PhysicsBodyHandle addBody( sw::IPhysicsScene3D& scene, const sw::PhysicsShapeDesc3D& shape, const sw::float3& position, sw::PhysicsBodyType type )
    {
        sw::PhysicsBodyDesc3D desc;
        desc._listShape.push_back( shape );
        desc._position = position;
        desc._type     = type;
        return scene.createBody( desc );
    }
} // namespace

/**
 * @brief [PhysicsMeshShapeTest] 삼각형 목록의 같은 위치는 한 점으로 합치고, 퇴화 삼각형과 셋으로 나누어떨어지지 않는 끝은 버린다
 */
SW_TEST_CASE( PhysicsMeshShapeTest, MakeShapeDataWeldsSharedCornersAndDropsDegenerate )
{
    sw::vector<sw::float3> listPosition = makeCubeTriangleList();
    listPosition.push_back( sw::float3{ 0.5f, 0.5f, 0.5f } ); // 퇴화 삼각형(두 점이 같다)
    listPosition.push_back( sw::float3{ 0.5f, 0.5f, 0.5f } );
    listPosition.push_back( sw::float3{ -0.5f, 0.5f, 0.5f } );
    listPosition.push_back( sw::float3{ 9.0f, 9.0f, 9.0f } ); // 셋으로 나누어떨어지지 않는 끝

    sw::PhysicsMeshShapeData data;
    sw::PhysicsMeshShapeCache::makeShapeData( sw::span<const sw::float3>{ listPosition.data(), listPosition.size() }, data );
    SW_EXPECT_EQUAL( static_cast<size_t>( 8 ), data._listPoint.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 36 ), data._listIndex.size() );
    for ( const uint32 index : data._listIndex )
    {
        SW_EXPECT_TRUE( index < 8u );
    }
    // 합친 뒤에도 삼각형은 원래 위치를 가리킨다.
    const sw::vector<sw::float3> listCube = makeCubeTriangleList();
    for ( size_t index = 0; index < listCube.size(); ++index )
    {
        const sw::float3& welded = data._listPoint[data._listIndex[index]];
        SW_EXPECT_TRUE( welded._x == listCube[index]._x && welded._y == listCube[index]._y && welded._z == listCube[index]._z );
    }
}

/**
 * @brief [PhysicsMeshShapeTest] `_meshPath` 만 적은 삼각 메시와 볼록 껍질은 실제 `.mesh` 파일의 정점으로 채워진다(경로마다 한 번 읽는다)
 */
SW_TEST_CASE( PhysicsMeshShapeTest, MeshPathFillsTriangleMeshAndConvexHull )
{
    const ScopedMeshLoader loader{ &loadMeshPositions };
    const sw::string       meshPath = writeCubeMesh();

    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    // 원점에 정적 삼각 메시 정육면체 하나 — 위에서 쏜 광선은 윗면에, 떨어진 껍질은 그 위에 닿는다.
    const sw::PhysicsBodyHandle meshBody =
        addBody( *pScene, makeMeshShape( sw::PhysicsShapeType3D::TriangleMesh, meshPath ), sw::float3{ 0.0f, 0.0f, 0.0f }, sw::PhysicsBodyType::Static );
    SW_ASSERT_TRUE( meshBody.isValid() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), sw::PhysicsMeshShapeCache::getCachedCount() );

    sw::PhysicsQueryFilter filter;
    sw::PhysicsCastHit3D   hit;
    SW_ASSERT_TRUE( pScene->raycast( sw::float3{ 0.0f, 10.0f, 0.0f }, sw::float3{ 0.0f, -1.0f, 0.0f }, 100.0f, filter, hit ) );
    SW_EXPECT_NEAR_EQUAL( 9.5f, hit._distance, 1e-3f ); // 메시 윗면(y = 0.5)
    SW_EXPECT_TRUE( hit._body == meshBody );

    // 같은 메시의 볼록 껍질은 움직일 수 있다 — 메시 정육면체 위(윗면 y = 0.5)에 떨어져 껍질 중심이 윗면 + 반 변(0.5) = 1.0 에서 멈춘다.
    const sw::PhysicsBodyHandle hullBody =
        addBody( *pScene, makeMeshShape( sw::PhysicsShapeType3D::ConvexHull, meshPath ), sw::float3{ 0.0f, 3.0f, 0.0f }, sw::PhysicsBodyType::Dynamic );
    SW_ASSERT_TRUE( hullBody.isValid() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), sw::PhysicsMeshShapeCache::getCachedCount() ); // 같은 경로는 다시 읽지 않는다
    for ( uint32 stepIndex = 0; stepIndex < 240; ++stepIndex )
    {
        pScene->step( kMeshShapeStep );
    }
    sw::float3     position{};
    sw::quaternion rotation{};
    SW_ASSERT_TRUE( pScene->getBodyTransform( hullBody, position, rotation ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, position._y, 0.05f );
    SW_EXPECT_NEAR_EQUAL( 1000.0f, pScene->getBodyMass( hullBody ), 30.0f ); // 1 m^3 × 밀도 1000(껍질의 볼록 반지름만큼 어긋난다)

    // 디버그 그리기와 시각화가 쓰는 창도 같은 기하를 본다.
    const sw::PhysicsShapeGeometry geometry = sw::PhysicsMeshShapeCache::acquireGeometry( makeMeshShape( sw::PhysicsShapeType3D::TriangleMesh, meshPath ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 8 ), geometry._listPoint.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 36 ), geometry._listIndex.size() );

    // 점을 직접 적었으면 메시보다 그것이 이긴다.
    sw::PhysicsShapeDesc3D written = makeMeshShape( sw::PhysicsShapeType3D::ConvexHull, meshPath );
    written._listPoint             = {
        sw::float3{0.0f, 0.0f, 0.0f},
        sw::float3{1.0f, 0.0f, 0.0f},
        sw::float3{0.0f, 1.0f, 0.0f},
        sw::float3{0.0f, 0.0f, 1.0f}
    };
    SW_EXPECT_EQUAL( static_cast<size_t>( 4 ), sw::PhysicsMeshShapeCache::acquireGeometry( written )._listPoint.size() );
}

/**
 * @brief [PhysicsMeshShapeTest] 읽기 함수가 없거나 메시를 읽지 못하면 바디를 만들지 않고, 실패를 기억해 다시 읽지 않는다
 */
SW_TEST_CASE( PhysicsMeshShapeTest, MissingMeshOrLoaderIsRejected )
{
    sw::unique_ptr<sw::IPhysicsScene3D> pScene = makeScene();
    SW_ASSERT_NOT_NULL( pScene.get() );
    SW_TEST_DEFENSIVE_SCOPE( "a mesh shape without a load function or file is rejected" );
    {
        const ScopedMeshLoader loader{ nullptr };
        SW_EXPECT_FALSE( sw::PhysicsMeshShapeCache::hasLoadFunction() );
        SW_EXPECT_FALSE( addBody( *pScene, makeMeshShape( sw::PhysicsShapeType3D::ConvexHull, "physics/never.mesh" ), sw::float3{}, sw::PhysicsBodyType::Dynamic ).isValid() );
    }
    {
        const ScopedMeshLoader loader{ &loadMeshPositions };
        const sw::string       missing = test::makeTempPath( "never_written.mesh" );
        SW_EXPECT_FALSE( addBody( *pScene, makeMeshShape( sw::PhysicsShapeType3D::TriangleMesh, missing ), sw::float3{}, sw::PhysicsBodyType::Static ).isValid() );
        SW_EXPECT_TRUE( sw::PhysicsMeshShapeCache::acquire( missing ) == nullptr );
        SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), sw::PhysicsMeshShapeCache::getCachedCount() );
    }
}
