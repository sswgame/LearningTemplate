/**
 * @file TestRHIStructuredBufferGrowth.cpp
 * @brief 구조 버퍼가 개수가 늘 때 늘림 정책(1.5 배)으로 다시 만들어 프레임마다 다시 만들지 않는가, 만들 때 내용을 한 번만 올리는가입니다.
 * @details 디바이스 · 팩토리는 가짜(`RHIFakeDevice.h`)다. 만든 수 · 원소 수 · 구조 버퍼 갱신 수만 본다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

/**
 * @brief [RHIStructuredBufferGrowthTest] 처음은 요청 그대로 만들고, 모자라면 1.5 배 이상으로 키워 한 개씩 늘어나는 동안 다시 만들지 않는다
 * @details 변이: 늘림을 빼고 요청 개수 그대로 만들면 11 · 12 · … 마다 다시 만들어 만든 수 단언이 진다.
 */
SW_TEST_CASE( RHIStructuredBufferGrowthTest, GrowsGeometricallyWhenTheCountRises )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;
    RHIStructuredBuffer buffer;

    SW_REQUIRE_TRUE( buffer.ensureCapacity( &device, sizeof( uint32 ), 10, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) );
    SW_EXPECT_EQUAL( 1u, factory._structuredCreateCount );
    SW_EXPECT_EQUAL( 10u, factory._lastStructuredElementCount );

    for ( uint32 count = 11; count <= 15; ++count ) // 프레임마다 하나씩 는다
    {
        SW_REQUIRE_TRUE( buffer.ensureCapacity( &device, sizeof( uint32 ), count, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) );
    }
    SW_EXPECT_EQUAL( 2u, factory._structuredCreateCount );
    SW_EXPECT_EQUAL( 15u, buffer._capacityElements );

    SW_REQUIRE_TRUE( buffer.ensureCapacity( &device, sizeof( uint32 ), 100, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, nullptr ) );
    SW_EXPECT_EQUAL( 100u, buffer._capacityElements ); // 크게 뛰면 요청 그대로
    SW_EXPECT_EQUAL( 0u, factory._structuredUpdateCount );
}

/**
 * @brief [RHIStructuredBufferGrowthTest] 키워 만든 버퍼의 초기 내용은 요청한 원소 수만큼 한 번 올린다(용량 전체를 읽지 않는다)
 * @details 변이: 키운 용량 크기로 초기 내용을 넘기면 원본 배열 밖을 읽는다(ASan). 초기 내용을 두 번 올리면 갱신 수 단언이 진다.
 */
SW_TEST_CASE( RHIStructuredBufferGrowthTest, InitialDataIsUploadedOnceForTheRequestedCount )
{
    test::FakeRHIBufferFactory factory;
    test::FakeRHIDevice        device;
    device._pResourceFactory = &factory;
    RHIStructuredBuffer  buffer;
    const vector<uint32> listValue( 12, 7u );

    SW_REQUIRE_TRUE( buffer.ensureCapacity( &device, sizeof( uint32 ), 10, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, listValue.data() ) );
    SW_EXPECT_EQUAL( 1u, factory._structuredUpdateCount );
    SW_REQUIRE_TRUE( buffer.ensureCapacity( &device, sizeof( uint32 ), 12, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, listValue.data() ) );
    SW_EXPECT_EQUAL( 15u, buffer._capacityElements );
    SW_EXPECT_EQUAL( 2u, factory._structuredUpdateCount );
}
