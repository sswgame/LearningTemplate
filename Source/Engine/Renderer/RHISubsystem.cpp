#include "pch.h"

#include "Engine/Renderer/RHISubsystem.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Diagnostics/CrashHandler.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/PackEngineSettingsSubsystem.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/UserSettings/UserSettingsEngineHandlers.h"
#include "Engine/UserSettings/UserSettingsManager.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

namespace sw
{
} // namespace sw

namespace sw
{
    RHISubsystem::RHISubsystem()
        : _rhi{ nullptr }
        , _initResult{ RHIInitResult::NotStarted }
        , _bReady{ SW_FALSE }
    {
        SW_MEMORY_SCOPE( RenderCPU );
        _rhi = make_unique<RHI>();
    }

    RHISubsystem::~RHISubsystem() = default;

    SubsystemInitResult RHISubsystem::initializeSubsystem()
    {
        CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();

        RHIBackend          commandLineBackend{};
        const EngineConfig& engineConfig = engine::getSubsystem<EngineConfig>();
        if ( RHI::findCommandLineBackend( commandLine, commandLineBackend ) == false )
            gv_rhiBackend = engineConfig._window._defaultRHI;

        const DisplaySettingsRequest& display = engine::getSubsystem<UserSettingsManager>().getDisplayRequest();
        // 주 창의 소유자는 서브시스템 MainWindow 다(의존이라 먼저 서 있다). 창이 이미 있으면(임베드한 호스트가 넘겼다) 만들지 않는다.
        MainWindowSubsystem* const pWindowHost = engine::findSubsystem<MainWindowSubsystem>();
        if ( pWindowHost == nullptr )
        {
            SW_LOG_ERROR_ID( "RHI.RHINeedsMainWindowSubsystem", "RHI needs the MainWindow subsystem (the main window owner) in the engine scope" );
            return SubsystemInitResult::Failed;
        }
        if ( pWindowHost->getWindow() == nullptr )
        {
            uint32 windowWidth  = display._bHasResolution ? display._width : engineConfig._window._width;
            uint32 windowHeight = display._bHasResolution ? display._height : engineConfig._window._height;
            // 인자를 주지 않으면 getArgument 가 false 를 돌려주므로 설정값이 그대로 남는다.
            commandLine.getArgument( CommandLineArgument::WIDTH, windowWidth );
            commandLine.getArgument( CommandLineArgument::HEIGHT, windowHeight );

            // 창 제목은 팩 설정(`<Engine _windowTitle>`)이다 — 이 단계는 그 설정을 읽은 뒤에 선다(의존).
            unique_ptr<IWindow> defaultWindow = IWindow::createMainWindow( engine::getSubsystem<PackEngineSettings>()._windowTitle.c_str(), windowWidth, windowHeight, commandLine );
            if ( defaultWindow != nullptr )
            {
                // 전체 화면은 스왑체인을 만들기 전에 고른다. 스왑체인이 처음부터 모니터 크기다.
                if ( display._bHasMode && display._mode != WindowDisplayMode::Windowed )
                    (void)defaultWindow->setDisplayMode( display._mode, windowWidth, windowHeight );
                // 소유권은 주 창 서브시스템이 갖는다(엔진 종료 때 RHI 가 내려간 뒤 파괴한다). 이미 주 창이 있어 실패하면 defaultWindow 가 여기서 파괴된다.
                if ( pWindowHost->attach( defaultWindow ) == false )
                    SW_LOG_ERROR_ID( "RHI.MainWindowOwnerRefused", "The main window owner refused the default window" );
            }
        }

        _rhi->setPreferredVSync( display._bHasVSync ? display._bVSync : engineConfig._window._bVSync );
        // RHI 는 창 시스템을 모른다. 표면(IRenderSurface)만 넘긴다. 창은 위에서 만들었거나 호스트가 가지고 있다.
        const bool bReady = _rhi->initialize( pWindowHost->getWindow() );
        _initResult       = _rhi->getInitResult();
        if ( bReady == false )
            return SubsystemInitResult::Failed;
        _bReady = SW_TRUE;
        // 백엔드가 정해졌으니 크래시 리포트에 남긴다. "어느 백엔드에서 났는가"가 범위를 좁히는 첫 질문이다.
        engine::getSubsystem<CrashHandler>().setContextValue( "RHI", _rhi->getDevice().getBackendName() );
        return SubsystemInitResult::Succeeded;
    }

