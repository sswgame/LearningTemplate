/**
 * @file TestRHITextureSlot.cpp
 * @brief 텍스처 칸(`RHITextureSlot`)이 같은 모양이면 다시 만들지 않고, 뷰 하나라도 못 만들면 아무것도 남기지 않고, 뷰를 먼저 놓는가입니다(가짜 디바이스).
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHIConstantBuffer.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIGeometryBuffer.h"
#include "Engine/Graphics/RHI/RHIPipelineState.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Graphics/RHI/RHITextureSlot.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

namespace
{
    /** @brief SRV · UAV 를 가진 렌더 타깃 서술입니다. */
    RHITextureDesc makeDesc( uint32 width )
    {
        RHITextureDesc desc{};
        desc._width              = width;
        desc._height             = 16;
        desc._format             = RHIFormat::R16G16B16A16_FLOAT;
        desc._bIsRenderTarget    = SW_TRUE;
        desc._bIsShaderResource  = SW_TRUE;
        desc._bIsUnorderedAccess = SW_TRUE;
        return desc;
    }
} // namespace

/**
 * @brief [RHITextureSlotTest] 같은 모양이면 그대로 두고 크기 · 뷰가 다르면 다시 만든다. 뷰를 먼저 놓고 텍스처를 놓는다
 * @details 변이: 크기 비교를 빼면 다시 만든 핸들 단언이, 텍스처를 먼저 놓으면 차례 단언이 진다.
 */
SW_TEST_CASE( RHITextureSlotTest, RecreatesOnlyWhenTheShapeChanges )
{
    test::FakeRHIBufferFactory factory;
    factory._nextTexture = 1;
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;
    RHITextureSlot slot;
    SW_REQUIRE_TRUE( slot.ensure( device, makeDesc( 32 ), "Slot" ) );
    const RHITextureHandle first = slot._texture;
    SW_EXPECT_TRUE( slot._srv != kInvalidDescriptorIndex );
    SW_EXPECT_TRUE( slot._uav != kInvalidDescriptorIndex );
    SW_REQUIRE_TRUE( slot.ensure( device, makeDesc( 32 ), "Slot" ) );
    SW_EXPECT_TRUE( slot._texture == first );
    SW_EXPECT_TRUE( factory._listReleaseStep.empty() );

    SW_REQUIRE_TRUE( slot.ensure( device, makeDesc( 64 ), "Slot" ) );
    SW_EXPECT_FALSE( slot._texture == first );
    SW_EXPECT_EQUAL( 64u, slot._width );
    RHITextureDesc withoutUAV      = makeDesc( 64 );
    withoutUAV._bIsUnorderedAccess = SW_FALSE;
    SW_REQUIRE_TRUE( slot.ensure( device, withoutUAV, "Slot" ) );
    SW_EXPECT_TRUE( slot._uav == kInvalidDescriptorIndex );

    factory._listReleaseStep.clear();
    slot.release( &device );
    SW_EXPECT_FALSE( slot.isValid() );
    SW_REQUIRE_FALSE( factory._listReleaseStep.empty() );
    SW_EXPECT_EQUAL( string( "srv" ), string( factory._listReleaseStep[0] ) ); // 뷰가 텍스처보다 먼저
}

/**
 * @brief [RHITextureSlotTest] 요청한 뷰 하나라도 등록하지 못하면 텍스처까지 놓고 false 다 — 반쪽 칸이 남지 않는다
 * @details 변이: 실패 때 놓지 않으면 `isValid` 단언이 진다.
 */
SW_TEST_CASE( RHITextureSlotTest, PartialViewFailureLeavesNothing )
{
    test::FakeRHIBufferFactory factory;
    factory._nextTexture     = 1;
    factory._bFailTextureUAV = true;
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;
    RHITextureSlot slot;
    SW_EXPECT_FALSE( slot.ensure( device, makeDesc( 32 ), "Slot" ) );
    SW_EXPECT_FALSE( slot.isValid() );
    SW_EXPECT_TRUE( slot._srv == kInvalidDescriptorIndex );

    test::FakeRHIBufferFactory failing; // 텍스처를 만들지 못한다
    device._pResourceFactory = &failing;
    SW_EXPECT_FALSE( slot.ensure( device, makeDesc( 32 ), "Slot" ) );
    SW_EXPECT_FALSE( slot.isValid() );
}

/** @brief [RHITextureSlotTest] 소유 형식은 옮기기만 되고, 옮긴 원본은 비어 두 번 놓지 않는다. 변이: 옮기기가 원본을 비우지 않으면 놓기가 두 번 불려 진다. */
SW_TEST_CASE( RHITextureSlotTest, MoveLeavesTheSourceEmpty )
{
    static_assert( std::is_copy_constructible_v<RHITextureSlot> == false && std::is_copy_constructible_v<RHIStructuredBuffer> == false );
    static_assert( std::is_copy_constructible_v<RHIConstantBuffer> == false && std::is_copy_constructible_v<RHIGeometryBuffer> == false );
    static_assert( std::is_copy_constructible_v<RHIPipelineState> == false );
    static_assert( std::is_default_constructible_v<RHIResourceCreateKey> == false && std::is_aggregate_v<RHIResourceCreateKey> == false );

    RHITextureSlot slot;
    slot._texture = RHITextureHandle{ 5 };
    slot._srv     = RHIDescriptorIndex{ 3 };
    RHITextureSlot moved{ std::move( slot ) };
    SW_EXPECT_FALSE( slot.isValid() );
    SW_EXPECT_EQUAL( kInvalidDescriptorIndex, slot._srv );
    SW_EXPECT_TRUE( moved._texture == RHITextureHandle{ 5 } );
    moved.release( nullptr ); // 디바이스가 없으면 값만 잊는다
}
