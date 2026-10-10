#include "pch.h"

#include "Engine/Graphics/RHI/RHI.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRenderSurface.h"
#include "Engine/Graphics/RHI/RHIBackendRegistry.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Reflection/ReflectionCore.h"

#include "sw/config/CookContract.gen.h"

namespace sw
{

    SW_LOG_CALLER( "RHI" );

    namespace
    {
        struct RHIInternal
        {
            /** @brief 명령줄 플래그 하나와 그것이 고르는 백엔드입니다. 쿠킹 표(`SW_RHI_BACKEND_TABLE`)의 줄마다 하나입니다. */
            struct BackendArgument
            {
                CommandLineArgument _argument;
                RHIBackend          _backend;
            };

            static constexpr BackendArgument kArrBackendArgument[] = {
#define SW_RHI_BACKEND_ARGUMENT_ROW( Backend, ShaderFolder, ShaderTarget, Argument, ... ) { CommandLineArgument::Argument, RHIBackend::Backend },
                SW_RHI_BACKEND_TABLE( SW_RHI_BACKEND_ARGUMENT_ROW )
#undef SW_RHI_BACKEND_ARGUMENT_ROW
            };

            /** @brief 표의 줄 순서가 RHIBackend 열거값과 같은지입니다(줄 순서로 값을 읽는 쪽이 있다). */
            static constexpr bool isTableInEnumOrder()
            {
                for ( uint32 rowIndex = 0; rowIndex < std::size( kArrBackendArgument ); ++rowIndex )
                {
                    if ( static_cast<uint32>( kArrBackendArgument[rowIndex]._backend ) != rowIndex )
                        return false;
                }
                return true;
            }
        };

        static_assert( RHIInternal::isTableInEnumOrder(), "Config/Engine/CookContract.json rhi_backends must follow RHIBackend values" );
    } // namespace

    // EngineConfig 로드 실패 시에도 WindowConfig::_defaultRHI(cpp 기본값)와 같은 백엔드로 기동하도록 맞춥니다.
    // 이 플랫폼에서 쓸 수 없으면 RHI::initialize 가 getDefaultPlatformBackend() 로 폴백합니다.
    SW_GLOBAL_VARIABLE( RHIBackend, gv_rhiBackend, RHIBackend::SW_RHI_BACKEND_DEFAULT, "Current RHI Backend" );

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
        , _bPremultipliedAlpha{ SW_FALSE }
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
        , _arraySize{ 1 }
        , _mipLevels{ 1 }
        , _format{ RHIFormat::R8G8B8A8_UNORM }
        , _clearDepth{ 1.0f }
        , _clearStencil{ 0 }
        , _bIsRenderTarget{ SW_FALSE }
        , _bIsDepthStencil{ SW_FALSE }
        , _bIsShaderResource{ SW_TRUE }
        , _bIsUnorderedAccess{ SW_FALSE }
        , _bIsTransient{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _dimension{ RHITextureDimension::Texture2D }
        , _arrReserved{ 0 }
    {
    }

    RHITextureUploadDesc::RHITextureUploadDesc() noexcept
        : _pData{ nullptr }
        , _sizeBytes{ 0 }
        , _mipLevels{ 0 }
        , _arraySlice{ 0 }
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
        , _arrColorTargetSlice{}
        , _depthTargetSlice{ 0 }
        , _bBindColor{ SW_TRUE }
        , _reservedFlags{ 0 }
        , _arrReserved{ 0 }
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

    bool validateTextureRegionUpload( RHIFormat format, uint32 textureWidth, uint32 textureHeight, uint32 mipLevels, uint32 arraySize,
                                      const RHITextureRegionUploadDesc& desc, uint32& outRowBytes )
    {
        const uint32 bytesPerPixel = getRHIFormatBytesPerPixel( format );
        if ( bytesPerPixel == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2DRegion: format %# is compressed, depth or unknown", static_cast<uint32>( format ) );
            return false;
        }
        if ( desc._mip >= mipLevels || desc._arraySlice >= arraySize || desc._width == 0 || desc._height == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2DRegion: mip %# of %#, slice %# of %#, or an empty %#x%# region", desc._mip, mipLevels, desc._arraySlice, arraySize,
                          desc._width, desc._height );
            return false;
        }
        const uint32 mipWidth  = ( textureWidth >> desc._mip ) > 0 ? ( textureWidth >> desc._mip ) : 1u;
        const uint32 mipHeight = ( textureHeight >> desc._mip ) > 0 ? ( textureHeight >> desc._mip ) : 1u;
        // 더하기가 넘치지 않게 뺄셈으로 비교한다(x + width 가 uint32 를 넘으면 안쪽으로 보인다).
        const bool bInsideX = desc._x < mipWidth && desc._width <= mipWidth - desc._x;
        const bool bInsideY = desc._y < mipHeight && desc._height <= mipHeight - desc._y;
        if ( bInsideX == false || bInsideY == false )
        {
            SW_LOG_ERROR( "uploadTexture2DRegion: region (%#,%# %#x%#) is outside the %#x%# mip %#", desc._x, desc._y, desc._width, desc._height, mipWidth,
                          mipHeight, desc._mip );
            return false;
        }
        outRowBytes                = desc._width * bytesPerPixel;
        const uint64 requiredBytes = static_cast<uint64>( outRowBytes ) * desc._height;
        if ( desc._pData == nullptr || desc._sizeBytes < requiredBytes )
        {
            SW_LOG_ERROR( "uploadTexture2DRegion: %# bytes for %# rows of %# bytes", desc._sizeBytes, desc._height, outRowBytes );
            return false;
        }
        return true;
    }

