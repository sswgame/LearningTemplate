#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceComposition.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"

namespace sw
{
    namespace
    {
        /** @brief 리소스 하나의 기록입니다. */
        struct ResourceCompositionRecord
        {
            vector<string> _listLoadTag;  ///< 결과가 기댄 로드 축 태그
            uint64         _hash;         ///< 합성 결과의 해시(대조)
            uint64         _revision;     ///< 결과가 마지막으로 바뀐 번호(`collectTagDependentChanges`)
            uint8          _bLayered : 1; ///< 덮어쓰기 층 파일을 얹었다

            ResourceCompositionRecord()
                : _listLoadTag{}
                , _hash{ 0 }
                , _revision{ 0 }
                , _bLayered{ SW_FALSE }
            {
            }

            /**
             * @brief 플레이어끼리 달라지면 안 되는데 달라질 수 있는 합성이면 true 입니다(접속 대조에 올린다).
             * @details 품질 · 언어(`ResourceTagScope::PerClient`)에만 기댄 합성은 뺀다 — 머티리얼이 품질 조건을 쓴다고 그래픽 설정이 다른 손님을 끊으면 안 된다.
             */
            [[nodiscard]] bool isComparedBetweenPeers() const
            {
                if ( _bLayered == SW_TRUE )
                    return true;
                return VectorUtil::containsIf( _listLoadTag, []( const string& tag )
                {
                    const ResourceTagAxis* pAxis = ResourceTags::findAxisOfTag( tag );
                    return pAxis == nullptr || pAxis->_scope == ResourceTagScope::Shared;
                } );
            }
        };
    } // namespace

    struct ResourceComposition::State
    {
        mutex                                            _mutex{};
        vector<string>                                   _listLayerRoot{};
        unordered_map<string, ResourceCompositionRecord> _mapRecord{};   ///< 리소스 id 로 합성한 리소스 모두
        unordered_map<string, vector<string>>            _mapRemapTag{}; ///< 경로 교체가 물은 로드 축 태그(`recordRemap`)
        vector<string>                                   _listPendingReload{};
        uint64                                           _revision{ 0 }; ///< 기록의 결과가 바뀔 때마다 는다
    };

    namespace
    {
        struct ResourceCompositionInternal
        {

            /** @brief @p assetID 를 다시 읽을 목록에 올립니다(잠금 안에서 부른다). */
            static void addPending( ResourceComposition::State& state, const string& assetID )
            {
                if ( VectorUtil::contains( state._listPendingReload, assetID ) == false )
                    state._listPendingReload.push_back( assetID );
            }

            /** @brief 번호 @p inoutRevision 뒤에 바뀐 기록을 모읍니다. @p bVaryingOnly 면 로드 축 · 층에 기댄 것만입니다. */
            static void collectChanges( ResourceComposition::State& state, uint64& inoutRevision, bool bVaryingOnly, vector<ResourceDigestEntry>& outListEntry )
            {
                outListEntry.clear();
                {
                    ScopedLock<mutex> lock{ state._mutex };
                    if ( state._revision == inoutRevision )
                        return; // 프레임마다 묻는 자리다 - 대개 여기서 끝난다
                    for ( const auto& [assetID, record] : state._mapRecord )
                    {
                        if ( record._revision > inoutRevision && ( bVaryingOnly == false || record.isComparedBetweenPeers() ) )
                            outListEntry.push_back( ResourceDigestEntry{ assetID, record._hash } );
                    }
                    inoutRevision = state._revision;
                }
                VectorUtil::sort( outListEntry, []( const ResourceDigestEntry& lhs, const ResourceDigestEntry& rhs )
                { return lhs._assetID < rhs._assetID; } );
            }
        };
    } // namespace

} // namespace sw

namespace sw
{
    ResourceComposition::ResourceComposition()
        : _pState{ sw::make_unique<State>() }
    {
    }

    ResourceComposition::~ResourceComposition() = default;

