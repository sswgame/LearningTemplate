#include "pch.h"

#include "Engine/Graphics/RHI/RHIBackendRegistry.h"

#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Modules/RHIModuleAbi.h"
#include "Engine/Graphics/RHI/RHI.h"

#if !defined( SW_RHI_AS_MODULES )
    #if defined( SW_SHIPPING )
        #if defined( SW_RHI_TARGET_DX12 )
            #include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
        #elif defined( SW_RHI_TARGET_DX11 )
            #include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
        #elif defined( SW_RHI_TARGET_VULKAN )
            #include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
        #elif defined( SW_RHI_TARGET_OPENGL )
            #include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
        #endif
    #else
        #if defined( SW_PLATFORM_WINDOWS )
            #include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
            #include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
        #endif
        #include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
        #include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
    #endif
#endif

namespace sw
{
    namespace
    {
        struct RHIBackendRegistryInternal
        {
#if !defined( SW_RHI_AS_MODULES )
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
        #if defined( SW_PLATFORM_WINDOWS )
            static sw::unique_ptr<IRHIDevice> createD3D11Device()
            {
                return make_unique<D3D11RHIDevice>();
            }
            static sw::unique_ptr<IRHIDevice> createD3D12Device()
            {
                return make_unique<D3D12RHIDevice>();
            }
        #endif
            static sw::unique_ptr<IRHIDevice> createVulkanDevice()
            {
                return make_unique<VulkanRHIDevice>();
            }
            static sw::unique_ptr<IRHIDevice> createOpenGLDevice()
            {
                return make_unique<OpenGLRHIDevice>();
            }
    #endif
#endif

#if defined( SW_RHI_AS_MODULES )
            static bool tryLoadBackendModule( RHIBackend backend, const utf8* pModuleBaseName )
            {
                RHIBackendRegistry& reg       = engine::getRHIBackendRegistry();
                const string        dllName   = FileUtil::formatSharedLibraryName( pModuleBaseName );
                const string        execDir   = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
                const string        besideExe = execDir.empty() ? dllName.c_str() : FileUtil::joinPath( execDir, dllName );

                if ( reg.tryLoadModule( backend, besideExe ) )
                    return true;
                return reg.tryLoadModule( backend, dllName );
            }

            /** @brief Load requested RHI MODULE on-demand when needed (not all at once). */
            static bool ensureBackendModuleLoaded( RHIBackend backend )
            {
                RHIBackendRegistry&    reg    = engine::getRHIBackendRegistry();
                const RHIBackendEntry* pEntry = reg.findBackend( backend );
                if ( pEntry != nullptr && pEntry->_factory.isBound() )
                    return true;

                switch ( backend )
                {
    #if defined( SW_PLATFORM_WINDOWS )
                    case RHIBackend::DirectX11:
                        return tryLoadBackendModule( RHIBackend::DirectX11, "RHI_DX11" );
                    case RHIBackend::DirectX12:
                        return tryLoadBackendModule( RHIBackend::DirectX12, "RHI_DX12" );
    #endif
                    case RHIBackend::OpenGL:
                        return tryLoadBackendModule( RHIBackend::OpenGL, "RHI_GL" );
                    case RHIBackend::Vulkan:
                        return tryLoadBackendModule( RHIBackend::Vulkan, "RHI_Vulkan" );
                    default:
                        return false;
                }
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
#if !defined( SW_RHI_AS_MODULES )
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
    #else
        #if defined( SW_PLATFORM_WINDOWS )
        registerBackend( RHIBackend::DirectX11, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, RHIBackendRegistryInternal::createD3D11Device ), RHIAvailability::query( RHIBackend::DirectX11 ) );
        registerBackend( RHIBackend::DirectX12, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, RHIBackendRegistryInternal::createD3D12Device ), RHIAvailability::query( RHIBackend::DirectX12 ) );
        #endif
        registerBackend( RHIBackend::Vulkan, SW_DELEGATE_FUNCTION( RHIDeviceFactoryDelegate, RHIBackendRegistryInternal::createVulkanDevice ), RHIAvailability::query( RHIBackend::Vulkan ) );
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
        for ( const RHIBackendEntry& entry : _listEntry )
        {
            if ( entry._backend == backend )
                return &entry;
        }
        return nullptr;
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
#if defined( SW_RHI_AS_MODULES )
        RHIBackendRegistryInternal::ensureBackendModuleLoaded( backend );
#endif

