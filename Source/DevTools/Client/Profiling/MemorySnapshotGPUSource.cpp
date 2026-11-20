/**
 * @file DevTools/Client/Profiling/MemorySnapshotGPUSource.cpp
 * @brief 메모리 스냅숏의 GPU 자원 원천입니다 — 클라이언트 전용 소스(Client/)라 전용 서버 구성은 짓지 않고, 그 구성에는 이 원천이 없다.
 */
#include "pch.h"

#include "DevTools/Profiling/Memory/MemorySnapshotSources.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Presentation/IEngineRenderDriver.h"

namespace sw
{
    namespace
    {
        /** @brief 지금 그리는 장치의 메모리 추적기와 백엔드 이름을 싣습니다. 장치가 서기 전 · 잃은 뒤면 GPU 표가 빈다. */
        void fillGPUSource( MemorySnapshotSources& inoutSources )
        {
            const IEngineRenderDriver* const pRenderDriver = engine::findSubsystem<IEngineRenderDriver>();
            const IRHIDevice* const          pDevice       = pRenderDriver != nullptr ? pRenderDriver->findDevice() : nullptr;
            if ( pDevice == nullptr )
                return;
            inoutSources._pGPUTracker    = &pDevice->getMemoryTracker();
            inoutSources._gpuBackendName = pDevice->getBackendName();
        }
    } // namespace

    SW_MEMORY_SNAPSHOT_SOURCE( gpuResources, "GPU resources", &fillGPUSource );
} // namespace sw
