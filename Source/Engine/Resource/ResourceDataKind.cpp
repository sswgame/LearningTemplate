#include "pch.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "ResourceDataKind" );

    bool ResourceDataKind::matches( string_view resourceID ) const
    {
        const bool bNoMatchRule = _pPrefix == nullptr && _pContains == nullptr && _pSuffix == nullptr;
        if ( bNoMatchRule )
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
    ResourceDataKindRegistry::ResourceDataKindRegistry()
        : _mutex{}
        , _registration{ "ResourceDataKindRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
        , _mapModuleKind{}
    {
    }

    uint32 ResourceDataKindRegistry::registerModuleKinds( string_view moduleName, const ResourceDataKindRegistrar* pHead )
    {
        // 목록은 나중에 만든 것이 앞이다 — 모아서 뒤에서부터 올리면 정적 초기화 순서(= 등록 순서)가 된다.
        vector<const ResourceDataKind*> listKind;
        for ( const ResourceDataKindRegistrar* pRegistrar = pHead; pRegistrar != nullptr; pRegistrar = pRegistrar->_pNext )
        {
            listKind.push_back( pRegistrar->_pKind );
        }
        uint32 registeredCount = 0;
        for ( size_t index = listKind.size(); index > 0; --index )
        {
            const ResourceDataKind* pKind = listKind[index - 1];
            if ( registerKind( pKind ) == false )
                continue;
            ScopedLock<mutex> lock{ _mutex };
            _mapModuleKind[string{ moduleName }].push_back( pKind );
            ++registeredCount;
        }
        return registeredCount;
    }

    uint32 ResourceDataKindRegistry::unregisterModuleKinds( string_view moduleName )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        iter = _mapModuleKind.find( string{ moduleName } );
        if ( iter == _mapModuleKind.end() )
            return 0;
        uint32 removedCount = 0;
        for ( const ResourceDataKind* pKind : iter->second )
        {
            if ( _registration.remove( pKind ) )
                ++removedCount;
        }
        _mapModuleKind.erase( iter );
        return removedCount;
    }

    bool ResourceDataKindRegistry::registerKind( const ResourceDataKind* pKind )
    {
        const bool bInvalidKind = pKind == nullptr || StringUtil::isNullOrEmpty( pKind->_pLabel ) || pKind->_pLoad == nullptr;
        if ( bInvalidKind )
            return false;
        RegistrationResult result = RegistrationResult::Added;
        {
            ScopedLock<mutex> lock{ _mutex };
            result = _registration.addLogged( pKind, "Resource data kind", pKind->_pLabel );
        }
        return result == RegistrationResult::Added;
    }

    void ResourceDataKindRegistry::unregisterKind( const ResourceDataKind* pKind )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_registration.remove( pKind ); // 등록되어 있지 않은 종류를 빼는 것은 할 일이 없다
    }

    const ResourceDataKind* ResourceDataKindRegistry::findByLabel( string_view label ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.findByName( label );
    }

    void ResourceDataKindRegistry::collectMatches( string_view resourceID, vector<const ResourceDataKind*>& outListKind ) const
    {
        outListKind.clear();
        ScopedLock<mutex> lock{ _mutex };
        for ( const ResourceDataKind* pKind : _registration.getItems() )
        {
            if ( pKind->matches( resourceID ) )
                outListKind.push_back( pKind );
        }
    }

    void ResourceDataKindRegistry::collectKinds( vector<const ResourceDataKind*>& outListKind ) const
    {
        outListKind.clear();
        ScopedLock<mutex> lock{ _mutex };
        for ( const ResourceDataKind* pKind : _registration.getItems() )
        {
            outListKind.push_back( pKind );
        }
    }

    uint32 ResourceDataKindRegistry::getCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.getCount();
    }
} // namespace sw

namespace sw
{
    ResourceDataKindRegistrar*& ResourceDataKindRegistrar::getHead()
    {
        static ResourceDataKindRegistrar* s_pHead = nullptr;
        return s_pHead;
    }

    ResourceDataKindRegistrar::ResourceDataKindRegistrar( const ResourceDataKind* pKind )
        : _pKind{ pKind }
        , _pNext{ getHead() }
    {
        getHead() = this;
    }
} // namespace sw

namespace sw
{
    const ResourceDataKind* ResourceDataKindUtil::findByLabel( string_view label )
    {
        const ResourceDataKindRegistry* pRegistry = engine::findSubsystem<ResourceDataKindRegistry>();
        if ( pRegistry == nullptr )
            return nullptr;
        return pRegistry->findByLabel( label );
    }

    void ResourceDataKindUtil::collectMatches( string_view resourceID, vector<const ResourceDataKind*>& outListKind )
    {
        outListKind.clear();
        const ResourceDataKindRegistry* pRegistry = engine::findSubsystem<ResourceDataKindRegistry>();
        if ( pRegistry == nullptr )
            return;
        pRegistry->collectMatches( resourceID, outListKind );
    }
} // namespace sw
