#include "pch.h"

#include "Core/Container/DependencyGraph.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Foundation/Constants.h"

namespace sw
{
    namespace
    {
        struct DependencyGraphInternal
        {
            /** @brief 찾지 못함을 뜻하는 노드 번호입니다. */
            static constexpr uint32 kNoNode = constant::invalid_index::kUint32;

            /** @brief 깊이 우선 방문 상태입니다. */
            enum class VisitState : uint8
            {
                Unvisited,
                OnPath,
                Done,
            };

            /** @brief 경로에서 @p node 가 처음 나온 자리부터 끝까지에 @p node 를 붙여 순환 경로로 담습니다. */
            static void takeCycle( const vector<uint32>& listPath, uint32 node, vector<uint32>& outListCycle )
            {
                size_t start = 0;
                while ( listPath[start] != node )
                {
                    ++start;
                }
                outListCycle.assign( listPath.begin() + static_cast<std::ptrdiff_t>( start ), listPath.end() );
                outListCycle.push_back( node );
            }

            /** @brief 후보(@p listCandidateMask)만 따라 깊이 우선으로 내려가며 경로 위의 노드를 다시 만나면 순환을 담고 true 입니다. */
            static bool visitForCycle( uint32 node, const vector<vector<uint32>>& listDependency, const vector<uint8>& listCandidateMask, vector<VisitState>& listState,
                                       vector<uint32>& listPath, vector<uint32>& outListCycle )
            {
                listState[node] = VisitState::OnPath;
                listPath.push_back( node );
                for ( const uint32 dependency : listDependency[node] )
                {
                    if ( listCandidateMask[dependency] == SW_FALSE )
                        continue;
                    if ( listState[dependency] == VisitState::OnPath )
                    {
                        takeCycle( listPath, dependency, outListCycle );
                        return true;
                    }
                    if ( listState[dependency] == VisitState::Unvisited && visitForCycle( dependency, listDependency, listCandidateMask, listState, listPath, outListCycle ) )
                        return true;
                }
                listPath.pop_back();
                listState[node] = VisitState::Done;
                return false;
            }

