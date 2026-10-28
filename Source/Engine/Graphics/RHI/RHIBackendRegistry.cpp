#include "pch.h"

#include "Engine/Graphics/RHI/RHIBackendRegistry.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleImageUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIModuleABI.h"

#if defined( SW_SHIPPING )
    #if defined( SW_RHI_TARGET_DX12 )
        #include "Engine/Graphics/RHI/DX/DX12/D3D12RHIDevice.h"
    #elif defined( SW_RHI_TARGET_DX11 )
        #include "Engine/Graphics/RHI/DX/DX11/D3D11RHIDevice.h"
    #elif defined( SW_RHI_TARGET_VULKAN )
        #include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
    #elif defined( SW_RHI_TARGET_OPENGL )
        #include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
    #elif defined( SW_WITH_CLIENT_CODE )
        #error "Shipping links exactly one RHI backend into Engine: SW_RHI_TARGET_* is missing (SW_SHIPPING_RHI_BACKEND)"
    #endif
#endif

namespace sw
{
    namespace
    {
        struct RHIBackendRegistryInternal
        {
#if defined( SW_SHIPPING )
    #if defined( SW_RHI_TARGET_DX12 )
            static sw::unique_ptr<IRHIDevice> createD3D12Device()
            {
                return make_unique<D3D12RHIDevice>();
            }
    #elif defined( SW_RHI_TARGET_DX11 )
            static sw::unique_ptr<IRHIDevice> createD3D11Device()
            {
                return make_unique<D3D11RHIDevice>();
            }
    #elif defined( SW_RHI_TARGET_VULKAN )
            static sw::unique_ptr<IRHIDevice> createVulkanDevice()
            {
                return make_unique<VulkanRHIDevice>();
            }
    #elif defined( SW_RHI_TARGET_OPENGL )
            static sw::unique_ptr<IRHIDevice> createOpenGLDevice()
            {
                return make_unique<OpenGLRHIDevice>();
            }
    #endif
#else
            [[nodiscard]] static bool tryLoadBackendModule( RHIBackend backend, const utf8* pModuleBaseName )
            {
                RHIBackendRegistry& reg        = engine::getSubsystem<RHIBackendRegistry>();
                const string        dllName    = ModuleImageUtil::formatSharedLibraryName( pModuleBaseName );
                const string        modulePath = ModuleImageUtil::findModuleLibraryPath( pModuleBaseName ); // Bin/Modules

                if ( reg.tryLoadModule( backend, modulePath ) )
                    return true;
                return reg.tryLoadModule( backend, dllName );
            }

            /** @brief 요청한 RHI MODULE 을 필요할 때 하나씩 로드합니다(한꺼번에 로드하지 않습니다). */
            [[nodiscard]] static bool ensureBackendModuleLoaded( RHIBackend backend )
            {
                RHIBackendRegistry&    reg    = engine::getSubsystem<RHIBackendRegistry>();
                const RHIBackendEntry* pEntry = reg.findBackend( backend );
                if ( pEntry != nullptr && pEntry->_factory.isBound() )
                    return true;

                // 이 플랫폼에 없는 백엔드(Linux 의 D3D)는 모듈을 찾지 않는다.
                const RHIBackendInfo* pInfo = RHIBackendInfoTable::find( backend );
                if ( pInfo == nullptr || RHIAvailability::isAvailable( backend ) == false )
                    return false;
                return tryLoadBackendModule( backend, pInfo->_pModuleName );
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "RHIBackendRegistry" );

    RHIBackendRegistry::RHIBackendRegistry()
    {
#if defined( SW_SHIPPING )
    #if defined( SW_RHI_TARGET_DX12 )
        registerBackend( RHIBackend::DirectX12, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, RHIBackendRegistryInternal::createD3D12Device ), RHIAvailability::query( RHIBackend::DirectX12 ) );
    #elif defined( SW_RHI_TARGET_DX11 )
        registerBackend( RHIBackend::DirectX11, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, RHIBackendRegistryInternal::createD3D11Device ), RHIAvailability::query( RHIBackend::DirectX11 ) );
    #elif defined( SW_RHI_TARGET_VULKAN )
        registerBackend( RHIBackend::Vulkan, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, RHIBackendRegistryInternal::createVulkanDevice ), RHIAvailability::query( RHIBackend::Vulkan ) );
    #elif defined( SW_RHI_TARGET_OPENGL )
        registerBackend( RHIBackend::OpenGL, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, RHIBackendRegistryInternal::createOpenGLDevice ), RHIAvailability::query( RHIBackend::OpenGL ) );
    #endif
