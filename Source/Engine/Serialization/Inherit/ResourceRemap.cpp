#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceRemap.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
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

            struct State
            {
                mutex          _mutex{};
                vector<Entry>  _listEntry{};
                atomic<uint32> _entryCount{ 0 }; ///< 표가 비었는지 잠금 없이 묻는다 — 모든 리소스 읽기가 지나는 자리다
            };

            static State& getState()
            {
                static State s_state;
                return s_state;
            }

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
        Internal::State&  state = Internal::getState();
        ScopedLock<mutex> lock{ state._mutex };
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
        ResourceRemapInternal::State& state = ResourceRemapInternal::getState();
        ScopedLock<mutex>             lock{ state._mutex };
        state._listEntry.clear();
        state._entryCount.store( 0 );
    }

    bool ResourceRemap::resolve( string_view assetID, string& outAssetID )
    {
        ResourceRemapInternal::State& state = ResourceRemapInternal::getState();
        if ( state._entryCount.load() == 0 )
            return false;
        const ResourceTagSet tags = ResourceTags::capture();
        vector<string>       listLoadTag;
        bool                 bRemapped = false;
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
            ResourceComposition::noteRemap( assetID, listLoadTag );
        return bRemapped;
    }
} // namespace sw
