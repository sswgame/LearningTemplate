/**
 * @file TestEngineRenderDriver.cpp
 * @brief GPU 없이 보는 `EngineRenderDriver` 의 장치 분실 답 — 잃은 장치는 Config 정책대로 끝내거나(종료 코드 DeviceLost) 다시 만든다(감사 R-2).
 * @details 실제 백엔드의 손실(개발 명령 `rhi.removeDevice`)은 가짜 디바이스의 `debugRemoveDevice` 로 대신한다 — 둘 다 `RHITransferSerials::markLost` 하나를 세운다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/EngineConfig.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/EngineRenderDriver.h"

using namespace sw;

/**
 * @brief [EngineRenderDriverTest] 잃지 않은 장치는 답이 없고, `rhi.removeDevice` 뒤의 장치는 `Quit` 정책이면 끝낸다(종료 코드 `DeviceLost`)
 * @details 드라이버는 이 답을 프레임마다 묻고(`submitFrame`) `Quit` 면 그 프레임에 `QuitRequest( DeviceLost )` 를 건다 — 분실 뒤 다음 게임 스레드 프레임 안에 정해진 코드로 끝나는 근거다.
 */
SW_TEST_CASE( EngineRenderDriverTest, LostDeviceQuitsWithDeviceLostCode )
{
    test::FakeRHIDevice device;
    SW_EXPECT_FALSE( device.isDeviceLost() );
    SW_EXPECT_FALSE_MSG( EngineRenderDriver::decideDeviceLostResponse( device, RHIBackend::DirectX12, RHIDeviceLostPolicy::Quit ).has_value(), "a live device must not end the loop" );

    SW_EXPECT_TRUE( device.debugRemoveDevice() );
    SW_EXPECT_TRUE_MSG( device.isDeviceLost(), "IRHIDevice::isDeviceLost must read the backend's transfer-serial loss mark" );
    const optional<RHIDeviceLostPolicy> response = EngineRenderDriver::decideDeviceLostResponse( device, RHIBackend::DirectX12, RHIDeviceLostPolicy::Quit );
    SW_EXPECT_TRUE_MSG( response.has_value() && *response == RHIDeviceLostPolicy::Quit, "the Quit policy must quit (ProcessExitCode::DeviceLost)" );
}

/**
 * @brief [EngineRenderDriverTest] `Recreate` 정책은 다시 만들 수 있는 백엔드(DX11 · DX12 · Vulkan)에서만 다시 만들고, OpenGL · WebGPU 는 끝낸다(감사 R-2)
 * @details 단위 테스트는 개발 구성이라 다시 만드는 길(백엔드 교체기)이 있다. 배포 구성은 백엔드와 상관없이 `Quit` 다(`build::kShipping`).
 */
SW_TEST_CASE( EngineRenderDriverTest, RecreatePolicyQuitsOnBackendsThatCannotRecreate )
{
    test::FakeRHIDevice device;
    SW_REQUIRE_TRUE( device.debugRemoveDevice() );
    const RHIBackend arrRecreatable[] = { RHIBackend::DirectX11, RHIBackend::DirectX12, RHIBackend::Vulkan };
    for ( const RHIBackend backend : arrRecreatable )
    {
        const optional<RHIDeviceLostPolicy> response = EngineRenderDriver::decideDeviceLostResponse( device, backend, RHIDeviceLostPolicy::Recreate );
        SW_EXPECT_TRUE_MSG( response.has_value() && *response == RHIDeviceLostPolicy::Recreate, RHI::getBackendTypeName( backend ) );
    }
    const RHIBackend arrQuitOnly[] = { RHIBackend::OpenGL, RHIBackend::WebGPU };
    for ( const RHIBackend backend : arrQuitOnly )
    {
        SW_EXPECT_FALSE_MSG( RHI::canRecreateAfterDeviceLost( backend ), RHI::getBackendTypeName( backend ) );
        const optional<RHIDeviceLostPolicy> response = EngineRenderDriver::decideDeviceLostResponse( device, backend, RHIDeviceLostPolicy::Recreate );
        SW_EXPECT_TRUE_MSG( response.has_value() && *response == RHIDeviceLostPolicy::Quit, RHI::getBackendTypeName( backend ) );
    }
}
