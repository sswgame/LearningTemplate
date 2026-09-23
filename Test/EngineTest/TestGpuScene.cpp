#include "pch.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/FrameArenaAllocator.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Renderer/Debug/RenderTargetRegistry.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Renderer/Scene/GpuInstanceRing.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
#include "Engine/Graphics/Renderer/Scene/GpuSceneBuilder.h"
#include "Engine/Graphics/Upload/GpuUploadQueue.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/IWindow.h"

#include "TestFramework/TestFramework.h"

// GpuSceneTest — 씬에서 GPU 스냅샷(인스턴스 · 배치 · 머티리얼 원소 · 퍼뮤테이션)을 만드는 규칙. 디바이스 없음(nogpu).
/**
 * @brief GpuScene: opaque 머지, transparent 연속 머지 + back-to-front, 빌드 캐시
 */

SW_TEST_CASE( GpuSceneTest, BuildBatchesAndSortTransparent )
{
    sw::Scene scene( "GpuSceneBatchTest" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    auto addMeshAt = [&]( const utf8* pName, float32 x, float32 z, sw::RHIBlendMode blend )
    {
        sw::GameObject* go = objects->createGameObject( sw::hashed_string( pName ) );
        SW_ASSERT_NOT_NULL( go );
        sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( mesh );
        mesh->setMesh( cube );
        mesh->setLocalPosition( sw::float3( x, 0.0f, z ) );
        mesh->setBlendMode( blend );
        mesh->setVisible( true );
    };

    addMeshAt( "OpaqueA", 0.0f, -1.0f, sw::RHIBlendMode::Opaque );
    addMeshAt( "OpaqueB", 1.0f, -1.0f, sw::RHIBlendMode::Opaque );
    addMeshAt( "TransparentFar", 0.0f, -10.0f, sw::RHIBlendMode::Transparent );
    addMeshAt( "TransparentNear", 0.0f, -2.0f, sw::RHIBlendMode::Transparent );

    sw::GpuSceneBuilder gpuScene;
    const sw::float3    camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos );

    SW_EXPECT_TRUE( gpuScene.getOpaqueBatches().empty() == false );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    // 동일 mesh/mat transparent 2개는 정렬 후 연속 머지 → 배치 1, instanceCount 2
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
    SW_EXPECT_EQUAL( 2u, gpuScene.getTransparentBatches()[0]._instanceCount );

    const sw::vector<sw::GpuInstance>& instances = gpuScene.getInstances();
    const sw::GpuMeshBatch&            trBatch   = gpuScene.getTransparentBatches()[0];
    const float32                      z0        = instances[trBatch._instanceBase]._boundsCenter._z;
    const float32                      z1        = instances[trBatch._instanceBase + 1]._boundsCenter._z;
    const float32                      d0        = z0 * z0;
    const float32                      d1        = z1 * z1;
    SW_EXPECT_TRUE( d0 >= d1 ); // far then near within merged batch

    // 동일 내용·카메라 → CPU dirty 없이 early-out
    SW_EXPECT_TRUE( gpuScene.isCpuSnapshotDirty() );
    gpuScene.buildFromScene( &scene, camPos );
    // early-out 시 dirty 플래그는 이전 값 유지(업로드 전이면 여전히 dirty)
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );

    // 카메라만 이동 → transparent 재정렬 경로
    const sw::float3 camMoved{ 0.0f, 0.0f, 5.0f };
    gpuScene.buildFromScene( &scene, camMoved );
    SW_EXPECT_TRUE( gpuScene.isCpuSnapshotDirty() );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
}

/**
 * @brief 메시 인스턴스 배치 — 씬 컴포넌트 없이 N 개가 실리고, 같은 메시·머티리얼의 컴포넌트와 한 배치로 합쳐지며, 항목 하나만
 *        고치면 등록부가 더티를 알고, 배치를 놓으면 사라진다.
 * @details 언리얼 ISM 의 자리. 배치의 항목은 등록부의 프리미티브 번호 공간에서 메시 컴포넌트 **뒤에** 이어지므로, 부분 수집·
 *          배치 병합·더티 구간이 컴포넌트와 같은 경로를 탄다. 항목 하나를 고친 뒤의 빌드가 그 인스턴스의 위치를 바꾸고
 *          나머지는 그대로여야 한다 — 번호가 어긋나면 엉뚱한 인스턴스가 움직인다.
 */
SW_TEST_CASE( GpuSceneTest, MeshInstanceBatchRendersWithoutComponents )
{
    sw::Scene scene( "GpuSceneInstanceBatch" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );
    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    // 비교 대상: 같은 메시의 컴포넌트 하나.
    sw::GameObject*    pObj  = objects->createGameObject( sw::hashed_string( "ComponentCube" ) );
    sw::MeshComponent* pMesh = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->setMesh( cube );
    pMesh->setLocalPosition( sw::float3( 5.0f, 0.0f, 0.0f ) );
    pMesh->setVisible( true );

    // 배치: 항목 셋, 씬 컴포넌트 없음.
    sw::shared_ptr<sw::MeshInstanceBatch> pBatch = sw::make_shared<sw::MeshInstanceBatch>( cube, nullptr, nullptr, 3u );
    for ( uint32 index = 0; index < 3; ++index )
    {
        pBatch->setWorld( index, sw::float4x4::createTrs( sw::float3( static_cast<float32>( index ), 0.0f, -2.0f ), sw::float3( 0.0f, 0.0f, 0.0f ),
                                                          sw::float3( 1.0f, 1.0f, 1.0f ) ) );
    }
    objects->getPrimitiveRegistry().addInstanceBatch( pBatch.get() );
    SW_EXPECT_TRUE( pBatch->isRegistered() );
    SW_EXPECT_EQUAL( 4u, objects->getPrimitiveRegistry().getSlotCount() );

    sw::GpuSceneBuilder gpuScene;
    const sw::float3    camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    // 같은 메시·머티리얼·블렌드라 컴포넌트와 항목 셋이 한 불투명 배치다.
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( gpuScene.getOpaqueBatches().size() ) );
    SW_EXPECT_EQUAL( 4u, gpuScene.getOpaqueBatches()[0]._instanceCount );

    // 항목 하나만 옮긴다 — 등록부가 더티를 알고, 빌드 뒤 그 인스턴스만 새 자리다.
    SW_EXPECT_FALSE( objects->getPrimitiveRegistry().hasDirty() );
    pBatch->setWorld( 1, sw::float4x4::createTrs( sw::float3( 1.0f, 7.0f, -2.0f ), sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 1.0f, 1.0f, 1.0f ) ) );
    SW_EXPECT_TRUE( objects->getPrimitiveRegistry().hasDirty() );
    gpuScene.buildFromScene( &scene, camPos );
    uint32 movedCount = 0;
    uint32 stillCount = 0;
    for ( const sw::GpuInstance& instance : gpuScene.getInstances() )
    {
        if ( sw::MathUtil::nearEqual( instance._boundsCenter._y, 7.0f ) )
            ++movedCount;
        else if ( sw::MathUtil::nearEqual( instance._boundsCenter._y, 0.0f ) )
            ++stillCount;
    }
    SW_EXPECT_EQUAL( 1u, movedCount );
    SW_EXPECT_EQUAL( 3u, stillCount );

    // 숨기면 항목 전부가 빠지고, 다시 보이면 돌아온다.
    pBatch->setVisible( false );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    pBatch->setVisible( true );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 배치를 놓으면 소멸자가 등록부에서 빠진다 — 컴포넌트 하나만 남는다.
    pBatch.reset();
    SW_EXPECT_EQUAL( 1u, objects->getPrimitiveRegistry().getSlotCount() );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getInstances().size() ) );
}

/**
 * @brief 투명 정렬 순서만 바뀐 프레임: 불투명 인스턴스는 제자리, 투명 꼬리만 새 순서로 다시 앉고, 그 꼬리가 더티로 실린다.
 * @details 예전에는 투명 순서가 바뀌면 배치 전체(불투명까지)를 다시 지었다. 지금은 꼬리만 다시 방출하므로
 *          (1) 불투명 슬롯의 내용이 그대로여야 하고 (2) 투명은 여전히 먼 것부터여야 하며 (3) 받는 쪽이 꼬리를
 *          다시 올리도록 더티 구간이 꼬리를 덮어야 한다. 셋 중 하나라도 빠지면 화면이 조용히 어긋난다.
 */
