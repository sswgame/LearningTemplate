#include "pch.h"

#include "Engine/Module/ModuleRegistrationHost.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

SW_LOG_CALLER( "ModuleRegistrationHost" );
namespace sw
{
    namespace
    {
        constexpr uint8 kScopeCount = static_cast<uint8>( ModuleSubsystemScope::Engine ) + 1; ///< 범위 열거자 수
    } // namespace

    ModuleRegistrationHost::ModuleRegistrationHost( SubsystemScope& processScope, SubsystemScope& engineScope, SubsystemHostMask hostMask )
        : _pProcessScope{ &processScope }
        , _pEngineScope{ &engineScope }
        , _hostMask{ hostMask }
        , _mapHead{}
        , _mapAttachedObject{}
    {
    }

    SubsystemScope& ModuleRegistrationHost::resolveScope( uint8 scope ) const
    {
        return scope == static_cast<uint8>( ModuleSubsystemScope::Process ) ? *_pProcessScope : *_pEngineScope;
    }

    bool ModuleRegistrationHost::mountModule( string_view moduleName, const ModuleSubsystemRegistrar* pHead )
    {
        // 사슬은 나중에 만든 것이 앞이다 — 만든 순서(정적 초기화 순서)로 더한다. 초기화 순서는 그와 상관없이 의존이 정한다.
        vector<const ModuleSubsystemRegistrar*> listRegistrar;
        for ( const ModuleSubsystemRegistrar* pRegistrar = pHead; pRegistrar != nullptr; pRegistrar = pRegistrar->getNext() )
        {
            listRegistrar.push_back( pRegistrar );
        }
        bool bMounted = true;
        for ( uint8 scope = 0; scope < kScopeCount; ++scope )
        {
            SubsystemScope& target        = resolveScope( scope );
            bool            bHasRegistrar = false;
            for ( const ModuleSubsystemRegistrar* pRegistrar : listRegistrar )
            {
                bHasRegistrar = bHasRegistrar || static_cast<uint8>( pRegistrar->getTargetScope() ) == scope;
            }
            if ( bHasRegistrar == false )
                continue;
            // 같은 모듈을 내리지 않고 다시 올리면 앞 이미지의 서브시스템이 남는다 — 올리는 쪽의 잘못이라 덮지 않고 알린다.
            if ( target.findChildScope( moduleName ) != nullptr )
            {
                SW_LOG_ERROR( "Module '%#' mounted its subsystems again without unloading - the earlier image's subsystems stay", moduleName );
                bMounted = false;
                continue;
            }
            SubsystemScope& moduleScope = target.mountChildScope( moduleName );
            for ( size_t index = listRegistrar.size(); index > 0; --index )
            {
                if ( static_cast<uint8>( listRegistrar[index - 1]->getTargetScope() ) == scope )
                    listRegistrar[index - 1]->addTo( moduleScope );
            }
            if ( moduleScope.getCollection().initializeAll( _hostMask ) == false )
            {
                SW_LOG_ERROR( "Module '%#' subsystems failed: %#", moduleName, moduleScope.getCollection().getError().c_str() );
                target.unmountChildScope( moduleName );
                bMounted = false;
            }
        }
        return bMounted;
    }

    void ModuleRegistrationHost::unmountModule( string_view moduleName )
    {
        // 엔진 범위 쪽이 프로세스 범위 쪽을 찾을 수 있다 — 매단 반대 순서로 뗀다.
        _pEngineScope->unmountChildScope( moduleName );
        _pProcessScope->unmountChildScope( moduleName );
    }

    void* ModuleRegistrationHost::findInScope( uint64 typeHash ) const
    {
        void* const pFound = _pProcessScope->findInMounted( typeHash );
        return pFound != nullptr ? pFound : _pEngineScope->findInMounted( typeHash );
    }
} // namespace sw
