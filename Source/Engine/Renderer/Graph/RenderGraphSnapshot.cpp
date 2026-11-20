#include "pch.h"

#include "Engine/Renderer/Graph/RenderGraphSnapshot.h"

#include "Core/Container/unordered_map.h"

namespace sw
{
    RenderGraphSnapshot RenderGraphSnapshot::build( const RenderGraph& graph, string_view graphName )
    {
        RenderGraphSnapshot snapshot{};
        snapshot._graphName = string( graphName );

        const vector<vector<hashed_string>>& listLevel = graph.getExecutionLevels();
        unordered_map<hashed_string, uint32> mapPassLevel;
        for ( uint32 levelIndex = 0; levelIndex < static_cast<uint32>( listLevel.size() ); ++levelIndex )
        {
            for ( const hashed_string& passName : listLevel[levelIndex] )
            {
                mapPassLevel[passName] = levelIndex;
            }
        }
        snapshot._levelCount = static_cast<uint32>( listLevel.size() );

        // 자원 번호는 처음 쓰이는 차례다 — 수명 표(`getResourceLifetimes`)가 실행 순서로 채워 같은 차례다.
        unordered_map<hashed_string, uint32> mapResourceIndex;
        for ( const RenderGraphResourceLifetime& life : graph.getResourceLifetimes() )
        {
            RenderGraphSnapshotResource resource{};
            resource._name               = life._name.c_str();
            resource._firstPass          = static_cast<uint32>( life._firstPassIndex );
            resource._lastPass           = static_cast<uint32>( life._lastPassIndex );
            resource._kind               = life._kind;
            mapResourceIndex[life._name] = static_cast<uint32>( snapshot._listResource.size() );
            snapshot._listResource.push_back( std::move( resource ) );
        }

        // 실행과 같은 추론을 빈 문맥으로 — 들인 자원은 처음 접근을 심고(전이로 세지 않는다), 패스마다 늘어난 수가 그 패스의 전이다.
        RenderGraphExecutionContext context{};
        context.reset();
        for ( const RenderGraphResourceAccess& imported : graph.getImportedResources() )
        {
            context.seedAccess( imported._resource, imported._access );
            if ( const auto it = mapResourceIndex.find( imported._resource ); it != mapResourceIndex.end() )
                snapshot._listResource[it->second]._bImported = SW_TRUE;
        }

        const vector<hashed_string>&        listOrder     = graph.getExecutionOrder();
        const vector<RenderGraphQueueStep>& listQueueStep = graph.getQueueSteps();
        snapshot._listPass.reserve( listOrder.size() );
        for ( size_t orderIndex = 0; orderIndex < listOrder.size(); ++orderIndex )
        {
            const RenderGraphNode* pNode = graph.findNode( listOrder[orderIndex] );
            if ( pNode == nullptr )
                continue;
            RenderGraphSnapshotPass pass{};
            pass._name = pNode->_name.c_str();
            if ( const auto it = mapPassLevel.find( pNode->_name ); it != mapPassLevel.end() )
                pass._level = it->second;
            if ( orderIndex < listQueueStep.size() )
            {
                pass._queue      = listQueueStep[orderIndex]._queue;
                pass._asyncBatch = listQueueStep[orderIndex]._batch;
            }
            const uint32 transitionsBefore = context._lastTransitionCount;
            for ( const RenderGraphResourceAccess& access : pNode->_listAccess )
            {
                (void)context.transitionTo( access._resource, access._access );
                const auto it = mapResourceIndex.find( access._resource );
                if ( it == mapResourceIndex.end() )
                    continue;
                snapshot._listResource[it->second]._viewable = access._viewable;
                pass._listAccess.push_back( RenderGraphSnapshotAccess{ it->second, access._access } );
            }
            pass._transitionCount = context._lastTransitionCount - transitionsBefore;
            snapshot._transitionCount += pass._transitionCount;
            snapshot._listPass.push_back( std::move( pass ) );
        }
        return snapshot;
    }

    optional<uint32> RenderGraphSnapshot::findResource( string_view name ) const
    {
        for ( uint32 index = 0; index < static_cast<uint32>( _listResource.size() ); ++index )
        {
            if ( string_view{ _listResource[index]._name } == name )
                return index;
        }
        return nullopt;
    }
} // namespace sw