SW_TEST_CASE( GpuSceneTest, TransparentOrderChangeKeepsOpaqueSlots )
{
    sw::Scene scene( "GpuSceneTransparentTail" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    auto addMeshAt = [&]( const utf8* pName, float32 x, float32 z, sw::RHIBlendMode blend ) -> sw::MeshComponent*
    {
        sw::GameObject* go = objects->createGameObject( sw::hashed_string( pName ) );
        if ( go == nullptr )
            return nullptr;
        sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
        if ( mesh == nullptr )
            return nullptr;
        mesh->setMesh( cube );
        mesh->setLocalPosition( sw::float3( x, 0.0f, z ) );
        mesh->setBlendMode( blend );
        mesh->setVisible( true );
        return mesh;
    };

    sw::MeshComponent* pOpaqueA = addMeshAt( "TailOpaqueA", 0.0f, -1.0f, sw::RHIBlendMode::Opaque );
    sw::MeshComponent* pOpaqueB = addMeshAt( "TailOpaqueB", 1.0f, -1.0f, sw::RHIBlendMode::Opaque );
    sw::MeshComponent* pFar     = addMeshAt( "TailFar", 0.0f, -10.0f, sw::RHIBlendMode::Transparent );
    sw::MeshComponent* pNear    = addMeshAt( "TailNear", 0.0f, -2.0f, sw::RHIBlendMode::Transparent );
    SW_ASSERT_NOT_NULL( pOpaqueA );
    SW_ASSERT_NOT_NULL( pOpaqueB );
    SW_ASSERT_NOT_NULL( pFar );
    SW_ASSERT_NOT_NULL( pNear );

    sw::GpuSceneBuilder  gpuScene;
    sw::GpuSceneSnapshot snapshot;
    const sw::float3     camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos );
    gpuScene.exportCpuSnapshot( snapshot );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
    const uint32 tailBase = gpuScene.getTransparentBatches()[0]._instanceBase;
    SW_EXPECT_EQUAL( 2u, tailBase );
    // 먼 것(-10)이 먼저 앉는다.
    SW_EXPECT_TRUE( gpuScene.getInstances()[tailBase]._boundsCenter._z < gpuScene.getInstances()[tailBase + 1]._boundsCenter._z );

    // 먼 것을 가까운 것보다 앞으로 당긴다 — 키(메시·머티리얼·블렌드)는 그대로, 순서만 뒤집힌다.
    pFar->setLocalPosition( sw::float3( 0.0f, 0.0f, -1.5f ) );
    gpuScene.buildFromScene( &scene, camPos );
    gpuScene.exportCpuSnapshot( snapshot );

    const sw::vector<sw::GpuInstance>& instances = gpuScene.getInstances();
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( instances.size() ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
    SW_EXPECT_EQUAL( tailBase, gpuScene.getTransparentBatches()[0]._instanceBase );
    // (1) 불투명 슬롯은 제자리다 — x 가 0, 1 그대로.
    SW_EXPECT_TRUE( sw::MathUtil::abs( instances[0]._boundsCenter._x - 0.0f ) < 1e-4f );
    SW_EXPECT_TRUE( sw::MathUtil::abs( instances[1]._boundsCenter._x - 1.0f ) < 1e-4f );
    // (2) 투명은 여전히 먼 것부터 — 이제 Near(-2) 가 더 멀다.
    SW_EXPECT_TRUE( sw::MathUtil::abs( instances[tailBase]._boundsCenter._z - ( -2.0f ) ) < 1e-4f );
    SW_EXPECT_TRUE( sw::MathUtil::abs( instances[tailBase + 1]._boundsCenter._z - ( -1.5f ) ) < 1e-4f );
    // (3) 꼬리가 더티로 실린다 — 전부 더티거나, 구간 하나가 [tailBase, 4) 를 덮는다.
    bool bTailCovered = snapshot._bAllInstancesDirty != SW_FALSE;
    for ( const sw::GpuInstanceRun& run : snapshot._listDirtyInstanceRun )
    {
        if ( run._start <= tailBase && tailBase + 2 <= run._start + run._count )
            bTailCovered = true;
    }
    SW_EXPECT_TRUE( bTailCovered );
}

/**
 * @brief 인스턴스 링: 발행본은 불변이고, 되쓰이는 슬롯은 그 뒤에 일어난 변경을 전부 따라잡는다.
 * @details 게임 스레드는 아무도 안 읽는 슬롯에 짓고 포인터만 발행한다(되복사 없음). 그러면 두 가지가 보장돼야 한다 —
 *          (1) 패킷이 든 발행본은 다음 프레임이 짓는 동안 **바뀌지 않는다**(렌더 스레드가 읽는 중이다).
 *          (2) 몇 프레임 전에 발행된 슬롯을 다시 쓸 때 부분 갱신은 그 사이 프레임들의 변경을 **먼저 따라잡는다**
 *              — 안 그러면 이번 프레임에 안 움직인 물체가 몇 프레임 전 자리로 되돌아간다.
 */
SW_TEST_CASE( GpuSceneTest, InstanceRingKeepsPublishedImmutableAndCatchesUp )
{
    sw::Scene scene( "GpuSceneInstanceRing" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    // 8 개 — 하나만 움직이면 더티 < 1/4 이라 부분 수집·부분 갱신 경로를 탄다.
    constexpr uint32               kMeshCount = 8;
    sw::vector<sw::MeshComponent*> listMesh;
    for ( uint32 index = 0; index < kMeshCount; ++index )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer64> name;
        name.append( "Ring" ).append( index );
        sw::GameObject* go = objects->createGameObject( sw::hashed_string( name.c_str() ) );
        SW_ASSERT_NOT_NULL( go );
        sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( mesh );
        mesh->setMesh( cube );
        mesh->setLocalPosition( sw::float3( static_cast<float32>( index ), 0.0f, -1.0f ) );
        mesh->setVisible( true );
        listMesh.push_back( mesh );
    }

    auto countAtX = [&]( const sw::vector<sw::GpuInstance>& listInstance, float32 x ) -> uint32
    {
        uint32 count = 0;
        for ( const sw::GpuInstance& inst : listInstance )
        {
            if ( sw::MathUtil::abs( inst._boundsCenter._x - x ) < 1e-4f )
                ++count;
        }
        return count;
    };

    sw::GpuSceneBuilder gpuScene;
    const sw::float3    camPos{ 0.0f, 0.0f, 0.0f };

    // f1: 전체 빌드 → 발행본 1 (잠깐 들었다가 놓는다 — 이 슬롯이 f4 에서 되쓰인다).
    gpuScene.buildFromScene( &scene, camPos );
    sw::GpuSceneSnapshot snapshot1;
    gpuScene.exportCpuSnapshot( snapshot1 );
    SW_ASSERT_EQUAL( kMeshCount, static_cast<uint32>( snapshot1.getInstances().size() ) );

    // f2: 0 번만 이동 → 발행본 2 (렌더 스레드처럼 계속 든다).
    listMesh[0]->setLocalPosition( sw::float3( 100.0f, 0.0f, -1.0f ) );
    gpuScene.buildFromScene( &scene, camPos );
    sw::GpuSceneSnapshot snapshot2;
    gpuScene.exportCpuSnapshot( snapshot2 );
    SW_EXPECT_EQUAL( 1u, countAtX( snapshot2.getInstances(), 100.0f ) );

    // f3: 1 번만 이동 → 발행본 3 (역시 든다).
    listMesh[1]->setLocalPosition( sw::float3( 200.0f, 0.0f, -1.0f ) );
    gpuScene.buildFromScene( &scene, camPos );
    sw::GpuSceneSnapshot snapshot3;
    gpuScene.exportCpuSnapshot( snapshot3 );
    SW_EXPECT_EQUAL( 1u, countAtX( snapshot3.getInstances(), 200.0f ) );

    // 발행본 1 을 놓는다 — 이제 그 슬롯만 "아무도 안 읽는" 슬롯이라 f4 가 그것을 되쓴다.
    snapshot1 = sw::GpuSceneSnapshot{};

    // f4: 2 번만 이동. 되쓰인 슬롯에는 f2·f3 의 변경이 없으므로 따라잡아야 한다.
    listMesh[2]->setLocalPosition( sw::float3( 300.0f, 0.0f, -1.0f ) );
    gpuScene.buildFromScene( &scene, camPos );
    sw::GpuSceneSnapshot snapshot4;
    gpuScene.exportCpuSnapshot( snapshot4 );
    const sw::vector<sw::GpuInstance>& instances4 = snapshot4.getInstances();
    SW_ASSERT_EQUAL( kMeshCount, static_cast<uint32>( instances4.size() ) );
    SW_EXPECT_EQUAL( 1u, countAtX( instances4, 100.0f ) ); // f2 의 변경이 따라잡혔다
    SW_EXPECT_EQUAL( 1u, countAtX( instances4, 200.0f ) ); // f3 의 변경이 따라잡혔다
    SW_EXPECT_EQUAL( 1u, countAtX( instances4, 300.0f ) ); // 이번 변경
    SW_EXPECT_EQUAL( 0u, countAtX( instances4, 0.0f ) );   // 0 번의 옛 자리로 되돌아가지 않았다
    SW_EXPECT_EQUAL( 0u, countAtX( instances4, 1.0f ) );

    // (1) 들고 있던 발행본 2·3 은 그대로다 — f4 가 그것들을 쓰지 않았다.
    SW_EXPECT_EQUAL( 1u, countAtX( snapshot2.getInstances(), 100.0f ) );
    SW_EXPECT_EQUAL( 1u, countAtX( snapshot2.getInstances(), 1.0f ) );
    SW_EXPECT_EQUAL( 0u, countAtX( snapshot2.getInstances(), 300.0f ) );
    SW_EXPECT_EQUAL( 1u, countAtX( snapshot3.getInstances(), 200.0f ) );
    SW_EXPECT_EQUAL( 0u, countAtX( snapshot3.getInstances(), 300.0f ) );
    // 발행본은 서로 다른 배열이다.
    SW_EXPECT_TRUE( snapshot2.getInstances().data() != snapshot3.getInstances().data() );
    SW_EXPECT_TRUE( snapshot3.getInstances().data() != instances4.data() );
}

/**
 * @brief `GpuInstanceRing` 만 따로 — 빌더 없이도 발행·슬롯 재사용·따라잡기 규칙이 성립한다.
 * @details 빌더에서 떼어 낸 이유가 "이 규칙은 씬 수집과 무관하다" 였으므로, 그 규칙을 빌더 없이 검사할 수 있어야 한다.
 *          (1) 발행본은 다른 슬롯이 쓰이는 동안 바뀌지 않는다 (2) 아무도 안 드는 슬롯만 되쓰인다 (3) 되쓰인 슬롯은
 *          그 사이 발행들의 변경 구간을 따라잡는다 (4) 이력이 끊기면 통째로 따라잡는다(값이 틀리지 않는다).
 */
SW_TEST_CASE( GpuSceneTest, InstanceRingStandalone )
{
    sw::GpuInstanceRing ring;
    auto                fill = [&]( sw::vector<sw::GpuInstance>& list, uint32 count, float32 seed )
    {
        list.resize( count );
        for ( uint32 index = 0; index < count; ++index )
            list[index]._boundsRadius = seed + static_cast<float32>( index );
    };

    // f1: 전부 새로 쓰고 발행 → p1 을 든다.
    fill( ring.acquireWrite(), 4, 100.0f );
    sw::vector<sw::GpuInstanceRun>                    listNoRun;
    sw::shared_ptr<const sw::vector<sw::GpuInstance>> p1 = ring.publish( true, listNoRun );
    SW_ASSERT_NOT_NULL( p1.get() );
    SW_EXPECT_EQUAL( size_t( 4 ), p1->size() );

    // f2: p1 이 들려 있으므로 새 슬롯이 나온다 — 따라잡기(통째, 이력 첫 발행) 뒤 1 번만 고쳐 발행.
    ring.syncWriteFromPublished();
    sw::vector<sw::GpuInstance>& work2 = ring.acquireWrite();
    SW_EXPECT_TRUE( work2.data() != p1->data() );
    SW_EXPECT_TRUE( sw::MathUtil::abs( work2[2]._boundsRadius - 102.0f ) < 1e-6f ); // 따라잡았다
    work2[1]._boundsRadius = 201.0f;
    sw::vector<sw::GpuInstanceRun> listRun2{
        sw::GpuInstanceRun{ 1, 1 }
    };
    sw::shared_ptr<const sw::vector<sw::GpuInstance>> p2 = ring.publish( false, listRun2 );
    SW_EXPECT_TRUE( sw::MathUtil::abs( ( *p1 )[1]._boundsRadius - 101.0f ) < 1e-6f ); // (1) p1 은 그대로

    // f3: 3 번만 고쳐 발행 (p2 도 든다).
    ring.syncWriteFromPublished();
    ring.acquireWrite()[3]._boundsRadius = 303.0f;
    sw::vector<sw::GpuInstanceRun> listRun3{
        sw::GpuInstanceRun{ 3, 1 }
    };
    sw::shared_ptr<const sw::vector<sw::GpuInstance>> p3 = ring.publish( false, listRun3 );

    // p1 을 놓는다 → f4 는 p1 의 슬롯을 되쓴다 (2). 그 슬롯은 f2·f3 의 변경(1 번·3 번)을 모른다 → 따라잡아야 한다 (3).
    const sw::GpuInstance* pSlot1 = p1->data();
    p1.reset();
    ring.syncWriteFromPublished();
    sw::vector<sw::GpuInstance>& work4 = ring.acquireWrite();
    SW_EXPECT_TRUE( work4.data() == pSlot1 );
    SW_EXPECT_TRUE( sw::MathUtil::abs( work4[1]._boundsRadius - 201.0f ) < 1e-6f );
    SW_EXPECT_TRUE( sw::MathUtil::abs( work4[3]._boundsRadius - 303.0f ) < 1e-6f );
    SW_EXPECT_TRUE( sw::MathUtil::abs( work4[0]._boundsRadius - 100.0f ) < 1e-6f );
    sw::shared_ptr<const sw::vector<sw::GpuInstance>> p4 = ring.publish( false, listNoRun );

    // (4) 이력보다 오래 잠든 슬롯 — p2 를 든 채 이력 크기만큼 발행을 거듭한 뒤 놓으면, 되쓸 때 통째로 따라잡는다.
    for ( size_t step = 0; step < sw::GpuInstanceRing::kPublishHistoryCount + 2; ++step )
    {
        ring.syncWriteFromPublished();
        ring.acquireWrite()[0]._boundsRadius = 1000.0f + static_cast<float32>( step );
        sw::vector<sw::GpuInstanceRun> listRun{
            sw::GpuInstanceRun{ 0, 1 }
        };
        p4 = ring.publish( false, listRun );
    }
    const sw::GpuInstance* pSlot2 = p2->data();
    p2.reset();
    p3.reset();
    ring.syncWriteFromPublished();
    sw::vector<sw::GpuInstance>& workLate = ring.acquireWrite();
    SW_EXPECT_TRUE( workLate.data() == pSlot2 || workLate.data() != nullptr );
    SW_EXPECT_TRUE( sw::MathUtil::abs( workLate[0]._boundsRadius - ( *p4 )[0]._boundsRadius ) < 1e-6f );
    SW_EXPECT_TRUE( sw::MathUtil::abs( workLate[3]._boundsRadius - 303.0f ) < 1e-6f );
}

/**
 * @brief 프리미티브 등록부: 변경을 알린 것만 다시 만들고, 알린 게 없으면 수집조차 하지 않는다.
 * @details 이 구조의 최악 실패 모드는 "움직였는데 화면이 안 따라오는 것"이다. 등록·해제·더티
 *          신호 중 하나라도 빠지면 여기서 걸린다.
 */
SW_TEST_CASE( GpuSceneTest, PrimitiveRegistryTracksChanges )
{
    sw::Scene scene( "GpuScenePrimitiveRegistry" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    auto addMeshAt = [&]( const utf8* pName, float32 x ) -> sw::MeshComponent*
    {
        sw::GameObject* go = objects->createGameObject( sw::hashed_string( pName ) );
        if ( go == nullptr )
            return nullptr;
        sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
        if ( mesh == nullptr )
            return nullptr;
        mesh->setMesh( cube );
        mesh->setLocalPosition( sw::float3( x, 0.0f, -1.0f ) );
        mesh->setVisible( true );
        return mesh;
    };

    sw::MeshComponent* pMeshA = addMeshAt( "RegA", 0.0f );
    sw::MeshComponent* pMeshB = addMeshAt( "RegB", 2.0f );
    SW_ASSERT_NOT_NULL( pMeshA );
    SW_ASSERT_NOT_NULL( pMeshB );

    // 붙는 것만으로 등록부에 들어간다 — 렌더러가 씬을 뒤져 찾지 않는다.
    SW_EXPECT_EQUAL( size_t( 2 ), objects->getPrimitiveRegistry().getAll().size() );

    sw::GpuSceneBuilder gpuScene;
    const sw::float3    camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 1) 움직이면 반영된다.
    pMeshA->setLocalPosition( sw::float3( 7.0f, 0.0f, -1.0f ) );
    SW_EXPECT_TRUE( objects->getPrimitiveRegistry().hasDirty() || objects->getPrimitiveRegistry().getSetGeneration() != 0 );
    gpuScene.buildFromScene( &scene, camPos );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    bool bFoundMoved = false;
    for ( const sw::GpuInstance& inst : gpuScene.getInstances() )
    {
        if ( sw::MathUtil::nearEqual( inst._boundsCenter._x, 7.0f ) )
            bFoundMoved = true;
    }
    SW_EXPECT_TRUE( bFoundMoved );

    // 2) 아무도 안 바뀌면 더티가 없다 — 이게 수집을 건너뛰는 근거다.
    SW_EXPECT_TRUE( objects->getPrimitiveRegistry().hasDirty() == false );

    // 3) 가시성을 끄면 빠진다.
    pMeshB->setVisible( false );
    SW_EXPECT_TRUE( objects->getPrimitiveRegistry().hasDirty() );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 4) 컴포넌트를 떼면 등록부에서도 빠진다.
    sw::GameObject* pOwnerB = pMeshB->getOwner();
    SW_ASSERT_NOT_NULL( pOwnerB );
    SW_EXPECT_TRUE( pOwnerB->removeComponent( pMeshB ) );
    SW_EXPECT_EQUAL( size_t( 1 ), objects->getPrimitiveRegistry().getAll().size() );

    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 5) 오브젝트를 비활성화하면 집합이 바뀐다.
    sw::GameObject* pOwnerA = pMeshA->getOwner();
    SW_ASSERT_NOT_NULL( pOwnerA );
    pOwnerA->setActive( false );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( gpuScene.getInstances().size() ) );
}

