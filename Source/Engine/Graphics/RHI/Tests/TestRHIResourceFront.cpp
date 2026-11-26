/**
 * @file TestRHIResourceFront.cpp
 * @brief 자원 생성 · bindless 등록의 공통 입구(`IRHIResourceFactory`)가 백엔드에 닿기 전에 거절하는 것입니다(nogpu). 다섯 백엔드가 이 입구 하나를 지난다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/RHIGeometryBuffer.h"
#include "Engine/Graphics/RHI/RHITextureSlot.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

namespace
{
    constexpr RHITextureHandle kTexture{ 7 };
    constexpr RHIBufferHandle  kPage{ 9 };
    constexpr uint32           kPageBytes = 1024;

    RHITextureDesc makeDesc( uint32 width, uint32 height, uint32 mipLevels )
    {
        RHITextureDesc desc{};
        desc._width     = width;
        desc._height    = height;
        desc._mipLevels = mipLevels;
        return desc;
    }
} // namespace

/**
 * @brief [RHIResourceFrontTest] 텍스처 모양: 크기 0 · 밉 0 · 체인보다 긴 밉 · 맞지 않는 면 수는 서술이 틀렸다
 * @details 변이: 밉 0 을 받으면 D3D11 은 전체 체인, 나머지는 밉 하나를 만든다 — 둘째 단언이 진다.
 */
SW_TEST_CASE( RHIResourceFrontTest, TextureShapeNamesOneMeaningForEveryBackend )
{
    SW_EXPECT_TRUE( isRHITextureShapeValid( makeDesc( 64, 16, 7 ) ) );
    SW_EXPECT_FALSE( isRHITextureShapeValid( makeDesc( 64, 16, 0 ) ) );
    SW_EXPECT_FALSE( isRHITextureShapeValid( makeDesc( 64, 16, 8 ) ) );
    SW_EXPECT_FALSE( isRHITextureShapeValid( makeDesc( 0, 16, 1 ) ) );
    RHITextureDesc sliced = makeDesc( 64, 64, 1 );
    sliced._arraySize     = 2;
    SW_EXPECT_FALSE( isRHITextureShapeValid( sliced ) );
    sliced._dimension = RHITextureDimension::Texture2DArray;
    SW_EXPECT_TRUE( isRHITextureShapeValid( sliced ) );
}

/**
 * @brief [RHIResourceFrontTest] 틀린 모양 · 빈 크기 · 내용 없는 정점 버퍼는 백엔드에 닿지 않고 같은 로그 ID 로 거절된다
 * @details 변이: 입구의 검사를 지우면 가짜 백엔드가 핸들을 내 유효 단언이 진다.
 */
SW_TEST_CASE( RHIResourceFrontTest, InvalidCreationNeverReachesTheBackend )
{
    SW_EXPECT_LOG( "RHI.TextureShapeInvalid" );
    SW_EXPECT_LOG( "RHI.BufferSizeZero" );
    SW_EXPECT_LOG( "RHI.BufferContentMissing" );
    test::FakeRHIBufferFactory factory;
    factory._nextTexture = 1;
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;

    RHITextureSlot slot;
    SW_EXPECT_FALSE( slot.ensure( device, makeDesc( 8, 8, 0 ), "Slot" ) );
    SW_EXPECT_EQUAL( uint64( 1 ), factory._nextTexture );

    const uint32      value{ 1 };
    RHIGeometryBuffer geometry;
    SW_EXPECT_FALSE( geometry.createVertex( device, nullptr, sizeof( value ) ) );
    SW_EXPECT_FALSE( geometry.createIndex( device, &value, 0 ) );
    SW_EXPECT_FALSE( geometry.createGPUVertex( device, 0 ) );
    SW_EXPECT_TRUE( factory._setLiveBuffer.empty() );
    SW_EXPECT_TRUE( geometry.createVertex( device, &value, sizeof( value ) ) );
    geometry.release( &device );
}

/**
 * @brief [RHIResourceFrontTest] 등록할 수 없는 텍스처(모르는 핸들 · 배열 · SRV 없음 · 첫 업로드 중 · UAV 없음)는 등록 · 다시 묶기 · 활성화가 모두 같은 로그 ID 로 거절한다
 * @details 변이: 검사 하나를 지우면 가짜 백엔드가 번호를 내 무효 단언이 진다(WebGPU 는 형태 검사가 없었고 DX11 · GL · Vulkan 은 UAV 검사가 저마다 달랐다).
 */
