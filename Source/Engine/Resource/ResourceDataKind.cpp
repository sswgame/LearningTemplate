#include "pch.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "ResourceDataKind" );

    bool ResourceDataKind::matches( string_view resourceID ) const
    {
        if ( _pPrefix == nullptr && _pContains == nullptr && _pSuffix == nullptr )
            return false;
        if ( _pPrefix != nullptr && StringUtil::startsWith( resourceID, string_view( _pPrefix ), true ) == false )
            return false;
        if ( _pContains != nullptr && StringUtil::contains( resourceID, string_view( _pContains ), true ) == false )
            return false;
        if ( _pSuffix != nullptr && StringUtil::endsWith( resourceID, string_view( _pSuffix ), true ) == false )
            return false;
        return true;
    }
} // namespace sw

namespace sw
{
    ResourceDataKindRegistry& ResourceDataKindRegistry::get()
    {
        static ResourceDataKindRegistry s_registry;
        return s_registry;
    }

    ResourceDataKindRegistry::ResourceDataKindRegistry()
        : _mutex{}
        , _registration{ "ResourceDataKindRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
    {
    }

    bool ResourceDataKindRegistry::registerKind( const ResourceDataKind* pKind )
    {
        if ( pKind == nullptr || StringUtil::isNullOrEmpty( pKind->_pLabel ) || pKind->_pLoad == nullptr )
            return false;
        RegistrationResult result = RegistrationResult::Added;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            result = _registration.add( pKind, pKind->_pLabel );
        }
        if ( result == RegistrationResult::DuplicateName || result == RegistrationResult::AlreadyPresent )
        {
            SW_LOG_ERROR( "Resource data kind '%#' is already registered - the second registration is ignored", pKind->_pLabel );
            return false;
        }
        return result == RegistrationResult::Added;
    }

    void ResourceDataKindRegistry::unregisterKind( const ResourceDataKind* pKind )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        (void)_registration.remove( pKind ); // 등록되어 있지 않은 종류를 빼는 것은 할 일이 없다
    }

    const ResourceDataKind* ResourceDataKindRegistry::findByLabel( string_view label ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _registration.findByName( label );
    }

    void ResourceDataKindRegistry::collectMatches( string_view resourceID, vector<const ResourceDataKind*>& outListKind ) const
    {
        outListKind.clear();
        std::scoped_lock<mutex> lock{ _mutex };
        for ( const ResourceDataKind* pKind : _registration.getItems() )
        {
            if ( pKind->matches( resourceID ) )
                outListKind.push_back( pKind );
        }
    }

    void ResourceDataKindRegistry::collectKinds( vector<const ResourceDataKind*>& outListKind ) const
    {
        outListKind.clear();
        std::scoped_lock<mutex> lock{ _mutex };
        for ( const ResourceDataKind* pKind : _registration.getItems() )
        {
            outListKind.push_back( pKind );
        }
    }

    uint32 ResourceDataKindRegistry::getCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _registration.getCount();
    }
} // namespace sw

namespace sw
{
    ResourceDataKindRegistrar::ResourceDataKindRegistrar( const ResourceDataKind* pKind )
        : _pKind{ pKind }
        , _bRegistered{ false }
    {
        _bRegistered = ResourceDataKindRegistry::get().registerKind( pKind );
    }

    ResourceDataKindRegistrar::~ResourceDataKindRegistrar()
    {
        if ( _bRegistered )
            ResourceDataKindRegistry::get().unregisterKind( _pKind );
    }
} // namespace sw