/**
 * @brief 트랜스폼만 바뀌면 배치를 다시 나누지 않지만, 결과는 다시 나눈 것과 같아야 한다.
 * @details 정렬을 건너뛰는 경로라 조용히 틀리기 쉽다. 키가 바뀐 경우와 나란히 확인한다.
 */
SW_TEST_CASE( GpuSceneTest, TransformOnlyChangeKeepsBatches )
{
    sw::Scene scene( "GpuSceneTransformOnly" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    auto addMeshAt = [&]( const utf8* pName, float32 x, float32 z, sw::RHIBlendMode blend ) -> sw::MeshComponent*
    {
        sw::GameObject* go = objects->createGameObject( sw::hashed_string( pName ) );
        if ( go == nullptr )
            return nullptr;
        sw::MeshComponent* mesh = go->addComponent<sw::MeshComponent>();
        if ( mesh == nullptr )
            return nullptr;
        mesh->setMesh( cube );
        mesh->setLocalPosition( sw::float3( x, 0.0f, z ) );
        mesh->setBlendMode( blend );
        mesh->setVisible( true );
        return mesh;
    };

    sw::MeshComponent* pOpaqueA = addMeshAt( "TfOpaqueA", 0.0f, -1.0f, sw::RHIBlendMode::Opaque );
    addMeshAt( "TfOpaqueB", 1.0f, -1.0f, sw::RHIBlendMode::Opaque );
    addMeshAt( "TfTransFar", 0.0f, -10.0f, sw::RHIBlendMode::Transparent );
    sw::MeshComponent* pTransNear = addMeshAt( "TfTransNear", 0.0f, -2.0f, sw::RHIBlendMode::Transparent );
    SW_ASSERT_NOT_NULL( pOpaqueA );
    SW_ASSERT_NOT_NULL( pTransNear );

    sw::GpuSceneBuilder gpuScene;
    const sw::float3    camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos );

    const uint32 opaqueBatchCount      = static_cast<uint32>( gpuScene.getOpaqueBatches().size() );
    const uint32 transparentBatchCount = static_cast<uint32>( gpuScene.getTransparentBatches().size() );
    SW_ASSERT_EQUAL( 1u, transparentBatchCount );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 1) 트랜스폼만 변경 — 배치 구성은 그대로여야 하고, 위치는 반영돼야 한다.
    pOpaqueA->setLocalPosition( sw::float3( 5.0f, 0.0f, -1.0f ) );
    gpuScene.buildFromScene( &scene, camPos );

    SW_EXPECT_EQUAL( opaqueBatchCount, static_cast<uint32>( gpuScene.getOpaqueBatches().size() ) );
    SW_EXPECT_EQUAL( transparentBatchCount, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    bool bMovedApplied = false;
    for ( const sw::GpuInstance& inst : gpuScene.getInstances() )
    {
        if ( sw::MathUtil::nearEqual( inst._boundsCenter._x, 5.0f ) )
            bMovedApplied = true;
    }
    SW_EXPECT_TRUE( bMovedApplied );

    // transparent 는 트랜스폼이 바뀌면 다시 정렬돼야 한다 (먼 것 → 가까운 것).
    {
        const sw::vector<sw::GpuInstance>& instances = gpuScene.getInstances();
        const sw::GpuMeshBatch&            trBatch   = gpuScene.getTransparentBatches()[0];
        SW_ASSERT_EQUAL( 2u, trBatch._instanceCount );
        const float32 z0 = instances[trBatch._instanceBase]._boundsCenter._z;
        const float32 z1 = instances[trBatch._instanceBase + 1]._boundsCenter._z;
        SW_EXPECT_TRUE( ( z0 * z0 ) >= ( z1 * z1 ) );
    }

    // 2) 배치 키가 바뀌면(블렌드 모드) 다시 나눠야 한다 — 빠른 경로로 새면 안 된다.
    pTransNear->setBlendMode( sw::RHIBlendMode::Opaque );
    gpuScene.buildFromScene( &scene, camPos );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
    SW_EXPECT_EQUAL( 1u, gpuScene.getTransparentBatches()[0]._instanceCount );
}

/**
 * @brief 서로 다른 MaterialInstance 키는 transparent 머지되지 않는다
 */
SW_TEST_CASE( GpuSceneTest, TransparentDifferentKeysStaySeparate )
{
    sw::Scene scene( "GpuSceneTransparentKeys" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    // 블렌드 모드는 **부모 머티리얼**이 정한다(GpuScene::buildFromScene). 예전에는 그 폴백이 순서 버그로 한 번도
    // 걸리지 않아 컴포넌트의 Transparent 가 우연히 이겼다 — 이제는 투명 머티리얼을 부모로 줘야 투명 배치가 된다.
    sw::shared_ptr<sw::Material> master = sw::Material::create();
    SW_EXPECT_TRUE( master->loadFromFile( "engine/materials/glassmaterial.material" ) );
    sw::shared_ptr<sw::MaterialInstance> a = sw::MaterialInstance::create( master.get() );
    sw::shared_ptr<sw::MaterialInstance> b = sw::MaterialInstance::create( master.get() );

    {
        sw::GameObject*    go = objects->createGameObject( sw::hashed_string( "T0" ) );
        sw::MeshComponent* mc = go->addComponent<sw::MeshComponent>();
        mc->setMesh( cube );
        mc->setLocalPosition( sw::float3( 0.0f, 0.0f, -10.0f ) );
        mc->setBlendMode( sw::RHIBlendMode::Transparent );
        mc->setMaterialInstance( a );
    }
    {
        sw::GameObject*    go = objects->createGameObject( sw::hashed_string( "T1" ) );
        sw::MeshComponent* mc = go->addComponent<sw::MeshComponent>();
        mc->setMesh( cube );
        mc->setLocalPosition( sw::float3( 0.0f, 0.0f, -2.0f ) );
        mc->setBlendMode( sw::RHIBlendMode::Transparent );
        mc->setMaterialInstance( b );
    }

    sw::GpuSceneBuilder gpuScene;
    const sw::float3    cam{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, cam );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );
}

/**
 * @brief [GpuSceneTest] 머티리얼 원소 인덱스가 프레임을 넘어 유지되고, 안 쓰이면 회수되는지 (GPU 불필요).
 * @details 언리얼 GPUScene 은 프리미티브·머티리얼에 등록 시점에 **영속 ID** 를 주고 더티한 것만 갱신한다.
 *          예전엔 매 빌드마다 그룹을 지우고 인스턴스마다 인덱스를 다시 부여했다 — O(인스턴스 x 머티리얼) 이고,
 *          같은 머티리얼의 인덱스가 프레임마다 달라져 "바뀐 것만 올린다" 를 할 수 없었다.
 *
 *          여기서 보는 것 둘: (1) 같은 머티리얼은 빌드를 반복해도 같은 인덱스를 갖는다,
 *          (2) 쓰이지 않게 된 원소는 지연 회수돼 자리가 재사용된다(자리를 **옮기지 않고**).
 *          기본 생성한 Material 은 셰이더 경로가 비어 있어 한 그룹에 모인다 — 리소스 없이 원소 로직만 본다.
 */
SW_TEST_CASE( GpuSceneTest, MaterialElementIdsPersistAcrossBuildsAndRetire )
{
    sw::Scene scene( "MaterialElementIdScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    sw::shared_ptr<sw::Mesh>     mesh      = sw::MeshUtil::createUnitCube();
    sw::shared_ptr<sw::Material> materialA = sw::Material::create();
    sw::shared_ptr<sw::Material> materialB = sw::Material::create();
    SW_ASSERT_TRUE( mesh != nullptr && materialA != nullptr && materialB != nullptr );

    auto addObject = [&]( const utf8* pName, sw::Material* pMaterial, float32 offsetX ) -> sw::GameObject*
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
        if ( pObj == nullptr )
            return nullptr;
        sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
        if ( pMeshComp == nullptr )
            return nullptr;
        pMeshComp->setMesh( mesh );
        pMeshComp->setMaterial( pMaterial );
        pMeshComp->setLocalPosition( sw::float3{ offsetX, 0.0f, 0.0f } );
        return pObj;
    };

    sw::GameObject* pObjA = addObject( "MatA", materialA.get(), -1.0f );
    sw::GameObject* pObjB = addObject( "MatB", materialB.get(), 1.0f );
    SW_ASSERT_TRUE( pObjA != nullptr && pObjB != nullptr );

    // 머티리얼 → 원소 인덱스를 읽는 helper. 그룹은 하나뿐이다(둘 다 셰이더 경로가 비어 있다).
    auto findElementIndex = [&]( const sw::GpuSceneBuilder& gpuScene, const sw::Material* pMaterial ) -> int32
    {
        for ( const sw::GpuMaterialGroup& group : gpuScene.getMaterialGroups() )
        {
            for ( uint32 index = 0; index < group._listEntry.size(); ++index )
            {
                if ( group._listEntry[index]._material.get() == pMaterial )
                    return static_cast<int32>( index );
            }
        }
        return -1;
    };

    sw::GpuSceneBuilder gpuScene;
    const sw::float3    cameraPos{ 0.0f, 1.2f, 3.2f };
    gpuScene.buildFromScene( &scene, cameraPos );
    const int32 firstA = findElementIndex( gpuScene, materialA.get() );
    const int32 firstB = findElementIndex( gpuScene, materialB.get() );
    SW_EXPECT_TRUE_MSG( firstA >= 0 && firstB >= 0, "첫 빌드에서 두 머티리얼이 모두 원소를 받아야 한다" );
    SW_EXPECT_TRUE_MSG( firstA != firstB, "서로 다른 머티리얼은 서로 다른 원소여야 한다" );

    // (1) 여러 번 다시 빌드해도 인덱스가 그대로여야 한다.
    for ( uint32 buildIndex = 0; buildIndex < 4; ++buildIndex )
    {
        gpuScene.buildFromScene( &scene, cameraPos );
        SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialA.get() ) == firstA, "머티리얼 A 의 원소 인덱스가 빌드마다 바뀐다" );
        SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialB.get() ) == firstB, "머티리얼 B 의 원소 인덱스가 빌드마다 바뀐다" );
    }

    // (2) B 를 씬에서 빼고 회수 기준을 넘겨 빌드하면 B 의 자리가 비워져야 한다.
    //     회수 시계는 **실제로 원소를 다시 부여한 빌드**에서만 돈다 — 내용이 그대로면 buildFromScene 이
    //     조기 종료하므로(살아 있는 원소를 다시 표시하지 않는다) 그때 시계를 돌리면 멀쩡한 것이 회수된다.
    //     그래서 매번 A 를 조금씩 움직여 실제 리빌드를 일으킨다.
    sw::MeshComponent* pMeshB = pObjB->getComponent<sw::MeshComponent>();
    SW_ASSERT_TRUE( pMeshB != nullptr );
    pMeshB->setVisible( false );
    sw::MeshComponent* pMeshA = pObjA->getComponent<sw::MeshComponent>();
    SW_ASSERT_TRUE( pMeshA != nullptr );
    for ( uint32 buildIndex = 0; buildIndex < sw::constant::kRenderFrameQueueDepth + 3; ++buildIndex )
    {
        pMeshA->setLocalPosition( sw::float3{ -1.0f + static_cast<float32>( buildIndex ) * 0.01f, 0.0f, 0.0f } );
        gpuScene.buildFromScene( &scene, cameraPos );
    }

    SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialB.get() ) < 0, "안 쓰이게 된 머티리얼 원소가 회수되지 않았다" );
    SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialA.get() ) == firstA, "남아 있는 머티리얼의 인덱스는 회수 뒤에도 그대로여야 한다" );

    // 회수된 자리는 새 머티리얼이 재사용한다 — 자리를 옮기지 않으므로 A 의 인덱스는 여전히 그대로다.
    sw::shared_ptr<sw::Material> materialC = sw::Material::create();
    SW_ASSERT_TRUE( addObject( "MatC", materialC.get(), 2.0f ) != nullptr );
    gpuScene.buildFromScene( &scene, cameraPos );
    SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialC.get() ) == firstB, "회수된 자리를 새 머티리얼이 재사용해야 한다" );
    SW_EXPECT_TRUE_MSG( findElementIndex( gpuScene, materialA.get() ) == firstA, "새 머티리얼이 들어와도 기존 인덱스는 그대로여야 한다" );
}

