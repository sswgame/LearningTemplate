#include "pch.h"

#include "Core/Common/TopologicalSortUtil.h"

#include "Core/Common/Defines.h"

namespace sw
{
    namespace
    {
        struct TopologicalSortUtilInternal
        {
            /** @brief 찾지 못함을 뜻하는 노드 번호입니다. */
            static constexpr uint32 kNoNode = invalid_index::kUint32;

            /** @brief 방문 상태입니다(깊이 우선 순환 찾기). */
            enum class VisitState : uint8
            {
                Unvisited,
                OnPath,
                Done,
            };

            /** @brief @p node 에서 깊이 우선으로 내려가며 경로 위의 노드를 다시 만나면 그 구간을 순환으로 담습니다. */
            static bool visit( uint32 node, const vector<vector<uint32>>& listDependency, const vector<uint8>& listCandidateMask, vector<VisitState>& listState,
                               vector<uint32>& listPath, vector<uint32>& outListCycle )
            {
                listState[node] = VisitState::OnPath;
                listPath.push_back( node );
                for ( const uint32 dependency : listDependency[node] )
                {
                    if ( dependency >= listCandidateMask.size() || listCandidateMask[dependency] == SW_FALSE )
                        continue;
                    if ( listState[dependency] == VisitState::OnPath )
                    {
                        // 경로에서 그 노드가 처음 나온 자리부터가 순환이다. 시작 노드로 닫는다.
                        size_t start = 0;
                        while ( listPath[start] != dependency )
                            ++start;
                        outListCycle.assign( listPath.begin() + static_cast<std::ptrdiff_t>( start ), listPath.end() );
                        outListCycle.push_back( dependency );
                        return true;
                    }
                    if ( listState[dependency] == VisitState::Unvisited && visit( dependency, listDependency, listCandidateMask, listState, listPath, outListCycle ) )
                        return true;
                }
                listPath.pop_back();
                listState[node] = VisitState::Done;
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool TopologicalSortUtil::sortByDependency( const vector<string_view>& listName, const vector<vector<uint32>>& listDependency, vector<uint32>& outListOrder,
                                                vector<uint32>& outListUnsorted )
    {
        const uint32 nodeCount = static_cast<uint32>( listName.size() );
        outListOrder.clear();
        outListUnsorted.clear();
        outListOrder.reserve( nodeCount );

        vector<uint32> listRemaining( nodeCount, 0 );
        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            listRemaining[nodeIndex] = static_cast<uint32>( listDependency[nodeIndex].size() );
        vector<uint8> listEmitted( nodeCount, SW_FALSE );

        // 노드가 수십 개라 매번 처음부터 훑는다(우선순위 큐가 필요 없는 크기).
        while ( outListOrder.size() < nodeCount )
        {
            uint32 readyIndex = TopologicalSortUtilInternal::kNoNode;
            for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            {
                if ( listEmitted[nodeIndex] == SW_TRUE || listRemaining[nodeIndex] != 0 )
                    continue;
                if ( readyIndex == TopologicalSortUtilInternal::kNoNode || listName[nodeIndex] < listName[readyIndex] )
                    readyIndex = nodeIndex;
            }
            if ( readyIndex == TopologicalSortUtilInternal::kNoNode )
            {
                for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
                {
                    if ( listEmitted[nodeIndex] == SW_FALSE )
                        outListUnsorted.push_back( nodeIndex );
                }
                outListOrder.clear();
                return false;
            }

            listEmitted[readyIndex] = SW_TRUE;
            outListOrder.push_back( readyIndex );
            for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            {
                for ( const uint32 dependencyIndex : listDependency[nodeIndex] )
                {
                    if ( dependencyIndex == readyIndex )
                        --listRemaining[nodeIndex];
                }
            }
        }
        return true;
    }

    bool TopologicalSortUtil::findCycle( const vector<vector<uint32>>& listDependency, const vector<uint32>& listCandidate, vector<uint32>& outListCycle )
    {
        outListCycle.clear();
        vector<uint8> listCandidateMask( listDependency.size(), SW_FALSE );
        for ( const uint32 node : listCandidate )
        {
            if ( node < listCandidateMask.size() )
                listCandidateMask[node] = SW_TRUE;
        }
        vector<TopologicalSortUtilInternal::VisitState> listState( listDependency.size(), TopologicalSortUtilInternal::VisitState::Unvisited );
        vector<uint32>                                  listPath;
        for ( const uint32 node : listCandidate )
        {
            if ( node < listState.size() && listState[node] == TopologicalSortUtilInternal::VisitState::Unvisited &&
                 TopologicalSortUtilInternal::visit( node, listDependency, listCandidateMask, listState, listPath, outListCycle ) )
                return true;
        }
        return false;
    }
} // namespace sw
