#include "pch.h"

#include "GameFramework/Base/Actor/AI/BehaviorTaskRegistry.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "BehaviorTaskRegistry" );

    BehaviorTaskRegistry& BehaviorTaskRegistry::get()
    {
        static BehaviorTaskRegistry s_registry;
        return s_registry;
    }

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
            result = _registration.addLogged( pEntry, "Behavior task", pEntry->_pName );
        }
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
        , _bRegistered{ false }
    {
        _bRegistered = BehaviorTaskRegistry::get().registerTask( pEntry );
    }

    BehaviorTaskRegistrar::~BehaviorTaskRegistrar()
    {
        if ( _bRegistered )
            BehaviorTaskRegistry::get().unregisterTask( _pEntry );
    }
} // namespace sw
