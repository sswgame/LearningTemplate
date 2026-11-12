/**
 * @file TestEngineRenderDriver.cpp
 * @brief GPU 없이 보는 `EngineRenderDriver` 의 장치 분실 답 — 잃은 장치는 Config 정책이 고른 종료 코드로 끝난다(감사 R-2).
 * @details 실제 백엔드의 손실(개발 명령 `rhi.removeDevice`)은 가짜 디바이스의 `debugRemoveDevice` 로 대신한다 — 둘 다 `RHITransferSerials::markLost` 하나를 세운다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/EngineRenderDriver.h"

using namespace sw;

/**
 * @brief [EngineRenderDriverTest] 잃지 않은 장치는 끝내지 않고, `rhi.removeDevice` 뒤의 장치는 종료 정책이 `DeviceLost` 종료 코드를 고른다
 * @details 드라이버는 이 답을 프레임마다 묻고(`submitFrame`) 값이 있으면 그 프레임에 `QuitRequest` 를 건다 — 분실 뒤 다음 게임 스레드 프레임 안에 정해진 코드로 끝나는 근거다.
 */
SW_TEST_CASE( EngineRenderDriverTest, LostDeviceQuitsWithDeviceLostCode )
{
    test::FakeRHIDevice device;
    SW_EXPECT_FALSE( device.isDeviceLost() );
    SW_EXPECT_FALSE_MSG( EngineRenderDriver::decideDeviceLostExit( device, RHIDeviceLostPolicy::Quit ).has_value(), "a live device must not end the loop" );

    SW_EXPECT_TRUE( device.debugRemoveDevice() );
    SW_EXPECT_TRUE_MSG( device.isDeviceLost(), "IRHIDevice::isDeviceLost must read the backend's transfer-serial loss mark" );
    const optional<ProcessExitCode> exitCode = EngineRenderDriver::decideDeviceLostExit( device, RHIDeviceLostPolicy::Quit );
    SW_EXPECT_TRUE_MSG( exitCode.has_value() && *exitCode == ProcessExitCode::DeviceLost, "the Quit policy must end with ProcessExitCode::DeviceLost" );
}
