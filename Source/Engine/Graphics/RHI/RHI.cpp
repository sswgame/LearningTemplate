#include "pch.h"

#include "Engine/Graphics/RHI/RHI.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Common/IRenderSurface.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{

    SW_LOG_CALLER( "RHI" );

    // EngineConfig 로드 실패 시에도 WindowConfig::_defaultRHI(cpp 기본값)와 같은 백엔드로 기동하도록 맞춥니다.
    // 이 플랫폼에서 쓸 수 없으면 RHI::initialize 가 getDefaultPlatformBackend() 로 폴백합니다.
    SW_GLOBAL_VARIABLE_ENUM( gv_rhiBackend, RHIBackend, RHIBackend::DirectX12, "Current RHI Backend" );

    // 커맨드 리스트를 프레임 끝에 모아 한 번에 제출할지(기본), 잘릴 때마다 바로 제출할지.
    // 두 모드 모두 기록 순서 = 실행 순서다 — 즉시 모드도 [세그먼트][리스트] 순서를 지켜 제출하고
    // 제출 '시점'만 달라진다. 즉시 모드는 제출 횟수가 늘어 오버헤드가 크지만, GPU 오류(DEVICE_HUNG,
    // 검증 레이어)가 어느 제출에서 났는지 좁히기 쉬워 디버깅에 쓴다.
    SW_GLOBAL_VARIABLE_BOOL( gv_rhiImmediateSubmit, false,
                             "RHI 커맨드 리스트를 프레임 끝에 모아 제출하지 않고 즉시 제출 (디버깅용, 오버헤드 큼)" );

    RHIPipelineStateDesc::RHIPipelineStateDesc() noexcept
        : _vertexShaderPath{}
        , _vertexEntryPoint{}
        , _pixelShaderPath{}
        , _pixelEntryPoint{}
        , _computeShaderPath{}
        , _computeEntryPoint{}
        , _listShaderDefine{}
        , _topology{ RHIPrimitiveTopology::TriangleList }
        , _fillMode{ RHIFillMode::Solid }
        , _cullMode{ RHICullMode::None }
        , _numRenderTargets{ 1 }
        , _arrRtvFormat{}
        , _depthStencilFormat{ RHIFormat::D24_UNORM_S8_UINT }
        , _bEnableDepthTest{ SW_FALSE }
        , _bEnableDepthWrite{ SW_TRUE }
        , _bEnableBlend{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
        for ( uint32 attachmentIndex = 0; attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            _arrRtvFormat[attachmentIndex] = RHIFormat::R8G8B8A8_UNORM;
        }
    }

    RHIRenderPassDesc::RHIRenderPassDesc() noexcept
        : _listColorAttachment{}
        , _clearDepth{ 1.0f }
        , _clearStencil{ 0 }
        , _bHasDepthStencil{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _arrReserved{ 0, 0 }
    {
    }

    RHITextureDesc::RHITextureDesc() noexcept
        : _clearColor{ 0.0f, 0.0f, 0.0f, 0.0f }
        , _width{ 1 }
        , _height{ 1 }
        , _depth{ 1 }
        , _mipLevels{ 1 }
        , _format{ RHIFormat::R8G8B8A8_UNORM }
        , _clearDepth{ 1.0f }
        , _clearStencil{ 0 }
        , _bIsRenderTarget{ SW_FALSE }
        , _bIsDepthStencil{ SW_FALSE }
        , _bIsShaderResource{ SW_TRUE }
        , _bIsUnorderedAccess{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _arrReserved{ 0, 0 }
    {
    }

    RHITextureUploadDesc::RHITextureUploadDesc() noexcept
        : _pData{ nullptr }
        , _sizeBytes{ 0 }
        , _mipLevels{ 0 }
    {
    }

    RHIRenderPassBeginInfo::RHIRenderPassBeginInfo() noexcept
        : _renderPass{ 0 }
        , _arrColorTarget{}
        , _depthTarget{ 0 }
        , _arrClearColor{}
        , _colorTargetCount{ 0 }
        , _width{ 0 }
        , _height{ 0 }
        , _clearDepth{ 1.0f }
        , _arrLoadOp{}
        , _depthLoadOp{ RHIRenderPassLoadOp::Clear }
        , _bBindColor{ SW_TRUE }
        , _reservedFlags{ 0 }
        , _arrReserved{ 0, 0, 0 }
    {
        for ( uint32 attachmentIndex = 0; attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
        {
            _arrClearColor[attachmentIndex] = kDefaultClearColor;
            _arrLoadOp[attachmentIndex]     = RHIRenderPassLoadOp::Clear;
        }
    }

    void RHIRenderPassBeginInfo::setColorTarget( RHITextureHandle target, const float4& clearColor, RHIRenderPassLoadOp loadOp )
    {
        _arrColorTarget[0] = target;
        _arrClearColor[0]  = clearColor;
        _arrLoadOp[0]      = loadOp;
        _colorTargetCount  = 1;
    }

    bool RHIBackendUtil::findCommandLineBackend( const CommandLineManager& commandLineManager, RHIBackend& outBackend )
    {
        bool bFlag{ false };
        if ( commandLineManager.getArgument( CommandLineArgument::DIRECTX_11, bFlag ) && bFlag )
        {
            outBackend = RHIBackend::DirectX11;
            return true;
        }
        if ( commandLineManager.getArgument( CommandLineArgument::DIRECTX_12, bFlag ) && bFlag )
        {
            outBackend = RHIBackend::DirectX12;
            return true;
        }
        if ( commandLineManager.getArgument( CommandLineArgument::VULKAN, bFlag ) && bFlag )
        {
            outBackend = RHIBackend::Vulkan;
            return true;
        }
        if ( commandLineManager.getArgument( CommandLineArgument::OPENGL, bFlag ) && bFlag )
        {
            outBackend = RHIBackend::OpenGL;
            return true;
        }

        // `-gv_rhiBackend=<n>` 도 **명시적 지정**이다. 예전엔 짧은 플래그만 봐서, 전역 변수로
        // 백엔드를 고르면 EngineConfig 기본값이 **조용히 덮어썼다** — 커맨드라인이 아무 일도 안
        // 하는 것처럼 보이고, 로그도 남지 않았다. 값 자체는 updateFromCommandLine 이 이미 전역
        // 변수에 넣어 두었으므로 여기서는 그것을 읽는다.
        if ( commandLineManager.isArgumentProvided( "gv_rhiBackend" ) )
        {
            outBackend = gv_rhiBackend;
            return true;
        }

        return false;
    }

    RHI::RHI()
        : _device{ nullptr }
        , _pSurface{ nullptr }
        , _pendingRHIBackend{ RHIBackend::DirectX12 }
        , _committedRHIBackend{ RHIBackend::DirectX12 }
        , _bPreferredVSync{ SW_FALSE }
        , _bPendingBackendChange{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    RHI::~RHI() = default;

    bool RHI::initialize( IRenderSurface* pSurface )
    {
        _pSurface = pSurface;
        // Priority: explicit CLI > current GVM value > OS default > first available
        RHIBackend currentBackend = gv_rhiBackend;

        // 커맨드라인이 고른 것은 **폴백하지 않는다** — 쓸 수 없으면 아래에서 에러로 선다.
        // 조용히 다른 백엔드로 뜨면 "네 백엔드를 확인했다" 가 거짓이 된다(실제로 그런 적이 있다).
        RHIBackend commandLineBackend{};
        const bool bCommandLineOverride = RHIBackendUtil::findCommandLineBackend( engine::getCommandLineManager(), commandLineBackend );
        if ( bCommandLineOverride )
            currentBackend = commandLineBackend;
        else if ( RHIAvailability::isAvailable( currentBackend ) == false )
            currentBackend = getDefaultPlatformBackend();

        if ( RHIAvailability::isAvailable( currentBackend ) == false )
        {
            SW_LOG_ERROR( "Requested RHI backend is unavailable on this platform." );
            return false;
        }

        gv_rhiBackend = currentBackend;
        SW_LOG_INFO( "Initializing RHI with backend: %#", getBackendTypeName( currentBackend ) );

        _device = createDevice( currentBackend );
        if ( _device == nullptr )
        {
            SW_LOG_ERROR( "Failed to create RHI Device!" );
            return false;
        }

        _device->setRenderSurface( _pSurface );
        _device->setPreferredVSync( _bPreferredVSync == SW_TRUE );

        if ( _device->initialize() == false )
        {
            SW_LOG_ERROR( "Failed to initialize RHI Device!" );
            _device.reset();
            return false;
        }

        SW_LOG_INFO( "RHI initialized successfully." );
        _committedRHIBackend = currentBackend;
        _pendingRHIBackend   = currentBackend;
        return true;
    }

    void RHI::shutdown()
    {
#if defined( SW_DEBUG )
#endif
        if ( _device != nullptr )
        {
            _device->shutdown();
            _device.reset();
        }
        // After devices are gone, drop MODULE DLLs (DX11/DX12) so FreeLibrary is safe.
        engine::getRHIBackendRegistry().unloadModules();
    }

    bool RHI::recreateDevice( RHIBackend backend )
    {
        if ( RHIAvailability::isAvailable( backend ) == false )
        {
            SW_LOG_ERROR( "recreateDevice: backend unavailable" );
            return false;
        }

        const RHIBackend previousBackend = _committedRHIBackend;

        if ( _device )
        {
            // 죽기 직전의 통보(RHIRenderResource::releaseAllFor)는 shutdown 이 스스로 낸다 — 여기서 손으로 훑지 않는다.
            _device->waitIdle();
            _device->shutdown();
            _device.reset();
        }

        const RHICapabilities currentCaps  = RHIAvailability::query( backend );
        const RHICapabilities previousCaps = RHIAvailability::query( previousBackend );
        if ( ( currentCaps._bRequiresWindowRecreate != SW_FALSE || previousCaps._bRequiresWindowRecreate != SW_FALSE ) && _pSurface != nullptr )
            _pSurface->recreateSurface();

        gv_rhiBackend = backend;
        _device       = createDevice( backend );
        if ( _device == nullptr )
            return false;

        _device->setRenderSurface( _pSurface );
        _device->setPreferredVSync( _bPreferredVSync == SW_TRUE );

        if ( _device->initialize() == false )
        {
            _device.reset();
            return false;
        }

        SW_LOG_INFO( "Soft-recreated device: %#", getBackendTypeName( backend ) );
        _committedRHIBackend = backend;
        _pendingRHIBackend   = backend;
        return true;
    }

    void RHI::schedulePendingBackendChange( RHIBackend requested )
    {
        if ( RHIAvailability::isAvailable( requested ) == false )
        {
            SW_LOG_WARNING( "Backend %# unavailable — reverting.", static_cast<int32>( requested ) );
            gv_rhiBackend = _committedRHIBackend;
            return;
        }

        if ( requested == _committedRHIBackend )
            return;

        SW_LOG_INFO( "RHI Backend change queued: %# → %#", getBackendTypeName( _committedRHIBackend ), getBackendTypeName( requested ) );
        _pendingRHIBackend     = requested;
        _bPendingBackendChange = SW_TRUE;
    }

    RHIBackend RHI::consumePendingBackendChange()
    {
        _bPendingBackendChange = SW_FALSE;
        return _pendingRHIBackend;
    }

    unique_ptr<IRHIDevice> RHI::createDevice( RHIBackend backend )
    {
        return engine::getRHIBackendRegistry().createDevice( backend );
    }

    const utf8* RHI::getBackendTypeName( RHIBackend backend )
    {
        const EnumInfo* pInfo = engine::getTypeRegistry().findEnum( hashed_string( "RHIBackend" ) );

        if ( pInfo != nullptr )
        {
            hashed_string name = pInfo->toString( static_cast<int64>( backend ) );
            if ( name.empty() == false )
                return name.c_str();
        }

        switch ( backend )
        {
            case RHIBackend::DirectX11:
                return "DirectX11";
            case RHIBackend::DirectX12:
                return "DirectX12";
            case RHIBackend::Vulkan:
                return "Vulkan";
            case RHIBackend::OpenGL:
                return "OpenGL";
            default:
                break;
        }
        return "Unknown";
    }

    RHIBackend RHI::getDefaultPlatformBackend()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return RHIBackend::DirectX12;
#elif defined( SW_PLATFORM_LINUX )
        return RHIBackend::Vulkan;
#else
        return RHIBackend::OpenGL;
#endif
    }

    ShaderTargetFormat RHI::getShaderTargetFormat( RHIBackend backend )
    {
        switch ( backend )
        {
            case RHIBackend::DirectX11:
                return ShaderTargetFormat::DXBC_D3D11;
            case RHIBackend::DirectX12:
                return ShaderTargetFormat::DXIL_D3D12;
            case RHIBackend::Vulkan:
                return ShaderTargetFormat::SPIRV_Vulkan;
            case RHIBackend::OpenGL:
                return ShaderTargetFormat::SPIRV_OpenGL;
            default:
                break;
        }
        return ShaderTargetFormat::DXIL_D3D12;
    }
} // namespace sw
