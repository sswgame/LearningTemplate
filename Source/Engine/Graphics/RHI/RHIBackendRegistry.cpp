#include "pch.h"

#include "Engine/Graphics/RHI/RHIBackendRegistry.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHIModuleABI.h"

namespace sw
{
    namespace
    {
        struct RHIBackendRegistryInternal
        {
            /** @brief 정적 등록자 목록의 머리입니다(함수 정적 — 정적 초기화 순서와 무관하다). */
            static const RHIStaticBackendRegistrar*& getStaticHead()
            {
                static const RHIStaticBackendRegistrar* s_pHead = nullptr;
                return s_pHead;
            }

#if SW_WITH_DYNAMIC_MODULES
            [[nodiscard]] static bool tryLoadBackendModule( RHIBackend backend, const utf8* pModuleBaseName )
            {
                RHIBackendRegistry& reg        = engine::getSubsystem<RHIBackendRegistry>();
                const string        dllName    = ModuleLibraryUtil::formatSharedLibraryName( pModuleBaseName );
                const string        modulePath = ModuleLibraryUtil::findModuleLibraryPath( pModuleBaseName ); // Bin/Modules

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

    RHIStaticBackendRegistrar::RHIStaticBackendRegistrar( RHIBackend backend, CreateFunction pCreate )
        : _backend{ backend }
        , _pCreate{ pCreate }
        , _pNext{ nullptr }
    {
        _pNext                                      = RHIBackendRegistryInternal::getStaticHead();
        RHIBackendRegistryInternal::getStaticHead() = this;
    }

    const RHIStaticBackendRegistrar* RHIStaticBackendRegistrar::getHead()
    {
        return RHIBackendRegistryInternal::getStaticHead();
    }

    RHIBackendRegistry::RHIBackendRegistry()
    {
        // 정적으로 링크한 백엔드(배포)는 스스로 올랐다 — 동적 모듈 구성은 비어 있고 `createDevice` 가 필요할 때 모듈을 올린다.
        for ( const RHIStaticBackendRegistrar* pRegistrar = RHIStaticBackendRegistrar::getHead(); pRegistrar != nullptr; pRegistrar = pRegistrar->_pNext )
        {
            const RHIStaticBackendRegistrar::CreateFunction pCreate = pRegistrar->_pCreate;
            registerBackend( pRegistrar->_backend, SW_DELEGATE_LAMBDA( RHIDeviceFactoryDelegate, [pCreate]() -> sw::unique_ptr<IRHIDevice>
            {
                return sw::unique_ptr<IRHIDevice>{ pCreate() };
            } ),
                             RHIAvailability::query( pRegistrar->_backend ) );
        }
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
#if SW_WITH_DYNAMIC_MODULES
        // 못 올렸으면 바로 아래 조회가 등록되지 않은 백엔드로 보고 무엇이 있는지와 함께 알린다.
        (void)RHIBackendRegistryInternal::ensureBackendModuleLoaded( backend );
#endif

        const RHIBackendEntry* pEntry = findBackend( backend );
        if ( pEntry == nullptr || pEntry->_factory.isBound() == false )
        {
            // 숫자만 찍으면(예: "backend 0") 설정 파일에 DirectX11 을 적어 둔 사람이 원인을 알 수
            // 없다. 배포본은 고른 백엔드 **하나만** 링크하므로(`SW_SHIPPING_RHI_BACKEND`), 무엇이 있는지
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

#if SW_WITH_DYNAMIC_MODULES
    bool RHIBackendRegistry::tryLoadModule( RHIBackend backend, string_view modulePath )
    {
        void* pModuleHandle = ModuleLibraryUtil::loadDynamicLibrary( modulePath );
        if ( pModuleHandle == nullptr )
            return false;

        PFN_GetRHIModuleAbiVersion pfnVersion = reinterpret_cast<PFN_GetRHIModuleAbiVersion>( ModuleLibraryUtil::getDynamicSymbol( pModuleHandle, "getRHIModuleABIVersion" ) );
        if ( pfnVersion == nullptr || pfnVersion() != kRHIModuleABIVersion )
        {
            SW_LOG_ERROR( "RHI MODULE ABI version mismatch or missing getRHIModuleABIVersion (%#)", modulePath );
            (void)ModuleLibraryUtil::unloadModuleLibrary( modulePath, pModuleHandle );
            return false;
        }

        PFN_GetRHIModuleABIStamp pfnStamp = reinterpret_cast<PFN_GetRHIModuleABIStamp>( ModuleLibraryUtil::getDynamicSymbol( pModuleHandle, "getRHIModuleABIStamp" ) );
        if ( pfnStamp == nullptr || StringUtil::equals( pfnStamp(), kRHIModuleABIStamp ) == false )
        {
            SW_LOG_ERROR( "RHI MODULE ABI stamp mismatch or missing getRHIModuleABIStamp (%#; expected %#)", modulePath, kRHIModuleABIStamp );
            (void)ModuleLibraryUtil::unloadModuleLibrary( modulePath, pModuleHandle );
            return false;
        }

        PFN_CreateRHIDevice pfnCreate = reinterpret_cast<PFN_CreateRHIDevice>( ModuleLibraryUtil::getDynamicSymbol( pModuleHandle, "createRHIDevice" ) );
        if ( pfnCreate == nullptr )
        {
            (void)ModuleLibraryUtil::unloadModuleLibrary( modulePath, pModuleHandle );
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

#endif // SW_WITH_DYNAMIC_MODULES

    void RHIBackendRegistry::unloadModules()
    {
#if SW_WITH_DYNAMIC_MODULES // 배포 빌드는 백엔드가 정적이라 내릴 DLL 이 없다
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
            (void)ModuleLibraryUtil::unloadModuleLibrary( module._modulePath, module._pHandle );
        }
        _listLoadedModule.clear();
#endif
    }

    RHIBackendRegistry::~RHIBackendRegistry()
    {
        unloadModules();
    }
} // namespace sw
