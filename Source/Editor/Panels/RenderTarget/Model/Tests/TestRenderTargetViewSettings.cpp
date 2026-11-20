#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/RenderTarget/Model/RenderTargetViewSettings.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( RenderTargetViewSettingsTest, DepthPlanesStayPositiveAndOrdered )
{
    RenderTargetViewSettings settings;
    settings.setDepthPlanes( 0.0f, -5.0f );
    SW_EXPECT_TRUE( settings._nearPlane > 0.0f );
    SW_EXPECT_TRUE( settings._farPlane > settings._nearPlane );
    settings.setDepthPlanes( 1.0f, 100.0f );
    SW_EXPECT_EQUAL( 1.0f, settings._nearPlane );
    SW_EXPECT_EQUAL( 100.0f, settings._farPlane );
}

SW_TEST_CASE( RenderTargetViewSettingsTest, ExposureScalesColorButNotAlpha )
{
    // R32G32B32A32 한 픽셀: 4.0 (HDR), 0.25, 0, 알파 0.5.
    const float32 arrPixel[4]{ 4.0f, 0.25f, 0.0f, 0.5f };
    vector<uint8> byte( sizeof( arrPixel ) );
    Memory::copy( byte.data(), arrPixel, sizeof( arrPixel ) );
    RHITextureMipSpan layout{};
    layout._width    = 1;
    layout._height   = 1;
    layout._rowBytes = sizeof( arrPixel );

    RenderTargetViewSettings settings;
    RenderTargetPreviewImage image;
    SW_REQUIRE_TRUE( RenderTargetViewSettings::makePreviewImage( byte, layout, RHIFormat::R32G32B32A32_FLOAT, false, settings, image ) );
    SW_EXPECT_EQUAL( uint8{ 255 }, image._byte[0] ); // 노출 0 이면 4.0 은 1 로 잘린다
    settings.setExposure( -2.0f );                   // 1/4 배 — 4.0 이 1 이 되고 0.25 는 0.0625
    SW_REQUIRE_TRUE( RenderTargetViewSettings::makePreviewImage( byte, layout, RHIFormat::R32G32B32A32_FLOAT, false, settings, image ) );
    SW_EXPECT_EQUAL( uint8{ 255 }, image._byte[0] );
    SW_EXPECT_EQUAL( uint8{ 16 }, image._byte[1] );
    settings._channel = RenderTargetChannel::Alpha; // 알파는 노출을 받지 않는다
    SW_REQUIRE_TRUE( RenderTargetViewSettings::makePreviewImage( byte, layout, RHIFormat::R32G32B32A32_FLOAT, false, settings, image ) );
    SW_EXPECT_EQUAL( uint8{ 128 }, image._byte[0] );
    SW_EXPECT_TRUE( settings.needsSnapshotImage( false ) );
}

SW_TEST_CASE( RenderTargetViewSettingsTest, ExposureClampsAndMipExtentStaysPositive )
{
    RenderTargetViewSettings settings;
    SW_EXPECT_FALSE( settings.needsSnapshotImage( false ) ); // RGBA · 노출 0 은 GPU 텍스처 그대로
    SW_EXPECT_TRUE( settings.needsSnapshotImage( true ) );   // 깊이는 늘 스냅숏
    settings.setExposure( 100.0f );
    SW_EXPECT_EQUAL( RenderTargetViewSettings::kMaxExposure, settings._exposure );
    SW_EXPECT_EQUAL( 480u, RenderTargetViewSettings::getMipExtent( 1920u, 2u ) );
    SW_EXPECT_EQUAL( 1u, RenderTargetViewSettings::getMipExtent( 3u, 5u ) );
    SW_EXPECT_EQUAL( 1u, RenderTargetViewSettings::getMipExtent( 3u, 40u ) );
}