/**
 * @brief [GpuSceneTest] 배치마다 **자기 머티리얼 원소**를 고르고, 값이 다르면 바이트도 다른지 (GPU 불필요).
 * @details 지금까지의 렌더 검증은 전부 씬 기본 머티리얼 하나였다 — 머티리얼 원소가 하나뿐이라 `materialIndex`
 *          가 늘 0 이었고, "배치마다 올바른 원소를 고르는가" 가 한 번도 검사되지 않았다. 영속 원소 ID 로 바꾼 뒤라
 *          특히 중요하다(인덱스가 프레임을 넘어 유지되고 회수 후 재사용된다).
 *
 *          픽셀로 보려 했지만 조명·톤매핑이 섞여 값이 흔들렸다. 여기서는 **CPU 스냅샷**을 본다 —
 *          두 머티리얼이 서로 다른 원소를 받는가, 그리고 프로퍼티 값이 다르면 패킹된 바이트도 다른가.
 *          백엔드 간 패킹 일치는 ShaderBindingContractTest.ReflectionNamesAreUniformAcrossBackends 가 본다.
 */
SW_TEST_CASE( GpuSceneTest, PerBatchMaterialElementsAreDistinct )
{
    // 머티리얼은 **실제 에셋**을 읽어 색만 바꾼다. XML 을 손으로 지어내면 퍼뮤테이션 선언(_permutations)이
    // 빠져 구워둔 셰이더 변형과 맞지 않는 머티리얼이 만들어진다 — 그러면 검증하려던 것과 다른 걸 재게 된다.
    auto makeMaterial = []( const utf8* pColor ) -> sw::shared_ptr<sw::Material>
    {
        // 반환 대상을 하나로 둔다 — nullptr 과 material 을 섞어 돌려주면 NRVO 가 걸리지 않는다.
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false ||
             material->setParameter( nullptr, sw::hashed_string( "color" ), pColor ) == false )
            material.reset();
        return material;
    };

    sw::shared_ptr<sw::Material> materialRed  = makeMaterial( "1.0 0.05 0.05 1.0" );
    sw::shared_ptr<sw::Material> materialBlue = makeMaterial( "0.05 0.05 1.0 1.0" );
    SW_ASSERT_TRUE( materialRed != nullptr && materialBlue != nullptr );

    // 프로퍼티 값이 다르면 패킹된 바이트도 달라야 한다 — 같은 셰이더라 레이아웃은 같고 값만 다르다.
    const sw::vector<uint8>& bytesRed  = materialRed->getBuffer();
    const sw::vector<uint8>& bytesBlue = materialBlue->getBuffer();
    SW_EXPECT_TRUE_MSG( bytesRed.empty() == false && bytesBlue.empty() == false, "머티리얼 바이트가 비어 있다" );
    SW_EXPECT_TRUE_MSG( bytesRed.size() == bytesBlue.size(), "같은 셰이더인데 패킹 크기가 다르다" );
    if ( bytesRed.size() == bytesBlue.size() && bytesRed.empty() == false )
    {
        SW_EXPECT_TRUE_MSG( sw::Memory::compare( bytesRed.data(), bytesBlue.data(), bytesRed.size() ) != 0,
                            "color 가 다른데 패킹된 바이트가 같다 — 프로퍼티가 원소에 반영되지 않는다" );
    }

    sw::Scene scene( "PerBatchMaterialScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    // 메시도 따로 만든다 — 배치 키에 메시가 들어가므로 배치가 갈린다.
    sw::shared_ptr<sw::Mesh> meshA = sw::MeshUtil::createUnitCube();
    sw::shared_ptr<sw::Mesh> meshB = sw::MeshUtil::createUnitCube();
    SW_ASSERT_TRUE( meshA != nullptr && meshB != nullptr );

    auto addObject = [&]( const utf8* pName, const sw::shared_ptr<sw::Mesh>& mesh, sw::Material* pMaterial, float32 offsetX )
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
        SW_ASSERT_TRUE( pObj != nullptr );
        sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
        SW_ASSERT_TRUE( pMeshComp != nullptr );
        pMeshComp->setMesh( mesh );
        pMeshComp->setMaterial( pMaterial );
        pMeshComp->setLocalPosition( sw::float3{ offsetX, 0.0f, 0.0f } );
    };
    addObject( "CubeRed", meshA, materialRed.get(), -1.1f );
    addObject( "CubeBlue", meshB, materialBlue.get(), 1.1f );

    sw::GpuSceneBuilder gpuScene;
    gpuScene.buildFromScene( &scene, sw::float3{ 0.0f, 1.2f, 3.2f } );

    // 배치가 둘 생기고, 각 배치가 자기 머티리얼의 원소를 가리켜야 한다.
    const sw::vector<sw::GpuMeshBatch>& batches = gpuScene.getOpaqueBatches();
    SW_EXPECT_TRUE_MSG( batches.size() == 2, "메시가 둘이면 배치도 둘이어야 한다" );
    SW_ASSERT_TRUE( batches.size() == 2 );
    SW_EXPECT_TRUE_MSG( batches[0]._materialIndex != batches[1]._materialIndex,
                        "서로 다른 머티리얼인데 같은 원소를 가리킨다 — materialIndex 가 어긋난다" );

    // 그 인덱스가 실제로 그 배치의 머티리얼을 가리키는지 그룹에서 확인한다.
    for ( const sw::GpuMeshBatch& batch : batches )
    {
        SW_ASSERT_TRUE( batch._materialGroup < gpuScene.getMaterialGroups().size() );
        const sw::GpuMaterialGroup& group = gpuScene.getMaterialGroups()[batch._materialGroup];
        SW_ASSERT_TRUE( batch._materialIndex < group._listEntry.size() );
        SW_EXPECT_TRUE_MSG( group._listEntry[batch._materialIndex]._material == batch._material,
                            "배치의 materialIndex 가 다른 머티리얼의 원소를 가리킨다" );
    }
}

/**
 * @brief [GpuSceneTest] 정적 스위치가 다르면 배치가 갈리고, 그 퍼뮤테이션이 배치에 실려 나가는지 (GPU 불필요).
 * @details 배치는 **PSO 하나로** 그린다. 그래서 배치를 묶는 키에 셰이더 퍼뮤테이션이 들어 있지 않으면,
 *          같은 .hlsl 을 쓰지만 정적 스위치가 다른 두 머티리얼이 한 배치로 접히고 한쪽 퍼뮤테이션이
 *          통째로 사라진다. 예전 키는 셰이더 **경로**뿐이라 정확히 그랬다 — 머티리얼이 선언한
 *          MATERIAL_BLEND_TRANSLUCENT 같은 것이 구워지기만 하고 한 번도 걸리지 않았다.
 *
 *          화면으로는 잡기 어렵다(퍼뮤테이션이 빠져도 그림은 그럴듯하게 나온다). 그래서 배치가 갈리는지와
 *          배치가 가리키는 퍼뮤테이션의 define 을 CPU 에서 직접 본다.
 */
SW_TEST_CASE( GpuSceneTest, PermutationSplitsBatchesAcrossMaterials )
{
    // 실제 에셋을 읽어 스위치만 바꾼다 — XML 을 손으로 지으면 _permutations 가 빠져 다른 걸 재게 된다.
    auto makeMaterial = []() -> sw::shared_ptr<sw::Material>
    {
        // 반환 대상을 하나로 둔다 — nullptr 과 material 을 섞어 돌려주면 NRVO 가 걸리지 않는다.
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        if ( material->loadFromFile( "engine/materials/defaultmaterial.material" ) == false )
            material.reset();
        return material;
    };

    sw::shared_ptr<sw::Material> materialPlain    = makeMaterial();
    sw::shared_ptr<sw::Material> materialSwitched = makeMaterial();
    SW_ASSERT_TRUE( materialPlain != nullptr && materialSwitched != nullptr );

    // 같은 셰이더, 같은 블렌드 모드. 다른 것은 정적 스위치 하나뿐이다.
    materialSwitched->setStaticSwitch( sw::hashed_string( "UseNormalMap" ), true );
    SW_EXPECT_TRUE_MSG( materialPlain->getShaderPath() == materialSwitched->getShaderPath(),
                        "이 테스트는 같은 셰이더를 쓰는 두 머티리얼을 전제로 한다" );
    SW_ASSERT_TRUE( materialPlain->getPermutationHash() != materialSwitched->getPermutationHash() );

    sw::Scene scene( "PermutationSplitScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    // 메시는 **하나를 공유한다**. 메시가 다르면 어차피 배치가 갈려서 퍼뮤테이션 때문에 갈린 것인지 알 수 없다.
    sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createUnitCube();
    SW_ASSERT_TRUE( mesh != nullptr );

    auto addObject = [&]( const utf8* pName, sw::Material* pMaterial, float32 offsetX )
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
        SW_ASSERT_TRUE( pObj != nullptr );
        sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
        SW_ASSERT_TRUE( pMeshComp != nullptr );
        pMeshComp->setMesh( mesh );
        pMeshComp->setMaterial( pMaterial );
        pMeshComp->setLocalPosition( sw::float3{ offsetX, 0.0f, 0.0f } );
    };
    addObject( "CubePlain", materialPlain.get(), -1.1f );
    addObject( "CubeSwitched", materialSwitched.get(), 1.1f );

    sw::GpuSceneBuilder gpuScene;
    // 머티리얼을 가로질러 합치는 모드 — 이 모드가 바로 퍼뮤테이션을 뭉개던 자리다.
    gpuScene.setMergeBatchesAcrossMaterials( true );
    gpuScene.buildFromScene( &scene, sw::float3{ 0.0f, 1.2f, 3.2f } );

    const sw::vector<sw::GpuMeshBatch>& batches = gpuScene.getOpaqueBatches();
    SW_EXPECT_TRUE_MSG( batches.size() == 2,
                        "퍼뮤테이션이 다른 두 머티리얼이 한 배치로 접혔다 — 배치는 PSO 하나로 그리므로 한쪽이 버려진다" );
    SW_ASSERT_TRUE( batches.size() == 2 );

    SW_EXPECT_TRUE_MSG( batches[0]._shaderPermutation != batches[1]._shaderPermutation,
                        "배치는 갈렸는데 같은 퍼뮤테이션을 가리킨다" );

    // 배치가 가리키는 퍼뮤테이션이 실제로 그 머티리얼의 define 을 들고 있어야 한다.
    bool bFoundSwitched{ false };
    for ( const sw::GpuMeshBatch& batch : batches )
    {
        const sw::GpuShaderPermutation* pPermutation = gpuScene.findShaderPermutation( batch._shaderPermutation );
        SW_ASSERT_TRUE( pPermutation != nullptr );
        SW_EXPECT_TRUE_MSG( pPermutation->_shaderPath.empty() == false, "퍼뮤테이션에 셰이더 경로가 없다" );

        bool bHasNormalMap{ false };
        for ( const sw::string& defineStr : pPermutation->_listDefine )
        {
            if ( defineStr == "MATERIAL_NORMALMAP" )
                bHasNormalMap = true;
        }
        if ( batch._material == materialSwitched )
        {
            bFoundSwitched = true;
            SW_EXPECT_TRUE_MSG( bHasNormalMap, "스위치를 켠 머티리얼의 배치인데 그 키워드가 퍼뮤테이션에 없다" );
        }
        else
        {
            SW_EXPECT_TRUE_MSG( bHasNormalMap == false, "스위치를 안 켠 머티리얼의 배치에 남의 키워드가 들어 있다" );
        }
    }
    SW_EXPECT_TRUE_MSG( bFoundSwitched, "스위치를 켠 머티리얼의 배치를 찾지 못했다" );
}

