#include "pch.h"

#include "Editor/Common/Widgets/EditorNodeGraphRules.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Widgets/Model/EditorListFilter.h"

#include <algorithm>

namespace sw::editor
{
    void EditorNodeGraphRules::filterNodeKinds( const vector<EditorGraphNodeKind>& listKind, string_view filter, vector<uint32>& outListIndex )
    {
        outListIndex.clear();
        const EditorListFilter listFilter{ filter };
        // 그룹이 처음 나온 순서를 지키며 그룹마다 모은다(정렬하지 않는다 — 패널이 적은 순서가 곧 보일 순서다).
        vector<string_view> listCategory;
        for ( const EditorGraphNodeKind& kind : listKind )
        {
            const string_view category = kind._pCategory != nullptr ? string_view{ kind._pCategory } : string_view{ "General" };
            bool              bKnown   = false;
            for ( const string_view known : listCategory )
            {
                bKnown = bKnown || known == category;
            }
            if ( bKnown == false )
                listCategory.push_back( category );
        }
        for ( const string_view category : listCategory )
        {
            for ( size_t index = 0; index < listKind.size(); ++index )
            {
                const EditorGraphNodeKind& kind         = listKind[index];
                const string_view          kindCategory = kind._pCategory != nullptr ? string_view{ kind._pCategory } : string_view{ "General" };
                if ( kindCategory != category )
                    continue;
                const string_view name = kind._pName != nullptr ? string_view{ kind._pName } : string_view{};
                if ( listFilter.matchesAny( { name, kindCategory } ) )
                    outListIndex.push_back( static_cast<uint32>( index ) );
            }
        }
    }

    bool EditorNodeGraphRules::canConnect( int32 pinA, const EditorGraphPinInfo& infoA, int32 pinB, const EditorGraphPinInfo& infoB, int32& outFromPin,
                                           int32& outToPin, const utf8*& outReason )
    {
        outReason = nullptr;
        if ( pinA == pinB )
        {
            outReason = "Cannot link a pin to itself";
            return false;
        }
        if ( infoA._bInput == infoB._bInput )
        {
            outReason = infoA._bInput ? "Both pins are inputs" : "Both pins are outputs";
            return false;
        }
        const bool bTypeMatches = infoA._type == infoB._type || infoA._type == kWildcardPinType || infoB._type == kWildcardPinType;
        if ( bTypeMatches == false )
        {
            outReason = "Pin types differ";
            return false;
        }
        outFromPin = infoA._bInput ? pinB : pinA;
        outToPin   = infoA._bInput ? pinA : pinB;
        return true;
    }

