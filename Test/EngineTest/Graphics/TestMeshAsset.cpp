#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/MeshLODSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/SceneAnimation/AnimationLOD.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Resource/AssetHandle.h"
#include "Engine/Resource/Cook/CookJob.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

#include "TestFramework/TestFramework.h"

// MeshAssetTest — 메시 에셋 파일(`.mesh`) 형식 · 경로 캐시 · MeshComponent 해석. 디바이스 없음(nogpu).

namespace
{
    /** @brief 정점 하나를 위치 · 색으로 만듭니다(노멀 +Y, UV 는 위치 xy). */
    sw::RHIVertex makeTestVertex( float32 x, float32 y, float32 z, float32 red )
    {
        sw::RHIVertex vertex{};
        vertex._arrPosition[0] = x;
        vertex._arrPosition[1] = y;
        vertex._arrPosition[2] = z;
        vertex._arrNormal[1]   = 1.0f;
        vertex._arrUv[0]       = x;
        vertex._arrUv[1]       = y;
        vertex._arrColor[0]    = red;
        vertex._arrColor[3]    = 1.0f;
        return vertex;
    }

    /** @brief 삼각형 @p triangleCount 개짜리 정점 배열입니다. 삼각형마다 x 가 1 씩 밀립니다. */
    sw::vector<sw::RHIVertex> makeTestTriangles( uint32 triangleCount, float32 red )
    {
        sw::vector<sw::RHIVertex> listVertex;
        for ( uint32 triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex )
        {
            const float32 offset = static_cast<float32>( triangleIndex );
            listVertex.push_back( makeTestVertex( offset, 0.0f, 0.0f, red ) );
            listVertex.push_back( makeTestVertex( offset, 1.0f, 0.0f, red ) );
            listVertex.push_back( makeTestVertex( offset + 1.0f, 0.0f, 0.0f, red ) );
        }
        return listVertex;
    }

    /**
     * @brief 컴포넌트의 메시 로드가 끝날 때까지 씬 프레임의 도착 단계를 돌립니다.
     * @details `acquire` 가 돌아와도 컴포넌트의 로드 블록이 끝났다는 보장은 없다 — 워커는 메시를 캐시에 넣고 진행 중 표에서 뺀 **뒤에** 블록을 끝내므로,
     *          그 틈의 `acquire` 는 캐시에서 새 블록을 받아 먼저 돌아온다. 그래서 컴포넌트 쪽 상태를 기다린다(시한 10 초).
     */
    void waitForMeshArrival( sw::GameObjectManager& manager, const sw::MeshComponent& component, const sw::string& path )
    {
        (void)sw::MeshCache::acquire( path ); // 아직 아무도 읽지 않았으면 이 스레드가 읽는다 — 결과는 컴포넌트가 받는다
        const sw::Stopwatch stopwatch;
        do
        {
            manager.getPrimitiveRegistry().updateAssetWaiters( 0.0f );
            if ( component.isMeshLoading() == false )
                return;
            std::this_thread::yield();
        } while ( stopwatch.getElapsedMilliseconds() < 10'000 );
    }

    struct TestMeshLODInternal
    {
        /** @brief LOD 0 은 삼각형 넷, LOD 1 은 둘(임계값 0.5), LOD 2 는 하나(임계값 0.1)인 메시입니다. */
        static sw::MeshAssetData makeLODData()
        {
            sw::MeshAssetData data{};
            data._listVertex = makeTestTriangles( 4, 0.1f );
            data._listLOD.push_back( sw::MeshLODLevel{ makeTestTriangles( 2, 0.1f ), 0.5f } );
            data._listLOD.push_back( sw::MeshLODLevel{ makeTestTriangles( 1, 0.1f ), 0.1f } );
            return data;
        }

        /** @brief 눈 @p eye 에서 @p target 을 보는 90 도 시야 뷰입니다 — 화면 크기 = 반지름 / 거리. */
        static sw::AnimationLODView makeViewToward( const sw::float3& eye, const sw::float3& target )
        {
            const sw::float4x4 view       = sw::float4x4::makeLookAt( eye, target, sw::float3{ 0.0f, 1.0f, 0.0f } );
            const sw::float4x4 projection = sw::float4x4::makePerspectiveFieldOfView( sw::MathUtil::kPi * 0.5f, 1.0f, 0.1f, 1000.0f );
            return sw::AnimationLODView::make( view * projection, eye );
        }
    };
} // namespace

/**
 * @brief [MeshAssetTest] 쓴 정점을 그대로 다시 읽는다 — 위치 · 노멀 · UV · 색과 경계 반지름까지. 탄젠트는 쓰기가 `Mesh::computeTangents` 로 구한 값이다
 */
SW_TEST_CASE( MeshAssetTest, WriteThenReadRoundTripsEveryField )
{
    const sw::vector<sw::RHIVertex> listWritten = makeTestTriangles( 2, 0.25f );
    sw::vector<uint8>               bytes;
    sw::MeshAssetFormat::makeBytes( listWritten, bytes );
    SW_ASSERT_EQUAL( size_t( sw::MeshAssetFormat::kHeaderSize + listWritten.size() * sw::MeshAssetFormat::kVertexSize ), bytes.size() );

    sw::vector<sw::RHIVertex> listExpected = listWritten;
    sw::Mesh::computeTangents( listExpected );
    sw::vector<sw::RHIVertex> listRead;
    float32                   boundingRadius = -1.0f;
    SW_ASSERT_TRUE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), listRead, &boundingRadius ) );
    SW_ASSERT_EQUAL( listWritten.size(), listRead.size() );
    for ( size_t vertexIndex = 0; vertexIndex < listRead.size(); ++vertexIndex )
    {
        SW_EXPECT_TRUE( sw::Memory::compare( &listExpected[vertexIndex], &listRead[vertexIndex], sizeof( sw::RHIVertex ) ) == 0 );
        // UV 의 u 가 +X 로 는다 — 탄젠트는 +X, 노멀(+Y)에 수직이다.
        SW_EXPECT_NEAR_EQUAL( 1.0f, listRead[vertexIndex]._arrTangent[0], 1e-5f );
    }
    // 가장 먼 정점은 (2, 0, 0) 이다.
    SW_EXPECT_NEAR_EQUAL( 2.0f, boundingRadius, 1e-6f );

    // 파일로도 같다.
    const sw::string path = test::makeTempPath( "roundtrip.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, listWritten ) );
    sw::vector<sw::RHIVertex> listLoaded;
    SW_ASSERT_TRUE( sw::MeshAssetFormat::loadFromResource( ::test::makeTempResourceID( path ), listLoaded ) );
    SW_EXPECT_EQUAL( listWritten.size(), listLoaded.size() );
}

