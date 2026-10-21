#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/RunMap.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    bool RunMap::loadSettings( string_view xmlText, string_view sourceName, RunMapSettings& outSettings )
    {
        XMLDocument doc;
        XMLNode     root;
        if ( GameDataXML::parseRoot( doc, xmlText, sourceName, "RunMap", root ) == false )
            return false;
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 틀린 원소가 하나라도 있으면 설정 전체를 읽지 못한 것으로 본다(반쪽 지도를 만들지 않는다).
        RunMapSettings settings;
        bool           bValid = GameDataXML::readFields( root, settings, sourceName );
        settings._listForcedFloor.assign( static_cast<size_t>( settings._floorCount ), hashed_string{} );
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( name == "Node" )
            {
                RunNodeRule rule;
                if ( GameDataXML::readFields( node, rule, sourceName ) )
                    settings._listRule.push_back( rule );
                else
                    bValid = false;
                continue;
            }
            if ( name != "Floor" )
            {
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
                bValid = false;
                continue;
            }
            RunForcedFloorDef forced;
            if ( GameDataXML::readFields( node, forced, sourceName ) == false )
            {
                bValid = false;
                continue;
            }
            const int32 index        = forced._index < 0 ? forced._index + settings._floorCount : forced._index; // −1 은 마지막 층이다
            const bool  bInsideFloor = index >= 0 && index < settings._floorCount;
            if ( bInsideFloor == false )
            {
                SW_LOG_ERROR( "%#: <Floor index=\"%#\"> is outside the %# floors", sourceName, forced._index, settings._floorCount );
                bValid = false;
                continue;
            }
            settings._listForcedFloor[static_cast<size_t>( index )] = forced._kind;
        }
        if ( settings._listRule.empty() )
        {
            SW_LOG_ERROR( "%#: <RunMap> needs at least one <Node kind=\"...\"/>", sourceName );
            bValid = false;
        }
        if ( bValid )
            outSettings = settings;
        return bValid;
    }

    int32 RunMap::findOrAddNode( int32 floor, int32 column )
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

    bool RunMap::crossesExisting( int32 floor, int32 fromColumn, int32 toColumn ) const
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

    void RunMap::generate( const RunMapSettings& settings, uint32 seed )
    {
        GameRandom random( seed );
        _floorCount  = MathUtil::max( 2, settings._floorCount );
        _columnCount = MathUtil::max( 1, settings._columnCount );
        _current     = kStart;
        _listNode.clear();
        _listCell.assign( static_cast<size_t>( _floorCount * _columnCount ), -1 );

        // 길을 아래에서 위로 긋는다. 마지막 층 바로 아래까지 긋는다. 첫 두 길은 서로 다른 열에서 시작한다.
        const int32 lastFloor  = _floorCount - 1;
        int32       firstStart = -1;
        for ( int32 pathIndex = 0; pathIndex < MathUtil::max( 1, settings._pathCount ); ++pathIndex )
        {
            int32      column               = random.nextInt( 0, _columnCount - 1 );
            const bool bAvoidDuplicateStart = pathIndex == 1 && column == firstStart && _columnCount > 1;
            if ( bAvoidDuplicateStart )
                column = ( column + 1 ) % _columnCount;
            if ( pathIndex == 0 )
                firstStart = column;
            int32 nodeIndex = findOrAddNode( 0, column );
            for ( int32 floor = 0; floor + 1 < lastFloor; ++floor )
            {
                int32 nextColumn = MathUtil::clamp( column + random.nextInt( -1, 1 ), 0, _columnCount - 1 );
                if ( crossesExisting( floor, column, nextColumn ) )
                    nextColumn = column; // 엇갈리면 곧게 편다
                const int32    nextIndex = findOrAddNode( floor + 1, nextColumn );
                vector<int32>& listNext  = _listNode[static_cast<size_t>( nodeIndex )]._listNext;
                if ( VectorUtil::contains( listNext, nextIndex ) == false )
                    listNext.push_back( nextIndex );
                nodeIndex = nextIndex;
                column    = nextColumn;
            }
        }
        // 마지막 층은 가운데 노드 하나로 모은다.
        const int32 bossIndex = findOrAddNode( lastFloor, _columnCount / 2 );
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
        std::stable_sort( listOrder.begin(), listOrder.end(), [this]( int32 lhs, int32 rhs )
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
            std::sort( node._listNext.begin(), node._listNext.end(), [this]( int32 lhs, int32 rhs )
            {
                return _listNode[static_cast<size_t>( lhs )]._column < _listNode[static_cast<size_t>( rhs )]._column;
            } );
        }
    }

    void RunMap::collectChoices( vector<int32>& outListNode ) const
    {
        outListNode.clear();
        if ( _current == kStart )
        {
            for ( int32 column = 0; column < _columnCount; ++column )
            {
                const int32 index = _listCell.empty() ? -1 : _listCell[static_cast<size_t>( column )];
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

    void RunMap::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listNode.size() );
        for ( const RunNode& node : _listNode )
        {
            StateArchiveUtil::writeName( outArchive, node._kind );
            outArchive << node._floor;
            outArchive << node._column;
            outArchive << static_cast<uint32>( node._listNext.size() );
            for ( const int32 next : node._listNext )
            {
                outArchive << next;
            }
        }
        outArchive << static_cast<uint32>( _listCell.size() );
        for ( const int32 cell : _listCell )
        {
            outArchive << cell;
        }
        outArchive << _floorCount;
        outArchive << _columnCount;
        outArchive << _current;
    }

    bool RunMap::readState( Archive& archive )
    {
        uint32 nodeCount = 0;
        // 노드마다 이름(4) + 층과 열(8) + 이어진 수(4)
        if ( StateArchiveUtil::readCount( archive, 16, nodeCount ) == false )
            return false;
        vector<RunNode> listNode( nodeCount );
        for ( RunNode& node : listNode )
        {
            uint32 nextCount = 0;
            if ( StateArchiveUtil::readName( archive, node._kind ) == false )
                return false;
            archive >> node._floor;
            archive >> node._column;
            if ( StateArchiveUtil::readCount( archive, 4, nextCount ) == false )
                return false;
            node._listNext.resize( nextCount, 0 );
            for ( int32& next : node._listNext )
            {
                archive >> next;
                if ( next < 0 || static_cast<uint32>( next ) >= nodeCount )
                    archive.setError();
            }
        }
        uint32 cellCount = 0;
        if ( archive.isError() || StateArchiveUtil::readCount( archive, 4, cellCount ) == false )
            return false;
        vector<int32> listCell( cellCount, -1 );
        for ( int32& cell : listCell )
        {
            archive >> cell;
        }
        int32 floorCount  = 0;
        int32 columnCount = 0;
        int32 current     = kStart;
        archive >> floorCount;
        archive >> columnCount;
        archive >> current;
        const bool bValid = archive.isOk() && 0 <= floorCount && 0 <= columnCount && static_cast<int64>( floorCount ) * columnCount == static_cast<int64>( cellCount ) &&
                            kStart <= current && current < static_cast<int32>( nodeCount );
        if ( bValid == false )
            return false;
        _listNode    = std::move( listNode );
        _listCell    = std::move( listCell );
        _floorCount  = floorCount;
        _columnCount = columnCount;
        _current     = current;
        return true;
    }
} // namespace sw
