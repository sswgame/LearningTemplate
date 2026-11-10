// MeshRenderDataTest — 메시의 GPU 버퍼(렌더 프록시 `MeshRenderData`)가 메시 수명에 묶여 디바이스 수명 통보를 받는지. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshRenderData.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

/**
 * @brief [MeshRenderDataTest] 메시가 태어날 때 프록시가 통보 목록에 들고, 메시가 사라지면 함께 빠지는지 — 에셋 해제 ↔ GPU 자원 해제가 한 수명인지 검증
 */
SW_TEST_CASE( MeshRenderDataTest, ProxyLivesAndDiesWithTheMesh )
{
    const uint32             before = sw::RHIRenderResource::getRegisteredCount();
    sw::shared_ptr<sw::Mesh> mesh   = sw::Mesh::create();
    SW_EXPECT_EQUAL( before + 1u, sw::RHIRenderResource::getRegisteredCount() );
    sw::MeshRenderData* pRenderData = &mesh->getRenderData();
    SW_EXPECT_FALSE( pRenderData->isRHIValid() );

    // 내용을 바꿔도(핫 리로드 · 지형 LOD 교체) 프록시는 그대로이고 버퍼만 놓인다 — 다음 initRHI 가 새 내용을 올린다.
    SW_REQUIRE_TRUE( mesh->setGeometry( sw::vector<sw::RHIVertex>( 3 ), sw::vector<uint32>{ 0, 1, 2 } ) );
    SW_EXPECT_TRUE( pRenderData == &mesh->getRenderData() );
    SW_EXPECT_FALSE( pRenderData->isRHIValid() );

    // 리소스 팩터리가 없는 디바이스는 올리지 못한다 — 실패해도 핸들이 남지 않는다.
    sw::test::FakeRHIDevice device;
    SW_EXPECT_FALSE( pRenderData->initRHI( &device ) );
    SW_EXPECT_FALSE( pRenderData->isRHIValid() );

    mesh.reset();
    SW_EXPECT_EQUAL( before, sw::RHIRenderResource::getRegisteredCount() );
}