    bool ResourceComposition::setLayers( const vector<string>& listRoot )
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
            State&            state = *_pState;
            ScopedLock<mutex> lock{ state._mutex };
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
        notifyLayersChanged( listTouched );
        return true;
    }

    vector<string> ResourceComposition::getLayers() const
    {
        State&            state = *_pState;
        ScopedLock<mutex> lock{ state._mutex };
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
        State&            state = *_pState;
        const string      key{ assetID };
        const uint64      hash = hashText( composedText ); // 기대지 않은 합성도 든다 - 상대가 층을 얹어 읽은 표와 맞댄다(파싱에 견주면 값싸다)
        ScopedLock<mutex> lock{ state._mutex };
        const auto [itRecord, bInserted]  = state._mapRecord.try_emplace( key );
        ResourceCompositionRecord& record = itRecord->second;
        if ( bInserted || record._hash != hash )
            record._revision = ++state._revision;
        record._listLoadTag = listLoadTag;
        record._hash        = hash;
        record._bLayered    = bLayered ? SW_TRUE : SW_FALSE;
    }

    void ResourceComposition::recordRemap( string_view assetID, const vector<string>& listLoadTag )
    {
        State&            state = *_pState;
        ScopedLock<mutex> lock{ state._mutex };
        vector<string>&   listTag = state._mapRemapTag[string{ assetID }];
        for ( const string& tag : listLoadTag )
        {
            if ( VectorUtil::contains( listTag, tag ) == false )
                listTag.push_back( tag );
        }
    }

    void ResourceComposition::notifyTagsChanged( const vector<string>& listChangedTag )
    {
        State&     state      = *_pState;
        const auto isAffected = [&listChangedTag]( const vector<string>& listTag )
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
            State&            state = *_pState;
            ScopedLock<mutex> lock{ state._mutex };
            for ( const auto& [assetID, record] : state._mapRecord )
            {
                if ( record._bLayered == SW_TRUE )
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
        State&            state = *_pState;
        ScopedLock<mutex> lock{ state._mutex };
        for ( const string& assetID : listLayered )
        {
            ResourceCompositionInternal::addPending( state, assetID );
        }
    }

    void ResourceComposition::takePendingReload( vector<string>& outListAssetID )
    {
        State& state = *_pState;
        {
            ScopedLock<mutex> lock{ state._mutex };
            outListAssetID.swap( state._listPendingReload );
            state._listPendingReload.clear();
        }
        VectorUtil::sort( outListAssetID );
    }

    void ResourceComposition::collectDigest( vector<ResourceDigestEntry>& outListEntry ) const
    {
        outListEntry.clear();
        {
            State&            state = *_pState;
            ScopedLock<mutex> lock{ state._mutex };
            for ( const auto& [assetID, record] : state._mapRecord )
            {
                if ( record.isComparedBetweenPeers() )
                    outListEntry.push_back( ResourceDigestEntry{ assetID, record._hash } );
            }
        }
        VectorUtil::sort( outListEntry, []( const ResourceDigestEntry& lhs, const ResourceDigestEntry& rhs )
        { return lhs._assetID < rhs._assetID; } );
    }

    void ResourceComposition::collectTagDependentChanges( uint64& inoutRevision, vector<ResourceDigestEntry>& outListEntry ) const
    {
        ResourceCompositionInternal::collectChanges( *_pState, inoutRevision, true, outListEntry );
    }

    void ResourceComposition::collectAllChanges( uint64& inoutRevision, vector<ResourceDigestEntry>& outListEntry ) const
    {
        ResourceCompositionInternal::collectChanges( *_pState, inoutRevision, false, outListEntry );
    }

    bool ResourceComposition::isDigestMismatch( const ResourceDigestEntry& entry ) const
    {
        State&            state = *_pState;
        ScopedLock<mutex> lock{ state._mutex };
        const auto        it = state._mapRecord.find( entry._assetID );
        return it != state._mapRecord.end() && it->second._hash != entry._hash;
    }

    bool ResourceComposition::isLayered( string_view assetID ) const
    {
        State&            state = *_pState;
        ScopedLock<mutex> lock{ state._mutex };
        const auto        it = state._mapRecord.find( string{ assetID } );
        return it != state._mapRecord.end() && it->second._bLayered == SW_TRUE;
    }

    uint64 ResourceComposition::hashText( string_view text )
    {
        return HashUtil::computeStringHash64( text.data(), text.size(), false );
    }

    void ResourceComposition::clear()
    {
        State&            state = *_pState;
        ScopedLock<mutex> lock{ state._mutex };
        state._mapRecord.clear();
        state._mapRemapTag.clear();
        state._listPendingReload.clear();
        ++state._revision; // 이미 받은 번호가 새 기록을 가리지 않게 되돌리지 않는다
    }

    ResourceDigestComparer::ResourceDigestComparer( const ResourceComposition& composition )
        : _composition{ composition }
        , _listLine{}
        , _revision{ 0 }
    {
    }

    bool ResourceDigestComparer::addPeerLine( int32 peer, const ResourceDigestEntry& entry, int64 nowNanoseconds )
    {
        const optional<int64> suspectStartTime = _composition.isDigestMismatch( entry ) ? optional<int64>{ nowNanoseconds } : nullopt;
        uint32                lineCount        = 0;
        for ( PeerLine& line : _listLine )
        {
            if ( line._peer != peer )
                continue;
            ++lineCount;
            if ( line._entry._assetID == entry._assetID )
            {
                line._entry._hash      = entry._hash;
                line._suspectStartTime = suspectStartTime;
                return true;
            }
        }
        if ( lineCount >= kMaxLinesPerPeer )
            return false;
        PeerLine& line         = _listLine.emplace_back();
        line._peer             = peer;
        line._entry            = entry;
        line._suspectStartTime = suspectStartTime;
        return true;
    }

    void ResourceDigestComparer::removePeer( int32 peer )
    {
        (void)VectorUtil::removeIf( _listLine, [peer]( const PeerLine& line )
        { return line._peer == peer; } );
    }

    void ResourceDigestComparer::update( int64 nowNanoseconds, int64 graceNanoseconds, vector<ResourceDigestMismatch>& outListMismatch )
    {
        vector<ResourceDigestEntry> listChanged;
        _composition.collectAllChanges( _revision, listChanged );
        for ( PeerLine& line : _listLine )
        {
            const bool bChanged = listChanged.empty() == false && VectorUtil::containsIf( listChanged, [&line]( const ResourceDigestEntry& changed )
            { return changed._assetID == line._entry._assetID; } );
            if ( ( bChanged || line._suspectStartTime.has_value() ) == false )
                continue;
            if ( _composition.isDigestMismatch( line._entry ) == false )
            {
                line._suspectStartTime = nullopt;
                continue;
            }
            if ( line._suspectStartTime.has_value() == false )
                line._suspectStartTime = nowNanoseconds;
            if ( nowNanoseconds - *line._suspectStartTime >= graceNanoseconds )
            {
                ResourceDigestMismatch& mismatch = outListMismatch.emplace_back();
                mismatch._peer                   = line._peer;
                mismatch._assetID                = line._entry._assetID;
                line._suspectStartTime           = nullopt;
            }
        }
    }

    void ResourceDigestComparer::clear()
    {
        _listLine.clear();
    }
} // namespace sw
