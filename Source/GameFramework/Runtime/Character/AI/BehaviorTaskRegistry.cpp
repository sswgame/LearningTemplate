#include "pch.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTaskRegistry.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

namespace sw
{
    BehaviorTaskRegistry::BehaviorTaskRegistry()
        : _mutex{}
        , _registration{ "BehaviorTaskRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
    {
    }

    bool BehaviorTaskRegistry::registerTask( const BehaviorTaskEntry* pEntry )
    {
        const bool bInvalidEntry = pEntry == nullptr || StringUtil::isNullOrEmpty( pEntry->_pName ) || pEntry->_function == nullptr;
        if ( bInvalidEntry )
            return false;
        RegistrationResult result = RegistrationResult::Added;
        {
            ScopedLock<mutex> lock{ _mutex };
            result = _registration.add( pEntry, pEntry->_pName );
        }
        reportRegistrationRejected( result, "Behavior task", pEntry->_pName );
        return result == RegistrationResult::Added;
    }

    void BehaviorTaskRegistry::unregisterTask( const BehaviorTaskEntry* pEntry )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_registration.remove( pEntry ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    BehaviorTaskFunction BehaviorTaskRegistry::findTask( string_view name ) const
    {
        ScopedLock<mutex>        lock{ _mutex };
        const BehaviorTaskEntry* pEntry = _registration.findByName( name );
        return pEntry != nullptr ? pEntry->_function : nullptr;
    }

    string BehaviorTaskRegistry::joinNames() const
    {
        string            joined;
        ScopedLock<mutex> lock{ _mutex };
        for ( const BehaviorTaskEntry* pEntry : _registration.getItems() )
        {
            if ( joined.empty() == false )
                joined += ", ";
            joined += pEntry->_pName;
        }
        return joined;
    }

    void BehaviorTaskRegistry::collectNames( vector<string>& outListName ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( const BehaviorTaskEntry* pEntry : _registration.getItems() )
        {
            outListName.push_back( string{ pEntry->_pName } );
        }
    }

    uint32 BehaviorTaskRegistry::getCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.getCount();
    }
} // namespace sw

namespace sw
{
    BehaviorTaskRegistrar::BehaviorTaskRegistrar( const BehaviorTaskEntry* pEntry )
        : _pEntry{ pEntry }
        , _bRegistered{ SW_FALSE }
    {
    }

    void BehaviorTaskRegistrar::attach( const ISubsystemLocator& scope )
    {
        _bRegistered = requireSubsystem<BehaviorTaskRegistry>( scope ).registerTask( _pEntry ) ? SW_TRUE : SW_FALSE;
    }

    void BehaviorTaskRegistrar::detach( const ISubsystemLocator& scope )
    {
        // 종료 때는 공용 모듈의 표가 키트보다 먼저 내려간다 — 표가 없으면 뺄 것도 없다.
        BehaviorTaskRegistry* const pRegistry = locateSubsystem<BehaviorTaskRegistry>( &scope );
        if ( _bRegistered == SW_TRUE && pRegistry != nullptr )
            pRegistry->unregisterTask( _pEntry );
        _bRegistered = SW_FALSE;
    }

    SW_MODULE_SUBSYSTEM( Engine, BehaviorTaskRegistry, BehaviorTaskRegistry );
} // namespace sw