/**
 * @brief [MeshAssetTest] 머리와 길이가 맞지 않는 파일은 거절한다 — 짧은 파일 · 남는 바이트 · 다른 버전 · 매직 없음 · 삼각형이 아닌 정점 수
 * @details 거절하지 않으면 깨진 파일의 정점 수만큼 버퍼 밖을 읽거나, 반쯤 쓴 파일을 조용히 일부만 그린다.
 */
SW_TEST_CASE( MeshAssetTest, MalformedBytesAreRejected )
{
    sw::vector<uint8> valid;
    sw::MeshAssetFormat::makeBytes( makeTestTriangles( 1, 1.0f ), valid );
    sw::vector<sw::RHIVertex>    listRead;
    test::ScopedDefensiveTestLog expected( "malformed mesh assets are rejected with a warning" );

    // 머리보다 짧다.
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( valid.data(), sw::MeshAssetFormat::kHeaderSize - 1, listRead ) );
    // 정점 배열이 잘렸다.
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( valid.data(), valid.size() - 4, listRead ) );
    SW_EXPECT_TRUE( listRead.empty() );

    // 뒤에 바이트가 남는다.
    sw::vector<uint8> trailing = valid;
    trailing.push_back( 0 );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( trailing.data(), trailing.size(), listRead ) );

    // 다른 버전.
    sw::vector<uint8> otherVersion = valid;
    otherVersion[4]                = static_cast<uint8>( sw::MeshAssetFormat::kVersion + 1 );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( otherVersion.data(), otherVersion.size(), listRead ) );

    // 매직이 없다.
    sw::vector<uint8> noMagic = valid;
    noMagic[0]                = 'X';
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( noMagic.data(), noMagic.size(), listRead ) );

    // 정점 수가 3 의 배수가 아니다(길이는 머리에 맞춘다).
    sw::vector<sw::RHIVertex> listTwoVertex = makeTestTriangles( 1, 1.0f );
    listTwoVertex.pop_back();
    sw::vector<uint8> notTriangles;
    sw::MeshAssetFormat::makeBytes( listTwoVertex, notTriangles );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( notTriangles.data(), notTriangles.size(), listRead ) );

    // 탄젠트 부호가 ±1 이 아니다(첫 정점의 탄젠트 w — 머리 뒤 float 열다섯 번째 칸).
    sw::vector<uint8> badTangent = valid;
    const size_t      signOffset = sw::MeshAssetFormat::kHeaderSize + 15 * sizeof( float32 );
    badTangent[signOffset + 0]   = 0;
    badTangent[signOffset + 1]   = 0;
    badTangent[signOffset + 2]   = 0;
    badTangent[signOffset + 3]   = 0;
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( badTangent.data(), badTangent.size(), listRead ) );

    // 원본은 여전히 읽힌다(위 거절이 우연이 아니다).
    SW_EXPECT_TRUE( sw::MeshAssetFormat::readFromBytes( valid.data(), valid.size(), listRead ) );
}

/**
 * @brief [MeshAssetTest] 같은 경로는 같은 Mesh 하나를 나눠 받고, 다시 읽기는 그 Mesh 를 제자리에서 바꾼다
 * @details 컴포넌트마다 제 Mesh 를 받으면 배치 키(메시 포인터)가 갈린다. 다시 읽기가 새 Mesh 를 만들면 이미 든 컴포넌트는 옛 정점을 그린다.
 */
SW_TEST_CASE( MeshAssetTest, CacheSharesOneMeshPerPathAndReloadsInPlace )
{
    const sw::string path = test::makeTempPath( "shared.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, makeTestTriangles( 1, 1.0f ) ) );

    const sw::shared_ptr<sw::Mesh> first  = sw::MeshCache::acquire( ::test::makeTempResourceID( path ) );
    const sw::shared_ptr<sw::Mesh> second = sw::MeshCache::acquire( ::test::makeTempResourceID( path ) );
    SW_ASSERT_NOT_NULL( first.get() );
    SW_EXPECT_TRUE( first == second );
    SW_EXPECT_EQUAL( 3u, first->getVertexCount() );

    sw::MeshCache cache;
    SW_EXPECT_TRUE( cache.isCached( ::test::makeTempResourceID( path ) ) );

    const uint64 contentBefore = first->getContentID();
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, makeTestTriangles( 3, 0.5f ) ) );
    SW_EXPECT_TRUE( sw::MeshCache::reloadShared( ::test::makeTempResourceID( path ), nullptr ) );
    SW_EXPECT_EQUAL( 9u, first->getVertexCount() );
    SW_EXPECT_TRUE( first->getContentID() != contentBefore ); // 정점 풀이 다시 올릴 근거
    SW_EXPECT_TRUE( sw::MeshCache::acquire( ::test::makeTempResourceID( path ) ) == first );

    // 깨진 파일로 다시 읽으면 옛 정점을 지킨다.
    const uint8 arrGarbage[3] = { 1, 2, 3 };
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, arrGarbage, sizeof( arrGarbage ) ) );
    {
        test::ScopedDefensiveTestLog expected( "reloading a malformed mesh keeps the old vertices" );
        SW_EXPECT_FALSE( sw::MeshCache::reloadShared( ::test::makeTempResourceID( path ), nullptr ) );
    }
    SW_EXPECT_EQUAL( 9u, first->getVertexCount() );
}

