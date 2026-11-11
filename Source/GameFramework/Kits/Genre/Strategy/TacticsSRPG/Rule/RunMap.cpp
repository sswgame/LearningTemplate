#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/RunMap.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

#include <algorithm>

namespace sw
{
    bool RunMap::loadSettings( string_view xmlText, string_view sourceName, RunMapSettings& outSettings )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( XMLReadUtil::parseRootOrWarn( doc, xmlText, sourceName, "RunMap", root ) == false )
            return false;
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 틀린 원소가 하나라도 있으면 설정 전체를 읽지 못한 것으로 본다(반쪽 지도를 만들지 않는다).
        RunMapSettings settings;
        bool           bValid = XMLFields::readFields( root, settings, sourceName );
        if ( bValid == false )
            return false; // 범위 밖 층 수(음수)로 아래 표를 만들면 터무니없는 크기를 할당한다
        settings._listForcedFloor.assign( static_cast<size_t>( settings._floorCount ), hashed_string{} );
        /** 설정은 끝까지 읽혀야 바깥으로 나가므로 지역 값을 든 읽는 쪽을 둔다. */
        struct RunMapElementReader
        {
            RunMapSettings& _settings;

            bool loadNode( const XMLNode& node, string_view sourceName ) { return GameDataXML::readInto( node, sourceName, _settings._listRule ); }
            bool loadFloor( const XMLNode& node, string_view sourceName )
            {
                RunForcedFloorDef forced;
                if ( XMLFields::readFields( node, forced, sourceName ) == false )
                    return false;
                const int32 index        = forced._index < 0 ? forced._index + _settings._floorCount : forced._index; // −1 은 마지막 층이다
                const bool  bInsideFloor = index >= 0 && index < _settings._floorCount;
                if ( bInsideFloor == false )
                {
                    SW_LOG_ERROR_ID( "RunMap.FloorIndexOutsideFloors", "%#: <Floor index=\"%#\"> is outside the %# floors", sourceName, forced._index, _settings._floorCount );
                    return false;
                }
                _settings._listForcedFloor[static_cast<size_t>( index )] = forced._kind;
                return true;
            }
        };
        using Element                          = XMLCatalogElement<RunMapElementReader>;
        static constexpr Element kArrElement[] = {
            Element::of<&RunMapElementReader::loadNode>( "Node" ).uncounted(),
            Element::of<&RunMapElementReader::loadFloor>( "Floor" ).uncounted(),
        };
        RunMapElementReader reader{ settings };
        bValid = XMLCatalogReadResult::readInDocumentOrder( reader, root, sourceName, kArrElement ).isClean();
        if ( settings._listRule.empty() )
        {
            SW_LOG_ERROR_ID( "RunMap.RunMapNeedsLeastOne", "%#: <RunMap> needs at least one <Node kind=\"...\"/>", sourceName );
            bValid = false;
        }
        if ( bValid )
            outSettings = settings;
        return bValid;
    }

    int32 RunMap::getOrCreateNode( int32 floor, int32 column )
    {
        int32& cell = _listCell[static_cast<size_t>( floor * _columnCount + column )];
        if ( cell < 0 )
        {
            RunNode node;
            node._floor  = floor;
            node._column = column;
            cell         = static_cast<int32>( _listNode.size() );
            _listNode.push_back( node );
        }
        return cell;
    }

    bool RunMap::intersectsExisting( int32 floor, int32 fromColumn, int32 toColumn ) const
    {
        if ( fromColumn == toColumn )
            return false;
        // 옆 열에서 거꾸로 이어진 선이 있으면 X 자로 엇갈린다.
        const int32 neighborIndex = _listCell[static_cast<size_t>( floor * _columnCount + toColumn )];
        if ( neighborIndex < 0 )
            return false;
        const int32 targetIndex = _listCell[static_cast<size_t>( ( floor + 1 ) * _columnCount + fromColumn )];
        if ( targetIndex < 0 )
            return false;
        const vector<int32>& listNext = _listNode[static_cast<size_t>( neighborIndex )]._listNext;
        return VectorUtil::contains( listNext, targetIndex );
    }

    hashed_string RunMap::pickKind( const RunMapSettings& settings, int32 floor, const hashed_string& previousKind, GameRandom& random ) const
    {
        if ( floor < static_cast<int32>( settings._listForcedFloor.size() ) && settings._listForcedFloor[static_cast<size_t>( floor )].empty() == false )
            return settings._listForcedFloor[static_cast<size_t>( floor )];
        float32 total = 0.0f;
        for ( const RunNodeRule& rule : settings._listRule )
        {
            const bool bAllowed = floor >= rule._minFloor && floor <= rule._maxFloor && ( rule._bNoRepeat == SW_FALSE || rule._kind != previousKind );
            total += bAllowed ? rule._weight : 0.0f;
        }
        if ( total <= 0.0f )
            return settings._listRule.empty() ? hashed_string{} : settings._listRule.front()._kind;
        float32 pick = random.nextFloat() * total;
        for ( const RunNodeRule& rule : settings._listRule )
        {
            const bool bAllowed = floor >= rule._minFloor && floor <= rule._maxFloor && ( rule._bNoRepeat == SW_FALSE || rule._kind != previousKind );
            if ( bAllowed == false )
                continue;
            if ( pick < rule._weight )
                return rule._kind;
            pick -= rule._weight;
        }
        return settings._listRule.back()._kind;
    }

    bool RunMap::generate( const RunMapSettings& settings, uint32 seed )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "RunMap.RunMapRefusedSettings", "RunMap: refused settings - %#", rangeError.c_str() );
            return false;
        }
        if ( seed == 0u )
        {
            // 0 은 난수의 고정점이다(`DeterministicRandom32::setSeed` 가 단언한다).
            SW_LOG_ERROR_ID( "RunMap.RunMapRefusedSeedZero", "RunMap: refused seed 0 - give a non-zero seed" );
            return false;
        }
        GameRandom random( seed );
        _floorCount  = settings._floorCount;
        _columnCount = settings._columnCount;
        _current     = kStart;
        _listNode.clear();
        _listCell.assign( static_cast<size_t>( _floorCount * _columnCount ), constant::invalid_index::kInt32 );

        // 길을 아래에서 위로 긋는다. 마지막 층 바로 아래까지 긋는다. 첫 두 길은 서로 다른 열에서 시작한다.
        const int32 lastFloor  = _floorCount - 1;
        int32       firstStart = constant::invalid_index::kInt32;
        for ( int32 pathIndex = 0; pathIndex < settings._pathCount; ++pathIndex )
        {
            int32      column               = random.nextInt( 0, _columnCount - 1 );
            const bool bAvoidDuplicateStart = pathIndex == 1 && column == firstStart && _columnCount > 1;
            if ( bAvoidDuplicateStart )
                column = ( column + 1 ) % _columnCount;
            if ( pathIndex == 0 )
                firstStart = column;
            int32 nodeIndex = getOrCreateNode( 0, column );
            for ( int32 floor = 0; floor + 1 < lastFloor; ++floor )
            {
                int32 nextColumn = MathUtil::clamp( column + random.nextInt( -1, 1 ), 0, _columnCount - 1 );
                if ( intersectsExisting( floor, column, nextColumn ) )
                    nextColumn = column; // 엇갈리면 곧게 편다
                const int32    nextIndex = getOrCreateNode( floor + 1, nextColumn );
                vector<int32>& listNext  = _listNode[static_cast<size_t>( nodeIndex )]._listNext;
                if ( VectorUtil::contains( listNext, nextIndex ) == false )
                    listNext.push_back( nextIndex );
                nodeIndex = nextIndex;
                column    = nextColumn;
            }
        }
        // 마지막 층은 가운데 노드 하나로 모은다.
        const int32 bossIndex = getOrCreateNode( lastFloor, _columnCount / 2 );
        for ( size_t index = 0; index < _listNode.size(); ++index )
        {
            if ( _listNode[index]._floor == lastFloor - 1 )
                _listNode[index]._listNext.push_back( bossIndex );
        }
        // 종류는 층 순서대로 정한다. 들어오는 노드 중 하나의 종류를 "바로 앞" 으로 본다.
        vector<int32> listOrder;
        for ( size_t index = 0; index < _listNode.size(); ++index )
        {
            listOrder.push_back( static_cast<int32>( index ) );
        }
        VectorUtil::stableSort( listOrder, [this]( int32 lhs, int32 rhs )
        {
            const RunNode& lhsNode = _listNode[static_cast<size_t>( lhs )];
            const RunNode& rhsNode = _listNode[static_cast<size_t>( rhs )];
            return lhsNode._floor != rhsNode._floor ? lhsNode._floor < rhsNode._floor : lhsNode._column < rhsNode._column;
        } );
        vector<hashed_string> listPreviousKind( _listNode.size(), hashed_string{} );
        for ( const int32 index : listOrder )
        {
            RunNode& node = _listNode[static_cast<size_t>( index )];
            node._kind    = pickKind( settings, node._floor, listPreviousKind[static_cast<size_t>( index )], random );
            for ( const int32 nextIndex : node._listNext )
            {
                listPreviousKind[static_cast<size_t>( nextIndex )] = node._kind;
            }
        }
        for ( RunNode& node : _listNode )
        {
            VectorUtil::sort( node._listNext, [this]( int32 lhs, int32 rhs )
            {
                return _listNode[static_cast<size_t>( lhs )]._column < _listNode[static_cast<size_t>( rhs )]._column;
            } );
        }
        return true;
    }

    void RunMap::collectChoices( vector<int32>& outListNode ) const
    {
        outListNode.clear();
        if ( _current == kStart )
        {
            for ( int32 column = 0; column < _columnCount; ++column )
            {
                const int32 index = _listCell.empty() ? constant::invalid_index::kInt32 : _listCell[static_cast<size_t>( column )];
                if ( index >= 0 )
                    outListNode.push_back( index );
            }
            return;
        }
        outListNode = _listNode[static_cast<size_t>( _current )]._listNext;
    }

    bool RunMap::moveTo( int32 nodeIndex )
    {
        vector<int32> listChoice;
        collectChoices( listChoice );
        if ( VectorUtil::contains( listChoice, nodeIndex ) == false )
            return false;
        _current = nodeIndex;
        return true;
    }

    const RunNode* RunMap::findNode( int32 nodeIndex ) const
    {
        return nodeIndex >= 0 && nodeIndex < static_cast<int32>( _listNode.size() ) ? &_listNode[static_cast<size_t>( nodeIndex )] : nullptr;
    }

    bool RunMap::isFinished() const
    {
        return _current >= 0 && _listNode[static_cast<size_t>( _current )]._floor == _floorCount - 1;
    }

    void RunMap::serializeState( StateIO& io )
    {
        // 노드마다 이름(4) + 층과 열(8) + 이어진 수(4)
        io.list( _listNode, 16, [&]( RunNode& node )
        {
            io.value( node._kind );
            io.value( node._floor );
            io.value( node._column );
            io.list( node._listNext, 4, [&]( int32& next )
            {
                io.value( next );
                io.check( 0 <= next && static_cast<size_t>( next ) < _listNode.size() );
            } );
        } );
        io.list( _listCell );
        io.value( _floorCount );
        io.value( _columnCount );
        io.value( _current );
        const int32 nodeCount = static_cast<int32>( _listNode.size() );
        io.check( 0 <= _floorCount && 0 <= _columnCount && static_cast<int64>( _floorCount ) * _columnCount == static_cast<int64>( _listCell.size() ) && kStart <= _current && _current < nodeCount );
        if ( io.isWriting() )
            return;
        // 칸의 번호는 고를 수 있는 노드가 되어 `_current` 로 들어간다. 노드 밖이면 다음 조회가 범위 밖을 읽는다.
        for ( const int32 cell : _listCell )
        {
            io.check( constant::invalid_index::kInt32 <= cell && cell < nodeCount );
        }
        for ( const RunNode& node : _listNode )
        {
            io.check( 0 <= node._floor && node._floor < _floorCount && 0 <= node._column && node._column < _columnCount );
        }
    }

} // namespace sw
