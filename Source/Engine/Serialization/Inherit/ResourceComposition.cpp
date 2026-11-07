#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceComposition.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"

namespace sw
{
    namespace
    {
        struct ResourceCompositionInternal
        {
            /** @brief 리소스 하나의 기록입니다. */
            struct Record
            {
                vector<string> _listLoadTag;  ///< 결과가 기댄 로드 축 태그
                uint64         _hash;         ///< 합성 결과의 해시(접속 대조)
                uint8          _bLayered : 1; ///< 덮어쓰기 층 파일을 얹었다

                Record()
                    : _listLoadTag{}
                    , _hash{ 0 }
                    , _bLayered{ SW_FALSE }
                {
                }
            };

            struct State
            {
                mutex                                 _mutex{};
                vector<string>                        _listLayerRoot{};
                unordered_map<string, Record>         _mapRecord{};
                unordered_map<string, vector<string>> _mapRemapTag{}; ///< 경로 교체가 물은 로드 축 태그(`recordRemap`)
                unordered_set<string>                 _uniqueComposedID{};
                vector<string>                        _listPendingReload{};
            };

            static State& getState()
            {
                static State s_state;
                return s_state;
            }

            /** @brief @p assetID 를 다시 읽을 목록에 올립니다(잠금 안에서 부른다). */
            static void addPending( State& state, const string& assetID )
            {
                if ( VectorUtil::contains( state._listPendingReload, assetID ) == false )
                    state._listPendingReload.push_back( assetID );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool ResourceLayers::setLayers( const vector<string>& listRoot )
    {
        for ( size_t index = 0; index < listRoot.size(); ++index )
        {
            const string& root = listRoot[index];
            if ( StringUtil::trim( root ).empty() || StringUtil::endsWith( root, "/" ) )
            {
                SW_LOG_ERROR_ID( "ResourceLayers.InvalidRoot", "Resource layer root '%#' is not a resource folder id (empty or ends with '/') - layers unchanged", root );
                return false;
            }
            for ( size_t other = 0; other < index; ++other )
            {
                if ( listRoot[other] == root )
                {
                    SW_LOG_ERROR_ID( "ResourceLayers.DuplicateRoot", "Resource layer root '%#' is listed twice - layers unchanged", root );
                    return false;
                }
            }
        }
        vector<string> listTouched;
        {
            ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
            ScopedLock<mutex>                   lock{ state._mutex };
            if ( state._listLayerRoot == listRoot )
                return true;
            listTouched = state._listLayerRoot;
            for ( const string& root : listRoot )
            {
                if ( VectorUtil::contains( listTouched, root ) == false )
                    listTouched.push_back( root );
            }
            state._listLayerRoot = listRoot;
        }
        ResourceComposition::notifyLayersChanged( listTouched );
        return true;
    }

    vector<string> ResourceLayers::getLayers()
    {
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        ScopedLock<mutex>                   lock{ state._mutex };
        return state._listLayerRoot;
    }

    string ResourceLayers::makeLayerAssetID( string_view root, string_view assetID )
    {
        string layerID{ root };
        layerID += '/';
        layerID += assetID;
        return layerID;
    }

    void ResourceComposition::record( string_view assetID, const vector<string>& listLoadTag, bool bLayered, string_view composedText )
    {
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        const string                        key{ assetID };
        const bool                          bVaries = listLoadTag.empty() == false || bLayered;
        const uint64                        hash    = bVaries ? hashText( composedText ) : 0; // 기대지 않은 합성(거의 전부)은 해시를 들지 않는다
        ScopedLock<mutex>                   lock{ state._mutex };
        state._uniqueComposedID.insert( key );
        if ( bVaries == false )
        {
            state._mapRecord.erase( key );
            return;
        }
        ResourceCompositionInternal::Record& record = state._mapRecord[key];
        record._listLoadTag                         = listLoadTag;
        record._hash                                = hash;
        record._bLayered                            = bLayered ? SW_TRUE : SW_FALSE;
    }

    void ResourceComposition::recordComposed( string_view assetID )
    {
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        ScopedLock<mutex>                   lock{ state._mutex };
        state._uniqueComposedID.insert( string{ assetID } );
    }

    void ResourceComposition::recordRemap( string_view assetID, const vector<string>& listLoadTag )
    {
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        ScopedLock<mutex>                   lock{ state._mutex };
        vector<string>&                     listTag = state._mapRemapTag[string{ assetID }];
        for ( const string& tag : listLoadTag )
        {
            if ( VectorUtil::contains( listTag, tag ) == false )
                listTag.push_back( tag );
        }
    }

    void ResourceComposition::notifyTagsChanged( const vector<string>& listChangedTag )
    {
        ResourceCompositionInternal::State& state      = ResourceCompositionInternal::getState();
        const auto                          isAffected = [&listChangedTag]( const vector<string>& listTag )
        {
            return VectorUtil::containsIf( listTag, [&listChangedTag]( const string& tag )
            { return VectorUtil::contains( listChangedTag, tag ); } );
        };
        ScopedLock<mutex> lock{ state._mutex };
        for ( const auto& [assetID, record] : state._mapRecord )
        {
            if ( isAffected( record._listLoadTag ) )
                ResourceCompositionInternal::addPending( state, assetID );
        }
        for ( const auto& [assetID, listTag] : state._mapRemapTag )
        {
            if ( isAffected( listTag ) )
                ResourceCompositionInternal::addPending( state, assetID );
        }
    }

    void ResourceComposition::notifyLayersChanged( const vector<string>& listRoot )
    {
        // 층 파일이 있는지는 잠금 밖에서 본다(파일 · 팩 색인을 읽는다). 후보는 리소스 id 로 합성한 적이 있는 것 — 읽지 않은 리소스는 다음 읽기가 새 층을 본다.
        vector<string> listCandidate;
        {
            ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
            ScopedLock<mutex>                   lock{ state._mutex };
            for ( const string& assetID : state._uniqueComposedID )
            {
                const auto it = state._mapRecord.find( assetID );
                if ( it != state._mapRecord.end() && it->second._bLayered == SW_TRUE )
                    ResourceCompositionInternal::addPending( state, assetID );
                else
                    listCandidate.push_back( assetID );
            }
        }
        vector<string> listLayered;
        for ( const string& assetID : listCandidate )
        {
            for ( const string& root : listRoot )
            {
                if ( AssetIO::hasAsset( AssetID{ ResourceLayers::makeLayerAssetID( root, assetID ) } ) )
                {
                    listLayered.push_back( assetID );
                    break;
                }
            }
        }
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        ScopedLock<mutex>                   lock{ state._mutex };
        for ( const string& assetID : listLayered )
        {
            ResourceCompositionInternal::addPending( state, assetID );
        }
    }

    void ResourceComposition::takePendingReload( vector<string>& outListAssetID )
    {
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        {
            ScopedLock<mutex> lock{ state._mutex };
            outListAssetID.swap( state._listPendingReload );
            state._listPendingReload.clear();
        }
        VectorUtil::sort( outListAssetID );
    }

    void ResourceComposition::collectDigest( vector<ResourceDigestEntry>& outListEntry )
    {
        outListEntry.clear();
        {
            ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
            ScopedLock<mutex>                   lock{ state._mutex };
            for ( const auto& [assetID, record] : state._mapRecord )
            {
                outListEntry.push_back( ResourceDigestEntry{ assetID, record._hash } );
            }
        }
        VectorUtil::sort( outListEntry, []( const ResourceDigestEntry& lhs, const ResourceDigestEntry& rhs )
        { return lhs._assetID < rhs._assetID; } );
    }

    bool ResourceComposition::isDigestMismatch( const ResourceDigestEntry& entry )
    {
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        ScopedLock<mutex>                   lock{ state._mutex };
        const auto                          it = state._mapRecord.find( entry._assetID );
        return it != state._mapRecord.end() && it->second._hash != entry._hash;
    }

    uint64 ResourceComposition::hashText( string_view text )
    {
        return HashUtil::computeStringHash64( text.data(), text.size(), false );
    }

    void ResourceComposition::clear()
    {
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        ScopedLock<mutex>                   lock{ state._mutex };
        state._mapRecord.clear();
        state._mapRemapTag.clear();
        state._uniqueComposedID.clear();
        state._listPendingReload.clear();
    }
} // namespace sw