    void RHISubsystem::shutdownSubsystem()
    {
        // 디바이스 생성이 실패하면 RHI 객체는 있어도 디바이스가 없다. getDevice() 를 부르기 전에 hasDevice() 로 확인한다.
        if ( _rhi->hasDevice() )
            _rhi->getDevice().waitIdle( RHIWaitIdleReason::Shutdown );
        _rhi->shutdown();
        _bReady = SW_FALSE;
    }

    span<const utf8* const> RHISubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { ConfigManager::kSubsystemName, PackEngineSettingsSubsystem::kSubsystemName, UserSettingsManager::kSubsystemName,
                                                          MainWindowSubsystem::kSubsystemName };
        return kArrDependency;
    }

    SubsystemHostMask RHISubsystem::getSubsystemHost() const
    {
        return EngineSubsystemHost::kClient;
    }

    namespace
    {
        struct RHISubsystemInternal
        {
            /** @brief 감시 값 `RHI.GPUIdleWaits` 입니다. 장치가 없으면(전용 서버 · 헤드리스) 읽지 못합니다. */
            [[nodiscard]] static bool readGPUIdleWaits( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const RHISubsystem* const pSubsystem = engine::findSubsystem<RHISubsystem>();
                const RHI* const          pRHI       = pSubsystem != nullptr ? pSubsystem->findRHI() : nullptr;
                if ( pRHI == nullptr || pRHI->hasDevice() == false )
                    return false;
                outValue = static_cast<float64>( pRHI->getDevice().getGPUIdleWaitCount() );
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
            [[nodiscard]] static bool readGeometryStage( const GameObjectManager* /*pManager*/, float64& outValue ) { return readCapability( &RHICapabilities::_bGeometryStage, outValue ); }
            [[nodiscard]] static bool readTessellationStages( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                return readCapability( &RHICapabilities::_bTessellationStages, outValue );
            }

            /** @brief 감시 값 `Texture.FormatVariantActive` 입니다. 이 장치가 임포트한 DDS 가 아닌 쿠킹 변형을 읽으면 1 입니다(데스크톱 대상은 0 — 그림이 바뀌지 않는다는 뜻이다). */
            [[nodiscard]] static bool readFormatVariantActive( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                if ( engine::areEngineServicesBound() == false )
                    return false;
                outValue = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().getFormatVariantSuffix().empty() ? 0.0 : 1.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( rhiGPUIdleWaits, "RHI.GPUIdleWaits", "Times waitIdle stalled for the whole GPU (cumulative) - scene activation and asset hot reload must not raise it", &RHISubsystemInternal::readGPUIdleWaits );
    SW_AUTOMATION_WATCH( rhiSupportsBC7, "RHI.SupportsBC7", "1 when this device can sample BC7 textures", &RHISubsystemInternal::readSupportsBC7 );
    SW_AUTOMATION_WATCH( rhiMaterialTessellation, "RHI.MaterialTessellation", "1 when this device draws displaced materials (hull and domain stages on material shaders) - 0 on OpenGL and WebGPU",
                         &RHISubsystemInternal::readMaterialTessellation );
    SW_AUTOMATION_WATCH( rhiGeometryStage, "RHI.GeometryStage", "1 when pipeline passes can add a geometry stage - 0 on WebGPU and on Vulkan devices without geometryShader",
                         &RHISubsystemInternal::readGeometryStage );
    SW_AUTOMATION_WATCH( rhiTessellationStages, "RHI.TessellationStages", "1 when pipeline passes can add hull and domain stages - 0 on WebGPU and on Vulkan devices without tessellationShader (dzn)",
                         &RHISubsystemInternal::readTessellationStages );
    SW_AUTOMATION_WATCH( textureFormatVariantActive, "Texture.FormatVariantActive", "1 when textures read a cooked format variant instead of the imported DDS (0 on the desktop target)", &RHISubsystemInternal::readFormatVariantActive );
} // namespace sw

namespace sw
{
    // 창 · 디바이스 · 렌더러 · 렌더 스레드 · 씬의 디바이스는 클라이언트 호스트의 것이다(전용 서버는 거르고, Server 타깃은 렌더러 폴더를 짓지 않는다).
    SW_ENGINE_SUBSYSTEM( RHISubsystem, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        if ( build._pStartupSetup->_bRenderer == false )
            return;
        build._pStartupParts->_pRHI = &scope.emplaceSubsystem<RHISubsystem>();
    } );
} // namespace sw