/**
 * @brief [GpuSceneTest] 절두체 평면을 viewProj 에서 제대로 뽑는지 (GPU 불필요).
 * @details 이 계산이 **없어서** GPU 컬링이 켜 놓고도 한 번도 아무것도 거르지 않았다. 상수버퍼의 평면
 *          배열이 0 인 채로 나갔고, 그러면 셰이더의 `dot( 0, center ) + 0 < -radius` 가 항상 거짓이라
 *          모든 인스턴스가 통과한다. 화면은 멀쩡해 보이므로 픽셀로는 잡히지 않는다 — 컬링이 안 될 뿐
 *          그림은 맞기 때문이다. 그래서 평면 자체를 CPU 에서 본다.
 *
 *          셰이더와 같은 판정식(`dot( plane.xyz, center ) + plane.w < -radius` 면 바깥)을 그대로 쓴다.
 */
SW_TEST_CASE( GpuSceneTest, FrustumPlanesFromViewProj )
{
    // 원점을 바라보는 카메라 — 엔진 기본 카메라와 같은 자리에 둔다.
    const sw::float3   eye{ 0.0f, 0.0f, 5.0f };
    const sw::float4x4 view     = sw::float4x4::createLookAt( eye, sw::float3::Zero, sw::float3::Up );
    const sw::float4x4 proj     = sw::float4x4::createPerspectiveFieldOfView( 0.8f, 1.0f, 0.5f, 100.0f );
    const sw::float4x4 viewProj = view * proj;

    // **실제 코드가 쓰는 경로**를 그대로 검증한다 — 뷰가 행렬과 절두체를 함께 갱신한다.
    sw::RenderView renderView{};
    renderView.setViewProjection( viewProj );
    const sw::float4( &arrPlane )[6] = renderView._frustum._arrPlane;

    // 평면은 정규화돼 있어야 한다 — 그래야 셰이더가 반지름을 그대로 비교할 수 있다.
    for ( uint32 planeIndex = 0; planeIndex < 6; ++planeIndex )
    {
        const float32 length = sw::MathUtil::sqrt( arrPlane[planeIndex]._x * arrPlane[planeIndex]._x +
                                                   arrPlane[planeIndex]._y * arrPlane[planeIndex]._y +
                                                   arrPlane[planeIndex]._z * arrPlane[planeIndex]._z );
        SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( length - 1.0f ) < 0.001f,
                            ( sw::string( "평면 " ) + sw::to_string( planeIndex ) + " 가 정규화되지 않았다 (길이 " +
                              sw::to_string( length ) + ")" )
                                .c_str() );
    }

    // 셰이더와 같은 판정 — 하나라도 -radius 보다 작으면 바깥이다.
    auto isVisible = [&arrPlane]( const sw::float3& center, float32 radius ) -> bool
    {
        for ( uint32 planeIndex = 0; planeIndex < 6; ++planeIndex )
        {
            const float32 distance = arrPlane[planeIndex]._x * center._x + arrPlane[planeIndex]._y * center._y +
                                     arrPlane[planeIndex]._z * center._z + arrPlane[planeIndex]._w;
            if ( distance < -radius )
                return false;
        }
        return true;
    };

    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 0.0f, 0.0f, 0.0f }, 0.5f ), "카메라가 보는 원점이 절두체 밖으로 판정됐다" );
    SW_EXPECT_TRUE_MSG( isVisible( eye + sw::float3{ 0.0f, 0.0f, -2.0f }, 0.5f ), "카메라 바로 앞이 절두체 밖으로 판정됐다" );

    // 여기부터가 핵심 — 평면이 0 이면 아래 넷이 전부 "보인다"로 나온다.
    SW_EXPECT_TRUE_MSG( isVisible( eye + sw::float3{ 0.0f, 0.0f, 20.0f }, 0.5f ) == false, "카메라 뒤가 걸러지지 않는다" );
    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 0.0f, 0.0f, -500.0f }, 0.5f ) == false, "원평면 너머가 걸러지지 않는다" );
    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 200.0f, 0.0f, 0.0f }, 0.5f ) == false, "화면 오른쪽 바깥이 걸러지지 않는다" );
    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 0.0f, 200.0f, 0.0f }, 0.5f ) == false, "화면 위쪽 바깥이 걸러지지 않는다" );

    // 반지름이 크면 경계 밖이어도 걸리면 안 된다 (셰이더가 반지름을 그대로 쓰는지).
    SW_EXPECT_TRUE_MSG( isVisible( sw::float3{ 200.0f, 0.0f, 0.0f }, 400.0f ), "반지름이 큰 물체를 잘못 걸렀다" );
}

/**
 * @brief [GpuSceneTest] 많은 물체 중 하나만 움직여도 그 하나가 갱신되고 나머지는 그대로인지 검증
 *
 * @details 수집은 이제 **바뀐 프리미티브만** 다시 모은다(등록부의 더티 목록). 예전에는 8000 개 중
 *          10 개만 움직여도 8000 개를 전부 다시 모았다(수집 244 us — 전부 움직일 때와 같았다).
 *          고친 뒤 같은 조건에서 1 us 다.
 *
 *          **이 최적화가 틀리는 모습은 "움직인 물체가 화면에서 얼어붙는 것"이다** — 더티 표시가
 *          빠지거나 후보 자리를 잘못 찾으면 그렇게 된다. 컴파일로도, 평균 픽셀로도 안 잡힌다.
 *          그래서 여기서는 움직인 것과 안 움직인 것을 **둘 다** 단언한다.
 */
SW_TEST_CASE( GpuSceneTest, PartialCollectUpdatesOnlyTheMovedPrimitive )
{
    sw::Scene scene( "PartialCollectScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( mesh.get() );

    constexpr uint32               kObjectCount = 8;
    sw::vector<sw::MeshComponent*> listComp;
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( ( "Cube" + sw::to_string( index ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pObj );
        sw::MeshComponent* pComp = pObj->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( pComp );
        pComp->setMesh( mesh );
        pComp->setLocalPosition( sw::float3{ static_cast<float32>( index ) * 2.0f, 0.0f, 0.0f } );
        listComp.push_back( pComp );
    }
    scene.getObjectManager()->flushSceneTransforms();

    sw::GpuSceneBuilder builder;
    const sw::float3    cameraPos{ 0.0f, 0.0f, -20.0f };

    builder.buildFromScene( &scene, cameraPos );
    sw::GpuSceneSnapshot first;
    builder.exportCpuSnapshot( first );
    SW_ASSERT_EQUAL( kObjectCount, static_cast<uint32>( first.getInstances().size() ) );

    // 하나만 움직인다 — 나머지 일곱은 더티가 아니라 수집에서 건너뛴다.
    listComp[5]->setLocalPosition( sw::float3{ 10.0f, 6.0f, 0.0f } );
    scene.getObjectManager()->flushSceneTransforms();
    builder.buildFromScene( &scene, cameraPos );

    sw::GpuSceneSnapshot moved;
    builder.exportCpuSnapshot( moved );
    SW_ASSERT_EQUAL( kObjectCount, static_cast<uint32>( moved.getInstances().size() ) );

    // 1. 움직인 것이 실제로 갱신됐어야 한다 — 빠지면 화면에서 얼어붙는다.
    uint32 movedSlotCount = 0;
    for ( const sw::GpuInstance& inst : moved.getInstances() )
    {
        if ( inst._world.getTranslation()._y > 5.0f )
            ++movedSlotCount;
    }
    SW_EXPECT_EQUAL( 1u, movedSlotCount );

    // 2. 나머지는 그대로여야 한다 — 부분 수집이 엉뚱한 자리를 덮어썼으면 여기서 걸린다.
    uint32 stillCount = 0;
    for ( const sw::GpuInstance& inst : moved.getInstances() )
    {
        if ( inst._world.getTranslation()._y < 0.001f )
            ++stillCount;
    }
    SW_EXPECT_EQUAL( kObjectCount - 1u, stillCount );

    // 3. 아무것도 안 움직인 프레임은 **같은 배열이 그대로 실려야 한다** — 수집도 발행도 하지 않는다.
    builder.buildFromScene( &scene, cameraPos );
    sw::GpuSceneSnapshot idle;
    builder.exportCpuSnapshot( idle );
    SW_EXPECT_TRUE( idle._pListInstance.get() == moved._pListInstance.get() );
}

/**
 * @brief [GpuSceneTest] 인스턴스 배열을 발행(공유)한 뒤에도 제자리 갱신이 움직임을 반영하는지 검증
 *
 * @details 인스턴스 배열은 값이 아니라 `shared_ptr` 로 **공유**된다 — RT 는 읽기만 하므로 안 바뀐
 *          프레임에 복사할 이유가 없다(정적 8000 엔티티에서 export 441 -> 3 us). 그래서 빌더는 다 지은
 *          배열을 **옮겨서** 발행하고, 자기 작업 배열은 비운다.
 *
 *          **여기서 지키는 것은 "발행한 배열은 다시 고치지 않는다" 하나다.** 빌더가 이미 넘긴
 *          배열을 제자리에서 갱신하면, 렌더 스레드가 그 프레임에 읽고 있는 데이터가 밑에서
 *          바뀐다(스레드를 넘는 데이터 레이스이고, 지난 프레임 패킷도 같이 변질된다). 그래서
 *          내용이 바뀐 프레임마다 **새 배열을 발행**한다 — 아래 마지막 두 단언이 그것이다.
 *
 *          (되돌려 받는 단계가 빠지는 실수는 안전하게 무너진다 — 제자리 갱신이 조건에 안 맞아
 *          전체 재구축으로 떨어질 뿐, 화면은 맞는다. 그래서 그쪽은 테스트가 잡지 않는다.)
 */
SW_TEST_CASE( GpuSceneTest, InstancesStayLiveAfterPublishWhenObjectsMove )
{
    sw::Scene scene( "MoveAfterPublishScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( mesh.get() );

    sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "Mover" ) );
    SW_ASSERT_NOT_NULL( pObj );
    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMeshComp );
    pMeshComp->setMesh( mesh );
    pMeshComp->setLocalPosition( sw::float3{ 1.0f, 0.0f, 0.0f } );
    scene.getObjectManager()->flushSceneTransforms();

    sw::GpuSceneBuilder builder;
    const sw::float3    cameraPos{ 0.0f, 0.0f, -5.0f };

    // 1 프레임: 전체 빌드 -> 발행. 여기서 작업 배열이 비워진다.
    builder.buildFromScene( &scene, cameraPos );
    sw::GpuSceneSnapshot first;
    builder.exportCpuSnapshot( first );
    SW_ASSERT_TRUE( first.getInstances().empty() == false );
    const float32 firstX = first.getInstances()[0]._world.getTranslation()._x;
    SW_EXPECT_NEAR_EQUAL( 1.0f, firstX, 0.001f );

    // 2 프레임: 물체만 움직인다 — 배치 구성은 그대로라 제자리 갱신 경로를 탄다.
    pMeshComp->setLocalPosition( sw::float3{ 7.0f, 0.0f, 0.0f } );
    scene.getObjectManager()->flushSceneTransforms();
    builder.buildFromScene( &scene, cameraPos );

    sw::GpuSceneSnapshot second;
    builder.exportCpuSnapshot( second );
    SW_ASSERT_TRUE( second.getInstances().empty() == false );
    SW_EXPECT_NEAR_EQUAL( 7.0f, second.getInstances()[0]._world.getTranslation()._x, 0.001f );

    // 발행본은 서로 다른 배열이어야 한다 — 같은 것을 고쳐 보내면 RT 가 이미 든 스냅샷이 바뀐다.
    SW_EXPECT_TRUE( first._pListInstance.get() != second._pListInstance.get() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, first.getInstances()[0]._world.getTranslation()._x, 0.001f );
}

