/**
 * @file TestTransientTextureLeases.cpp
 * @brief 뷰들이 나눠 쓰는 트랜지언트 텍스처 풀입니다 — 다른 뷰는 같은 텍스처를, 같은 뷰는 다른 텍스처를 받고, 마지막 뷰가 돌려줄 때 놓는다(가짜 디바이스).
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/Frame/Resource/TransientTextureLeases.h"

using namespace sw;

/**
 * @brief [TransientTextureLeasesTest] 다른 뷰는 같은 서술의 텍스처를 나눠 쓰고, 같은 뷰의 두 첨부는 나누지 않으며, 서술이 다르면 새로 만든다
 * @details 변이: 빌린 쪽을 보지 않으면 한 뷰의 두 첨부가 같은 텍스처를 받고, 마지막 뷰를 세지 않으면 첫 반납에서 텍스처가 사라진다.
 */
SW_TEST_CASE( TransientTextureLeasesTest, ViewsShareButOneViewDoesNotAlias )
{
    test::FakeRHIBufferFactory factory;
    factory._nextTexture = 1;
    test::FakeRHIDevice device;
    device._pResourceFactory = &factory;
    TransientTextureLeases leases;
    RHITextureDesc         desc{};
    desc._width             = 64;
    desc._height            = 32;
    desc._format            = RHIFormat::R16G16B16A16_FLOAT;
    desc._bIsRenderTarget   = SW_TRUE;
    desc._bIsShaderResource = SW_TRUE;
    const uint32 viewA{ 0 };
    const uint32 viewB{ 0 };

    const uint32 first  = leases.lease( device, desc, &viewA, "A0" );
    const uint32 second = leases.lease( device, desc, &viewA, "A1" );
    SW_EXPECT_TRUE( first != second );
    SW_EXPECT_EQUAL( first, leases.lease( device, desc, &viewB, "B0" ) );
    RHITextureDesc other = desc;
    other._height        = 64;
    SW_EXPECT_TRUE( leases.lease( device, other, &viewB, "B1" ) != second );
    SW_EXPECT_EQUAL( 3u, leases.getLiveCount() );

    leases.release( &device, first, &viewA );
    SW_EXPECT_TRUE( leases.get( first ).isValid() ); // 뷰 B 가 아직 쓴다
    leases.release( &device, first, &viewB );
    SW_EXPECT_FALSE( leases.get( first ).isValid() );
    SW_EXPECT_EQUAL( 2u, leases.getLiveCount() );
}
