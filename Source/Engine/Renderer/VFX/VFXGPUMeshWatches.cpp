/**
 * @file VFXGPUMeshWatches.cpp
 * @brief GPU 메시 입자의 자동화 감시 값입니다 — `VFX.GPUMeshEmitters` · `VFX.GPUMeshAlive` · `VFX.GPUMeshDrawn` · `VFX.GPUMeshDrawnOverAlive`.
 * @details 시나리오(game/empty/automation/vfxgpumeshslots)만 읽는 값이라 렌더러 TU 가 아니라 이 파일에 둔다(`RenderScaleWatches.cpp` 와 같은 모양).
 *          값은 렌더 스레드가 프레임 끝에 GPU 에서 되읽어 원자값으로 낸 것이다(`VFXGPUMeshReadback`) — 몇 프레임 늦고, 사본이 아직 없으면 값이 없다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/VFX/VFXGPUMeshReadback.h"

namespace sw
{
    namespace
    {
        struct VFXGPUMeshWatchesInternal
        {
            /** @brief 주 렌더러의 되읽기입니다. 렌더러가 없거나(서버 · 테스트) 사본이 아직 없으면 nullptr 입니다. */
            [[nodiscard]] static const VFXGPUMeshReadback* findReadback()
            {
                const FrameRendererSubsystem* const pSubsystem = engine::findSubsystem<FrameRendererSubsystem>();
                if ( pSubsystem == nullptr )
                    return nullptr;
                const VFXGPUMeshReadback& readback = pSubsystem->getFrameRenderer().getVFXGPUMeshReadback();
                return readback.hasValue() ? &readback : nullptr;
            }

            [[nodiscard]] static bool readEmitters( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXGPUMeshReadback* const pReadback = findReadback();
                if ( pReadback == nullptr )
                    return false;
                outValue = static_cast<float64>( pReadback->getEmitterCount() );
                return true;
            }

            [[nodiscard]] static bool readAlive( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXGPUMeshReadback* const pReadback = findReadback();
                if ( pReadback == nullptr )
                    return false;
                uint32 alive = 0;
                uint32 drawn = 0;
                pReadback->readCounts( alive, drawn );
                outValue = static_cast<float64>( alive );
                return true;
            }

            [[nodiscard]] static bool readDrawn( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXGPUMeshReadback* const pReadback = findReadback();
                if ( pReadback == nullptr )
                    return false;
                uint32 alive = 0;
                uint32 drawn = 0;
                pReadback->readCounts( alive, drawn );
                outValue = static_cast<float64>( drawn );
                return true;
            }

            [[nodiscard]] static bool readDrawnOverAlive( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const VFXGPUMeshReadback* const pReadback = findReadback();
                if ( pReadback == nullptr )
                    return false;
                uint32 alive = 0;
                uint32 drawn = 0;
                pReadback->readCounts( alive, drawn );
                outValue = static_cast<float64>( drawn ) - static_cast<float64>( alive );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( vfxGPUMeshEmitters, "VFX.GPUMeshEmitters", "GPU mesh emitters counted by the last GPU readback (frames late; no value before the first readback)",
                         &VFXGPUMeshWatchesInternal::readEmitters );
    SW_AUTOMATION_WATCH( vfxGPUMeshAlive, "VFX.GPUMeshAlive", "Live particles of the GPU mesh emitters in the last GPU readback (the simulation's alive count)", &VFXGPUMeshWatchesInternal::readAlive );
    SW_AUTOMATION_WATCH( vfxGPUMeshDrawn, "VFX.GPUMeshDrawn", "Instances the main view drew for the GPU mesh emitters' batches in the last GPU readback (culled indirect instance count)",
                         &VFXGPUMeshWatchesInternal::readDrawn );
    SW_AUTOMATION_WATCH( vfxGPUMeshDrawnOverAlive, "VFX.GPUMeshDrawnOverAlive", "Drawn minus alive from the same GPU readback - above 0 means dead slots were drawn",
                         &VFXGPUMeshWatchesInternal::readDrawnOverAlive );
} // namespace sw
