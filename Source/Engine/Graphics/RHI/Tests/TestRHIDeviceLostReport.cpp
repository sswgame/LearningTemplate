/**
 * @file TestRHIDeviceLostReport.cpp
 * @brief 장치 손실 보고 한 입구(`IRHIDevice::reportDeviceLost`)가 표시하고 처음 한 번만 보고하는가입니다(nogpu).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"

using namespace sw;

/**
 * @brief [RHIDeviceLostReportTest] 첫 보고가 손실을 표시하고 결함 상세 훅을 한 번 부른다 — 뒤 보고는 아무것도 하지 않는다
 * @details 변이: 표시 여부를 보지 않고 늘 보고하면 훅 호출 수가 2 가 되어 진다. 표시를 빼면 `isDeviceLost` 가 거짓이라 진다.
 */
SW_TEST_CASE( RHIDeviceLostReportTest, FirstReportMarksAndReportsOnce )
{
    SW_EXPECT_LOG( "RHI.DeviceLost" );
    SW_EXPECT_LOG( "GPUBreadcrumbs.DeviceLostBackendLeaves" );
    test::FakeRHIDevice device;
    SW_REQUIRE_FALSE( device.isDeviceLost() );
    device.reportDeviceLost( "test" );
    SW_EXPECT_TRUE( device.isDeviceLost() );
    device.reportDeviceLost( "test again" );
    SW_EXPECT_EQUAL( 1u, device._deviceFaultReportCount );
}