/**
 * @brief [MeshAssetTest] 준비 집합은 읽은 메시를 쥐어 캐시에 남기고, 뒤의 acquire 는 그 메시를 받으며, clear 하면 놓는다
 * @details 캐시는 약한 참조라 집합이 쥐지 않으면 미리 읽은 메시가 바로 사라져 스폰이 다시 디스크를 읽는다(StarSkirmish 의 플레이 중 히치).
 *          없는 경로는 집합을 `Failed` 로 만들고 이유를 남긴다 — 기본 메시로 대신 채우지 않는다.
 */
SW_TEST_CASE( MeshAssetTest, ReadySetHoldsMeshesUntilCleared )
{
    const sw::string pathA = test::makeTempPath( "preload_a.mesh" );
    const sw::string pathB = test::makeTempPath( "preload_b.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( pathA, makeTestTriangles( 1, 1.0f ) ) );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( pathB, makeTestTriangles( 2, 1.0f ) ) );

    sw::AssetReadySet readySet;
    readySet.add( sw::MeshCache::request( ::test::makeTempResourceID( pathA ) ) );
    readySet.add( sw::MeshCache::request( ::test::makeTempResourceID( pathB ) ) );
    const sw::Stopwatch stopwatch;
    while ( readySet.isComplete() == false && stopwatch.getElapsedMilliseconds() < 10'000 )
    {
        std::this_thread::yield();
    }
    SW_ASSERT_TRUE( readySet.isReady() );
    SW_EXPECT_EQUAL( size_t( 2 ), readySet.countReady() );

    sw::MeshCache cache;
    SW_EXPECT_TRUE( cache.isCached( ::test::makeTempResourceID( pathA ) ) );
    SW_EXPECT_EQUAL( 6u, sw::MeshCache::acquire( ::test::makeTempResourceID( pathB ) )->getVertexCount() );

    readySet.clear();
    SW_EXPECT_FALSE( cache.isCached( ::test::makeTempResourceID( pathA ) ) );

    {
        test::ScopedDefensiveTestLog expected( "a missing mesh in a ready set fails the set" );
        readySet.add( sw::MeshCache::request( test::makeTempResourceID( test::makeTempPath( "preload_missing.mesh" ) ) ) );
        while ( readySet.isComplete() == false && stopwatch.getElapsedMilliseconds() < 10'000 )
        {
            std::this_thread::yield();
        }
    }
    SW_EXPECT_TRUE( readySet.hasFailed() );
    sw::vector<sw::string> listError;
    readySet.appendFailures( listError );
    SW_EXPECT_EQUAL( size_t( 1 ), listError.size() );
}

/**
 * @brief [MeshAssetTest] 동기 acquire 는 요청과 같은 로드 블록을 탄다 — 먼저 낸 요청의 핸들과 acquire 가 같은 메시를 받고, 핸들 상태는 `Ready` 다
 * @details 두 분기(비동기 요청 · 동기 읽기)가 따로 읽으면 같은 경로가 두 번 읽히고 배치 키(메시 포인터)가 갈린다.
 */
SW_TEST_CASE( MeshAssetTest, AcquireWaitsOnTheSameLoadAsRequest )
{
    const sw::string path = test::makeTempPath( "same_path.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, makeTestTriangles( 1, 1.0f ) ) );

    const sw::MeshHandle handle = sw::MeshCache::request( ::test::makeTempResourceID( path ) );
    SW_ASSERT_TRUE( handle.isValid() );
    const sw::shared_ptr<sw::Mesh> mesh = sw::MeshCache::acquire( ::test::makeTempResourceID( path ) );
    SW_ASSERT_NOT_NULL( mesh.get() );
    SW_EXPECT_TRUE( handle.wait() == mesh );
    SW_EXPECT_TRUE( handle.getState() == sw::AssetLoadState::Ready );
    SW_EXPECT_TRUE( handle.getError().empty() );

    // 빈 경로는 읽지 않고 바로 실패한다(이유가 있다).
    const sw::MeshHandle empty = sw::MeshCache::request( "" );
    SW_EXPECT_TRUE( empty.hasFailed() );
    SW_EXPECT_FALSE( empty.getError().empty() );
    SW_EXPECT_NULL( empty.get().get() );

    // 요청 없는 핸들은 상태가 없다.
    const sw::MeshHandle none;
    SW_EXPECT_FALSE( none.isValid() );
    SW_EXPECT_FALSE( none.isReady() );
    SW_EXPECT_FALSE( none.hasFailed() );
}

/**
 * @brief [MeshAssetTest] 쿠킹 작업 안의 비동기 요청은 요청한 자리에서 읽혀 그 작업의 입력이 된다
 * @details 작업 범위는 스레드 지역 값이다. 워커에서 읽으면 범위 밖이라 쿠킹 세션이 그 읽기를 실패시킨다 — 쿠커가 씬을 세우면 `MeshComponent` 가
 *          메시를 비동기로 요청하므로 Shipping 의 `--cook-all` 이 메시마다 오류를 냈다.
 */
SW_TEST_CASE( MeshAssetTest, AsyncRequestInsideACookJobIsReadAsAnInput )
{
    const sw::string path = test::makeTempPath( "cook_job_request.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, makeTestTriangles( 1, 1.0f ) ) );

    sw::CookSession session;
    sw::CookJobDesc desc;
    desc._cookerName    = "TestCooker";
    desc._cookerVersion = 1;
    desc._target        = "Windows";
    desc._settingsHash  = 0;
    sw::CookJob job( desc );                                  // 작업에 입력을 기록하므로 const 가 아니다
    SW_EXPECT_FALSE( sw::AssetLoadBlock::canLoadOnWorker() ); // 오디오 디코드 · 스트리밍 큐도 이 값으로 정한다
    const sw::MeshHandle handle = sw::MeshCache::request( ::test::makeTempResourceID( path ) );
    SW_EXPECT_TRUE( handle.isReady() ); // 돌아오기 전에 이 스레드에서 읽었다
    SW_EXPECT_EQUAL( size_t( 1 ), job.getInputs().size() );
}

/**
 * @brief [MeshAssetTest] `_meshID` 가 `.mesh` 경로면 MeshComponent 는 그 에셋의 메시를 그린다 — 내장 도형 이름은 그대로 통한다
 */
SW_TEST_CASE( MeshAssetTest, MeshComponentResolvesMeshAssetPath )
{
    const sw::string path = test::makeTempPath( "component.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, makeTestTriangles( 2, 1.0f ) ) );

    const sw::SerializeContext& context = sw::SerializeContext::getDefault( sw::SerializePurpose::Authoring );
    sw::GameObjectManager       manager;
    sw::GameObject*             pProp = manager.createGameObject( sw::hashed_string( "Model" ) );
    sw::MeshComponent*          pMesh = pProp->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    const sw::PropertyInfo* pMeshID = pMesh->getTypeInfo()->findPropertyInHierarchy( sw::hashed_string( "_meshID" ) );
    SW_ASSERT_NOT_NULL( pMeshID );

    SW_ASSERT_TRUE( sw::SerializerUtil::applyPropertyText( *pMeshID, sw::MutableObjectAddress{ pMesh }, ::test::makeTempResourceID( path ), context ) );
    pMesh->onPropertyChanged( pMeshID->_name );
    // 에셋은 워커가 읽는다 — 다 읽을 때까지 메시가 없고(그리지 않는다), 씬 프레임의 도착 단계가 넣는다.
    waitForMeshArrival( manager, *pMesh, ::test::makeTempResourceID( path ) );
    SW_EXPECT_FALSE( pMesh->isMeshLoading() );
    SW_ASSERT_NOT_NULL( pMesh->getRawMesh() );
    SW_EXPECT_EQUAL( 6u, pMesh->getRawMesh()->getVertexCount() );
    SW_EXPECT_TRUE( pMesh->getRawMesh() == sw::MeshCache::acquire( ::test::makeTempResourceID( path ) ).get() );

    // 세터도 같은 길이다.
    pMesh->setMeshID( "Sphere" );
    SW_EXPECT_TRUE( pMesh->getRawMesh() == sw::MeshUtil::acquirePrimitive( "Sphere" ).get() );

    // 없는 에셋은 메시 없이 남는다(그리지 않는다).
    {
        test::ScopedDefensiveTestLog expected( "a missing mesh asset leaves the component without a mesh" );
        const sw::string             missingPath = test::makeTempPath( "missing.mesh" );
        pMesh->setMeshID( ::test::makeTempResourceID( missingPath ) );
        waitForMeshArrival( manager, *pMesh, ::test::makeTempResourceID( missingPath ) );
    }
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getPrimitiveRegistry().countAssetWaiters() );
    SW_EXPECT_NULL( pMesh->getRawMesh() );
}

/**
 * @brief [MeshAssetTest] 등장 · 사라지기는 `_appearSeconds` 동안 씬 프레임의 도착 단계가 비율을 올리고 내린다 — 끝나면 대기에서 빠지고, 다 사라지면 그리지 않는다
 */
SW_TEST_CASE( MeshAssetTest, AppearanceRampsOverAppearSeconds )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pProp = manager.createGameObject( sw::hashed_string( "Appearing" ) );
    sw::MeshComponent*    pMesh = pProp->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    SW_EXPECT_EQUAL( 1.0f, pMesh->getAppearance() ); // 바로 보이는 메시(내장 도형)는 다 보인다

    const float32 halfStep = sw::MeshComponent::kDefaultAppearSeconds * 0.5f;
    pMesh->beginAppearance();
    SW_EXPECT_EQUAL( 0.0f, pMesh->getAppearance() );
    SW_EXPECT_EQUAL( size_t( 1 ), manager.getPrimitiveRegistry().countAssetWaiters() );
    manager.getPrimitiveRegistry().updateAssetWaiters( halfStep );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pMesh->getAppearance(), 1e-4f );
    manager.getPrimitiveRegistry().updateAssetWaiters( halfStep );
    SW_EXPECT_EQUAL( 1.0f, pMesh->getAppearance() );
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getPrimitiveRegistry().countAssetWaiters() );

    pMesh->beginDisappearance();
    manager.getPrimitiveRegistry().updateAssetWaiters( sw::MeshComponent::kDefaultAppearSeconds );
    SW_EXPECT_EQUAL( 0.0f, pMesh->getAppearance() );
    SW_EXPECT_TRUE( pMesh->isFullyDisappeared() );
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getPrimitiveRegistry().countAssetWaiters() );
}

/**
 * @brief [MeshAssetTest] 스킨 스트림 왕복 — 본 번호 · 가중치 · 본 수가 남고, 캐시가 메시에 스킨을 건다. 본 수를 넘는 본 번호는 거절한다
 */
SW_TEST_CASE( MeshAssetTest, SkinStreamRoundTripsAndRejectsOutOfRangeJoints )
{
    sw::MeshAssetData written{};
    written._listVertex    = makeTestTriangles( 2, 0.5f );
    written._skinBoneCount = 3;
    for ( size_t vertexIndex = 0; vertexIndex < written._listVertex.size(); ++vertexIndex )
    {
        sw::MeshSkinVertex skin{};
        skin._arrJoint[0]  = static_cast<uint16>( vertexIndex % 3 );
        skin._arrJoint[1]  = 2;
        skin._arrWeight[0] = 0.75f;
        skin._arrWeight[1] = 0.25f;
        written._listSkinVertex.push_back( skin );
    }
    sw::vector<uint8> bytes;
    sw::MeshAssetFormat::makeBytes( written, bytes );
    SW_ASSERT_EQUAL( size_t( sw::MeshAssetFormat::kHeaderSize + written._listVertex.size() * ( sw::MeshAssetFormat::kVertexSize + sw::MeshAssetFormat::kSkinVertexSize ) ),
                     bytes.size() );

    sw::MeshAssetData read{};
    SW_ASSERT_TRUE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_ASSERT_TRUE( read.hasSkin() );
    SW_EXPECT_EQUAL( 3u, read._skinBoneCount );
    SW_EXPECT_EQUAL( uint16( 1 ), read._listSkinVertex[1]._arrJoint[0] );
    SW_EXPECT_EQUAL( static_cast<uint16>( 2 ), read._listSkinVertex[4]._arrJoint[1] );
    SW_EXPECT_NEAR_EQUAL( 0.25f, read._listSkinVertex[4]._arrWeight[1], 0.0f );

    const sw::string path = test::makeTempPath( "skinned.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, written ) );
    sw::shared_ptr<sw::Mesh> mesh = sw::MeshCache::acquire( ::test::makeTempResourceID( path ) );
    SW_ASSERT_NOT_NULL( mesh.get() );
    SW_EXPECT_TRUE( mesh->hasSkin() );
    SW_EXPECT_EQUAL( 3u, mesh->getSkinBoneCount() );

    test::ScopedDefensiveTestLog expected( "a skin joint beyond the bone count is rejected" );
    written._listSkinVertex[2]._arrJoint[3] = 3;
    sw::MeshAssetFormat::makeBytes( written, bytes );
    SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_EXPECT_FALSE( read.hasSkin() );
}

/**
 * @brief [MeshAssetTest] 모프 타깃 덩어리(MRPH)가 이름 · 정점 번호 · 위치 · 노멀 차이를 그대로 오가고, 모프가 없는 메시는 덩어리 없이 이전과 같은 바이트다
 * @details 깨진 덩어리 — 모르는 이름 · 길이가 파일을 넘음 · 정점 범위 밖 번호 · 길이와 내용이 어긋남 — 는 모두 읽기 실패이고 결과는 비어 있다.
 */
SW_TEST_CASE( MeshAssetTest, MorphTargetChunkRoundTripsAndRejectsMalformedChunks )
{
    sw::MeshAssetData written{};
    written._listVertex = makeTestTriangles( 2, 0.5f );
    sw::vector<uint8> plainBytes;
    sw::MeshAssetFormat::makeBytes( written, plainBytes );
    SW_EXPECT_EQUAL( size_t( sw::MeshAssetFormat::kHeaderSize + written._listVertex.size() * sw::MeshAssetFormat::kVertexSize ), plainBytes.size() );

    auto makeDelta = []( uint32 vertexIndex, const sw::float3& position, const sw::float3& normal )
    {
        sw::MeshMorphDelta delta{};
        delta._vertexIndex = vertexIndex;
        delta._position    = position;
        delta._normal      = normal;
        return delta;
    };
    sw::MeshMorphTarget jaw{};
    jaw._name = sw::hashed_string( "jawOpen" );
    jaw._listDelta.push_back( makeDelta( 1u, sw::float3( 0.0f, -0.25f, 0.5f ), sw::float3( 0.0f, 0.1f, 0.0f ) ) );
    jaw._listDelta.push_back( makeDelta( 4u, sw::float3( 1.0f, 2.0f, 3.0f ), sw::float3() ) );
    sw::MeshMorphTarget blink{};
    blink._name = sw::hashed_string( "blinkLeft" );
    blink._listDelta.push_back( makeDelta( 5u, sw::float3( 0.0f, 0.0f, -1.0f ), sw::float3( 0.0f, 0.0f, 0.5f ) ) );
    written._listMorphTarget = { jaw, blink };

    sw::vector<uint8> bytes;
    sw::MeshAssetFormat::makeBytes( written, bytes );
    SW_ASSERT_TRUE( bytes.size() > plainBytes.size() );
    SW_EXPECT_TRUE( sw::Memory::compare( bytes.data(), plainBytes.data(), plainBytes.size() ) == 0 );
    sw::MeshAssetData read{};
    SW_ASSERT_TRUE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_ASSERT_EQUAL( size_t( 2 ), read._listMorphTarget.size() );
    SW_EXPECT_TRUE( read._listMorphTarget[0]._name == sw::hashed_string( "jawOpen" ) );
    SW_EXPECT_TRUE( read._listMorphTarget[1]._name == sw::hashed_string( "blinkLeft" ) );
    SW_ASSERT_EQUAL( size_t( 2 ), read._listMorphTarget[0]._listDelta.size() );
    SW_EXPECT_EQUAL( 4u, read._listMorphTarget[0]._listDelta[1]._vertexIndex );
    SW_EXPECT_NEAR_EQUAL( 3.0f, read._listMorphTarget[0]._listDelta[1]._position._z, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, read._listMorphTarget[0]._listDelta[0]._normal._y, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, read._listMorphTarget[1]._listDelta[0]._normal._z, 0.0f );

    // 캐시가 메시에 타깃을 싣는다(이름 → 번호).
    const sw::string path = test::makeTempPath( "morph.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, written ) );
    sw::shared_ptr<sw::Mesh> mesh = sw::MeshCache::acquire( ::test::makeTempResourceID( path ) );
    SW_ASSERT_NOT_NULL( mesh.get() );
    SW_EXPECT_EQUAL( 2u, mesh->getMorphTargetCount() );
    SW_EXPECT_EQUAL( 1, mesh->findMorphTargetIndex( sw::hashed_string( "blinkLeft" ) ) );

    test::ScopedDefensiveTestLog expected( "malformed morph target chunks are rejected" );
    const size_t                 chunkStart     = plainBytes.size();
    auto                         expectRejected = [&]( sw::vector<uint8> listBroken )
    {
        sw::MeshAssetData result{};
        SW_EXPECT_FALSE( sw::MeshAssetFormat::readFromBytes( listBroken.data(), listBroken.size(), result ) );
        SW_EXPECT_TRUE( result._listVertex.empty() && result._listMorphTarget.empty() );
    };
    sw::vector<uint8> unknownName = bytes;
    unknownName[chunkStart]       = 'X';
    expectRejected( unknownName );
    sw::vector<uint8> truncated( bytes.begin(), bytes.end() - 3 );
    expectRejected( truncated );
    // 첫 타깃의 첫 정점 번호(덩어리 머리 8 + 타깃 수 4 + 이름 길이 4 + "jawOpen" 7 + 정점 수 4)를 정점 수 밖으로.
    sw::vector<uint8> outOfRange        = bytes;
    const size_t      firstVertexOffset = chunkStart + 8 + 4 + 4 + 7 + 4;
    outOfRange[firstVertexOffset]       = 0xFF;
    outOfRange[firstVertexOffset + 1]   = 0xFF;
    expectRejected( outOfRange );
    sw::vector<uint8> trailing = bytes;
    trailing[chunkStart + 4]   = static_cast<uint8>( trailing[chunkStart + 4] + 4 );
    trailing.insert( trailing.end(), { 0, 0, 0, 0 } );
    expectRejected( trailing );
}

/**
 * @brief [MeshAssetTest] 구간(`SECT` 덩어리)이 쓴 그대로 다시 읽히고, 메시는 정점 밖 · 삼각형이 아닌 · 이름이 겹친 구간을 받지 않는다
 */
SW_TEST_CASE( MeshAssetTest, SectionsRoundTripAndAreValidated )
{
    sw::MeshAssetData data{};
    data._listVertex = makeTestTriangles( 4, 0.5f );
    data._listSection.push_back( sw::MeshSection{ sw::hashed_string( "Body" ), 0, 6, sw::MeshPersonVisibility::Both } );
    data._listSection.push_back( sw::MeshSection{ sw::hashed_string( "Hair" ), 6, 6, sw::MeshPersonVisibility::ThirdPersonOnly } );
    sw::vector<uint8> bytes;
    sw::MeshAssetFormat::makeBytes( data, bytes );
    sw::MeshAssetData read{};
    SW_ASSERT_TRUE( sw::MeshAssetFormat::readFromBytes( bytes.data(), bytes.size(), read ) );
    SW_ASSERT_EQUAL( size_t( 2 ), read._listSection.size() );
    SW_EXPECT_TRUE( read._listSection[1]._name == sw::hashed_string( "Hair" ) );
    SW_EXPECT_EQUAL( 6u, read._listSection[1]._firstVertex );
    SW_EXPECT_TRUE( read._listSection[1]._personVisibility == sw::MeshPersonVisibility::ThirdPersonOnly );

    sw::shared_ptr<sw::Mesh> mesh = sw::Mesh::create();
    mesh->setVertices( read._listVertex );
    SW_EXPECT_TRUE( mesh->setSections( read._listSection ) );
    SW_EXPECT_EQUAL( 1, mesh->findSectionIndex( sw::hashed_string( "Hair" ) ) );
    // 정점 밖 · 삼각형이 아님 · 이름 겹침은 받지 않는다(구간을 비운다).
    SW_EXPECT_FALSE( mesh->setSections( {
        sw::MeshSection{ sw::hashed_string( "Out" ), 9, 6, sw::MeshPersonVisibility::Both }
    } ) );
    SW_EXPECT_TRUE( mesh->getSections().empty() );
    SW_EXPECT_FALSE( mesh->setSections( {
        sw::MeshSection{ sw::hashed_string( "Odd" ), 0, 4, sw::MeshPersonVisibility::Both }
    } ) );
    SW_EXPECT_FALSE( mesh->setSections( {
        sw::MeshSection{sw::hashed_string( "A" ), 0, 3, sw::MeshPersonVisibility::Both},
        sw::MeshSection{sw::hashed_string( "A" ), 3, 3, sw::MeshPersonVisibility::Both}
    } ) );
}

/**
 * @brief [MeshAssetTest] 메시 컴포넌트는 구간 이름의 정점 범위만 그리고, 없는 이름은 그리지 않으며, 1인칭 시점에서 3인칭 전용 구간을 숨긴다
 */
SW_TEST_CASE( MeshAssetTest, MeshComponentDrawsNamedSectionAndHonorsPerson )
{
    sw::shared_ptr<sw::Mesh> mesh = sw::Mesh::create();
    mesh->setVertices( makeTestTriangles( 4, 0.5f ) );
    SW_ASSERT_TRUE( mesh->setSections( {
        sw::MeshSection{sw::hashed_string( "Body" ), 0, 6,            sw::MeshPersonVisibility::Both},
        sw::MeshSection{sw::hashed_string( "Hair" ), 6, 6, sw::MeshPersonVisibility::ThirdPersonOnly}
    } ) );
    sw::MeshComponent component;
    component.setMesh( mesh );
    uint32 first{ 99 };
    uint32 count{ 99 };
    SW_EXPECT_TRUE( component.findDrawSection( first, count ) );
    SW_EXPECT_EQUAL( 0u, count ); // 이름이 없으면 메시 전체
    component.setSectionName( sw::hashed_string( "Hair" ) );
    SW_EXPECT_TRUE( component.findDrawSection( first, count ) );
    SW_EXPECT_EQUAL( 6u, first );
    SW_EXPECT_EQUAL( 6u, count );
    SW_EXPECT_TRUE( component.isVisibleInCurrentPerson() );
    component.setFirstPersonView( true );
    SW_EXPECT_FALSE( component.isVisibleInCurrentPerson() );
    component.setSectionName( sw::hashed_string( "Missing" ) );
    SW_EXPECT_FALSE( component.findDrawSection( first, count ) );
}

/**
 * @brief [MeshAssetTest] LOD 단계는 `.mesh` 를 오가고(`LODS` 덩어리), 메시는 LOD 0 을 CPU 정점으로 두고 낮은 단계를 그릴 정점 흐름 뒤에 잇는다.
 *        임계값이 내려가지 않거나 삼각형이 아닌 단계, 구간이 있는 메시의 LOD 는 거절한다(계획 PreloadAndLod P5)
 */
SW_TEST_CASE( MeshAssetTest, LODLevelsRoundTripAndFollowLODZero )
{
    const sw::string path = test::makeTempPath( "lod.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, TestMeshLODInternal::makeLODData() ) );
    const sw::shared_ptr<sw::Mesh> mesh = sw::MeshCache::acquire( ::test::makeTempResourceID( path ) );
    SW_ASSERT_NOT_NULL( mesh.get() );
    SW_EXPECT_EQUAL( 3u, mesh->getLODCount() );
    SW_EXPECT_EQUAL( 12u, mesh->getVertexCount() );     // CPU 정점(물리 · 피킹)은 LOD 0 이다
    SW_EXPECT_EQUAL( 21u, mesh->getDrawVertexCount() ); // 그릴 흐름은 LOD 0 + 6 + 3
    SW_EXPECT_EQUAL( 12u, mesh->getLODs()[0]._firstVertex );
    SW_EXPECT_EQUAL( 6u, mesh->getLODs()[0]._vertexCount );
    SW_EXPECT_EQUAL( 18u, mesh->getLODs()[1]._firstVertex );
    SW_EXPECT_EQUAL( 0u, mesh->selectLOD( 0.9f ) );
    SW_EXPECT_EQUAL( 1u, mesh->selectLOD( 0.3f ) );
    SW_EXPECT_EQUAL( 2u, mesh->selectLOD( 0.05f ) );
    // 물리 셰이프가 읽는 정점만 읽기는 LOD 0 이다.
    sw::vector<sw::RHIVertex> listPhysicsVertex;
    SW_ASSERT_TRUE( sw::MeshAssetFormat::loadFromResource( ::test::makeTempResourceID( path ), listPhysicsVertex ) );
    SW_EXPECT_EQUAL( size_t( 12 ), listPhysicsVertex.size() );

    // 정점을 바꾸면 옛 LOD 0 에서 만든 단계는 맞지 않아 떨어진다.
    const sw::shared_ptr<sw::Mesh> rebuilt = sw::Mesh::create();
    rebuilt->setVertices( makeTestTriangles( 4, 0.1f ) );
    SW_EXPECT_TRUE( rebuilt->setLODs( TestMeshLODInternal::makeLODData()._listLOD ) );
    rebuilt->setVertices( makeTestTriangles( 3, 0.1f ) );
    SW_EXPECT_EQUAL( 1u, rebuilt->getLODCount() );

    test::ScopedDefensiveTestLog expected( "malformed LOD levels are rejected and the mesh keeps LOD 0" );
    sw::vector<sw::MeshLODLevel> listRising = TestMeshLODInternal::makeLODData()._listLOD;
    listRising[1]._screenSize               = 0.7f;
    SW_EXPECT_FALSE( rebuilt->setLODs( listRising ) );
    SW_EXPECT_EQUAL( 1u, rebuilt->getLODCount() );
    sw::vector<sw::MeshLODLevel> listBroken = TestMeshLODInternal::makeLODData()._listLOD;
    listBroken[0]._listVertex.pop_back();
    SW_EXPECT_FALSE( rebuilt->setLODs( listBroken ) );
    SW_ASSERT_TRUE( rebuilt->setSections( {
        sw::MeshSection{ sw::hashed_string( "Body" ), 0, 9, sw::MeshPersonVisibility::Both }
    } ) );
    SW_EXPECT_FALSE( rebuilt->setLODs( TestMeshLODInternal::makeLODData()._listLOD ) );
}

/**
 * @brief [MeshAssetTest] 스킨드 메시의 LOD 는 단계마다 스킨 가중치를 가지고(`LSKN` 덩어리) `.mesh` 를 오가며, 스킨 사본(`createSkinInstance`)도 LOD 를 이어받는다.
 *        단계의 스킨 수가 정점 수와 다르거나 본 번호가 본 수를 넘으면 거절한다(계획 AnimationLodAndCrowd C4)
 */
SW_TEST_CASE( MeshAssetTest, SkinnedLODLevelsRoundTripAndSkinInstanceKeepsThem )
{
    constexpr uint32  kBoneCount = 3;
    sw::MeshAssetData data       = TestMeshLODInternal::makeLODData();
    data._skinBoneCount          = kBoneCount;
    data._listSkinVertex.assign( data._listVertex.size(), sw::MeshSkinVertex{} );
    for ( sw::MeshLODLevel& level : data._listLOD )
    {
        level._listSkinVertex.assign( level._listVertex.size(), sw::MeshSkinVertex{} );
        level._listSkinVertex.back()._arrJoint[0] = 2; // 마지막 정점이 본 2 를 쓴다 — 읽고 쓸 때 단계마다 따로 보존되어야 한다
    }

    const sw::string path = test::makeTempPath( "skinnedlod.mesh" );
    SW_ASSERT_TRUE( sw::MeshAssetFormat::saveToFile( path, data ) );
    const sw::shared_ptr<sw::Mesh> mesh = sw::MeshCache::acquire( ::test::makeTempResourceID( path ) );
    SW_ASSERT_NOT_NULL( mesh.get() );
    SW_EXPECT_TRUE( mesh->hasSkin() );
    SW_EXPECT_EQUAL( 3u, mesh->getLODCount() );
    SW_EXPECT_EQUAL( size_t( 9 ), mesh->getLODSkinVertices().size() ); // 6 + 3
    SW_EXPECT_EQUAL( static_cast<uint16>( 2 ), mesh->getLODSkinVertices()[5]._arrJoint[0] );
    SW_EXPECT_EQUAL( static_cast<uint16>( 2 ), mesh->getLODSkinVertices()[8]._arrJoint[0] );
    SW_EXPECT_EQUAL( static_cast<uint16>( 0 ), mesh->getLODSkinVertices()[0]._arrJoint[0] );

    const sw::shared_ptr<sw::Mesh> instance = sw::Mesh::createSkinInstance( *mesh );
    SW_ASSERT_NOT_NULL( instance.get() );
    SW_EXPECT_EQUAL( 3u, instance->getLODCount() );
    SW_EXPECT_EQUAL( mesh->getDrawVertexCount(), instance->getDrawVertexCount() );
    SW_EXPECT_EQUAL( mesh->getLODSkinVertices().size(), instance->getLODSkinVertices().size() );

    // 스킨을 다시 정하면 옛 스킨으로 만든 단계는 맞지 않아 떨어진다.
    const sw::shared_ptr<sw::Mesh> rebuilt = sw::Mesh::create();
    rebuilt->setVertices( data._listVertex );
    rebuilt->setSkin( data._listSkinVertex, kBoneCount );
    SW_ASSERT_TRUE( rebuilt->setLODs( data._listLOD ) );
    SW_EXPECT_EQUAL( 3u, rebuilt->getLODCount() );
    rebuilt->setSkin( data._listSkinVertex, kBoneCount );
    SW_EXPECT_EQUAL( 1u, rebuilt->getLODCount() );

    test::ScopedDefensiveTestLog expected( "skinned LOD levels with a mismatched skin are rejected" );
    sw::vector<sw::MeshLODLevel> listShort = data._listLOD;
    listShort[0]._listSkinVertex.pop_back();
    SW_EXPECT_FALSE( rebuilt->setLODs( listShort ) );
    sw::vector<sw::MeshLODLevel> listBadJoint       = data._listLOD;
    listBadJoint[1]._listSkinVertex[0]._arrJoint[0] = static_cast<uint16>( kBoneCount );
    SW_EXPECT_FALSE( rebuilt->setLODs( listBadJoint ) );
    sw::vector<sw::MeshLODLevel> listNoSkin = data._listLOD;
    listNoSkin[0]._listSkinVertex.clear();
    SW_EXPECT_FALSE( rebuilt->setLODs( listNoSkin ) );
    SW_EXPECT_EQUAL( 1u, rebuilt->getLODCount() );
}

/**
 * @brief [MeshAssetTest] 메시 LOD 고르기는 뷰에서 본 화면 크기로 단계를 정하고(거친 쪽은 바로, 고운 쪽은 여유를 넘어야), 대기 뷰가 가까우면 그 시점의 단계를 쓴다.
 *        컴포넌트는 고른 단계의 정점 범위를 그리고, LOD 없는 메시로 바꾸면 LOD 목록에서 빠진다
 */
SW_TEST_CASE( MeshAssetTest, MeshLODSystemPicksLevelsByScreenSize )
{
    const sw::shared_ptr<sw::Mesh> mesh = sw::Mesh::create();
    mesh->setVertices( makeTestTriangles( 4, 0.1f ) );
    SW_ASSERT_TRUE( mesh->setLODs( TestMeshLODInternal::makeLODData()._listLOD ) );

    sw::GameObjectManager  manager;
    sw::GameObject*        pProp    = manager.createGameObject( sw::hashed_string( "LODProp" ) );
    sw::MeshComponent*     pMesh    = pProp->addComponent<sw::MeshComponent>();
    sw::MeshLODSystem&     meshLOD  = manager.getSubsystem<sw::MeshLODSystem>();
    sw::PrimitiveRegistry& registry = manager.getPrimitiveRegistry();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setMesh( mesh );
    SW_EXPECT_EQUAL( size_t( 1 ), registry.getLODPrimitives().size() );

    // 뷰를 받기 전(렌더 없는 실행)에는 원본이다.
    meshLOD.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, pMesh->getLODIndex() );

    sw::float3 center{};
    float32    radius = 0.0f;
    SW_ASSERT_TRUE( pMesh->getWorldBounds( center, radius ) );
    const auto lodAtScreenSize = [&]( float32 screenSize )
    {
        const sw::float3 eye{ center._x, center._y, center._z - radius / screenSize };
        meshLOD.setViews( { TestMeshLODInternal::makeViewToward( eye, center ) } );
        meshLOD.update( 0.0f );
        return pMesh->getLODIndex();
    };
    SW_EXPECT_EQUAL( 0u, lodAtScreenSize( 0.8f ) );
    SW_EXPECT_EQUAL( 1u, lodAtScreenSize( 0.3f ) );
    uint32 first{ 0 };
    uint32 count{ 0 };
    SW_EXPECT_TRUE( pMesh->findDrawSection( first, count ) );
    SW_EXPECT_EQUAL( 12u, first );
    SW_EXPECT_EQUAL( 6u, count );
    SW_EXPECT_EQUAL( 2u, lodAtScreenSize( 0.05f ) );
    SW_EXPECT_EQUAL( 1u, lodAtScreenSize( 0.2f ) );  // 거친 쪽에서 고운 쪽으로 — 임계값 0.1 의 여유를 넘었다
    SW_EXPECT_EQUAL( 1u, lodAtScreenSize( 0.52f ) ); // 임계값 0.5 를 넘었지만 여유(10 %) 안이다 — 그대로
    SW_EXPECT_EQUAL( 0u, lodAtScreenSize( 0.6f ) );
    SW_EXPECT_TRUE( pMesh->findDrawSection( first, count ) );
    SW_EXPECT_EQUAL( 0u, count ); // 원본은 메시 전체(LOD 0)

    // 뷰가 멀어도 대기 뷰(컷 프리롤)가 가까우면 그 시점의 단계다.
    SW_EXPECT_EQUAL( 2u, lodAtScreenSize( 0.05f ) );
    meshLOD.setPendingView( TestMeshLODInternal::makeViewToward( sw::float3{ center._x, center._y, center._z - radius / 0.8f }, center ) );
    meshLOD.update( 0.0f );
    SW_EXPECT_EQUAL( 0u, pMesh->getLODIndex() );
    meshLOD.clearPendingView();

    // LOD 없는 메시로 바꾸면 목록에서 빠지고 원본으로 돌아간다.
    pMesh->setMeshID( "Cube" );
    SW_EXPECT_EQUAL( size_t( 0 ), registry.getLODPrimitives().size() );
    SW_EXPECT_EQUAL( 0u, pMesh->getLODIndex() );
}