#endif
    }

    void RHIBackendRegistry::registerBackend( RHIBackend backend, const RHIDeviceFactoryDelegate& factory, const RHICapabilities& caps )
    {
        for ( RHIBackendEntry& entry : _listEntry )
        {
            if ( entry._backend == backend )
            {
                entry._factory = factory;
                entry._caps    = caps;
                return;
            }
        }
        _listEntry.push_back( RHIBackendEntry{ backend, factory, caps } );
    }

    const RHIBackendEntry* RHIBackendRegistry::findBackend( RHIBackend backend ) const
    {
        return VectorUtil::findBy<&RHIBackendEntry::_backend>( _listEntry, backend );
    }

    string RHIBackendRegistry::describeRegisteredBackends() const
    {
        string result;
        for ( const RHIBackendEntry& entry : _listEntry )
        {
            if ( entry._factory.isBound() == false )
                continue;
            if ( result.empty() == false )
                result.append( ", " );
            result.append( RHI::getBackendTypeName( entry._backend ) );
        }

        if ( result.empty() )
            result.append( "(없음)" );
        return result;
    }

    unique_ptr<IRHIDevice> RHIBackendRegistry::createDevice( RHIBackend backend ) const
    {
#if !defined( SW_SHIPPING )
        // 못 올렸으면 바로 아래 조회가 등록되지 않은 백엔드로 보고 무엇이 있는지와 함께 알린다.
        (void)RHIBackendRegistryInternal::ensureBackendModuleLoaded( backend );
#endif

        const RHIBackendEntry* pEntry = findBackend( backend );
        if ( pEntry == nullptr || pEntry->_factory.isBound() == false )
        {
            // 숫자만 찍으면(예: "backend 0") 설정 파일에 DirectX11 을 적어 둔 사람이 원인을 알 수
            // 없다. 배포본은 SW_RHI_TARGET_* 로 고른 백엔드 **하나만** 링크하므로, 무엇이 있는지
            // 같이 알려 준다.
            SW_LOG_ERROR( "The %# backend is not in this build. Available: %#",
                          RHI::getBackendTypeName( backend ), describeRegisteredBackends().c_str() );
            return nullptr;
        }
        if ( RHIAvailability::isAvailable( backend ) == false )
        {
            SW_LOG_ERROR( "Backend %# is not available on this platform", static_cast<int32>( backend ) );
            return nullptr;
        }
        return pEntry->_factory();
    }

    bool RHIBackendRegistry::tryLoadModule( RHIBackend backend, string_view modulePath )
    {
        void* pModuleHandle = ModuleImageUtil::loadDynamicLibrary( modulePath );
        if ( pModuleHandle == nullptr )
            return false;

        PFN_GetRHIModuleAbiVersion pfnVersion = reinterpret_cast<PFN_GetRHIModuleAbiVersion>( ModuleImageUtil::getDynamicSymbol( pModuleHandle, "getRHIModuleABIVersion" ) );
        if ( pfnVersion == nullptr || pfnVersion() != kRHIModuleABIVersion )
        {
            SW_LOG_ERROR( "RHI MODULE ABI version mismatch or missing getRHIModuleABIVersion (%#)", modulePath );
            (void)ModuleImageUtil::unloadModuleImage( modulePath, pModuleHandle );
            return false;
        }

        PFN_GetRHIModuleAbiStamp pfnStamp = reinterpret_cast<PFN_GetRHIModuleAbiStamp>( ModuleImageUtil::getDynamicSymbol( pModuleHandle, "getRHIModuleABIStamp" ) );
        if ( pfnStamp == nullptr || StringUtil::equals( pfnStamp(), kRHIModuleABIStamp ) == false )
        {
            SW_LOG_ERROR( "RHI MODULE ABI stamp mismatch or missing getRHIModuleABIStamp (%#; expected %#)", modulePath, kRHIModuleABIStamp );
            (void)ModuleImageUtil::unloadModuleImage( modulePath, pModuleHandle );
            return false;
        }

        PFN_CreateRHIDevice pfnCreate = reinterpret_cast<PFN_CreateRHIDevice>( ModuleImageUtil::getDynamicSymbol( pModuleHandle, "createRHIDevice" ) );
        if ( pfnCreate == nullptr )
        {
            (void)ModuleImageUtil::unloadModuleImage( modulePath, pModuleHandle );
            return false;
        }

        RHIDeviceFactoryDelegate factory = SW_DELEGATE_LAMBDA( RHIDeviceFactoryDelegate, [pfnCreate]() -> sw::unique_ptr<IRHIDevice>
        {
            return sw::unique_ptr<IRHIDevice>{ pfnCreate() };
        } );

        registerBackend( backend, factory, RHIAvailability::query( backend ) );
        _listLoadedModule.push_back( LoadedModule{ backend, pModuleHandle, string{ modulePath } } );

        SW_LOG_INFO( "Loaded module %# for backend %#", modulePath, RHI::getBackendTypeName( backend ) );
        return true;
    }

    void RHIBackendRegistry::unloadModules()
    {
        if ( _listLoadedModule.empty() )
            return;

        // FreeLibrary 전에 팩토리를 지운다. 그래야 create() 가 내려간 코드로 들어가지 않는다.
        // 이 팩토리가 만든 디바이스는 이미 파괴돼 있어야 한다(RHI::shutdown 참고).
        for ( const LoadedModule& module : _listLoadedModule )
        {
            for ( RHIBackendEntry& entry : _listEntry )
            {
                if ( entry._backend == module._backend )
                {
                    entry._factory = {};
                    break;
                }
            }
        }

        // 언로드하지 못하면(다른 코드가 아직 그 이미지의 이벤트 채널을 구독한다) 프로세스 끝까지 남아 있을 뿐이다. 이유는 경고로 남는다.
        for ( const LoadedModule& module : _listLoadedModule )
        {
            (void)ModuleImageUtil::unloadModuleImage( module._modulePath, module._pHandle );
        }
        _listLoadedModule.clear();
    }

    RHIBackendRegistry::~RHIBackendRegistry()
    {
        unloadModules();
    }
} // namespace sw
