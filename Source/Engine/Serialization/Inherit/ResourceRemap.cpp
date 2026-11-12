#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceRemap.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct ResourceRemapInternal
        {
            static constexpr const utf8* kRemapElement = "Remap";

            /** @brief 표의 줄 하나입니다. */
            struct Entry
            {
                string          _assetID; ///< 바꿀 리소스 id
                string          _target;  ///< 조건이 참이면 읽을 리소스 id
                ResourceTagTest _test;    ///< 컴파일한 `when`
            };

            /** @brief 표 전체입니다(`ResourceRemapTable` 이 든다). */
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
     * @class ResourceRemapTable
     * @brief 지금 읽은 경로 교체 표입니다(엔진 서비스 서브시스템 — 예전 함수 정적은 엔진을 다시 세워도 앞 엔진의 표가 남았다). `ResourceRemap` 의 정적 창구가 이것을 쓴다.
     */
    class ResourceRemapTable final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ResourceRemapTable";

        ResourceRemapTable()
            : _state{}
        {
        }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 든 표입니다. */
        ResourceRemapInternal::State& getState() { return _state; }

    private:
        ResourceRemapInternal::State _state;

        SW_SERVICE_SLOT( ResourceRemapTable, Required );
    };
} // namespace sw

namespace sw
{
    bool ResourceRemap::loadFromXMLText( string_view text, string_view sourceName )
    {
        using Internal = ResourceRemapInternal;
        XMLDocument doc;
        if ( doc.parse( text, sourceName ) == false )
        {
            SW_LOG_ERROR_ID( "ResourceRemap.NotXML", "Resource remap table '%#' could not be parsed: %#", sourceName, doc.getLastError() );
            return false;
        }
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR_ID( "ResourceRemap.WrongRoot", "Resource remap table '%#' has no <%#> root", sourceName, kRootElement );
            return false;
        }
        vector<Internal::Entry> listEntry;
        for ( XMLNode node = root.findChild(); node.isValid(); node = node.findNextSibling() )
        {
            if ( StringUtil::isNullOrEmpty( node.getName() ) )
                continue;
            if ( string_view( node.getName() ) != Internal::kRemapElement )
            {
                SW_LOG_ERROR_ID( "ResourceRemap.UnknownElement", "Resource remap table '%#': <%#> is not <%#>", sourceName, node.getName(), Internal::kRemapElement );
                return false;
            }
            const string_view assetID = node.getAttributeText( "id" );
            const string_view target  = node.getAttributeText( "to" );
            const utf8*       pWhen   = node.findAttribute( "when" );
            if ( assetID.empty() || target.empty() || pWhen == nullptr )
            {
                SW_LOG_ERROR_ID( "ResourceRemap.MissingField", "Resource remap table '%#': a <%#> needs id, when and to", sourceName, Internal::kRemapElement );
                return false;
            }
            if ( assetID == target )
            {
                SW_LOG_ERROR_ID( "ResourceRemap.SelfRemap", "Resource remap table '%#': '%#' is remapped to itself", sourceName, assetID );
                return false;
            }
            Internal::Entry entry;
            entry._assetID = string{ assetID };
            entry._target  = string{ target };
            if ( ResourceTags::compileTest( pWhen, sourceName, entry._test ) == false )
                return false;
            listEntry.push_back( std::move( entry ) );
        }
        for ( const Internal::Entry& entry : listEntry )
        {
            if ( Internal::hasEntry( listEntry, entry._target ) )
            {
                SW_LOG_ERROR_ID( "ResourceRemap.Chained", "Resource remap table '%#': '%#' is remapped to '%#', which is remapped again - remap to the final file", sourceName, entry._assetID,
                                 entry._target );
                return false;
            }
        }
        ResourceRemapInternal::State& state = engine::getSubsystem<ResourceRemapTable>().getState();
        ScopedLock<mutex>             lock{ state._mutex };
        state._listEntry = std::move( listEntry );
        state._entryCount.store( static_cast<uint32>( state._listEntry.size() ) );
        return true;
    }

    bool ResourceRemap::loadFromAsset( const AssetID& assetID )
    {
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "ResourceRemap.NotFound", "Resource remap table '%#' could not be read", assetID.view() );
            return false;
        }
        return loadFromXMLText( text, assetID.view() );
    }

    void ResourceRemap::clear()
    {
        ResourceRemapInternal::State& state = engine::getSubsystem<ResourceRemapTable>().getState();
        ScopedLock<mutex>             lock{ state._mutex };
        state._listEntry.clear();
        state._entryCount.store( 0 );
    }

    bool ResourceRemap::resolve( string_view assetID, string& outAssetID )
    {
        // 모든 파일 읽기가 지난다 — 엔진이 서기 전(설정 · 기동 파일)에는 교체 표도 없다(표는 엔진의 팩 설정이 읽는다).
        ResourceRemapTable* const pTable = engine::findSubsystem<ResourceRemapTable>();
        if ( pTable == nullptr || pTable->getState()._entryCount.load() == 0 )
            return false;
        ResourceRemapInternal::State& state = pTable->getState();
        const ResourceTagSet          tags  = engine::getSubsystem<ResourceTags>().capture();
        vector<string>                listLoadTag;
        bool                          bRemapped = false;
        {
            ScopedLock<mutex> lock{ state._mutex };
            for ( const ResourceRemapInternal::Entry& entry : state._listEntry )
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
            engine::getSubsystem<ResourceComposition>().recordRemap( assetID, listLoadTag );
        return bRemapped;
    }
} // namespace sw

namespace sw
{
    // 경로 교체 표 — 팩 설정(`PackEngineSettingsSubsystem`)이 채우고 파일 읽기(`AssetIO`)가 묻는다.
    SW_ENGINE_SERVICE_SUBSYSTEM( ResourceRemapTable, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( ResourceRemapTable ) );
} // namespace sw
