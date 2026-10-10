#include "pch.h"

#include "Editor/Panels/ProfilerCallTree.h"

#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw::editor
{
    void ProfilerCallTree::compute( const vector<ProfilerTimelineThread>& listThread, vector<ProfilerCallNode>& outListNode )
    {
        outListNode.clear();
        vector<uint32> listPath;      // 지금 경로(겹 순) — 끝이 지금 사건의 부모
        vector<uint32> listLastChild; // 노드마다 마지막 자식(자식을 처음 나온 순서로 잇는다)
        for ( uint32 threadIndex = 0; threadIndex < static_cast<uint32>( listThread.size() ); ++threadIndex )
        {
            listPath.clear();
            for ( const ProfilerTimelineEvent& event : listThread[threadIndex]._listEvent )
            {
                // 겹 d 의 사건은 바로 앞의 겹 d - 1 사건 안에 있다. 녹화 링이 바깥 구간을 잃었으면(앞이 덮였다) 있는 데까지만 붙인다.
                if ( listPath.size() > event._depth )
                    listPath.resize( event._depth );
                const uint32 parent = listPath.empty() ? kNoParent : listPath.back();

                // 같은 부모 아래 같은 슬롯이면 접는다.
                uint32 nodeIndex = kNoParent;
                uint32 child     = parent == kNoParent ? kNoParent : outListNode[parent]._firstChild;
                if ( parent == kNoParent )
                {
                    for ( uint32 index = 0; index < static_cast<uint32>( outListNode.size() ); ++index )
                    {
                        const ProfilerCallNode& node = outListNode[index];
                        if ( node._parent == kNoParent && node._threadIndex == threadIndex && node._slot == event._slot )
                            nodeIndex = index;
                    }
                }
                for ( ; child != kNoParent && nodeIndex == kNoParent; child = outListNode[child]._nextSibling )
                {
                    if ( outListNode[child]._slot == event._slot )
                        nodeIndex = child;
                }
                if ( nodeIndex == kNoParent )
                {
                    nodeIndex = static_cast<uint32>( outListNode.size() );
                    ProfilerCallNode node{};
                    node._slot        = event._slot;
                    node._parent      = parent;
                    node._threadIndex = threadIndex;
                    node._firstChild  = kNoParent;
                    node._nextSibling = kNoParent;
                    node._depth       = static_cast<uint16>( listPath.size() );
                    outListNode.push_back( node );
                    listLastChild.push_back( kNoParent );
                    if ( parent != kNoParent )
                    {
                        if ( listLastChild[parent] == kNoParent )
                            outListNode[parent]._firstChild = nodeIndex;
                        else
                            outListNode[listLastChild[parent]]._nextSibling = nodeIndex;
                        listLastChild[parent] = nodeIndex;
                    }
                }
                const uint64 duration = event._endNanos > event._beginNanos ? event._endNanos - event._beginNanos : 0;
                outListNode[nodeIndex]._totalNanos += duration;
                outListNode[nodeIndex]._selfNanos += duration;
                ++outListNode[nodeIndex]._callCount;
                if ( parent != kNoParent )
                    outListNode[parent]._selfNanos -= MathUtil::min( outListNode[parent]._selfNanos, duration );
                listPath.push_back( nodeIndex );
            }
        }
    }

    void ProfilerCallTree::sortChildrenByTotal( vector<ProfilerCallNode>& inoutListNode )
    {
        vector<uint32> listChild;
        for ( ProfilerCallNode& node : inoutListNode )
        {
            listChild.clear();
            for ( uint32 child = node._firstChild; child != kNoParent; child = inoutListNode[child]._nextSibling )
            {
                listChild.push_back( child );
            }
            if ( listChild.size() < 2 )
                continue;
            std::stable_sort( listChild.begin(), listChild.end(), [&inoutListNode]( uint32 left, uint32 right )
            { return inoutListNode[left]._totalNanos > inoutListNode[right]._totalNanos; } );
            node._firstChild = listChild[0];
            for ( size_t index = 0; index < listChild.size(); ++index )
            {
                inoutListNode[listChild[index]]._nextSibling = index + 1 < listChild.size() ? listChild[index + 1] : kNoParent;
            }
        }
    }

    void ProfilerCallTree::collectRoots( const vector<ProfilerCallNode>& listNode, vector<uint32>& outListRoot )
    {
        outListRoot.clear();
        for ( uint32 index = 0; index < static_cast<uint32>( listNode.size() ); ++index )
        {
            if ( listNode[index]._parent == kNoParent )
                outListRoot.push_back( index );
        }
        std::stable_sort( outListRoot.begin(), outListRoot.end(), [&listNode]( uint32 left, uint32 right )
        {
            if ( listNode[left]._threadIndex != listNode[right]._threadIndex )
                return listNode[left]._threadIndex < listNode[right]._threadIndex;
            return listNode[left]._totalNanos > listNode[right]._totalNanos;
        } );
    }
} // namespace sw::editor
