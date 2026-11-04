#include "pch.h"

#include "Editor/Common/Workspace/HotReload/AssetReloadCascade.h"

#include "Core/Container/TopologicalSortUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/Reference/EditorReferenceIndex.h"

namespace sw::editor
{
    bool AssetReloadCascade::planDependents( const EditorReferenceIndex& index, string_view changedID, AssetInlinesReferencesFunc pfnInlinesReferences, vector<string>& outListOrder )
    {
        outListOrder.clear();
        // 0 번이 바뀐 에셋이고, 나머지는 그것을 (사슬로) 펼쳐 넣는 참조자다. 방문 집합이 같은 참조자를 두 번 넣지 않고 순환에서도 끝나게 한다.
        vector<string>                listNode{ FileUtil::normalizePath( changedID ) };
        unordered_map<string, uint32> mapNodeIndex{
            { listNode[0], 0u }
        };
        vector<EditorAssetReference> listReference;
        for ( size_t cursor = 0; cursor < listNode.size(); ++cursor )
        {
            index.findReferrers( listNode[cursor], listReference );
            for ( const EditorAssetReference& reference : listReference )
            {
                if ( mapNodeIndex.contains( reference._referrerPath ) || pfnInlinesReferences( reference._referrerPath ) == false )
                    continue;
                mapNodeIndex.emplace( reference._referrerPath, static_cast<uint32>( listNode.size() ) );
                listNode.push_back( reference._referrerPath );
            }
        }
        if ( listNode.size() == 1 )
            return true;

        // 노드마다 집합 안에서 그것이 펼쳐 넣는 대상이 먼저 온다. 바뀐 에셋 자신의 의존도 넣어야 그것을 거쳐 도는 순환이 드러난다.
        vector<vector<uint32>> listDependency( listNode.size() );
        for ( uint32 nodeIndex = 0; nodeIndex < listNode.size(); ++nodeIndex )
        {
            index.findDependencies( listNode[nodeIndex], listReference );
            for ( const EditorAssetReference& reference : listReference )
            {
                const auto itTarget = mapNodeIndex.find( reference._targetPath );
                if ( itTarget == mapNodeIndex.end() || itTarget->second == nodeIndex || VectorUtil::contains( listDependency[nodeIndex], itTarget->second ) )
                    continue;
                listDependency[nodeIndex].push_back( itTarget->second );
            }
        }

        vector<string_view> listName;
        listName.reserve( listNode.size() );
        for ( const string& node : listNode )
        {
            listName.push_back( node );
        }
        vector<uint32> listOrder;
        vector<uint32> listUnsorted;
        if ( TopologicalSortUtil::sortByDependency( listName, listDependency, listOrder, listUnsorted ) == false )
        {
            vector<uint32> listCycle;
            string         cycleText;
            if ( TopologicalSortUtil::findCycle( listDependency, listUnsorted, listCycle ) )
            {
                for ( const uint32 nodeIndex : listCycle )
                {
                    cycleText += cycleText.empty() ? "" : " -> ";
                    cycleText += listNode[nodeIndex];
                }
            }
            SW_LOG_ERROR_ID( "AssetReloadCascade.ReferenceCycle", "Asset references loop (%#) - assets that inline %# are not reloaded", cycleText.c_str(), listNode[0].c_str() );
            return false;
        }

        outListOrder.reserve( listOrder.size() - 1 );
        for ( const uint32 nodeIndex : listOrder )
        {
            if ( nodeIndex != 0 )
                outListOrder.push_back( listNode[nodeIndex] );
        }
        return true;
    }
} // namespace sw::editor
