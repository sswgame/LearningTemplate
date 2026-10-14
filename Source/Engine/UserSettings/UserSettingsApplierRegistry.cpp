#include "pch.h"

#include "Engine/UserSettings/UserSettingsApplierRegistry.h"

#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw
{
    UserSettingsApplierRegistry::UserSettingsApplierRegistry()
        : _listApplier{}
        , _listProvider{}
    {
    }

    void UserSettingsApplierRegistry::registerApplier( const hashed_string& name, const UserSettingApplierDelegate& applier, const UserSettingValueFilterDelegate& filter )
    {
        for ( ApplierEntry& entry : _listApplier )
        {
            if ( entry._name == name )
            {
                entry._applier = applier;
                entry._filter  = filter;
                return;
            }
        }
        _listApplier.push_back( ApplierEntry{ name, applier, filter } );
    }

    void UserSettingsApplierRegistry::unregisterApplier( const hashed_string& name )
    {
        for ( size_t entryIndex = 0; entryIndex < _listApplier.size(); ++entryIndex )
        {
            if ( _listApplier[entryIndex]._name == name )
            {
                _listApplier.erase( _listApplier.begin() + static_cast<ptrdiff_t>( entryIndex ) );
                return;
            }
        }
    }

    bool UserSettingsApplierRegistry::hasApplier( const hashed_string& name ) const
    {
        return findApplierEntry( name ) != nullptr;
    }

    bool UserSettingsApplierRegistry::invokeApplier( const hashed_string& name, const UserSettingApplyParams& context ) const
    {
        const ApplierEntry* pEntry = findApplierEntry( name );
        if ( pEntry == nullptr || pEntry->_applier.isBound() == false )
            return false;
        // 적용 함수가 레지스트리를 바꿀 수 있으므로(다른 적용 함수를 등록하는 게임 코드) 사본을 부른다.
        const UserSettingApplierDelegate applier = pEntry->_applier;
        return applier( context );
    }

    bool UserSettingsApplierRegistry::acceptsValue( const hashed_string& name, string_view value ) const
    {
        const ApplierEntry* pEntry = findApplierEntry( name );
        if ( pEntry == nullptr || pEntry->_filter.isBound() == false )
            return true;
        return pEntry->_filter( value );
    }

    void UserSettingsApplierRegistry::registerOptionProvider( const hashed_string& name, const UserSettingOptionProviderDelegate& provider )
    {
        for ( ProviderEntry& entry : _listProvider )
        {
            if ( entry._name == name )
            {
                entry._provider = provider;
                return;
            }
        }
        _listProvider.push_back( ProviderEntry{ name, provider } );
    }

    bool UserSettingsApplierRegistry::hasOptionProvider( const hashed_string& name ) const
    {
        return findProviderEntry( name ) != nullptr;
    }

    bool UserSettingsApplierRegistry::collectOptions( const hashed_string& name, vector<UserSettingOption>& outListOption ) const
    {
        outListOption.clear();
        const ProviderEntry* pEntry = findProviderEntry( name );
        if ( pEntry == nullptr || pEntry->_provider.isBound() == false )
            return false;
        pEntry->_provider( outListOption );
        return true;
    }

    uint32 UserSettingsApplierRegistry::removeCodeWithin( const void* pBegin, const void* pEnd )
    {
        uint32 removedCount{ 0 };
        for ( size_t entryIndex = _listApplier.size(); entryIndex > 0; --entryIndex )
        {
            const ApplierEntry& entry        = _listApplier[entryIndex - 1];
            const bool          bInsideRange = entry._applier.isCodeWithin( pBegin, pEnd ) || entry._filter.isCodeWithin( pBegin, pEnd );
            if ( bInsideRange )
            {
                _listApplier.erase( _listApplier.begin() + static_cast<ptrdiff_t>( entryIndex - 1 ) );
                ++removedCount;
            }
        }
        for ( size_t entryIndex = _listProvider.size(); entryIndex > 0; --entryIndex )
        {
            if ( _listProvider[entryIndex - 1]._provider.isCodeWithin( pBegin, pEnd ) )
            {
                _listProvider.erase( _listProvider.begin() + static_cast<ptrdiff_t>( entryIndex - 1 ) );
                ++removedCount;
            }
        }
        return removedCount;
    }

    void UserSettingsApplierRegistry::clear()
    {
        _listApplier.clear();
        _listProvider.clear();
    }

    const UserSettingsApplierRegistry::ApplierEntry* UserSettingsApplierRegistry::findApplierEntry( const hashed_string& name ) const
    {
        for ( const ApplierEntry& entry : _listApplier )
        {
            if ( entry._name == name )
                return &entry;
        }
        return nullptr;
    }

    const UserSettingsApplierRegistry::ProviderEntry* UserSettingsApplierRegistry::findProviderEntry( const hashed_string& name ) const
    {
        for ( const ProviderEntry& entry : _listProvider )
        {
            if ( entry._name == name )
                return &entry;
        }
        return nullptr;
    }
} // namespace sw