    bool RHIBackendUtil::findCommandLineBackend( const CommandLineManager& commandLineManager, RHIBackend& outBackend )
    {
        for ( const RHIInternal::BackendArgument& row : RHIInternal::kArrBackendArgument )
        {
            bool bFlag{ false };
            if ( commandLineManager.getArgument( row._argument, bFlag ) && bFlag )
            {
                outBackend = row._backend;
                return true;
            }
        }

        // `-gv_rhiBackend=<n>` 도 **명시적 지정**이다. 짧은 플래그만 보면 전역 변수로 고른 백엔드를
        // EngineConfig 기본값이 로그 없이 덮어쓴다. 값 자체는 updateFromCommandLine 이 이미 전역
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
        , _initResult{ RHIInitResult::NotStarted }
        , _bPreferredVSync{ SW_FALSE }
        , _bPendingBackendChange{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    RHI::~RHI() = default;

    bool RHI::initialize( IRenderSurface* pSurface )
    {
        _pSurface   = pSurface;
        _initResult = RHIInitResult::Failed;
        // 우선순위: 명시한 CLI > 지금 전역 변수 값 > OS 기본값 > 처음으로 쓸 수 있는 것
        RHIBackend currentBackend = gv_rhiBackend;

        // 커맨드라인이 고른 것은 **폴백하지 않는다.** 쓸 수 없으면 아래에서 에러로 선다.
        // 조용히 다른 백엔드로 뜨면 "네 백엔드를 확인했다" 가 거짓이 된다.
        RHIBackend commandLineBackend{};
        const bool bCommandLineOverride = RHIBackendUtil::findCommandLineBackend( engine::getCommandLineManager(), commandLineBackend );
        if ( bCommandLineOverride )
            currentBackend = commandLineBackend;
        else if ( RHIAvailability::isAvailable( currentBackend ) == false )
            currentBackend = getDefaultPlatformBackend();

        if ( RHIAvailability::isAvailable( currentBackend ) == false )
        {
            SW_LOG_ERROR( "Requested RHI backend is unavailable on this platform." );
            _initResult = RHIInitResult::BackendNotBuilt;
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
            _initResult = _device->getInitResult();
            _device.reset();
            return false;
        }

        SW_LOG_INFO( "RHI initialized successfully." );
        _initResult          = RHIInitResult::Succeeded;
        _committedRHIBackend = currentBackend;
        _pendingRHIBackend   = currentBackend;
        return true;
    }

    void RHI::shutdown()
    {
        if ( _device != nullptr )
        {
            _device->shutdown();
            _device.reset();
        }
        // 디바이스가 모두 사라진 뒤에 MODULE DLL 을 내린다. 그래야 FreeLibrary 가 안전하다.
        engine::getRHIBackendRegistry().unloadModules();
    }

    bool RHI::recreateSurfaceForSwap( IRenderSurface* pSurface, const RHIBackend previousBackend, const RHIBackend nextBackend )
    {
        const bool bRequiresRecreate = RHIAvailability::query( previousBackend )._bRequiresWindowRecreate != SW_FALSE || RHIAvailability::query( nextBackend )._bRequiresWindowRecreate != SW_FALSE;
        if ( bRequiresRecreate == false || pSurface == nullptr )
            return true;
        return pSurface->recreateSurface();
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
            // 죽기 직전의 통보(RHIRenderResource::releaseAllFor)는 shutdown 이 스스로 낸다. 여기서 손으로 훑지 않는다.
            _device->waitIdle();
            _device->shutdown();
            _device.reset();
        }

        if ( recreateSurfaceForSwap( _pSurface, previousBackend, backend ) == false )
        {
            SW_LOG_ERROR( "recreateDevice: render surface could not be recreated for %# -> %#", getBackendTypeName( previousBackend ),
                          getBackendTypeName( backend ) );
            return false;
        }

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
        }
        return ShaderTargetFormat::DXIL_D3D12;
    }
} // namespace sw
