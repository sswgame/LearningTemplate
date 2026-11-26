/**
 * @file TestRHIBufferDesc.cpp
 * @brief 범용 버퍼 서술(`RHIBufferDesc`)이 비거나 틀리면 값을 지어내지 않고 거절되는가 · 장치 기동 서술(`RHIDeviceDesc`)의 깃발이 꺼진 채로 서는가입니다(nogpu).
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

/**
 * @brief [RHIBufferDescTest] 빈 서술 · 곱이 맞지 않는 크기 · 다른 종류(정점 · 인덱스 · 상수)는 거절한다
 * @details 변이: 옛 기본 구현(원소 0 → 4 바이트 · 1 개)으로 되돌리면 빈 서술이 맞는 서술이 되어 첫 단언이 진다.
 */
SW_TEST_CASE( RHIBufferDescTest, InvalidDescIsRejectedWithoutInventingValues )
{
    SW_EXPECT_LOG( "RHI.BufferDescSize" );
    SW_EXPECT_LOG( "RHI.BufferDescOtherKind" );
    SW_EXPECT_FALSE( IRHIResourceFactory::isValidBufferDesc( RHIBufferDesc{}, "createBuffer" ) );

    RHIBufferDesc mismatched{};
    mismatched._elementSize  = 16;
    mismatched._elementCount = 4;
    mismatched._sizeBytes    = 60;
    mismatched._usage        = RHIBufferUsage::Structured;
    SW_EXPECT_FALSE( IRHIResourceFactory::isValidBufferDesc( mismatched, "createBuffer" ) );

    RHIBufferDesc constant{};
    constant._elementSize  = 16;
    constant._elementCount = 4;
    constant._sizeBytes    = 64;
    constant._usage        = RHIBufferUsage::Constant;
    SW_EXPECT_FALSE( IRHIResourceFactory::isValidBufferDesc( constant, "createBuffer" ) );
}

/**
 * @brief [RHIBufferDescTest] 맞는 서술은 그 원소 크기 · 수 그대로 구조 버퍼를 만든다
 */
SW_TEST_CASE( RHIBufferDescTest, ValidDescCreatesTheExactStructuredBuffer )
{
    test::FakeRHIBufferFactory factory;
    RHIBufferDesc              desc{};
    desc._elementSize  = 16;
    desc._elementCount = 4;
    desc._sizeBytes    = 64;
    desc._usage        = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess;
    SW_EXPECT_NOT_EQUAL( RHIBufferHandle{ 0 }, factory.createBufferNative( desc ) );
    SW_EXPECT_EQUAL( 1u, factory._structuredCreateCount );
    SW_EXPECT_EQUAL( 4u, factory._lastStructuredElementCount );
}

/**
 * @brief [RHIBufferDescTest] 장치 기동 서술은 진단 깃발이 모두 꺼진 채로 서고, 주 스왑체인 서술을 따로 든다
 */
SW_TEST_CASE( RHIBufferDescTest, DeviceDescStartsWithFlagsOff )
{
    const RHIDeviceDesc desc{};
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( desc._bSoftwareAdapter ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( desc._bValidationLayer ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( desc._bDRED ) );
    SW_EXPECT_TRUE( desc._swapChain._pWindowHandle == nullptr );
}