SW_TEST_CASE( RHIResourceFrontTest, UnregistrableTextureIsRejectedTheSameWay )
{
    SW_EXPECT_LOG( "RHI.BindlessTextureUnknown" );
    SW_EXPECT_LOG( "RHI.BindlessTextureNotTexture2D" );
    SW_EXPECT_LOG( "RHI.BindlessTextureNotShaderResource" );
    SW_EXPECT_LOG( "RHI.RegisterBeforeTransferDone" );
    SW_EXPECT_LOG( "RHI.BindlessTextureNotUnorderedAccess" );
    test::FakeRHIBufferFactory factory;
    SW_EXPECT_TRUE( factory.registerBindlessTexture( kTexture ) != kInvalidDescriptorIndex );
    SW_EXPECT_TRUE( factory.isTextureReadyToSample( kTexture ) );
    SW_EXPECT_TRUE( factory.registerBindlessTexture( RHITextureHandle{ 0 } ) == kInvalidDescriptorIndex );

    factory._textureInfo._dimension = RHITextureDimension::TextureCube;
    SW_EXPECT_TRUE( factory.registerBindlessTexture( kTexture ) == kInvalidDescriptorIndex );
    SW_EXPECT_FALSE( factory.rebindBindlessTexture( RHITextureSRVIndex{ 1 }, kTexture ) );
    SW_EXPECT_FALSE( factory.activateReservedBindlessTexture( RHITextureSRVIndex{ 1 }, kTexture ) );
    factory._textureInfo._dimension = RHITextureDimension::Texture2D;

    factory._textureInfo._bShaderResource = SW_FALSE;
    SW_EXPECT_TRUE( factory.registerBindlessTexture( kTexture ) == kInvalidDescriptorIndex );
    factory._textureInfo._bShaderResource = SW_TRUE;

    factory._textureInfo._bFirstUploadComplete = SW_FALSE;
    SW_EXPECT_TRUE( factory.registerBindlessTexture( kTexture ) == kInvalidDescriptorIndex );
    SW_EXPECT_FALSE( factory.isTextureReadyToSample( kTexture ) );
    factory._textureInfo._bFirstUploadComplete = SW_TRUE;

    factory._textureInfo._bUnorderedAccess = SW_FALSE;
    SW_EXPECT_TRUE( factory.registerBindlessTextureUAV( kTexture ) == kInvalidDescriptorIndex );
}

/**
 * @brief [RHIResourceFrontTest] 상수 버퍼 구간은 256 B 배수에서 시작하고 페이지 안이어야 한다 — 다섯 백엔드가 같은 규칙이다
 * @details 예전에는 DX11 이 끝을 보지 않았고 GL 은 장치의 정렬만 봤다. 변이: 끝 검사를 지우면 넘는 구간 단언이 진다.
 */
SW_TEST_CASE( RHIResourceFrontTest, ConstantSliceMustLieInsideThePage )
{
    SW_EXPECT_LOG( "RHI.BadConstantBufferSlice" );
    test::FakeRHIBufferFactory factory;
    SW_EXPECT_TRUE( factory.registerBindlessBufferSRV( RHIBufferSlice{ kPage, 0, 256 } ) == kInvalidDescriptorIndex ); // 페이지가 아니다
    factory._constantPageBytes = kPageBytes;
    SW_EXPECT_TRUE( factory.registerBindlessBufferSRV( RHIBufferSlice{ kPage, 256, 768 } ) != kInvalidDescriptorIndex );
    SW_EXPECT_TRUE( factory.registerBindlessBufferSRV( RHIBufferSlice{ kPage, 128, 256 } ) == kInvalidDescriptorIndex );
    SW_EXPECT_TRUE( factory.registerBindlessBufferSRV( RHIBufferSlice{ kPage, 512, 768 } ) == kInvalidDescriptorIndex );
    SW_EXPECT_TRUE( factory.registerBindlessBufferSRV( RHIBufferSlice{ kPage, 0, 0 } ) == kInvalidDescriptorIndex );
    SW_EXPECT_TRUE( factory.registerBindlessBufferSRV( RHIBufferSlice{ kPage, 0xFFFFFF00u, 512 } ) == kInvalidDescriptorIndex ); // 더하기가 32비트를 넘는다
}

/** @brief [RHIResourceFrontTest] 산 번호가 아닌 것의 해제는 어느 백엔드에서든 알린다 — 예전에는 DX11 · GL · WebGPU 가 조용히 지나갔다 */
SW_TEST_CASE( RHIResourceFrontTest, ReleasingAnIndexTwiceIsReported )
{
    SW_EXPECT_LOG( "RHI.BindlessIndexNotOwned" );
    test::FakeRHIBufferFactory factory;
    factory.unregisterBindlessResource( RHIBufferSRVIndex{ 3 } );
    factory._releasedIndex = RHIBufferSRVIndex{ 3 };
    factory.unregisterBindlessResource( RHIBufferSRVIndex{ 3 } );
}