    void EditorNodeGraphRules::collectUnreachableNodes( const vector<int32>& listInputNode, const vector<EditorGraphEdge>& listEdge, vector<EditorGraphNodeIssue>& outListIssue )
    {
        outListIssue.clear();
        for ( const int32 nodeID : listInputNode )
        {
            bool bReached = false;
            for ( const EditorGraphEdge& edge : listEdge )
            {
                bReached = bReached || ( edge._toNode == nodeID && edge._fromNode != nodeID );
            }
            if ( bReached )
                continue;
            EditorGraphNodeIssue issue{};
            issue._nodeID  = nodeID;
            issue._message = "Nothing links into this node - it is never reached";
            outListIssue.push_back( std::move( issue ) );
        }
    }
    bool EditorNodeGraphRules::alignNodes( vector<EditorGraphNodeRect>& ioListRect, EditorGraphAlign align )
    {
        const bool bDistribute = align == EditorGraphAlign::DistributeHorizontal || align == EditorGraphAlign::DistributeVertical;
        if ( ioListRect.size() < ( bDistribute ? 3u : 2u ) || align == EditorGraphAlign::Count )
            return false;
        float2 boundsMin = ioListRect[0]._position;
        float2 boundsMax = ioListRect[0]._position + ioListRect[0]._size;
        for ( const EditorGraphNodeRect& rect : ioListRect )
        {
            boundsMin = float2{ MathUtil::min( boundsMin._x, rect._position._x ), MathUtil::min( boundsMin._y, rect._position._y ) };
            boundsMax = float2{ MathUtil::max( boundsMax._x, rect._position._x + rect._size._x ), MathUtil::max( boundsMax._y, rect._position._y + rect._size._y ) };
        }
        const float2 center = ( boundsMin + boundsMax ) * 0.5f;
        if ( bDistribute == false )
        {
            for ( EditorGraphNodeRect& rect : ioListRect )
            {
                if ( align == EditorGraphAlign::Left )
                    rect._position._x = boundsMin._x;
                else if ( align == EditorGraphAlign::Top )
                    rect._position._y = boundsMin._y;
                else if ( align == EditorGraphAlign::CenterHorizontal )
                    rect._position._x = center._x - rect._size._x * 0.5f;
                else
                    rect._position._y = center._y - rect._size._y * 0.5f;
            }
            return true;
        }
        // 나누기: 축 위 순서로 줄 세우고 양 끝을 둔 채 빈칸을 고르게 나눈다.
        const bool     bHorizontal = align == EditorGraphAlign::DistributeHorizontal;
        vector<uint32> listOrder;
        float32        totalSize = 0.0f;
        for ( uint32 index = 0; index < static_cast<uint32>( ioListRect.size() ); ++index )
        {
            listOrder.push_back( index );
            totalSize += bHorizontal ? ioListRect[index]._size._x : ioListRect[index]._size._y;
        }
        std::sort( listOrder.begin(), listOrder.end(), [&ioListRect, bHorizontal]( uint32 a, uint32 b )
        { return bHorizontal ? ioListRect[a]._position._x < ioListRect[b]._position._x : ioListRect[a]._position._y < ioListRect[b]._position._y; } );
        const float32 span   = bHorizontal ? boundsMax._x - boundsMin._x : boundsMax._y - boundsMin._y;
        const float32 gap    = ( span - totalSize ) / static_cast<float32>( listOrder.size() - 1 );
        float32       cursor = bHorizontal ? boundsMin._x : boundsMin._y;
        for ( const uint32 index : listOrder )
        {
            EditorGraphNodeRect& rect = ioListRect[index];
            if ( bHorizontal )
                rect._position._x = cursor;
            else
                rect._position._y = cursor;
            cursor += ( bHorizontal ? rect._size._x : rect._size._y ) + gap;
        }
        return true;
    }

    bool EditorNodeGraphRules::fitMinimap( const vector<EditorGraphNodeRect>& listRect, const float2& viewMin, const float2& viewMax, const float2& mapSize,
                                           EditorGraphMinimapTransform& outTransform )
    {
        if ( mapSize._x <= 0.0f || mapSize._y <= 0.0f )
            return false;
        float2 boundsMin = viewMin;
        float2 boundsMax = viewMax;
        for ( const EditorGraphNodeRect& rect : listRect )
        {
            boundsMin = float2{ MathUtil::min( boundsMin._x, rect._position._x ), MathUtil::min( boundsMin._y, rect._position._y ) };
            boundsMax = float2{ MathUtil::max( boundsMax._x, rect._position._x + rect._size._x ), MathUtil::max( boundsMax._y, rect._position._y + rect._size._y ) };
        }
        const float2 extent = boundsMax - boundsMin;
        if ( extent._x <= 0.0f || extent._y <= 0.0f )
            return false;
        outTransform._scale        = MathUtil::min( mapSize._x / extent._x, mapSize._y / extent._y );
        outTransform._canvasOrigin = boundsMin;
        outTransform._mapOffset    = ( mapSize - extent * outTransform._scale ) * 0.5f; // 남는 쪽은 가운데로
        return true;
    }

    float2 EditorNodeGraphRules::canvasToMinimap( const EditorGraphMinimapTransform& transform, const float2& canvasPoint )
    {
        return ( canvasPoint - transform._canvasOrigin ) * transform._scale + transform._mapOffset;
    }

    float2 EditorNodeGraphRules::minimapToCanvas( const EditorGraphMinimapTransform& transform, const float2& mapPoint )
    {
        if ( transform._scale <= 0.0f )
            return transform._canvasOrigin;
        return ( mapPoint - transform._mapOffset ) * ( 1.0f / transform._scale ) + transform._canvasOrigin;
    }

    optional<int32> EditorNodeGraphRules::findNearestNode( const vector<EditorGraphNodeRect>& listRect, const float2& canvasPoint )
    {
        optional<int32> nearestID;
        float32         nearestDist = 0.0f;
        for ( const EditorGraphNodeRect& rect : listRect )
        {
            const float2  delta = rect._position + rect._size * 0.5f - canvasPoint;
            const float32 dist  = delta._x * delta._x + delta._y * delta._y;
            if ( nearestID.has_value() && dist >= nearestDist )
                continue;
            nearestID   = rect._nodeID;
            nearestDist = dist;
        }
        return nearestID;
    }
} // namespace sw::editor