            /** @brief 후위 정렬의 방문입니다. 의존을 먼저 담고 자기를 담는다. 순환이면 경로를 담고 false 입니다. */
            static bool visitForOrder( uint32 node, const vector<vector<uint32>>& listDependency, vector<VisitState>& listState, vector<uint32>& listPath, DependencyOrder& outOrder )
            {
                if ( listState[node] == VisitState::Done )
                    return true;
                if ( listState[node] == VisitState::OnPath )
                {
                    takeCycle( listPath, node, outOrder._listCycle );
                    return false;
                }
                listState[node] = VisitState::OnPath;
                listPath.push_back( node );
                for ( const uint32 dependency : listDependency[node] )
                {
                    if ( visitForOrder( dependency, listDependency, listState, listPath, outOrder ) == false )
                        return false;
                }
                listPath.pop_back();
                listState[node] = VisitState::Done;
                outOrder._listOrder.push_back( node );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DependencyGraph::DependencyGraph( uint32 nodeCount )
        : _listDependency( nodeCount )
    {
    }

    void DependencyGraph::addDependency( uint32 node, uint32 dependency )
    {
        SW_ASSERT( node < _listDependency.size() && dependency < _listDependency.size() && "DependencyGraph: node index out of range" );
        _listDependency[node].push_back( dependency );
    }

    uint32 DependencyGraph::getNodeCount() const
    {
        return static_cast<uint32>( _listDependency.size() );
    }

    const vector<uint32>& DependencyGraph::getDependencies( uint32 node ) const
    {
        return _listDependency[node];
    }

    DependencyOrder DependencyGraph::sortInDataOrder() const
    {
        vector<uint32> listRank( _listDependency.size(), 0 );
        for ( uint32 nodeIndex = 0; nodeIndex < getNodeCount(); ++nodeIndex )
        {
            listRank[nodeIndex] = nodeIndex;
        }
        return sortByRank( listRank );
    }

    DependencyOrder DependencyGraph::sortByName( const vector<string_view>& listName ) const
    {
        SW_ASSERT( listName.size() == _listDependency.size() && "DependencyGraph::sortByName: one name per node" );
        // 이름 순서를 번호로 한 번 바꿔 두면 데이터 순서 정렬과 같은 Kahn 이다. 같은 이름은 안정 정렬이라 번호가 작은 쪽이 앞이다.
        vector<uint32> listByName( _listDependency.size(), 0 );
        for ( uint32 nodeIndex = 0; nodeIndex < getNodeCount(); ++nodeIndex )
        {
            listByName[nodeIndex] = nodeIndex;
        }
        VectorUtil::stableSort( listByName, [&listName]( uint32 left, uint32 right )
        { return listName[left] < listName[right]; } );
        vector<uint32> listRank( _listDependency.size(), 0 );
        for ( uint32 rank = 0; rank < getNodeCount(); ++rank )
        {
            listRank[listByName[rank]] = rank;
        }
        return sortByRank( listRank );
    }

    DependencyOrder DependencyGraph::sortDepthFirst( const vector<uint32>& listRoot ) const
    {
        DependencyOrder                             order;
        vector<DependencyGraphInternal::VisitState> listState( _listDependency.size(), DependencyGraphInternal::VisitState::Unvisited );
        vector<uint32>                              listPath;
        for ( const uint32 root : listRoot )
        {
            SW_ASSERT( root < _listDependency.size() && "DependencyGraph::sortDepthFirst: root out of range" );
            if ( DependencyGraphInternal::visitForOrder( root, _listDependency, listState, listPath, order ) == false )
            {
                order._listOrder.clear();
                return order;
            }
        }
        return order;
    }

    vector<uint32> DependencyGraph::findCycle( const vector<uint32>& listCandidate ) const
    {
        vector<uint32> listCycle;
        vector<uint8>  listCandidateMask( _listDependency.size(), SW_FALSE );
        for ( const uint32 node : listCandidate )
        {
            SW_ASSERT( node < _listDependency.size() && "DependencyGraph::findCycle: candidate out of range" );
            listCandidateMask[node] = SW_TRUE;
        }
        vector<DependencyGraphInternal::VisitState> listState( _listDependency.size(), DependencyGraphInternal::VisitState::Unvisited );
        vector<uint32>                              listPath;
        for ( const uint32 node : listCandidate )
        {
            const bool bCycleFoundFromNode = listState[node] == DependencyGraphInternal::VisitState::Unvisited &&
                                             DependencyGraphInternal::visitForCycle( node, _listDependency, listCandidateMask, listState, listPath, listCycle );
            if ( bCycleFoundFromNode )
                break;
        }
        return listCycle;
    }

    DependencyOrder DependencyGraph::sortByRank( const vector<uint32>& listRank ) const
    {
        const uint32    nodeCount = getNodeCount();
        DependencyOrder order;
        order._listOrder.reserve( nodeCount );

        vector<uint32>         listRemaining( nodeCount, 0 );
        vector<vector<uint32>> listDependent( nodeCount );
        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            listRemaining[nodeIndex] = static_cast<uint32>( _listDependency[nodeIndex].size() );
            for ( const uint32 dependency : _listDependency[nodeIndex] )
            {
                listDependent[dependency].push_back( nodeIndex );
            }
        }
        vector<uint8> listEmitted( nodeCount, SW_FALSE );

        // shortcut: 준비된 노드를 매번 처음부터 훑는다(O(n²)) — 그래프가 수백 노드를 넘으면 순위 힙으로 바꾼다.
        while ( order._listOrder.size() < nodeCount )
        {
            uint32 readyIndex = DependencyGraphInternal::kNoNode;
            for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            {
                if ( listEmitted[nodeIndex] == SW_TRUE || listRemaining[nodeIndex] != 0 )
                    continue;
                if ( readyIndex == DependencyGraphInternal::kNoNode || listRank[nodeIndex] < listRank[readyIndex] )
                    readyIndex = nodeIndex;
            }
            if ( readyIndex == DependencyGraphInternal::kNoNode )
            {
                for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
                {
                    if ( listEmitted[nodeIndex] == SW_FALSE )
                        order._listUnsorted.push_back( nodeIndex );
                }
                order._listOrder.clear();
                order._listCycle = findCycle( order._listUnsorted );
                return order;
            }

            listEmitted[readyIndex] = SW_TRUE;
            order._listOrder.push_back( readyIndex );
            for ( const uint32 dependent : listDependent[readyIndex] )
            {
                --listRemaining[dependent];
            }
        }
        return order;
    }
} // namespace sw
