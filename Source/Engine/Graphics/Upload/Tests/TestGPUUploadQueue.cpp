// GPUUploadQueueTest — 게임 스레드가 앞당겨 만드는 메시 업로드: 워커 생성을 못 하는 백엔드는 받지 않고, 같은 메시는 한 번, 비우면 모두 상주하고, 디바이스를 바꾸면 쌓인 요청을 버린다(가짜 RHI — nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Task/TaskManager.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/Upload/GPUUploadQueue.h"

using namespace sw;

namespace
{
    struct GPUUploadQueueTestUtil
    {
        /** @brief 삼각형 하나짜리 메시입니다. */
        static shared_ptr<Mesh> makeTriangle()
        {
            const shared_ptr<Mesh> mesh = Mesh::create( MeshCPUAccess::Keep );
            vector<RHIVertex>      listVertex( 3 );
            listVertex[1]._arrPosition[0] = 1.0f;
            listVertex[2]._arrPosition[1] = 1.0f;
            const bool bSet               = mesh->setGeometry( std::move( listVertex ), vector<uint32>{ 0, 1, 2 } ); // 값 셋 · 인덱스 셋 — 늘 맞다
            SW_EXPECT_TRUE( bSet );
            return mesh;
        }
    };
} // namespace

/** @brief [GPUUploadQueueTest] 워커 생성을 허용하지 않는 백엔드(OpenGL)면 요청을 받지 않는다 — 렌더 스레드가 만든다 */
SW_TEST_CASE( GPUUploadQueueTest, BackendWithoutWorkerCreationTakesNothing )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory    = &factory;
    const shared_ptr<Mesh> mesh = GPUUploadQueueTestUtil::makeTriangle();
    TaskManager            taskManager; // 시작하지 않았다 — 병렬 본문은 이 스레드에서 돈다
    GPUUploadQueue         queue;
    queue.bindDevice( &device, &taskManager );
    SW_EXPECT_FALSE( queue.shouldCreateMeshes() );
    queue.requestMesh( mesh );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    SW_EXPECT_EQUAL( 0u, queue.flush() );
    SW_EXPECT_FALSE( mesh->getRenderData().isRHIValid() );
}

/** @brief [GPUUploadQueueTest] 같은 메시의 되풀이 요청 · 빈 메시 · 없는 메시는 세지 않고, 비우면 모두 상주하며, 상주한 메시는 다시 받지 않는다 */
SW_TEST_CASE( GPUUploadQueueTest, FlushCreatesEachRequestedMeshOnce )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory            = &factory;
    device._bThreadSafeResourceCreation = SW_TRUE;
    const shared_ptr<Mesh> first        = GPUUploadQueueTestUtil::makeTriangle();
    const shared_ptr<Mesh> second       = GPUUploadQueueTestUtil::makeTriangle();
    const shared_ptr<Mesh> empty        = Mesh::create( MeshCPUAccess::Keep );
    TaskManager            taskManager;
    GPUUploadQueue         queue;
    queue.bindDevice( &device, &taskManager );
    SW_EXPECT_TRUE( queue.shouldCreateMeshes() );
    queue.requestMesh( first );
    queue.requestMesh( first );
    queue.requestMesh( second );
    queue.requestMesh( empty );
    queue.requestMesh( nullptr );
    SW_EXPECT_EQUAL( 2u, queue.getPendingCount() );
    SW_EXPECT_EQUAL( 2u, queue.flush() );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    SW_EXPECT_TRUE( first->getRenderData().isRHIValid() );
    SW_EXPECT_TRUE( second->getRenderData().isRHIValid() );

    queue.requestMesh( first );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    SW_EXPECT_EQUAL( 0u, queue.flush() );
}

/** @brief [GPUUploadQueueTest] 디바이스를 다시 걸면 쌓인 요청을 버리고 같은 메시를 새로 받으며, 풀면(nullptr) 받지 않는다 */
SW_TEST_CASE( GPUUploadQueueTest, RebindingDropsOldRequests )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory            = &factory;
    device._bThreadSafeResourceCreation = SW_TRUE;
    const shared_ptr<Mesh> mesh         = GPUUploadQueueTestUtil::makeTriangle();
    TaskManager            taskManager;
    GPUUploadQueue         queue;
    queue.bindDevice( &device, &taskManager );
    queue.requestMesh( mesh );
    SW_EXPECT_EQUAL( 1u, queue.getPendingCount() );
    queue.bindDevice( &device, &taskManager );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    queue.requestMesh( mesh ); // 거르는 표도 비었다
    SW_EXPECT_EQUAL( 1u, queue.getPendingCount() );

    queue.bindDevice( nullptr, nullptr );
    SW_EXPECT_FALSE( queue.shouldCreateMeshes() );
    queue.requestMesh( mesh );
    SW_EXPECT_EQUAL( 0u, queue.getPendingCount() );
    SW_EXPECT_EQUAL( 0u, queue.flush() );
    SW_EXPECT_FALSE( mesh->getRenderData().isRHIValid() );
}
