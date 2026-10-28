#include "pch.h"

#include "Engine/Asset/AssetDataKind.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    SW_LOG_CALLER( "AssetDataKind" );

    bool AssetDataKind::matches( string_view assetID ) const
    {
        const bool bNoMatchRule = _pPrefix == nullptr && _pContains == nullptr && _pSuffix == nullptr;
        if ( bNoMatchRule )
            return false;
        if ( _pPrefix != nullptr && StringUtil::startsWith( assetID, string_view( _pPrefix ), true ) == false )
            return false;
        if ( _pContains != nullptr && StringUtil::contains( assetID, string_view( _pContains ), true ) == false )
            return false;
        if ( _pSuffix != nullptr && StringUtil::endsWith( assetID, string_view( _pSuffix ), true ) == false )
            return false;
        return true;
    }
} // namespace sw

namespace sw
{
    AssetDataKindRegistry::AssetDataKindRegistry()
        : _mutex{}
        , _registration{ "AssetDataKindRegistry", RegistrationOrder::Insertion, true, NameCase::IgnoreCase }
        , _mapModuleKind{}
    {
    }

    uint32 AssetDataKindRegistry::registerModuleKinds( string_view moduleName, const AssetDataKindRegistrar* pHead )
    {
        // 목록은 나중에 만든 것이 앞이다 — 모아서 뒤에서부터 올리면 정적 초기화 순서(= 등록 순서)가 된다.
        vector<const AssetDataKind*> listKind;
        for ( const AssetDataKindRegistrar* pRegistrar = pHead; pRegistrar != nullptr; pRegistrar = pRegistrar->_pNext )
        {
            listKind.push_back( pRegistrar->_pKind );
        }
        uint32 registeredCount = 0;
        for ( size_t index = listKind.size(); index > 0; --index )
        {
            const AssetDataKind* pKind = listKind[index - 1];
            if ( registerKind( pKind ) == false )
                continue;
            ScopedLock<mutex> lock{ _mutex };
            _mapModuleKind[string{ moduleName }].push_back( pKind );
            ++registeredCount;
        }
        return registeredCount;
    }

    uint32 AssetDataKindRegistry::unregisterModuleKinds( string_view moduleName )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        iter = _mapModuleKind.find( string{ moduleName } );
        if ( iter == _mapModuleKind.end() )
            return 0;
        uint32 removedCount = 0;
        for ( const AssetDataKind* pKind : iter->second )
        {
            if ( _registration.remove( pKind ) )
                ++removedCount;
        }
        _mapModuleKind.erase( iter );
        return removedCount;
    }

    bool AssetDataKindRegistry::registerKind( const AssetDataKind* pKind )
    {
        const bool bInvalidKind = pKind == nullptr || StringUtil::isNullOrEmpty( pKind->_pLabel ) || pKind->_pLoad == nullptr;
        if ( bInvalidKind )
            return false;
        RegistrationResult result = RegistrationResult::Added;
        {
            ScopedLock<mutex> lock{ _mutex };
            result = _registration.add( pKind, pKind->_pLabel );
        }
        reportRegistrationRejected( result, "Resource data kind", pKind->_pLabel );
        return result == RegistrationResult::Added;
    }

    void AssetDataKindRegistry::unregisterKind( const AssetDataKind* pKind )
    {
        ScopedLock<mutex> lock{ _mutex };
        (void)_registration.remove( pKind ); // 등록되어 있지 않은 종류를 빼는 것은 할 일이 없다
    }

    const AssetDataKind* AssetDataKindRegistry::findByLabel( string_view label ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.findByName( label );
    }

    void AssetDataKindRegistry::collectMatches( string_view assetID, vector<const AssetDataKind*>& outListKind ) const
    {
        outListKind.clear();
        ScopedLock<mutex> lock{ _mutex };
        for ( const AssetDataKind* pKind : _registration.getItems() )
        {
            if ( pKind->matches( assetID ) )
                outListKind.push_back( pKind );
        }
    }

    void AssetDataKindRegistry::collectKinds( vector<const AssetDataKind*>& outListKind ) const
    {
        outListKind.clear();
        ScopedLock<mutex> lock{ _mutex };
        for ( const AssetDataKind* pKind : _registration.getItems() )
        {
            outListKind.push_back( pKind );
        }
    }

    uint32 AssetDataKindRegistry::getCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _registration.getCount();
    }
} // namespace sw

namespace sw
{
    AssetDataKindRegistrar*& AssetDataKindRegistrar::getHead()
    {
        static AssetDataKindRegistrar* s_pHead = nullptr;
        return s_pHead;
    }

    AssetDataKindRegistrar::AssetDataKindRegistrar( const AssetDataKind* pKind )
        : _pKind{ pKind }
        , _pNext{ getHead() }
    {
        getHead() = this;
    }
} // namespace sw

namespace sw
{
    const AssetDataKind* AssetDataKindUtil::findByLabel( string_view label )
    {
        const AssetDataKindRegistry* pRegistry = engine::findSubsystem<AssetDataKindRegistry>();
        if ( pRegistry == nullptr )
            return nullptr;
        return pRegistry->findByLabel( label );
    }

    void AssetDataKindUtil::collectMatches( string_view assetID, vector<const AssetDataKind*>& outListKind )
    {
        outListKind.clear();
        const AssetDataKindRegistry* pRegistry = engine::findSubsystem<AssetDataKindRegistry>();
        if ( pRegistry == nullptr )
            return;
        pRegistry->collectMatches( assetID, outListKind );
    }
} // namespace sw
