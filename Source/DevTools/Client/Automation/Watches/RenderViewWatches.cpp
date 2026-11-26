/**
 * @file RenderViewWatches.cpp
 * @brief 추가 뷰 수집의 자동화 감시 값 `RenderView.HiddenCount` · `RenderView.OverflowCount` 입니다.
 * @details 시나리오만 읽는 감시 값이라 수집 TU 가 아니라 이 파일에 둔다 — 값은 패킷 빌더의 뷰 설정이 든다(`RenderPacketBuilder::getLastHiddenViewCount`).
 */
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/DevCounter.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/PackEngineSettingsSubsystem.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/QuitRequest.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/View/ScreenshotRequestQueue.h"
#include "Engine/Graphics/View/SimulationOnlyFrameQueue.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Capture/CaptureReadiness.h"
#include "Engine/Renderer/EngineRenderDriver.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/Frame/OffscreenSceneRenderers.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"
#include "Engine/Renderer/RHISubsystem.h"
#include "Engine/Renderer/RenderThread.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"
#include "Engine/Renderer/SceneRHISubsystem.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Text/FontSubsystem.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/UserSettingsEngineHandlers.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Window/GenericWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
    namespace
    {
        /** @brief 마지막 수집에서 보임 판정으로 쉰 추가 뷰 수입니다. 씬과 무관하다(주 렌더러의 패킷 빌더가 든다). 렌더러가 없는 실행에는 값이 없다. */
        [[nodiscard]] bool readHiddenViewCount( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pFrameRenderer = engine::findSubsystem<FrameRendererSubsystem>();
            if ( pFrameRenderer == nullptr )
                return false;
            outValue = static_cast<float64>( pFrameRenderer->getPacketBuilder().getLastHiddenViewCount() );
            return true;
        }

        /** @brief 마지막 수집에서 자리(`RenderConfig::_maxExtraViewCount`)를 얻지 못해 빠진 추가 뷰 수입니다. 렌더러가 없는 실행에는 값이 없다. */
        [[nodiscard]] bool readOverflowViewCount( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pFrameRenderer = engine::findSubsystem<FrameRendererSubsystem>();
            if ( pFrameRenderer == nullptr )
                return false;
            outValue = static_cast<float64>( pFrameRenderer->getPacketBuilder().getLastOverflowViewCount() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( renderViewOverflowCount, "RenderView.OverflowCount", "Extra views that got no view slot last frame (more cameras than EngineConfig _render._maxExtraViewCount)",
                         &readOverflowViewCount );
    SW_AUTOMATION_WATCH( renderViewHiddenCount, "RenderView.HiddenCount", "Extra views the visibility test rested last frame (monitor off screen, behind a wall or too small)",
                         &readHiddenViewCount );
} // namespace sw

// Engine/Renderer/EngineRenderDriver.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct EngineRenderDriverWatchesInternal
        {
            /** @brief 감시 값 `Display.SDRFallback` 입니다 — 마지막 화면 출력 요청이 HDR 인데 백엔드 · 모니터가 받지 않아 SDR 로 떨어졌으면 1 입니다. */
            [[nodiscard]] static bool readDisplaySDRFallback( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                EngineRenderDriverSubsystem* const pSubsystem = engine::areEngineSubsystemsBound() ? engine::findSubsystem<EngineRenderDriverSubsystem>() : nullptr;
                if ( pSubsystem == nullptr )
                    return false;
                outValue = pSubsystem->getRenderDriver().isDisplayOutputFallenBackToSDR() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( displaySDRFallback, "Display.SDRFallback", "1 when the requested HDR display output (-gv_displayOutput, display.hdr) fell back to SDR because the backend or monitor does not take it",
                         &EngineRenderDriverWatchesInternal::readDisplaySDRFallback );
} // namespace sw

// Engine/Renderer/RHISubsystem.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct RHISubsystemWatchesInternal
        {
            /** @brief 감시 값 `RHI.GeometryStage` · `RHI.TessellationStages` 입니다. 패스 XML 의 선택 스테이지를 붙일 수 있는 장치면 1 입니다(`RHICapabilities` 의 같은 이름 칸). */
            [[nodiscard]] static bool readCapability( uint8 RHICapabilities::* pField, float64& outValue )
            {
                const RHISubsystem* const pSubsystem = engine::findSubsystem<RHISubsystem>();
                const RHI* const          pRHI       = pSubsystem != nullptr ? pSubsystem->findRHI() : nullptr;
                if ( pRHI == nullptr || pRHI->hasDevice() == false )
                    return false;
                outValue = pRHI->getDevice().getCapabilities().*pField != SW_FALSE ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `Texture.FormatVariantActive` 입니다. 이 장치가 임포트한 DDS 가 아닌 쿠킹 변형을 읽으면 1 입니다(데스크톱 대상은 0 — 그림이 바뀌지 않는다는 뜻이다). */
            [[nodiscard]] static bool readFormatVariantActive( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( engine::areEngineSubsystemsBound() == false )
                    return false;
                outValue = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().getFormatVariantSuffix().empty() ? 0.0 : 1.0;
                return true;
            }

            /** @brief 감시 값 `RHI.GPUCulling` 입니다. 컴퓨트 컬링이 간접 인자를 만드는 장치면 1 입니다(DX11 은 0 — 간접 인자를 구조 버퍼로 쓸 수 없어 CPU 개수로 그린다). */
            [[nodiscard]] static bool readGPUCulling( const GameObjectManager* /*pManager*/, float64& outValue ) { return readCapability( &RHICapabilities::_bGPUCulling, outValue ); }

            /** @brief 감시 값 `RHI.GPUIdleWaits` 입니다. 장치가 없으면(전용 서버 · 헤드리스) 읽지 못합니다. */
            [[nodiscard]] static bool readGPUIdleWaits( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHISubsystem* const pSubsystem = engine::findSubsystem<RHISubsystem>();
                const RHI* const          pRHI       = pSubsystem != nullptr ? pSubsystem->findRHI() : nullptr;
                if ( pRHI == nullptr || pRHI->hasDevice() == false )
                    return false;
                outValue = static_cast<float64>( pRHI->getDevice().getGPUIdleWaitCount().get() );
                return true;
            }

            [[nodiscard]] static bool readGeometryStage( const GameObjectManager* /*pManager*/, float64& outValue ) { return readCapability( &RHICapabilities::_bGeometryStage, outValue ); }

            /** @brief 감시 값 `RHI.MaterialTessellation` 입니다. 변위 머티리얼을 그리는 장치면 1 입니다(`RHICapabilities::_bMaterialTessellation` — OpenGL · WebGPU 는 0). */
            [[nodiscard]] static bool readMaterialTessellation( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHISubsystem* const pSubsystem = engine::findSubsystem<RHISubsystem>();
                const RHI* const          pRHI       = pSubsystem != nullptr ? pSubsystem->findRHI() : nullptr;
                if ( pRHI == nullptr || pRHI->hasDevice() == false )
                    return false;
                outValue = pRHI->getDevice().getCapabilities()._bMaterialTessellation != SW_FALSE ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `RHI.NativeBindless` 입니다. 셰이더가 bindless 번호로 텍스처를 읽는 장치(DX12 · Vulkan)면 1 입니다 — 이름 바인딩 패스가 도는 판정과 같은 함수다. */
            [[nodiscard]] static bool readNativeBindless( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHISubsystem* const pSubsystem = engine::findSubsystem<RHISubsystem>();
                const RHI* const          pRHI       = pSubsystem != nullptr ? pSubsystem->findRHI() : nullptr;
                if ( pRHI == nullptr || pRHI->hasDevice() == false )
                    return false;
                outValue = pRHI->getDevice().supportsNativeBindlessSampling() ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `RHI.SupportsBC7` 입니다. 네 데스크톱 백엔드가 모두 1 이어야 합니다(`IRHIDevice::supportsTextureFormat` 의 실제 장치 답). */
            [[nodiscard]] static bool readSupportsBC7( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHISubsystem* const pSubsystem = engine::findSubsystem<RHISubsystem>();
                const RHI* const          pRHI       = pSubsystem != nullptr ? pSubsystem->findRHI() : nullptr;
                if ( pRHI == nullptr || pRHI->hasDevice() == false )
                    return false;
                outValue = pRHI->getDevice().supportsTextureFormat( RHIFormat::BC7_UNORM ) ? 1.0 : 0.0;
                return true;
            }

            [[nodiscard]] static bool readTessellationStages( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readCapability( &RHICapabilities::_bTessellationStages, outValue );
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( rhiGPUIdleWaits, "RHI.GPUIdleWaits", "Times waitIdle stalled for the whole GPU (cumulative) - scene activation and asset hot reload must not raise it", &RHISubsystemWatchesInternal::readGPUIdleWaits );
    SW_AUTOMATION_WATCH( rhiSupportsBC7, "RHI.SupportsBC7", "1 when this device can sample BC7 textures", &RHISubsystemWatchesInternal::readSupportsBC7 );
    SW_AUTOMATION_WATCH( rhiMaterialTessellation, "RHI.MaterialTessellation", "1 when this device draws displaced materials (hull and domain stages on material shaders) - 0 on OpenGL and WebGPU",
                         &RHISubsystemWatchesInternal::readMaterialTessellation );
    SW_AUTOMATION_WATCH( rhiGPUCulling, "RHI.GPUCulling", "1 when compute culling writes the indirect draw counts on this device - 0 on DirectX 11 (CPU counts)", &RHISubsystemWatchesInternal::readGPUCulling );
    SW_AUTOMATION_WATCH( rhiNativeBindless, "RHI.NativeBindless", "1 when shaders read textures by bindless index (DX12, Vulkan) - name-bound Fullscreen and Compute passes run only here", &RHISubsystemWatchesInternal::readNativeBindless );
    SW_AUTOMATION_WATCH( rhiGeometryStage, "RHI.GeometryStage", "1 when pipeline passes can add a geometry stage - 0 on WebGPU and on Vulkan devices without geometryShader",
                         &RHISubsystemWatchesInternal::readGeometryStage );
    SW_AUTOMATION_WATCH( rhiTessellationStages, "RHI.TessellationStages", "1 when pipeline passes can add hull and domain stages - 0 on WebGPU and on Vulkan devices without tessellationShader (dzn)",
                         &RHISubsystemWatchesInternal::readTessellationStages );
    SW_AUTOMATION_WATCH( textureFormatVariantActive, "Texture.FormatVariantActive", "1 when textures read a cooked format variant instead of the imported DDS (0 on the desktop target)", &RHISubsystemWatchesInternal::readFormatVariantActive );
} // namespace sw
