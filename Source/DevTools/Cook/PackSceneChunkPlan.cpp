#include "pch.h"

#include "DevTools/Cook/PackSceneChunkPlan.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct PackSceneChunkPlanInternal
        {
            static constexpr string_view kGamePrefix  = "game/";
            static constexpr string_view kSceneSuffix = ".scene.xml";
            static constexpr string_view kChunkPrefix = "scene_";
            static constexpr string_view kMapsFolder  = "/maps/";

            static bool isNameLetter( const utf8 letter ) { return ( 'a' <= letter && letter <= 'z' ) || ( '0' <= letter && letter <= '9' ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string PackSceneChunkPlan::makeSceneChunkName( string_view sceneID )
    {
        using Internal         = PackSceneChunkPlanInternal;
        string      normalized = StringUtil::toLower( sceneID );
        string_view rest       = normalized;
        if ( StringUtil::startsWith( rest, Internal::kGamePrefix ) )
            rest.remove_prefix( Internal::kGamePrefix.size() );
        if ( StringUtil::endsWith( rest, Internal::kSceneSuffix ) )
            rest.remove_suffix( Internal::kSceneSuffix.size() );
        // `playground/maps/level2` → `playground_level2` — 맵 폴더는 모든 씬에 같아 이름에서 뺀다.
        string       body{ rest };
        const size_t maps = body.find( Internal::kMapsFolder.data(), 0, Internal::kMapsFolder.size() );
        if ( maps != string::npos )
            body.replace( maps, Internal::kMapsFolder.size(), "/" );
        string name{ Internal::kChunkPrefix };
        for ( const utf8 letter : body )
        {
            name.push_back( Internal::isNameLetter( letter ) ? letter : '_' );
        }
        return name;
    }

    expected<PackSceneChunkPlan> PackSceneChunkPlan::plan( const unordered_set<string>& setStartup, const vector<PackSceneReach>& listScene )
    {
        // 씬 이름이 겹치면 두 씬의 에셋이 한 청크에 섞여 한쪽을 열 때 다른 쪽까지 받는다 — 이름을 바꾸게 멈춘다.
        unordered_map<string, string> mapNameScene;
        for ( const PackSceneReach& scene : listScene )
        {
            const string name              = makeSceneChunkName( scene._sceneID );
            const auto [itName, bInserted] = mapNameScene.try_emplace( name, scene._sceneID );
            if ( bInserted == false && itName->second != scene._sceneID )
            {
                StringBuilder<constant::buffer::kMax512> detail;
                detail.appendFormat( "%# / %#", itName->second, scene._sceneID );
                return unexpected( Error{ ErrorKind::InvalidArgument, "two scenes make the same pack chunk name - rename one", detail.view() } );
            }
        }

        // 에셋마다 쓰는 씬을 센다(기동 필수는 셈하지 않는다).
        unordered_map<string, string> mapFirstScene;
        unordered_set<string>         setShared;
        for ( const PackSceneReach& scene : listScene )
        {
            for ( const string& assetID : scene._setReached )
            {
                if ( setStartup.contains( assetID ) )
                    continue;
                const auto [itAsset, bInserted] = mapFirstScene.try_emplace( assetID, scene._sceneID );
                if ( bInserted == false && itAsset->second != scene._sceneID )
                    setShared.insert( assetID );
            }
        }
        PackSceneChunkPlan result;
        for ( const auto& [assetID, sceneID] : mapFirstScene )
        {
            result._mapAssetChunk.emplace( assetID, setShared.contains( assetID ) ? string( kSharedChunk ) : makeSceneChunkName( sceneID ) );
        }
        return result;
    }

    vector<PackSceneEntry> PackSceneChunkPlan::makeSceneChunks( const vector<PackSceneReach>& listScene, const unordered_map<string, string>& mapAssetChunk, const unordered_set<string>& setStartupChunk )
    {
        vector<PackSceneEntry> listResult;
        for ( const PackSceneReach& scene : listScene )
        {
            unordered_set<string> setChunk;
            for ( const string& assetID : scene._setReached )
            {
                const auto itChunk = mapAssetChunk.find( assetID );
                if ( itChunk != mapAssetChunk.end() && setStartupChunk.contains( itChunk->second ) == false )
                    setChunk.insert( itChunk->second );
            }
            PackSceneEntry& entry = listResult.emplace_back();
            entry._sceneID        = scene._sceneID;
            entry._listChunk.assign( setChunk.begin(), setChunk.end() );
            VectorUtil::sort( entry._listChunk );
        }
        VectorUtil::sort( listResult, []( const PackSceneEntry& left, const PackSceneEntry& right )
        { return left._sceneID < right._sceneID; } );
        return listResult;
    }

    vector<PackChunkIssue> PackSceneChunkPlan::findUncovered( const vector<PackSceneReach>& listScene, const unordered_map<string, string>& mapAssetChunk, const unordered_set<string>& setStartupChunk,
                                                              const vector<PackSceneEntry>& listSceneChunks )
    {
        unordered_map<string, const PackSceneEntry*> mapSceneEntry; // 씬마다 목록 전체를 훑지 않는다
        for ( const PackSceneEntry& entry : listSceneChunks )
        {
            mapSceneEntry[entry._sceneID] = &entry;
        }
        vector<PackChunkIssue> listIssue;
        for ( const PackSceneReach& scene : listScene )
        {
            const auto                  entryIter = mapSceneEntry.find( scene._sceneID );
            const PackSceneEntry* const pEntry    = entryIter != mapSceneEntry.end() ? entryIter->second : nullptr;
            for ( const string& assetID : scene._setReached )
            {
                const auto   itChunk  = mapAssetChunk.find( assetID );
                const bool   bPacked  = itChunk != mapAssetChunk.end();
                const string chunk    = bPacked ? itChunk->second : string{};
                const bool   bStartup = bPacked && setStartupChunk.contains( chunk );
                const bool   bListed  = bPacked && pEntry != nullptr && VectorUtil::contains( pEntry->_listChunk, chunk );
                if ( bStartup == false && bListed == false )
                    listIssue.push_back( PackChunkIssue{ scene._sceneID, assetID, chunk } );
            }
        }
        VectorUtil::sort( listIssue, []( const PackChunkIssue& left, const PackChunkIssue& right )
        { return left._sceneID != right._sceneID ? left._sceneID < right._sceneID : left._assetID < right._assetID; } );
        return listIssue;
    }
} // namespace sw
