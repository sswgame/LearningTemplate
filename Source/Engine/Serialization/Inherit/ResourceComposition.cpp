#include "pch.h"

#include "Engine/Serialization/Inherit/ResourceComposition.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"

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
                uint64         _hash;         ///< 합성 결과의 해시(대조)
                uint64         _revision;     ///< 결과가 마지막으로 바뀐 번호(`collectTagDependentChanges`)
                uint8          _bLayered : 1; ///< 덮어쓰기 층 파일을 얹었다

                Record()
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

            /** @brief 합성 기록 전체와 그것을 바꾸는 일입니다. 모든 멤버가 스스로 잠근다(여러 스레드가 읽기 · 기록한다). */
            class State
            {
            public:
                State()
                    : _mutex{}
                    , _listLayerRoot{}
                    , _mapRecord{}
                    , _mapRemapTag{}
                    , _listPendingReload{}
                    , _revision{ 0 }
                {
                }

                /** @brief 층 뿌리를 @p listRoot 로 바꿉니다. 바뀌었으면 옛 · 새 뿌리를 합친 목록을 @p outListTouched 에 담고 true 입니다. */
                bool replaceLayers( const vector<string>& listRoot, vector<string>& outListTouched )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    if ( _listLayerRoot == listRoot )
                        return false;
                    outListTouched = _listLayerRoot;
                    for ( const string& root : listRoot )
                    {
                        if ( VectorUtil::contains( outListTouched, root ) == false )
                            outListTouched.push_back( root );
                    }
                    _listLayerRoot = listRoot;
                    return true;
                }

                vector<string> getLayers()
                {
                    ScopedLock<mutex> lock{ _mutex };
                    return _listLayerRoot;
                }

                void record( string_view assetID, const vector<string>& listLoadTag, bool bLayered, uint64 hash )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    const auto [itRecord, bInserted] = _mapRecord.try_emplace( string{ assetID } );
                    Record& record                   = itRecord->second;
                    if ( bInserted || record._hash != hash )
                        record._revision = ++_revision;
                    record._listLoadTag = listLoadTag;
                    record._hash        = hash;
                    record._bLayered    = bLayered ? SW_TRUE : SW_FALSE;
                }

                void recordRemap( string_view assetID, const vector<string>& listLoadTag )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    vector<string>&   listTag = _mapRemapTag[string{ assetID }];
                    for ( const string& tag : listLoadTag )
                    {
                        if ( VectorUtil::contains( listTag, tag ) == false )
                            listTag.push_back( tag );
                    }
                }

                /** @brief 바뀐 태그 @p listChangedTag 에 기댄 합성 · 경로 교체를 다시 읽을 목록에 올립니다. */
                void markTagDependents( const vector<string>& listChangedTag )
                {
                    const auto isAffected = [&listChangedTag]( const vector<string>& listTag )
                    {
                        return VectorUtil::containsIf( listTag, [&listChangedTag]( const string& tag )
                        { return VectorUtil::contains( listChangedTag, tag ); } );
                    };
                    ScopedLock<mutex> lock{ _mutex };
                    for ( const auto& [assetID, record] : _mapRecord )
                    {
                        if ( isAffected( record._listLoadTag ) )
                            addPending( assetID );
                    }
                    for ( const auto& [assetID, listTag] : _mapRemapTag )
                    {
                        if ( isAffected( listTag ) )
                            addPending( assetID );
                    }
                }

                /** @brief 층을 얹은 합성은 다시 읽을 목록에 올리고, 얹지 않은 합성의 id 를 @p outListCandidate 에 담습니다(층 파일이 새로 생겼는지는 부른 쪽이 잠금 밖에서 본다). */
                void markLayeredAndCollectCandidates( vector<string>& outListCandidate )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    for ( const auto& [assetID, record] : _mapRecord )
                    {
                        if ( record._bLayered == SW_TRUE )
                            addPending( assetID );
                        else
                            outListCandidate.push_back( assetID );
                    }
                }

                void markPending( const vector<string>& listAssetID )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    for ( const string& assetID : listAssetID )
                    {
                        addPending( assetID );
                    }
                }

                void takePendingReload( vector<string>& outListAssetID )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    outListAssetID.swap( _listPendingReload );
                    _listPendingReload.clear();
                }

                /** @brief 번호 @p inoutRevision 뒤에 바뀐 기록을 모읍니다(@p bVaryingOnly 면 접속 대조에 올리는 것만, 번호 0 이면 전부). 정렬은 부른 쪽이 한다. */
                void collectChanges( uint64& inoutRevision, bool bVaryingOnly, vector<ResourceDigestEntry>& outListEntry )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    if ( _revision == inoutRevision )
                        return; // 프레임마다 묻는 자리다 - 대개 여기서 끝난다
                    for ( const auto& [assetID, record] : _mapRecord )
                    {
                        if ( record._revision > inoutRevision && ( bVaryingOnly == false || record.isComparedBetweenPeers() ) )
                            outListEntry.push_back( ResourceDigestEntry{ assetID, record._hash } );
                    }
                    inoutRevision = _revision;
                }

                void collectDigest( vector<ResourceDigestEntry>& outListEntry )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    for ( const auto& [assetID, record] : _mapRecord )
                    {
                        if ( record.isComparedBetweenPeers() )
                            outListEntry.push_back( ResourceDigestEntry{ assetID, record._hash } );
                    }
                }

                bool isDigestMismatch( const ResourceDigestEntry& entry )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    const auto        it = _mapRecord.find( entry._assetID );
                    return it != _mapRecord.end() && it->second._hash != entry._hash;
                }

                bool isLayered( string_view assetID )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    const auto        it = _mapRecord.find( string{ assetID } );
                    return it != _mapRecord.end() && it->second._bLayered == SW_TRUE;
                }

                void clear()
                {
                    ScopedLock<mutex> lock{ _mutex };
                    _mapRecord.clear();
                    _mapRemapTag.clear();
                    _listPendingReload.clear();
                    ++_revision; // 이미 받은 번호가 새 기록을 가리지 않게 되돌리지 않는다
                }

            private:
                /** @brief @p assetID 를 다시 읽을 목록에 올립니다(잠금 안에서 부른다). */
                void addPending( const string& assetID ) SW_REQUIRES_LOCK( _mutex )
                {
                    if ( VectorUtil::contains( _listPendingReload, assetID ) == false )
                        _listPendingReload.push_back( assetID );
                }

            private:
                mutex                                              _mutex;
                vector<string> _listLayerRoot                      SW_GUARDED_BY( _mutex );
                unordered_map<string, Record> _mapRecord           SW_GUARDED_BY( _mutex ); ///< 리소스 id 로 합성한 리소스 모두
                unordered_map<string, vector<string>> _mapRemapTag SW_GUARDED_BY( _mutex ); ///< 경로 교체가 물은 로드 축 태그(`recordRemap`)
                vector<string> _listPendingReload                  SW_GUARDED_BY( _mutex );
                uint64 _revision                                   SW_GUARDED_BY( _mutex ); ///< 기록의 결과가 바뀔 때마다 는다
            };

            static State& getState()
            {
                static State s_state;
                return s_state;
            }

            /** @brief 항목을 리소스 id 순으로 정렬합니다(받는 쪽이 줄 단위로 대조한다). */
            static void sortByAssetID( vector<ResourceDigestEntry>& inoutListEntry )
            {
                VectorUtil::sort( inoutListEntry, []( const ResourceDigestEntry& lhs, const ResourceDigestEntry& rhs )
                { return lhs._assetID < rhs._assetID; } );
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
        if ( ResourceCompositionInternal::getState().replaceLayers( listRoot, listTouched ) == false )
            return true;
        ResourceComposition::notifyLayersChanged( listTouched );
        return true;
    }

    vector<string> ResourceLayers::getLayers()
    {
        return ResourceCompositionInternal::getState().getLayers();
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
        // 기대지 않은 합성도 든다 - 상대가 층을 얹어 읽은 표와 맞댄다(파싱에 견주면 값싸다). 해시는 잠금 밖에서 짓는다.
        ResourceCompositionInternal::getState().record( assetID, listLoadTag, bLayered, hashText( composedText ) );
    }

    void ResourceComposition::recordRemap( string_view assetID, const vector<string>& listLoadTag )
    {
        ResourceCompositionInternal::getState().recordRemap( assetID, listLoadTag );
    }

    void ResourceComposition::notifyTagsChanged( const vector<string>& listChangedTag )
    {
        ResourceCompositionInternal::getState().markTagDependents( listChangedTag );
    }

    void ResourceComposition::notifyLayersChanged( const vector<string>& listRoot )
    {
        // 층 파일이 있는지는 잠금 밖에서 본다(파일 · 팩 색인을 읽는다). 후보는 리소스 id 로 합성한 적이 있는 것 — 읽지 않은 리소스는 다음 읽기가 새 층을 본다.
        ResourceCompositionInternal::State& state = ResourceCompositionInternal::getState();
        vector<string>                      listCandidate;
        state.markLayeredAndCollectCandidates( listCandidate );
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
        state.markPending( listLayered );
    }

    void ResourceComposition::takePendingReload( vector<string>& outListAssetID )
    {
        ResourceCompositionInternal::getState().takePendingReload( outListAssetID );
        VectorUtil::sort( outListAssetID );
    }

    void ResourceComposition::collectDigest( vector<ResourceDigestEntry>& outListEntry )
    {
        outListEntry.clear();
        ResourceCompositionInternal::getState().collectDigest( outListEntry );
        ResourceCompositionInternal::sortByAssetID( outListEntry );
    }

    void ResourceComposition::collectTagDependentChanges( uint64& inoutRevision, vector<ResourceDigestEntry>& outListEntry )
    {
        outListEntry.clear();
        ResourceCompositionInternal::getState().collectChanges( inoutRevision, true, outListEntry );
        ResourceCompositionInternal::sortByAssetID( outListEntry );
    }

    void ResourceComposition::collectAllChanges( uint64& inoutRevision, vector<ResourceDigestEntry>& outListEntry )
    {
        outListEntry.clear();
        ResourceCompositionInternal::getState().collectChanges( inoutRevision, false, outListEntry );
        ResourceCompositionInternal::sortByAssetID( outListEntry );
    }

    bool ResourceComposition::isDigestMismatch( const ResourceDigestEntry& entry )
    {
        return ResourceCompositionInternal::getState().isDigestMismatch( entry );
    }

    bool ResourceComposition::isLayered( string_view assetID )
    {
        return ResourceCompositionInternal::getState().isLayered( assetID );
    }

    uint64 ResourceComposition::hashText( string_view text )
    {
        return HashUtil::computeStringHash64( text.data(), text.size(), false );
    }

    void ResourceComposition::clear()
    {
        ResourceCompositionInternal::getState().clear();
    }

    ResourceDigestComparer::ResourceDigestComparer()
        : _listLine{}
        , _revision{ 0 }
    {
    }

    bool ResourceDigestComparer::addPeerLine( int32 peer, const ResourceDigestEntry& entry, int64 nowNanoseconds )
    {
        const int64 suspectStartTime = ResourceComposition::isDigestMismatch( entry ) ? nowNanoseconds : -1;
        uint32      lineCount        = 0;
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
        ResourceComposition::collectAllChanges( _revision, listChanged );
        for ( PeerLine& line : _listLine )
        {
            const bool bChanged = listChanged.empty() == false && VectorUtil::containsIf( listChanged, [&line]( const ResourceDigestEntry& changed )
            { return changed._assetID == line._entry._assetID; } );
            if ( ( bChanged || line._suspectStartTime >= 0 ) == false )
                continue;
            if ( ResourceComposition::isDigestMismatch( line._entry ) == false )
            {
                line._suspectStartTime = -1;
                continue;
            }
            if ( line._suspectStartTime < 0 )
                line._suspectStartTime = nowNanoseconds;
            if ( nowNanoseconds - line._suspectStartTime >= graceNanoseconds )
            {
                ResourceDigestMismatch& mismatch = outListMismatch.emplace_back();
                mismatch._peer                   = line._peer;
                mismatch._assetID                = line._entry._assetID;
                line._suspectStartTime           = -1;
            }
        }
    }

    void ResourceDigestComparer::clear()
    {
        _listLine.clear();
    }
} // namespace sw
