/**
 * @file TestGPUMeshVertexPoolFence.cpp
 * @brief 정점 풀이 나간 메시의 구간을 GPU 펜스가 끝난 뒤에만 다시 내주는가 — `update` 를 부른 수로 짐작하지 않는다.
 * @details 디바이스는 가짜(`test::FakeRHIDevice` — `notifyFrameCompletion` 을 쌓기만 한다)이고 팩토리는 `test::FakeRHIBufferFactory` 다. 쌓인 통지를 `Completed` 로 부르는 것이 "GPU 가 끝냈다" 다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/Scene/GPUMeshVertexPool.h"

using namespace sw;

namespace
{
    /** @brief 삼각형 하나짜리 Packed 메시입니다(만들 때마다 내용 토큰이 새것이다). */
    shared_ptr<Mesh> makeTriangle()
    {
        const shared_ptr<Mesh> mesh = Mesh::create();
        const bool             bSet = mesh->setGeometry( vector<RHIVertex>( 3 ), vector<uint32>{ 0, 1, 2 } ); // 값 셋 · 인덱스 셋 — 늘 맞다
        SW_ASSERT( bSet );
        (void)bSet;
        return mesh;
    }
} // namespace

/**
 * @brief [GPUMeshVertexPoolFenceTest] 나간 메시의 정점 구간은 몇 프레임이 지나도 GPU 가 알리기 전에는 새 메시에 가지 않고, 알린 뒤에는 간다
 * @details 변이: 대기 키를 "부른 수 + 인플라이트 프레임 수" 로 되돌리면 넷째 `update` 앞의 프레임 셋에서 반납 자리(0)가 새 메시에 가 첫 단언 묶음이 진다.
 */
SW_TEST_CASE( GPUMeshVertexPoolFenceTest, RetiredSpanWaitsForTheGPUFence )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory     = &factory;
    const shared_ptr<Mesh> meshA = makeTriangle();
    const shared_ptr<Mesh> meshB = makeTriangle();

    GPUMeshVertexPool pool{ RHIMeshVertexFormat::Packed };
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ meshA.get() } ) );
    SW_REQUIRE_EQUAL( 0u, pool.getPlacementOf( meshA.get() )._baseVertex );
    SW_REQUIRE_TRUE( pool.update( &device, vector<Mesh*>{ meshB.get() } ) ); // A 가 나간다 — 구간 0 은 이 펜스 값을 기다린다
    const uint32 baseB = pool.getPlacementOf( meshB.get() )._baseVertex;
    SW_EXPECT_NOT_EQUAL( 0u, baseB );

    // 프레임이 여럿 지나도(인플라이트 상한보다 많이) GPU 가 끝냈다고 알리지 않았으면 구간 0 은 다시 나가지 않는다.
    vector<shared_ptr<Mesh>> listKeep;
    vector<Mesh*>            listMesh{ meshB.get() };
    for ( uint32 frame = 0; frame < 6; ++frame )
    {
        listKeep.push_back( makeTriangle() );
        listMesh.push_back( listKeep.back().get() );
        (void)pool.update( &device, listMesh );
        SW_EXPECT_NOT_EQUAL( 0u, pool.getPlacementOf( listKeep.back().get() )._baseVertex );
    }

    // GPU 가 지금까지 낸 프레임을 모두 끝냈다 — 다음 `update` 가 구간 0 을 돌려받아 새 메시에 준다(가장 잘 맞는 빈 구간).
    for ( const RHIFrameCompletionDelegate& completion : device._listFrameCompletion )
    {
        completion( RHIFrameCompletion::Completed );
    }
    const shared_ptr<Mesh> meshLate = makeTriangle();
    listMesh.push_back( meshLate.get() );
    SW_REQUIRE_TRUE( pool.update( &device, listMesh ) );
    SW_EXPECT_EQUAL( 0u, pool.getPlacementOf( meshLate.get() )._baseVertex );
}