/**
 * @brief [GpuSceneTest] 물체 하나만 움직이면 그 인스턴스 구간 하나만 더티로 표시되는지 검증
 *
 * @details 인스턴스 버퍼는 뭐 하나라도 바뀌면 **전체**를 다시 올리고 있었다 — 8000 개 중 10 개만
 *          움직여도 800 개를 움직일 때와 같은 100 us 를 썼다. 지금은 빌더가 바뀐 구간만 적어 주고
 *          받는 쪽이 그 구간들을 **한 번의 호출**로 올린다(1/20/800 개 이동 = 16/18/34 us).
 *
 *          여기서 지키는 것은 그 구간 계산이다. 너무 넓게 잡으면 이득이 사라지고, **너무 좁게 잡으면
 *          움직인 물체가 화면에서 얼어붙는다** — 둘 다 컴파일로는 안 잡힌다.
 */
SW_TEST_CASE( GpuSceneTest, MovingOneObjectMarksOnlyItsInstanceRun )
{
    sw::Scene scene( "DirtyRunScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( mesh.get() );

    sw::vector<sw::MeshComponent*> listComp;
    for ( uint32 index = 0; index < 4; ++index )
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( ( "Cube" + sw::to_string( index ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pObj );
        sw::MeshComponent* pComp = pObj->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( pComp );
        pComp->setMesh( mesh );
        pComp->setLocalPosition( sw::float3{ static_cast<float32>( index ) * 2.0f, 0.0f, 0.0f } );
        listComp.push_back( pComp );
    }
    scene.getObjectManager()->flushSceneTransforms();

    sw::GpuSceneBuilder builder;
    const sw::float3    cameraPos{ 0.0f, 0.0f, -10.0f };

    // 1 프레임: 전체 빌드 — 전부 더티여야 한다.
    builder.buildFromScene( &scene, cameraPos );
    sw::GpuSceneSnapshot first;
    builder.exportCpuSnapshot( first );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( first.getInstances().size() ) );
    SW_EXPECT_TRUE( first._bAllInstancesDirty != SW_FALSE );

    // 2 프레임: 아무것도 안 움직인다 — 빌드가 통째로 건너뛰므로 스냅샷은 지난 것 그대로다.
    builder.buildFromScene( &scene, cameraPos );

    // 3 프레임: 하나만 움직인다.
    listComp[2]->setLocalPosition( sw::float3{ 4.0f, 5.0f, 0.0f } );
    scene.getObjectManager()->flushSceneTransforms();
    builder.buildFromScene( &scene, cameraPos );

    sw::GpuSceneSnapshot moved;
    builder.exportCpuSnapshot( moved );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( moved.getInstances().size() ) );

    // 전부가 아니라 **구간 하나**여야 한다.
    SW_EXPECT_TRUE( moved._bAllInstancesDirty == SW_FALSE );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( moved._listDirtyInstanceRun.size() ) );
    SW_EXPECT_EQUAL( 1u, moved._listDirtyInstanceRun[0]._count );

    // 그 구간이 실제로 움직인 인스턴스를 가리켜야 한다 — 좁게 잡으면 화면이 언다.
    const uint32 dirtySlot = moved._listDirtyInstanceRun[0]._start;
    SW_ASSERT_TRUE( dirtySlot < moved.getInstances().size() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, moved.getInstances()[dirtySlot]._world.getTranslation()._y, 0.001f );

    // 나머지는 그대로다.
    for ( uint32 slot = 0; slot < moved.getInstances().size(); ++slot )
    {
        if ( slot == dirtySlot )
            continue;
        SW_EXPECT_NEAR_EQUAL( 0.0f, moved.getInstances()[slot]._world.getTranslation()._y, 0.001f );
    }
}

/**
 * @brief [GpuSceneTest] CPU 스냅샷이 **퍼뮤테이션 표까지** 건너오는지 검증.
 * @details 배치의 `_shaderPermutation` 은 `GpuScene::getShaderPermutations()` 의 **인덱스**다. 표를
 *          함께 보내지 않으면 받는 쪽에서 `findShaderPermutation` 이 늘 nullptr 을 돌려주고, 배치는
 *          퍼뮤테이션이 없는 것처럼 보인다 — 실제로 그랬다. 그 결과 패킷 경로(= 실제 앱과 에디터가
 *          쓰는 경로)에서는 머티리얼 퍼뮤테이션이 **하나도** 걸리지 않았고, 유리 머티리얼의
 *          `MATERIAL_BLEND_TRANSLUCENT` 도 화면에 닿은 적이 없었다.
 *
 *          `MaterialPermutationDrivesBatchPso` 는 동기 `execute()` 경로만 태우므로 이 결함을 볼 수
 *          없었다 — 두 경로를 가르는 것이 이 테스트의 존재 이유다. GPU 가 필요 없다.
 */
SW_TEST_CASE( GpuSceneTest, CpuSnapshotCarriesShaderPermutations )
{
    sw::shared_ptr<sw::Material> materialGlass = sw::Material::create();
    SW_ASSERT_TRUE( materialGlass->loadFromFile( "engine/materials/glassmaterial.material" ) );

    sw::Scene scene( "SnapshotPermutationScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( mesh.get() );

    sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( "GlassCube" ) );
    SW_ASSERT_NOT_NULL( pObj );
    sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pMeshComp );
    pMeshComp->setMesh( mesh );
    pMeshComp->setMaterial( materialGlass.get() );

    // 게임 스레드 쪽 GpuScene — 여기가 퍼뮤테이션 표의 정본이다.
    sw::GpuSceneBuilder gtScene;
    gtScene.buildFromScene( &scene, sw::float3{ 0.0f, 0.0f, 0.0f } );

    const sw::vector<sw::GpuMeshBatch>& gtBatches = gtScene.getTransparentBatches();
    SW_ASSERT_FALSE( gtBatches.empty() ); // 반투명 머티리얼인데 반투명 배치가 없으면 전제가 깨진 것이다
    const uint32 permutationIndex = gtBatches[0]._shaderPermutation;
    SW_ASSERT_TRUE( permutationIndex != sw::kInvalidShaderPermutation ); // 배치에 퍼뮤테이션이 붙어야 한다

    const sw::GpuShaderPermutation* pGtPermutation = gtScene.findShaderPermutation( permutationIndex );
    SW_ASSERT_NOT_NULL( pGtPermutation );

    // 패킷을 거쳐 렌더 스레드 쪽 GpuScene 으로 옮긴다 (EngineLoop 가 매 프레임 하는 그대로).
    sw::GpuSceneSnapshot packetScene;
    gtScene.exportCpuSnapshot( packetScene );
    sw::GpuScene rtScene;
    rtScene.adoptCpuSnapshot( packetScene ); // 바꿔치기 — 패킷 자리에는 RT 의 지난(빈) 스냅샷이 남는다

    const sw::vector<sw::GpuMeshBatch>& rtBatches = rtScene.getTransparentBatches();
    SW_EXPECT_TRUE_MSG( rtBatches.empty() == false, "스냅샷에 반투명 배치가 없다" );
    if ( rtBatches.empty() == false )
    {
        SW_EXPECT_TRUE_MSG( rtBatches[0]._shaderPermutation == permutationIndex,
                            "스냅샷의 배치가 다른 퍼뮤테이션 인덱스를 가리킨다" );

        const sw::GpuShaderPermutation* pRtPermutation = rtScene.findShaderPermutation( permutationIndex );
        // 여기가 결함의 자리다 — 표가 안 오면 받는 쪽에서 머티리얼 퍼뮤테이션이 통째로 사라진다.
        SW_EXPECT_NOT_NULL( pRtPermutation );
        if ( pRtPermutation != nullptr )
        {
            SW_EXPECT_TRUE_MSG( pRtPermutation->_hash == pGtPermutation->_hash,
                                "스냅샷의 퍼뮤테이션 해시가 원본과 다르다" );
            SW_EXPECT_TRUE_MSG( pRtPermutation->_listDefine.size() == pGtPermutation->_listDefine.size(),
                                "스냅샷의 퍼뮤테이션 define 개수가 원본과 다르다" );
        }
    }

    // 정본은 GT 에 남아 있어야 한다 — 빼앗아 가면 다음 프레임의 인덱스가 0 부터 다시 매겨진다.
    SW_EXPECT_NOT_NULL( gtScene.findShaderPermutation( permutationIndex ) );
}

/**
 * @brief [GpuSceneTest] **인스턴스**의 퍼뮤테이션을 런타임에 바꾸면 배치가 다시 갈리는지 (GPU 불필요).
 * @details `PermutationSplitsBatchesAcrossMaterials` 는 서로 다른 **머티리얼**이 갈리는지를 본다.
 *          이 테스트는 그보다 어려운 자리다 — 부모 머티리얼이 **같고** `MaterialInstance` 만 정적 스위치를
 *          바꾼 경우, 그리고 그것을 **첫 빌드 뒤에** 바꾼 경우다. 실제로 셋이 겹쳐 죽어 있었다:
 *            1. 배치 키가 퍼뮤테이션 해시 대신 "대표 머티리얼 포인터" 를 썼다 — 부모가 같으면 대표도
 *               같아서 서로 다른 셰이더로 그려야 할 것이 한 배치로 접혔다.
 *            2. 증분 경로(`hasSameBatchKey`)가 퍼뮤테이션을 보지 않아 다시 갈리지 않았다.
 *            3. 정지한 씬은 프리미티브가 더러워지지 않아 수집 자체를 건너뛰었다.
 *          그래서 런타임에 정적 스위치를 바꾸는 길이 통째로 조용히 죽어 있었다.
 */
