#include "pch.h"

#include "Engine/UserSettings/UserSettingsHandlerRegistry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Registry/RegistrationChain.h"

#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw
{
    namespace
    {
        struct UserSettingsHandlerRegistryInternal
        {
            /** @brief 핸들러 묶음 등록자 사슬입니다(상수 초기화 — 정적 초기화 순서와 무관하다). */
            static SW_DEFINE_REGISTRATION_CHAIN( getRegistrarChain, UserSettingsHandlerSetRegistrar )
        };
    } // namespace
} // namespace sw

namespace sw
{
    UserSettingsHandlerSetRegistrar::UserSettingsHandlerSetRegistrar( const utf8* pName, RegisterFunction pRegister )
        : _pName{ pName }
        , _pRegister{ pRegister }
        , _pNext{ UserSettingsHandlerRegistryInternal::getRegistrarChain().pushFront( this ) }
    {
    }

    const UserSettingsHandlerSetRegistrar* UserSettingsHandlerSetRegistrar::getHead()
    {
        return UserSettingsHandlerRegistryInternal::getRegistrarChain().getHead();
    }

    void UserSettingsHandlerSetRegistrar::registerAll( UserSettingsHandlerRegistry& registry, vector<Subscription>& outListRegistration )
    {
        for ( const UserSettingsHandlerSetRegistrar* pRegistrar = getHead(); pRegistrar != nullptr; pRegistrar = pRegistrar->_pNext )
        {
            pRegistrar->_pRegister( registry, outListRegistration );
        }
    }
} // namespace sw

namespace sw
{
    UserSettingsHandlerRegistry::UserSettingsHandlerRegistry()
        : _listHandler{}
        , _listProvider{}
        , _subscriptionIDAllocator{}
    {
    }

    Subscription UserSettingsHandlerRegistry::registerHandler( const hashed_string& name, const UserSettingHandlerDelegate& handler, const UserSettingValueFilterDelegate& filter )
    {
        const uint64 subscriptionID = _subscriptionIDAllocator.allocate();
        for ( HandlerEntry& entry : _listHandler )
        {
            if ( entry._name == name )
            {
                entry._handler        = handler;
                entry._filter         = filter;
                entry._subscriptionID = subscriptionID;
                return _subscriptions.issue( subscriptionID );
            }
        }
        _listHandler.push_back( HandlerEntry{ name, handler, filter, subscriptionID } );
        return _subscriptions.issue( subscriptionID );
    }

    bool UserSettingsHandlerRegistry::hasHandler( const hashed_string& name ) const
    {
        return findHandlerEntry( name ) != nullptr;
    }

    bool UserSettingsHandlerRegistry::invokeHandler( const hashed_string& name, const UserSettingApplyParams& context ) const
    {
        const HandlerEntry* pEntry = findHandlerEntry( name );
        if ( pEntry == nullptr || pEntry->_handler.isBound() == false )
            return false;
        // 핸들러가 레지스트리를 바꿀 수 있으므로(다른 핸들러를 등록하는 게임 코드) 사본을 부른다.
        const UserSettingHandlerDelegate handler = pEntry->_handler;
        return handler( context );
    }

    bool UserSettingsHandlerRegistry::canAcceptValue( const hashed_string& name, string_view value ) const
    {
        const HandlerEntry* pEntry = findHandlerEntry( name );
        if ( pEntry == nullptr || pEntry->_filter.isBound() == false )
            return true;
        return pEntry->_filter( value );
    }

    Subscription UserSettingsHandlerRegistry::registerOptionProvider( const hashed_string& name, const UserSettingOptionProviderDelegate& provider )
    {
        const uint64 subscriptionID = _subscriptionIDAllocator.allocate();
        for ( ProviderEntry& entry : _listProvider )
        {
            if ( entry._name == name )
            {
                entry._provider       = provider;
                entry._subscriptionID = subscriptionID;
                return _subscriptions.issue( subscriptionID );
            }
        }
        _listProvider.push_back( ProviderEntry{ name, provider, subscriptionID } );
        return _subscriptions.issue( subscriptionID );
    }

    void UserSettingsHandlerRegistry::releaseSubscription( uint64 id )
    {
        const optional<size_t> handlerIndex = VectorUtil::findIndexBy<&HandlerEntry::_subscriptionID>( _listHandler, id );
        if ( handlerIndex.has_value() )
        {
            _listHandler.erase( _listHandler.begin() + static_cast<ptrdiff_t>( *handlerIndex ) );
            return;
        }
        const optional<size_t> providerIndex = VectorUtil::findIndexBy<&ProviderEntry::_subscriptionID>( _listProvider, id );
        if ( providerIndex.has_value() )
            _listProvider.erase( _listProvider.begin() + static_cast<ptrdiff_t>( *providerIndex ) );
    }

    bool UserSettingsHandlerRegistry::hasOptionProvider( const hashed_string& name ) const
    {
        return findProviderEntry( name ) != nullptr;
    }

    bool UserSettingsHandlerRegistry::collectOptions( const hashed_string& name, vector<UserSettingOption>& outListOption ) const
    {
        outListOption.clear();
        const ProviderEntry* pEntry = findProviderEntry( name );
        if ( pEntry == nullptr || pEntry->_provider.isBound() == false )
            return false;
        pEntry->_provider( outListOption );
        return true;
    }

    void UserSettingsHandlerRegistry::clear()
    {
        _subscriptions.detachAll();
        _listHandler.clear();
        _listProvider.clear();
    }

    const UserSettingsHandlerRegistry::HandlerEntry* UserSettingsHandlerRegistry::findHandlerEntry( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&HandlerEntry::_name>( _listHandler, name );
    }

    const UserSettingsHandlerRegistry::ProviderEntry* UserSettingsHandlerRegistry::findProviderEntry( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&ProviderEntry::_name>( _listProvider, name );
    }
} // namespace sw