        const RHIBackendEntry* pEntry = findBackend( backend );
        if ( pEntry == nullptr || pEntry->_factory.isBound() == false )
        {
            // 숫자만 찍으면(예: "backend 0") 설정 파일에 DirectX11 을 적어 둔 사람이 원인을 알 수
            // 없다. 배포본은 SW_RHI_TARGET_* 로 고른 백엔드 **하나만** 링크하므로, 무엇이 있는지
            // 같이 알려 준다.
            SW_LOG_ERROR( "%# 백엔드는 이 빌드에 없습니다. 사용 가능: %#",
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
        void* pModuleHandle = FileUtil::loadDynamicLibrary( modulePath );
        if ( pModuleHandle == nullptr )
            return false;

        PFN_GetRHIModuleAbiVersion pfnVersion = reinterpret_cast<PFN_GetRHIModuleAbiVersion>( FileUtil::getDynamicSymbol( pModuleHandle, "getRHIModuleAbiVersion" ) );
        if ( pfnVersion == nullptr || pfnVersion() != kRHIModuleAbiVersion )
        {
            SW_LOG_ERROR( "RHI MODULE ABI version mismatch or missing getRHIModuleAbiVersion (%#)", modulePath );
            FileUtil::unloadDynamicLibrary( pModuleHandle );
            return false;
        }

        PFN_GetRHIModuleAbiStamp pfnStamp = reinterpret_cast<PFN_GetRHIModuleAbiStamp>( FileUtil::getDynamicSymbol( pModuleHandle, "getRHIModuleAbiStamp" ) );
        if ( pfnStamp == nullptr || StringUtil::equals( pfnStamp(), kRHIModuleAbiStamp ) == false )
        {
            SW_LOG_ERROR( "RHI MODULE ABI stamp mismatch or missing getRHIModuleAbiStamp (%#; expected %#)", modulePath, kRHIModuleAbiStamp );
            FileUtil::unloadDynamicLibrary( pModuleHandle );
            return false;
        }

        PFN_CreateRHIDevice pfnCreate = reinterpret_cast<PFN_CreateRHIDevice>( FileUtil::getDynamicSymbol( pModuleHandle, "createRHIDevice" ) );
        if ( pfnCreate == nullptr )
        {
            FileUtil::unloadDynamicLibrary( pModuleHandle );
            return false;
        }

        RHIDeviceFactoryDelegate factory = SW_DELEGATE_LAMBDA( RHIDeviceFactoryDelegate, [pfnCreate]() -> sw::unique_ptr<IRHIDevice>
        {
            return sw::unique_ptr<IRHIDevice>{ pfnCreate() };
        } );

        registerBackend( backend, factory, RHIAvailability::query( backend ) );
        _listLoadedModule.push_back( LoadedModule{ backend, pModuleHandle } );

        SW_LOG_INFO( "Loaded module %# for backend %#", modulePath, RHI::getBackendTypeName( backend ) );
        return true;
    }

    void RHIBackendRegistry::unloadModules()
    {
        if ( _listLoadedModule.empty() )
            return;

        // Clear factories before FreeLibrary so create() cannot dispatch into unloaded code.
        // Devices from these factories must already be destroyed (see RHI::shutdown).
        for ( const auto& [backEnd, handle] : _listLoadedModule )
        {
            for ( RHIBackendEntry& entry : _listEntry )
            {
                if ( entry._backend == backEnd )
                {
                    entry._factory = {};
                    break;
                }
            }
        }

        for ( const LoadedModule& module : _listLoadedModule )
        {
            if ( module._pHandle != nullptr )
                FileUtil::unloadDynamicLibrary( module._pHandle );
        }
        _listLoadedModule.clear();
    }

    RHIBackendRegistry::~RHIBackendRegistry()
    {
        unloadModules();
    }
} // namespace sw