SW_TEST_CASE( GpuSceneTest, InstancePermutationChangeRebuildsBatches )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromFile( "engine/materials/defaultmaterial.material" ) );

    sw::Scene scene( "InstancePermutationScene" );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );

    // 메시도 부모 머티리얼도 **같다**. 다른 것은 인스턴스뿐이다.
    sw::shared_ptr<sw::Mesh> mesh = sw::MeshUtil::createUnitCube();
    SW_ASSERT_TRUE( mesh != nullptr );

    auto addObject = [&]( const utf8* pName, float32 offsetX ) -> sw::MeshComponent*
    {
        sw::GameObject* pObj = scene.getObjectManager()->createGameObject( sw::hashed_string( pName ) );
        if ( pObj == nullptr )
            return nullptr;
        sw::MeshComponent* pMeshComp = pObj->addComponent<sw::MeshComponent>();
        if ( pMeshComp == nullptr )
            return nullptr;
        pMeshComp->setMesh( mesh );
        pMeshComp->setMaterial( material.get() );
        pMeshComp->setLocalPosition( sw::float3{ offsetX, 0.0f, 0.0f } );
        return pMeshComp;
    };
    sw::MeshComponent* pPlain    = addObject( "CubePlain", -1.1f );
    sw::MeshComponent* pSwitched = addObject( "CubeSwitched", 1.1f );
    SW_ASSERT_TRUE( pPlain != nullptr && pSwitched != nullptr );

    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
    SW_ASSERT_TRUE( instance != nullptr );
    pSwitched->setMaterialInstance( instance );

    sw::GpuSceneBuilder gpuScene;
    gpuScene.setMergeBatchesAcrossMaterials( true ); // 합치기가 켜진 쪽이 바로 접히던 자리다
    const sw::float3 cameraPos{ 0.0f, 1.2f, 3.2f };
    gpuScene.buildFromScene( &scene, cameraPos );
    SW_ASSERT_TRUE( gpuScene.getOpaqueBatches().size() == 1 );

    // **첫 빌드 뒤에** 인스턴스의 정적 스위치를 켠다. 씬에서는 아무것도 움직이지 않는다.
    instance->enableKeyword( sw::hashed_string( "MATERIAL_NORMALMAP" ) );
    gpuScene.buildFromScene( &scene, cameraPos );

    const sw::vector<sw::GpuMeshBatch>& batches = gpuScene.getOpaqueBatches();
    SW_EXPECT_TRUE_MSG( batches.size() == 2,
                        "인스턴스의 퍼뮤테이션이 달라졌는데 한 배치로 남았다 — 배치는 PSO 하나로 그린다" );
    SW_ASSERT_TRUE( batches.size() == 2 );

    SW_EXPECT_TRUE_MSG( batches[0]._shaderPermutation != batches[1]._shaderPermutation,
                        "배치는 갈렸는데 같은 퍼뮤테이션을 가리킨다" );

    // 합치기가 켜지면 배치의 `_materialInstance` 는 nullptr 이다(값은 원소 표가 든다) — 배치를
    // 인스턴스로 식별할 수 없다. 그래서 **퍼뮤테이션 쪽에서** 센다: 켠 것 하나, 안 켠 것 하나여야 한다.
    uint32 withNormalMap{ 0 };
    for ( const sw::GpuMeshBatch& batch : batches )
    {
        const sw::GpuShaderPermutation* pPermutation = gpuScene.findShaderPermutation( batch._shaderPermutation );
        SW_ASSERT_TRUE( pPermutation != nullptr );
        SW_EXPECT_TRUE_MSG( pPermutation->_shaderPath.empty() == false, "퍼뮤테이션에 셰이더 경로가 없다" );
        for ( const sw::string& defineStr : pPermutation->_listDefine )
        {
            if ( defineStr == "MATERIAL_NORMALMAP" )
                ++withNormalMap;
        }
    }
    SW_EXPECT_TRUE_MSG( withNormalMap == 1,
                        "인스턴스가 켠 키워드를 든 배치가 정확히 하나여야 한다 — 0 이면 인스턴스 define 이 통째로 빠진 것이고, 2 면 남의 배치에까지 번진 것이다" );
}

/**
 * @brief [GpuSceneTest] 걸러진 프리미티브 때문에 슬롯이 밀려도 **지난 프레임 값이 남지 않는다**
 * @details 수집은 후보 배열을 비우지 않고 제자리에 덮어쓴다(참조 카운트를 매 프레임 내렸다 올리지
 *          않으려고). 그래서 앞의 것이 걸러지면 **뒤의 것이 앞 슬롯으로 내려오고**, 그 슬롯에는
 *          다른 프리미티브의 값이 들어 있다 — 한 필드라도 다시 쓰지 않으면 남의 메시·블렌드 모드로
 *          그려진다. 이 케이스가 그 자리를 잡는다: 첫 번째를 숨기고 다시 지으면 0번 슬롯이
 *          **두 번째 것으로 완전히** 바뀌어야 한다.
 */
SW_TEST_CASE( GpuSceneTest, ReusedCandidateSlotsCarryNoStaleData )
{
    sw::Scene scene( "GpuSceneSlotReuse" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );

    sw::GameObjectManager* objects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( objects );

    // 메시가 서로 달라야 "남의 메시가 남았다" 를 볼 수 있다.
    sw::shared_ptr<sw::Mesh> cube   = sw::MeshUtil::createUnitCube();
    sw::shared_ptr<sw::Mesh> sphere = sw::MeshUtil::createSphere();
    SW_ASSERT_NOT_NULL( cube.get() );
    SW_ASSERT_NOT_NULL( sphere.get() );
    SW_ASSERT_TRUE( cube.get() != sphere.get() );

    auto addMesh = [&]( const utf8* pName, const sw::shared_ptr<sw::Mesh>& mesh, float32 x, sw::RHIBlendMode blend ) -> sw::MeshComponent*
    {
        sw::GameObject* go = objects->createGameObject( sw::hashed_string( pName ) );
        if ( go == nullptr )
            return nullptr;
        sw::MeshComponent* pMeshComp = go->addComponent<sw::MeshComponent>();
        if ( pMeshComp == nullptr )
            return nullptr;
        pMeshComp->setMesh( mesh );
        pMeshComp->setLocalPosition( sw::float3( x, 0.0f, -4.0f ) );
        pMeshComp->setBlendMode( blend );
        pMeshComp->setVisible( true );
        return pMeshComp;
    };

    // 0번은 큐브·불투명, 1번은 구·반투명 — 숨기면 1번이 0번 슬롯으로 내려온다.
    sw::MeshComponent* pFirst  = addMesh( "SlotFirstCube", cube, -3.0f, sw::RHIBlendMode::Opaque );
    sw::MeshComponent* pSecond = addMesh( "SlotSecondSphere", sphere, 3.0f, sw::RHIBlendMode::Transparent );
    SW_ASSERT_NOT_NULL( pFirst );
    SW_ASSERT_NOT_NULL( pSecond );

    sw::GpuSceneBuilder gpuScene;
    const sw::float3    camPos{ 0.0f, 0.0f, 0.0f };
    gpuScene.buildFromScene( &scene, camPos );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // **여기서 한 번 더 짓는 것이 이 케이스의 핵심이다.** 수집이 쓰는 배열과 기준 배열은 끝에서
    // 맞바뀌므로, 첫 빌드가 끝난 시점의 수집 배열은 아직 **비어 있다**(맞바꾸기 전 기준이 비었다).
    // 두 번째 빌드를 지나야 지지난 프레임의 값이 수집 배열로 돌아온다 — 재사용이 처음 일어나는
    // 자리가 거기다. 한 번만 짓고 검사하면 슬롯을 통째로 무시하는 구현도 통과한다(실제로 그랬다).
    pSecond->setLocalPosition( sw::float3( 3.0f, 0.5f, -4.0f ) );
    gpuScene.buildFromScene( &scene, camPos );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( gpuScene.getInstances().size() ) );

    // 앞의 것을 숨긴다 — 남는 것은 구 하나뿐이어야 한다.
    pFirst->setVisible( false );
    gpuScene.buildFromScene( &scene, camPos );

    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( gpuScene.getInstances().size() ) );
    SW_EXPECT_TRUE_MSG( gpuScene.getOpaqueBatches().empty(),
                        "불투명 배치가 남았다 — 숨긴 큐브의 값이 재사용된 슬롯에 남아 있다" );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( gpuScene.getTransparentBatches().size() ) );

    const sw::GpuMeshBatch& batch = gpuScene.getTransparentBatches()[0];
    SW_EXPECT_TRUE_MSG( batch._mesh.get() == sphere.get(),
                        "배치가 든 메시가 구가 아니다 — 앞 슬롯의 큐브가 그대로 남았다" );
    SW_EXPECT_EQUAL( 1u, batch._instanceCount );

    // 바운드 중심도 두 번째 것의 값이어야 한다(위치까지 갈아엎혔는가).
    const sw::GpuInstance& instance = gpuScene.getInstances()[batch._instanceBase];
    SW_EXPECT_TRUE_MSG( sw::MathUtil::nearEqual( instance._boundsCenter._x, 3.0f ),
                        "바운드 중심이 숨긴 큐브의 자리다 — 슬롯이 덜 덮어써졌다" );
}

/**
 * @brief [GpuSceneTest] 등록부는 더티 표시를 프리미티브당 한 번만 센다 — 락 없이, 워커 여럿이 동시에 찍어도
 * @details `markDirty` 는 락 없는 원자 exchange 다. 같은 프리미티브를 몇 번 찍든 개수는 하나여야 하고,
 *          `consumeDirty` 는 슬롯을 한 번씩만 돌려주며 그 뒤 `hasDirty` 는 false 다. 개수를 무조건 올리면
 *          소비한 뒤에도 "더티가 남았다" 가 되어 매 프레임 헛수집을 한다.
 */
SW_TEST_CASE( GpuSceneTest, PrimitiveRegistryCountsEachMarkOnce )
{
    sw::Scene scene( "GpuScenePrimitiveRegistryOnce" );
    SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
    SW_ASSERT_NOT_NULL( cube.get() );

    constexpr uint32               kMeshCount = 8;
    sw::vector<sw::MeshComponent*> listMesh;
    for ( uint32 index = 0; index < kMeshCount; ++index )
    {
        sw::GameObject* pObject = pObjects->createGameObject( sw::hashed_string( ( sw::string( "Once" ) + sw::to_string( index ) ).c_str() ) );
        SW_ASSERT_NOT_NULL( pObject );
        sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        pMesh->setMesh( cube );
        listMesh.push_back( pMesh );
    }
    sw::PrimitiveRegistry& registry = pObjects->getPrimitiveRegistry();
    SW_ASSERT_EQUAL( size_t( kMeshCount ), registry.getAll().size() );
    registry.clearDirty();
    SW_ASSERT_FALSE( registry.hasDirty() );

    // 1) 같은 프리미티브를 세 번 찍어도 하나다.
    listMesh[0]->setLocalPosition( sw::float3( 1.0f, 0.0f, 0.0f ) );
    listMesh[0]->setLocalPosition( sw::float3( 2.0f, 0.0f, 0.0f ) );
    listMesh[0]->setVisible( true );
    SW_EXPECT_TRUE( registry.hasDirty() );
    sw::vector<uint32> listSlot;
    registry.consumeDirty( listSlot );
    SW_EXPECT_EQUAL( size_t( 1 ), listSlot.size() );
    SW_EXPECT_FALSE( registry.hasDirty() );

    // 2) 스레드 여덟이 전부를 동시에, 여러 번 찍는다 — 슬롯은 한 번씩만 나오고 소비한 뒤엔 남는 게 없다.
    constexpr uint32        kThreadCount = 8;
    constexpr uint32        kRepeat      = 256;
    sw::atomic<bool>        bGo{ false };
    sw::vector<std::thread> listThread;
    listThread.reserve( kThreadCount );
    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&registry, &listMesh, &bGo]()
        {
            while ( bGo.load( std::memory_order_acquire ) == false )
                std::this_thread::yield();
            for ( uint32 repeat = 0; repeat < kRepeat; ++repeat )
            {
                for ( sw::MeshComponent* pMesh : listMesh )
                    registry.markDirty( pMesh );
            }
        } );
    }
    bGo.store( true, std::memory_order_release );
    for ( std::thread& thread : listThread )
        thread.join();

    SW_EXPECT_TRUE( registry.hasDirty() );
    registry.consumeDirty( listSlot );
    SW_EXPECT_EQUAL( size_t( kMeshCount ), listSlot.size() );
    sw::vector<uint8> listSeen( kMeshCount, 0 );
    for ( uint32 slot : listSlot )
    {
        SW_ASSERT_TRUE( slot < kMeshCount );
        SW_EXPECT_EQUAL( uint8( 0 ), listSeen[slot] );
        listSeen[slot] = 1;
    }
    SW_EXPECT_FALSE( registry.hasDirty() );
    registry.consumeDirty( listSlot );
    SW_EXPECT_TRUE( listSlot.empty() );
}

/**
 * @brief [GpuSceneTest] 프리미티브가 문턱을 넘으면 수집 채우기가 잡으로 나뉘어도 결과는 직렬과 같다
 * @details 워커는 프리미티브 번호 자리에만 쓰고, 앞으로 당기기와 해시는 직렬이다. 인스턴스 수와 위치의 합이
 *          직렬 씬과 같은 규칙으로 맞아야 하고, 전부 움직인 뒤(부분 수집 불가 → 다시 전체 수집) 도 그래야 한다.
 */
