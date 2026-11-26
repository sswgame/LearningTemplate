// MemorySnapshotGPUSourceTest — 메모리 스냅숏의 GPU 원천은 지금 그리는 장치의 추적기를 싣고, 장치가 없으면 싣지 않는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "DevTools/Profiling/Memory/MemorySnapshotSources.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Presentation/IEngineRenderDriver.h"

using namespace sw;

/** @brief [MemorySnapshotGPUSourceTest] 등록된 원천을 채우면 GPU 추적기는 장치가 있을 때만 그 장치의 것이고 이름도 함께 온다 */
SW_TEST_CASE( MemorySnapshotGPUSourceTest, FollowsTheRenderDevice )
{
    MemorySnapshotSources sources;
    MemorySnapshotSourceRegistration::fillAll( sources );
    const IEngineRenderDriver* const pRenderDriver = engine::findSubsystem<IEngineRenderDriver>();
    const IRHIDevice* const          pDevice       = pRenderDriver != nullptr ? pRenderDriver->findDevice() : nullptr;
    SW_EXPECT_TRUE( ( sources._pGPUTracker != nullptr ) == ( pDevice != nullptr ) );
    SW_EXPECT_TRUE( sources._gpuBackendName.empty() == ( pDevice == nullptr ) );
}
