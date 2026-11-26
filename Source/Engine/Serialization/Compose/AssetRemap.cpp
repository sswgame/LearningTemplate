#include "pch.h"

#include "Engine/Serialization/Compose/AssetRemap.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Serialization/Compose/AssetComposition.h"
#include "Engine/Serialization/Compose/AssetTags.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct AssetRemapInternal
        {
            static constexpr const utf8* kRemapElement = "Remap";

            /** @brief 표의 줄 하나입니다. */
            struct Entry
            {
                string       _assetID; ///< 바꿀 리소스 id
                string       _target;  ///< 조건이 참이면 읽을 리소스 id
                AssetTagTest _test;    ///< 컴파일한 `when`
            };

            /** @brief 표 전체입니다(`AssetRemapTable` 이 든다). */
            struct State
            {
                mutex          _mutex{};
                vector<Entry>  _listEntry{};
                atomic<uint32> _entryCount{ 0 }; ///< 표가 비었는지 잠금 없이 묻는다 — 모든 리소스 읽기가 지나는 자리다
            };

            static bool hasEntry( const vector<Entry>& listEntry, string_view assetID )
            {
                for ( const Entry& entry : listEntry )
                {
                    if ( entry._assetID == assetID )
                        return true;
                }
                return false;
            }
        };
    } // namespace

    /**
     * @class AssetRemapTable
     * @brief 지금 읽은 경로 교체 표입니다(엔진 서비스 서브시스템 — 예전 함수 정적은 엔진을 다시 세워도 앞 엔진의 표가 남았다). `AssetRemap` 의 정적 창구가 이것을 쓴다.
     */
    class AssetRemapTable final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "AssetRemapTable";

        AssetRemapTable()
            : _state{}
        {
        }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 든 표입니다. */
        AssetRemapInternal::State& getState() { return _state; }

    private:
        AssetRemapInternal::State _state;

        SW_SUBSYSTEM_SLOT( AssetRemapTable, Required );
    };
} // namespace sw

namespace sw
{
    bool AssetRemap::loadFromXMLText( string_view text, string_view sourceName )
    {
        using Internal = AssetRemapInternal;
        XMLDocument doc;
        if ( doc.parse( text, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "AssetRemap.NotXML", "Asset remap table '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "AssetRemap.WrongRoot", "Asset remap table '%#' has no <%#> root", sourceName, kRootElement );
            return false;
        }
        vector<Internal::Entry> listEntry;
        for ( XMLNode node = root.findChild(); node.isValid(); node = node.findNextSibling() )
        {
            if ( StringUtil::isNullOrEmpty( node.getName() ) )
                continue;
            if ( string_view( node.getName() ) != Internal::kRemapElement )
            {
                SW_LOG_ERROR_ID( "AssetRemap.UnknownElement", "Asset remap table '%#': <%#> is not <%#>", sourceName, node.getName(), Internal::kRemapElement );
                return false;
            }
            const string_view assetID = node.getAttributeText( "id" );
            const string_view target  = node.getAttributeText( "to" );
            const utf8*       pWhen   = node.findAttribute( "when" );
            if ( assetID.empty() || target.empty() || pWhen == nullptr )
            {
                SW_LOG_ERROR_ID( "AssetRemap.MissingField", "Asset remap table '%#': a <%#> needs id, when and to", sourceName, Internal::kRemapElement );
                return false;
            }
            if ( assetID == target )
            {
                SW_LOG_ERROR_ID( "AssetRemap.SelfRemap", "Asset remap table '%#': '%#' is remapped to itself", sourceName, assetID );
                return false;
            }
            Internal::Entry entry;
            entry._assetID = string{ assetID };
            entry._target  = string{ target };
            if ( AssetTags::compileTest( pWhen, sourceName, entry._test ) == false )
                return false;
            listEntry.push_back( std::move( entry ) );
        }
        for ( const Internal::Entry& entry : listEntry )
        {
            if ( Internal::hasEntry( listEntry, entry._target ) )
            {
                SW_LOG_ERROR_ID( "AssetRemap.Chained", "Asset remap table '%#': '%#' is remapped to '%#', which is remapped again - remap to the final file", sourceName, entry._assetID,
                                 entry._target );
                return false;
            }
        }
        AssetRemapInternal::State& state = engine::getSubsystem<AssetRemapTable>().getState();
        ScopedLock<mutex>          lock{ state._mutex };
        state._listEntry = std::move( listEntry );
        state._entryCount.store( static_cast<uint32>( state._listEntry.size() ) );
        return true;
    }

    bool AssetRemap::loadFromAsset( const AssetID& assetID )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "AssetRemap.NotFound", "Asset remap table '%#' could not be read", assetID.view() );
            return false;
        }
        return loadFromXMLText( text, assetID.view() );
    }

    void AssetRemap::clear()
    {
        AssetRemapInternal::State& state = engine::getSubsystem<AssetRemapTable>().getState();
        ScopedLock<mutex>          lock{ state._mutex };
        state._listEntry.clear();
        state._entryCount.store( 0 );
    }

    bool AssetRemap::resolve( string_view assetID, string& outAssetID )
    {
        // 모든 파일 읽기가 지난다 — 엔진이 서기 전(설정 · 기동 파일)에는 교체 표도 없다(표는 엔진의 팩 설정이 읽는다).
        AssetRemapTable* const pTable = engine::findSubsystem<AssetRemapTable>();
        if ( pTable == nullptr || pTable->getState()._entryCount.load() == 0 )
            return false;
        AssetRemapInternal::State& state = pTable->getState();
        const AssetTagSet          tags  = engine::getSubsystem<AssetTags>().capture();
        vector<string>             listLoadTag;
        bool                       bRemapped = false;
        {
            const ScopedDeferredLog deferredLog;
            ScopedLock<mutex>       lock{ state._mutex };
            for ( const AssetRemapInternal::Entry& entry : state._listEntry )
            {
                if ( entry._assetID != assetID )
                    continue;
                listLoadTag.insert( listLoadTag.end(), entry._test._listLoadTag.begin(), entry._test._listLoadTag.end() );
                if ( entry._test.evaluate( tags ) )
                {
                    outAssetID = entry._target;
                    bRemapped  = true;
                    break;
                }
            }
        }
        if ( listLoadTag.empty() == false )
            engine::getSubsystem<AssetComposition>().recordRemap( assetID, listLoadTag );
        return bRemapped;
    }
} // namespace sw

namespace sw
{
    // 경로 교체 표 — 팩 설정(`PackEngineSettingsSubsystem`)이 채우고 파일 읽기(`AssetIO`)가 묻는다.
    SW_ENGINE_SUBSYSTEM_WITH_SLOT( AssetRemapTable, HostOnly, Process, Shared, (), SW_ENGINE_DEFAULT_FACTORY( AssetRemapTable ) );
} // namespace sw
