#include "pch.h"

#include "Engine/Content/AssetRegistry.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    namespace
    {
        struct AssetRegistryInternal
        {
            static string toLowerID( string_view assetID ) { return StringUtil::toLower( assetID ); }

            /** @brief 한 층 안에 GUID · 경로(대소문자 무시)가 겹치지 않으면 빈 결과, 겹치면 그 까닭입니다. */
            [[nodiscard]] static expected<void> validateLayer( const AssetRegistryLayer& layer )
            {
                unordered_set<AssetGUID> setGUID;
                unordered_set<string>    setPath;
                for ( const AssetRegistryEntry& entry : layer._listEntry )
                {
                    if ( setGUID.insert( entry._guid ).second == false )
                        return unexpected( Error{ ErrorKind::AlreadyExists, "two assets of one index layer carry the same GUID", entry._assetID } );
                    if ( entry._assetID.empty() || setPath.insert( toLowerID( entry._assetID ) ).second == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "an index layer has an empty or repeated asset id", entry._assetID } );
                }
                return {};
            }
        };
    } // namespace

    /**
     * @class AssetRegistryTable
     * @brief 프로세스의 색인입니다(엔진 서비스 서브시스템 — `AssetRedirectTable` 과 같은 자리). `AssetRegistry` 의 정적 창구가 이것을 쓴다.
     * @details 기동 · 마운트 때 한 번 바꾸고 모든 참조 읽기가 묻는다 — 읽기가 많아 공유 잠금이다.
     */
    class AssetRegistryTable final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "AssetRegistryTable";

        AssetRegistryTable()
            : _registry{}
        {
        }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override { (void)replace( AssetRegistry{} ); }
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        AssetRegistry replace( AssetRegistry registry )
        {
            ScopedLock<shared_mutex> lock{ _mutex };
            std::swap( _registry, registry );
            return registry;
        }
        optional<string> findPath( const AssetGUID& guid ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            return _registry.findPath( guid );
        }
        optional<AssetGUID> findGUID( string_view assetID ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _mutex };
            return _registry.findGUID( assetID );
        }
        void put( const AssetGUID& guid, string_view assetID )
        {
            ScopedLock<shared_mutex> lock{ _mutex };
            _registry.put( guid, assetID );
        }
        bool move( string_view oldAssetID, string_view newAssetID )
        {
            ScopedLock<shared_mutex>  lock{ _mutex };
            const optional<AssetGUID> guid = _registry.findGUID( oldAssetID );
            if ( guid.has_value() == false )
                return false;
            _registry.put( *guid, newAssetID );
            return true;
        }

    private:
        mutable shared_mutex    _mutex;
        AssetRegistry _registry SW_GUARDED_BY( _mutex );

        SW_SERVICE_SLOT( AssetRegistryTable, Required );
    };

    expected<AssetRegistry> AssetRegistry::merge( const vector<AssetRegistryLayer>& listLayer )
    {
        AssetRegistry registry;
        for ( const AssetRegistryLayer& layer : listLayer )
        {
            const expected<void> checked = AssetRegistryInternal::validateLayer( layer );
            if ( checked.has_value() == false )
            {
                fixed_string<constant::buffer::kMax512> detail;
                formatstring( detail.data(), detail.capacity(), "%#: %#", layer._sourceName, checked.error().detail() );
                return unexpected( Error{ checked.error().kind(), checked.error().message().data(), detail } );
            }
            for ( const AssetRegistryEntry& entry : layer._listEntry )
            {
                registry.put( entry._guid, entry._assetID );
            }
        }
        return registry;
    }

    void AssetRegistry::put( const AssetGUID& guid, string_view assetID )
    {
        const string lowerID = AssetRegistryInternal::toLowerID( assetID );
        // 이 GUID 의 옛 자리를 비운다 — 옮긴 에셋의 옛 경로로 찾으면 없어야 한다.
        const auto oldPath = _mapGUIDToPath.find( guid );
        if ( oldPath != _mapGUIDToPath.end() )
            _mapPathToGUID.erase( AssetRegistryInternal::toLowerID( oldPath->second ) );
        // 이 자리를 쓰던 다른 GUID 는 풀리지 않게 한다 — 그 파일은 이제 이 에셋이다.
        const auto previousOwner = _mapPathToGUID.find( lowerID );
        if ( previousOwner != _mapPathToGUID.end() && previousOwner->second != guid )
            _mapGUIDToPath.erase( previousOwner->second );
        _mapGUIDToPath[guid] = string( assetID );
        _mapPathToGUID.erase( lowerID );
        _mapPathToGUID.emplace( lowerID, guid );
    }

    optional<string> AssetRegistry::findPath( const AssetGUID& guid ) const
    {
        const auto found = _mapGUIDToPath.find( guid );
        if ( found == _mapGUIDToPath.end() )
            return nullopt;
        return found->second;
    }

    optional<AssetGUID> AssetRegistry::findGUID( string_view assetID ) const
    {
        const auto found = _mapPathToGUID.find( AssetRegistryInternal::toLowerID( assetID ) );
        if ( found == _mapPathToGUID.end() )
            return nullopt;
        return found->second;
    }

    AssetRegistry AssetRegistry::install( AssetRegistry registry )
    {
        return engine::getSubsystem<AssetRegistryTable>().replace( std::move( registry ) );
    }

    void AssetRegistry::record( const AssetGUID& guid, string_view assetID )
    {
        AssetRegistryTable* const pTable = engine::findSubsystem<AssetRegistryTable>();
        if ( pTable != nullptr )
            pTable->put( guid, assetID );
    }

    bool AssetRegistry::move( string_view oldAssetID, string_view newAssetID )
    {
        AssetRegistryTable* const pTable = engine::findSubsystem<AssetRegistryTable>();
        return pTable != nullptr && pTable->move( oldAssetID, newAssetID );
    }

    optional<string> AssetRegistry::resolve( const AssetGUID& guid )
    {
        // 엔진이 서기 전(설정 · 기동 파일)과 엔진 없이 도는 도구에는 표가 없다.
        const AssetRegistryTable* const pTable = engine::findSubsystem<AssetRegistryTable>();
        if ( pTable == nullptr )
            return nullopt;
        return pTable->findPath( guid );
    }

    optional<AssetGUID> AssetRegistry::resolveGUID( string_view assetID )
    {
        const AssetRegistryTable* const pTable = engine::findSubsystem<AssetRegistryTable>();
        if ( pTable == nullptr )
            return nullopt;
        return pTable->findGUID( assetID );
    }

    string AssetRegistry::makeReferenceText( const AssetGUID& guid )
    {
        StringBuilder<constant::buffer::kMax64> builder;
        builder.appendFormat( "%#%#", kReferencePrefix, guid.toString() );
        return string( builder.view() );
    }

    bool AssetRegistry::isReferenceText( string_view text )
    {
        return StringUtil::startsWith( text, kReferencePrefix );
    }

    optional<AssetGUID> AssetRegistry::parseReferenceText( string_view text )
    {
        if ( isReferenceText( text ) == false )
            return nullopt;
        return AssetGUID::tryParse( text.substr( kReferencePrefix.size() ) );
    }

    expected<AssetReferenceTarget> AssetRegistry::readReferenceText( string_view text )
    {
        const string_view    trimmed = StringUtil::trim( text );
        AssetReferenceTarget target;
        if ( isReferenceText( trimmed ) == false )
        {
            target._path = string{ trimmed };
            if ( trimmed.empty() == false )
                target._guid = resolveGUID( trimmed );
            return target;
        }
        target._guid = parseReferenceText( trimmed );
        if ( target._guid.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "not a GUID reference (guid: and 32 lowercase hex digits)", trimmed } );
        optional<string> resolved = resolve( *target._guid );
        if ( resolved.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "not in the asset index - the asset was deleted or the index is not installed", trimmed } );
        target._path = std::move( *resolved );
        return target;
    }

    string AssetRegistry::makeReferenceText( string_view path, const optional<AssetGUID>& guid )
    {
        return guid.has_value() ? makeReferenceText( *guid ) : string{ path };
    }
} // namespace sw

namespace sw
{
    // GUID ↔ 리소스 id 색인 — 에셋 매니저가 마운트 때 채우고(`AssetManager::mountContent`) 참조 읽기(`AssetRefBase`)가 묻는다.
    SW_ENGINE_SERVICE_SUBSYSTEM( AssetRegistryTable, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( AssetRegistryTable ) );
} // namespace sw