SW_TEST_CASE( GpuSceneTest, ParallelCollectMatchesSerial )
{
    auto runScene = [&]( uint32 primitiveCount )
    {
        sw::Scene scene( "GpuSceneParallelCollect" );
        SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
        sw::GameObjectManager* pObjects = scene.getObjectManager();
        SW_ASSERT_NOT_NULL( pObjects );
        sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
        SW_ASSERT_NOT_NULL( cube.get() );

        for ( uint32 index = 0; index < primitiveCount; ++index )
        {
            sw::GameObject* pObject = pObjects->createGameObject( sw::hashed_string( ( sw::string( "PC" ) + sw::to_string( index ) ).c_str() ) );
            SW_ASSERT_NOT_NULL( pObject );
            sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
            SW_ASSERT_NOT_NULL( pMesh );
            pMesh->setMesh( cube );
            pMesh->setLocalPosition( sw::float3( static_cast<float32>( index ), 0.0f, -1.0f ) );
            pMesh->setVisible( true );
        }

        sw::GpuSceneBuilder builder;
        const sw::float3    camPos{ 0.0f, 0.0f, 0.0f };
        builder.buildFromScene( &scene, camPos );
        SW_ASSERT_EQUAL( primitiveCount, static_cast<uint32>( builder.getInstances().size() ) );

        // 위치 x 는 0..N-1 이 한 번씩 — 합과 범위로 본다(순서는 배치 정렬이 바꿔도 된다).
        auto sumOfX = [&]() -> float64
        {
            float64 sum = 0.0;
            for ( const sw::GpuInstance& inst : builder.getInstances() )
                sum += static_cast<float64>( inst._boundsCenter._x );
            return sum;
        };
        const float64 expectedSum = static_cast<float64>( primitiveCount ) * static_cast<float64>( primitiveCount - 1 ) * 0.5;
        SW_EXPECT_NEAR_EQUAL( expectedSum, sumOfX(), 0.5 );

        // 전부 옮기면 부분 수집이 안 되어 다시 전체 수집(병렬)이다 — 합이 N 만큼 밀린다.
        for ( sw::MeshComponent* pMesh : pObjects->getPrimitiveRegistry().getAll() )
            pMesh->setLocalPosition( pMesh->getLocalPosition() + sw::float3( 1.0f, 0.0f, 0.0f ) );
        builder.buildFromScene( &scene, camPos );
        SW_ASSERT_EQUAL( primitiveCount, static_cast<uint32>( builder.getInstances().size() ) );
        SW_EXPECT_NEAR_EQUAL( expectedSum + static_cast<float64>( primitiveCount ), sumOfX(), 0.5 );
    };

    runScene( 64 );
    runScene( sw::GpuSceneBuilder::kParallelCollectPrimitiveCount + 29 );
}

/**
 * @brief [GpuSceneTest] 투명 순서가 매 프레임 바뀌는 씬에서 **이어 지은 결과가 처음 보는 빌더가 지은 결과와 같다**.
 * @details 투명 순서만 바뀐 프레임은 불투명 접두부를 제자리 갱신하고 투명 꼬리만 다시 짓는다. 그 꼬리는 접두부 청크와 **같은
 *          병렬 구간**(블록 0)에서 돌고, 투명 정렬은 지난 순서에서 삽입 정렬로 출발하며(옮김이 예산을 넘으면 std::sort), 투명
 *          배치 키의 머티리얼은 대표 맵 없이 가른다. 셋 다 "빠른데 값이 다르다" 가 가장 무서운 실패라 칸마다 견준다.
 *          - 불투명은 갱신 청크(512 칸)가 여럿이 되도록 넉넉히 — 꼬리와 청크가 실제로 나란히 돈다.
 *          - 투명은 머티리얼 인스턴스 셋을 번갈아 끼운다 — 합치기가 꺼져 있으면 원소마다 배치 키가 갈린다.
 *          - 한 프레임은 투명의 깊이 순서를 통째로 뒤집는다 — 뒤집힌 쌍이 N^2/2 라 삽입 정렬 예산을 넘긴다.
 *          - 합치기를 끈 판과 켠 판을 모두 본다(투명 키의 머티리얼 판정이 둘로 갈린다).
 *          움직이기만 한 프레임이 실제로 제자리 갱신을 탔는지도 본다 — 전체 재구축이면 전부 더티로 실린다.
 */
SW_TEST_CASE( GpuSceneTest, IncrementalTransparentTailMatchesFreshBuild )
{
    auto runScene = [&]( bool bMerge )
    {
        sw::Scene scene( "GpuSceneTailEquivalence" );
        SW_EXPECT_TRUE( scene.ensureDefaultCameras() );
        sw::GameObjectManager* pObjects = scene.getObjectManager();
        SW_ASSERT_NOT_NULL( pObjects );
        sw::shared_ptr<sw::Mesh> cube = sw::MeshUtil::createUnitCube();
        SW_ASSERT_NOT_NULL( cube.get() );

        sw::shared_ptr<sw::Material> glass = sw::Material::create();
        SW_ASSERT_TRUE( glass->loadFromFile( "engine/materials/glassmaterial.material" ) );
        constexpr uint32                     kInstanceCount = 3;
        sw::shared_ptr<sw::MaterialInstance> arrInstance[kInstanceCount];
        for ( uint32 instanceIndex = 0; instanceIndex < kInstanceCount; ++instanceIndex )
            arrInstance[instanceIndex] = sw::MaterialInstance::create( glass.get() );

        constexpr uint32               kOpaqueCount      = 1500;
        constexpr uint32               kTransparentCount = 300;
        sw::vector<sw::MeshComponent*> listMesh;
        for ( uint32 index = 0; index < kOpaqueCount + kTransparentCount; ++index )
        {
            sw::GameObject* pObject = pObjects->createGameObject( sw::hashed_string( ( sw::string( "Tail" ) + sw::to_string( index ) ).c_str() ) );
            SW_ASSERT_NOT_NULL( pObject );
            sw::MeshComponent* pMesh = pObject->addComponent<sw::MeshComponent>();
            SW_ASSERT_NOT_NULL( pMesh );
            pMesh->setMesh( cube );
            pMesh->setVisible( true );
            if ( index >= kOpaqueCount )
            {
                pMesh->setMaterial( glass.get() );
                pMesh->setMaterialInstance( arrInstance[index % kInstanceCount] );
            }
            listMesh.push_back( pMesh );
        }

        // 프레임마다 전부 움직인다(벤치의 사인파와 같은 모양) — 투명은 카메라 거리가 바뀌어 순서가 섞인다.
        auto placeAll = [&]( uint32 frame, bool bReverseTransparent )
        {
            for ( uint32 index = 0; index < static_cast<uint32>( listMesh.size() ); ++index )
            {
                const float32 wave = sw::MathUtil::sin( static_cast<float32>( frame ) * 0.9f + static_cast<float32>( index ) * 0.37f );
                if ( index < kOpaqueCount )
                {
                    listMesh[index]->setLocalPosition( sw::float3( static_cast<float32>( index % 40 ), wave, -static_cast<float32>( index / 40 ) - 1.0f ) );
                    continue;
                }
                const uint32  order = bReverseTransparent ? ( kOpaqueCount + kTransparentCount - 1 - index ) : ( index - kOpaqueCount );
                const float32 depth = -5.0f - static_cast<float32>( order ) * 0.05f;
                listMesh[index]->setLocalPosition( sw::float3( static_cast<float32>( index % 7 ) * 0.1f, wave * 0.75f, depth ) );
            }
        };

        struct ElementIdentity
        {
            const sw::Material*         _pMaterial{ nullptr };
            const sw::MaterialInstance* _pInstance{ nullptr };
        };
        // 원소 인덱스는 빌더마다 처음 본 순서로 매겨지므로(영속 ID) 숫자가 아니라 **가리키는 (머티리얼, 인스턴스)** 로 견준다.
        auto resolveElement = []( const sw::GpuSceneBuilder& builder, const sw::GpuInstance& instance ) -> ElementIdentity
        {
            const uint32            opaqueBatchCount = static_cast<uint32>( builder.getOpaqueBatches().size() );
            const sw::GpuMeshBatch& batch            = ( instance._meshBatchIndex < opaqueBatchCount )
                                                         ? builder.getOpaqueBatches()[instance._meshBatchIndex]
                                                         : builder.getTransparentBatches()[instance._meshBatchIndex - opaqueBatchCount];
            if ( batch._materialGroup >= builder.getMaterialGroups().size() )
                return ElementIdentity{};
            const sw::GpuMaterialGroup& group = builder.getMaterialGroups()[batch._materialGroup];
            if ( instance._materialIndex >= group._listEntry.size() )
                return ElementIdentity{};
            return ElementIdentity{ group._listEntry[instance._materialIndex]._material.get(), group._listEntry[instance._materialIndex]._instance.get() };
        };
        auto expectSameBatches = []( const sw::vector<sw::GpuMeshBatch>& listExpected, const sw::vector<sw::GpuMeshBatch>& listActual )
        {
            SW_ASSERT_EQUAL( static_cast<uint32>( listExpected.size() ), static_cast<uint32>( listActual.size() ) );
            for ( size_t batchIndex = 0; batchIndex < listExpected.size(); ++batchIndex )
            {
                SW_EXPECT_EQUAL( listExpected[batchIndex]._instanceBase, listActual[batchIndex]._instanceBase );
                SW_EXPECT_EQUAL( listExpected[batchIndex]._instanceCount, listActual[batchIndex]._instanceCount );
                SW_EXPECT_TRUE( listExpected[batchIndex]._mesh.get() == listActual[batchIndex]._mesh.get() );
                SW_EXPECT_TRUE( listExpected[batchIndex]._material.get() == listActual[batchIndex]._material.get() );
                SW_EXPECT_TRUE( listExpected[batchIndex]._materialInstance.get() == listActual[batchIndex]._materialInstance.get() );
            }
        };

        sw::GpuSceneBuilder persistent;
        persistent.setMergeBatchesAcrossMaterials( bMerge );
        sw::GpuSceneSnapshot snapshot;
        const sw::float3     camPos{ 0.0f, 0.0f, 0.0f };
        constexpr uint32     kFrameCount   = 6;
        constexpr uint32     kReverseFrame = 3;
        for ( uint32 frame = 0; frame < kFrameCount; ++frame )
        {
            placeAll( frame, frame >= kReverseFrame );
            // 이어 짓는 쪽이 먼저다 — 등록부의 더티 표시는 먼저 지은 빌더가 가져간다(처음 보는 빌더는 어차피 전부 모은다).
            persistent.buildFromScene( &scene, camPos );
            persistent.exportCpuSnapshot( snapshot );
            if ( frame > 0 )
            {
                // 배치 키는 그대로다 — 제자리 갱신(+ 꼬리 다시 짓기)이어야 하고, 그러면 전부 더티가 아니라 구간으로 실린다.
                SW_EXPECT_TRUE( snapshot._bAllInstancesDirty == SW_FALSE );
                SW_EXPECT_TRUE( snapshot._listDirtyInstanceRun.empty() == false );
            }

            sw::GpuSceneBuilder fresh;
            fresh.setMergeBatchesAcrossMaterials( bMerge );
            fresh.buildFromScene( &scene, camPos );

            const sw::vector<sw::GpuInstance>& listExpected = fresh.getInstances();
            const sw::vector<sw::GpuInstance>& listActual   = persistent.getInstances();
            SW_ASSERT_EQUAL( kOpaqueCount + kTransparentCount, static_cast<uint32>( listExpected.size() ) );
            SW_ASSERT_EQUAL( static_cast<uint32>( listExpected.size() ), static_cast<uint32>( listActual.size() ) );
            uint32 mismatchCount = 0;
            for ( size_t slot = 0; slot < listExpected.size(); ++slot )
            {
                const sw::GpuInstance& expected   = listExpected[slot];
                const sw::GpuInstance& actual     = listActual[slot];
                const bool             bSameValue = sw::Memory::compare( &expected._world, &actual._world, sizeof( expected._world ) ) == 0 &&
                                        sw::Memory::compare( &expected._boundsCenter, &actual._boundsCenter, sizeof( expected._boundsCenter ) ) == 0 &&
                                        expected._boundsRadius == actual._boundsRadius && expected._meshBatchIndex == actual._meshBatchIndex &&
                                        expected._blendMode == actual._blendMode && expected._spinSeed == actual._spinSeed;
                const ElementIdentity expectedElement = resolveElement( fresh, expected );
                const ElementIdentity actualElement   = resolveElement( persistent, actual );
                const bool            bSameElement    = expectedElement._pMaterial == actualElement._pMaterial && expectedElement._pInstance == actualElement._pInstance;
                if ( bSameValue == false || bSameElement == false )
                    ++mismatchCount;
            }
            SW_EXPECT_EQUAL( 0u, mismatchCount );
            expectSameBatches( fresh.getOpaqueBatches(), persistent.getOpaqueBatches() );
            expectSameBatches( fresh.getTransparentBatches(), persistent.getTransparentBatches() );
        }
    };

    runScene( false );
    runScene( true );
}
