#include "pch.h"

#include "GameFramework/World/AreaGraph.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Data/GameDataXml.h"
#include "GameFramework/Data/GameFlags.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "AreaGraph" );

    namespace
    {
        struct AreaGraphInternal
        {
            static hashed_string readName( const XmlNode& node, const utf8* pName )
            {
                const utf8* pValue = node.findAttribute( pName );
                return pValue != nullptr ? hashed_string( pValue ) : hashed_string{};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AreaGraph::AreaGraph()
        : _catalog{}
        , _listLink{}
        , _listAdjacency{}
        , _listVisited{}
        , _listDiscovered{}
        , _visitedCount{ 0 }
    {
    }

    bool AreaGraph::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "AreaGraph", root, sourceName ) && loadRoot( root, sourceName ) > 0;
    }

    bool AreaGraph::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "AreaGraph", root ) && loadRoot( root, sourceName ) > 0;
    }

    uint32 AreaGraph::loadRoot( const XmlNode& root, string_view sourceName )
    {
        clear();
        const uint32 loadedCount = loadFromNode( root, sourceName );
        resetState();
        return loadedCount;
    }

    uint32 AreaGraph::loadFromNode( const XmlNode& node, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XmlNode areaNode = node.findChild( "Area" ); areaNode; areaNode = areaNode.findNextSibling( "Area" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( areaNode, sourceName );
            if ( pId == nullptr )
                continue;
            AreaDef area;
            area._id     = hashed_string( pId );
            area._name   = AreaGraphInternal::readName( areaNode, "name" );
            area._region = AreaGraphInternal::readName( areaNode, "region" );
            area._x      = areaNode.getAttributeFloat( "x", area._x );
            area._y      = areaNode.getAttributeFloat( "y", area._y );
            area._width  = MathUtil::max( 0.0f, areaNode.getAttributeFloat( "w", area._width ) );
            area._height = MathUtil::max( 0.0f, areaNode.getAttributeFloat( "h", area._height ) );
            if ( addArea( area ) )
                ++loadedCount;
            else
                SW_LOG_WARNING( "%#: duplicate area '%#' - skipped", sourceName, pId );
        }
        for ( XmlNode linkNode = node.findChild( "Link" ); linkNode; linkNode = linkNode.findNextSibling( "Link" ) )
        {
            const hashed_string from     = AreaGraphInternal::readName( linkNode, "from" );
            const hashed_string to       = AreaGraphInternal::readName( linkNode, "to" );
            bool                bInvalid = false;
            if ( addLink( from, to, AreaGraphInternal::readName( linkNode, "kind" ), linkNode.getAttributeText( "requires" ),
                          linkNode.getAttributeBool( "oneWay", false ), bInvalid ) == false )
            {
                SW_LOG_WARNING( "%#: link '%#' -> '%#' needs two different known areas - skipped", sourceName, from.c_str(), to.c_str() );
                continue;
            }
            if ( bInvalid )
                SW_LOG_WARNING( "%#: link '%#' -> '%#' has an invalid condition and stays locked", sourceName, from.c_str(), to.c_str() );
        }
        return loadedCount;
    }

    void AreaGraph::clear()
    {
        _catalog.clear();
        _listLink.clear();
        _listAdjacency.clear();
        resetState();
    }

    bool AreaGraph::addArea( const AreaDef& area )
    {
        if ( area._id.empty() || _catalog.findIndex( area._id ) >= 0 )
            return false;
        AreaDef added = area;
        if ( added._name.empty() )
            added._name = added._id;
        (void)_catalog.add( added );
        _listAdjacency.resize( _catalog.getCount() );
        _listVisited.resize( _catalog.getCount(), SW_FALSE );
        _listDiscovered.resize( _catalog.getCount(), SW_FALSE );
        return true;
    }

    bool AreaGraph::addLink( const hashed_string& from, const hashed_string& to, const hashed_string& kind, string_view condition, bool bOneWay,
                             bool& outbInvalidCondition )
    {
        outbInvalidCondition = false;
        AreaLink link;
        link._from      = from;
        link._to        = to;
        link._kind      = kind;
        link._fromIndex = _catalog.findIndex( from );
        link._toIndex   = _catalog.findIndex( to );
        link._bOneWay   = bOneWay ? SW_TRUE : SW_FALSE;
        if ( link._fromIndex < 0 || link._toIndex < 0 || link._fromIndex == link._toIndex )
            return false;
        link._requires = string( condition.data(), condition.size() );
        bool bIgnored  = false;
        if ( GameFlags::parseCondition( link._requires, GameFlags{}, bIgnored ) == false )
        {
            link._bInvalidCondition = SW_TRUE;
            outbInvalidCondition    = true;
        }
        const int32 linkIndex = static_cast<int32>( _listLink.size() );
        _listAdjacency[static_cast<size_t>( link._fromIndex )].push_back( linkIndex );
        _listAdjacency[static_cast<size_t>( link._toIndex )].push_back( linkIndex );
        _listLink.push_back( link );
        return true;
    }

    void AreaGraph::resetState()
    {
        _listVisited.assign( _catalog.getCount(), SW_FALSE );
        _listDiscovered.assign( _catalog.getCount(), SW_FALSE );
        _visitedCount = 0;
    }

    int32 AreaGraph::findOtherSide( const AreaLink& link, int32 fromIndex ) const
    {
        if ( link._fromIndex == fromIndex )
            return link._toIndex;
        if ( link._toIndex == fromIndex && link._bOneWay == SW_FALSE )
            return link._fromIndex;
        return -1;
    }

    bool AreaGraph::isUnlocked( const AreaLink& link, const GameFlags& flags ) const
    {
        if ( link._bInvalidCondition == SW_TRUE )
            return false;
        return link._requires.empty() || flags.evaluate( link._requires );
    }

    void AreaGraph::markVisited( int32 areaIndex )
    {
        const size_t index = static_cast<size_t>( areaIndex );
        if ( _listVisited[index] == SW_FALSE )
        {
            _listVisited[index] = SW_TRUE;
            ++_visitedCount;
        }
        _listDiscovered[index] = SW_TRUE;
    }

    bool AreaGraph::enterArea( const hashed_string& areaId )
    {
        const int32 areaIndex = _catalog.findIndex( areaId );
        if ( areaIndex < 0 )
            return false;
        const bool bFirstVisit = _listVisited[static_cast<size_t>( areaIndex )] == SW_FALSE;
        markVisited( areaIndex );
        for ( const int32 linkIndex : _listAdjacency[static_cast<size_t>( areaIndex )] )
        {
            const int32 otherIndex = findOtherSide( _listLink[static_cast<size_t>( linkIndex )], areaIndex );
            if ( otherIndex >= 0 )
                _listDiscovered[static_cast<size_t>( otherIndex )] = SW_TRUE;
        }
        return bFirstVisit;
    }

    bool AreaGraph::discoverArea( const hashed_string& areaId )
    {
        const int32 areaIndex = _catalog.findIndex( areaId );
        if ( areaIndex < 0 || _listDiscovered[static_cast<size_t>( areaIndex )] == SW_TRUE )
            return false;
        _listDiscovered[static_cast<size_t>( areaIndex )] = SW_TRUE;
        return true;
    }

    int32 AreaGraph::discoverRegion( const hashed_string& region )
    {
        int32 discoveredCount = 0;
        for ( size_t index = 0; index < _catalog.getCount(); ++index )
        {
            if ( _catalog.getAt( index )._region == region && _listDiscovered[index] == SW_FALSE )
            {
                _listDiscovered[index] = SW_TRUE;
                ++discoveredCount;
            }
        }
        return discoveredCount;
    }

    bool AreaGraph::canTraverse( const hashed_string& fromId, const hashed_string& toId, const GameFlags& flags ) const
    {
        const int32 fromIndex = _catalog.findIndex( fromId );
        const int32 toIndex   = _catalog.findIndex( toId );
        if ( fromIndex < 0 || toIndex < 0 )
            return false;
        for ( const int32 linkIndex : _listAdjacency[static_cast<size_t>( fromIndex )] )
        {
            const AreaLink& link = _listLink[static_cast<size_t>( linkIndex )];
            if ( findOtherSide( link, fromIndex ) == toIndex && isUnlocked( link, flags ) )
                return true;
        }
        return false;
    }

    bool AreaGraph::findPath( const hashed_string& fromId, const hashed_string& toId, const GameFlags& flags, vector<hashed_string>& outListArea ) const
    {
        outListArea.clear();
        const int32 fromIndex = _catalog.findIndex( fromId );
        const int32 toIndex   = _catalog.findIndex( toId );
        if ( fromIndex < 0 || toIndex < 0 )
            return false;

        // 연결마다 잠금을 한 번만 평가한다(조건식은 문자열 해석이라 방문마다 다시 읽지 않게).
        vector<uint8> listUnlocked;
        listUnlocked.resize( _listLink.size() );
        for ( size_t linkIndex = 0; linkIndex < _listLink.size(); ++linkIndex )
            listUnlocked[linkIndex] = isUnlocked( _listLink[linkIndex], flags ) ? SW_TRUE : SW_FALSE;

        vector<int32> listParent;
        listParent.resize( _catalog.getCount(), -2 ); // −2 = 아직, −1 = 출발
        vector<int32> listQueue;
        listQueue.reserve( _catalog.getCount() );
        listParent[static_cast<size_t>( fromIndex )] = -1;
        listQueue.push_back( fromIndex );
        for ( size_t head = 0; head < listQueue.size(); ++head )
        {
            const int32 areaIndex = listQueue[head];
            if ( areaIndex == toIndex )
                break;
            for ( const int32 linkIndex : _listAdjacency[static_cast<size_t>( areaIndex )] )
            {
                if ( listUnlocked[static_cast<size_t>( linkIndex )] == SW_FALSE )
                    continue;
                const int32 otherIndex = findOtherSide( _listLink[static_cast<size_t>( linkIndex )], areaIndex );
                if ( otherIndex < 0 || listParent[static_cast<size_t>( otherIndex )] != -2 )
                    continue;
                listParent[static_cast<size_t>( otherIndex )] = areaIndex;
                listQueue.push_back( otherIndex );
            }
        }
        if ( listParent[static_cast<size_t>( toIndex )] == -2 )
            return false;
        for ( int32 areaIndex = toIndex; areaIndex >= 0; areaIndex = listParent[static_cast<size_t>( areaIndex )] )
            outListArea.push_back( _catalog.getAt( static_cast<size_t>( areaIndex ) )._id );
        std::reverse( outListArea.begin(), outListArea.end() );
        return true;
    }

    float32 AreaGraph::computeExplorationRatio() const
    {
        return _catalog.isEmpty() ? 0.0f : static_cast<float32>( _visitedCount ) / static_cast<float32>( _catalog.getCount() );
    }

    float32 AreaGraph::computeRegionRatio( const hashed_string& region ) const
    {
        AreaRegionProgress progress;
        for ( size_t index = 0; index < _catalog.getCount(); ++index )
        {
            if ( _catalog.getAt( index )._region != region )
                continue;
            ++progress._totalCount;
            if ( _listVisited[index] == SW_TRUE )
                ++progress._visitedCount;
        }
        return progress.computeRatio();
    }

    void AreaGraph::collectRegionProgress( vector<AreaRegionProgress>& outListProgress ) const
    {
        outListProgress.clear();
        for ( size_t index = 0; index < _catalog.getCount(); ++index )
        {
            const hashed_string& region    = _catalog.getAt( index )._region;
            AreaRegionProgress*  pProgress = nullptr;
            for ( AreaRegionProgress& progress : outListProgress )
            {
                if ( progress._region == region )
                {
                    pProgress = &progress;
                    break;
                }
            }
            if ( pProgress == nullptr )
            {
                outListProgress.push_back( AreaRegionProgress{ region, 0, 0 } );
                pProgress = &outListProgress.back();
            }
            ++pProgress->_totalCount;
            if ( _listVisited[index] == SW_TRUE )
                ++pProgress->_visitedCount;
        }
    }

    void AreaGraph::collectLockedFrontier( const GameFlags& flags, vector<const AreaLink*>& outListLink ) const
    {
        outListLink.clear();
        for ( const AreaLink& link : _listLink )
        {
            const bool bFromVisited = _listVisited[static_cast<size_t>( link._fromIndex )] == SW_TRUE;
            const bool bToVisited   = _listVisited[static_cast<size_t>( link._toIndex )] == SW_TRUE;
            // 방문한 쪽에서 나가 아직 가 보지 않은 쪽으로 — 일방통행은 정방향만.
            const bool bForward  = bFromVisited && bToVisited == false;
            const bool bBackward = bToVisited && bFromVisited == false && link._bOneWay == SW_FALSE;
            if ( ( bForward || bBackward ) && isUnlocked( link, flags ) == false )
                outListLink.push_back( &link );
        }
    }

    void AreaGraph::fillMarkedAreas( const vector<uint8>& listMark, vector<hashed_string>& outListArea ) const
    {
        outListArea.clear();
        for ( size_t index = 0; index < listMark.size(); ++index )
        {
            if ( listMark[index] == SW_TRUE )
                outListArea.push_back( _catalog.getAt( index )._id );
        }
        std::sort( outListArea.begin(), outListArea.end(), HashedStringLexicalLess{} );
    }

    void AreaGraph::fillVisitedAreas( vector<hashed_string>& outListArea ) const
    {
        fillMarkedAreas( _listVisited, outListArea );
    }

    void AreaGraph::fillDiscoveredAreas( vector<hashed_string>& outListArea ) const
    {
        fillMarkedAreas( _listDiscovered, outListArea );
    }

    void AreaGraph::restoreState( const vector<hashed_string>& listVisited, const vector<hashed_string>& listDiscovered )
    {
        resetState();
        for ( const hashed_string& areaId : listDiscovered )
            (void)discoverArea( areaId );
        for ( const hashed_string& areaId : listVisited )
        {
            const int32 areaIndex = _catalog.findIndex( areaId );
            if ( areaIndex >= 0 )
                markVisited( areaIndex );
        }
    }

    bool AreaGraph::isVisited( const hashed_string& areaId ) const
    {
        const int32 areaIndex = _catalog.findIndex( areaId );
        return areaIndex >= 0 && _listVisited[static_cast<size_t>( areaIndex )] == SW_TRUE;
    }

    bool AreaGraph::isDiscovered( const hashed_string& areaId ) const
    {
        const int32 areaIndex = _catalog.findIndex( areaId );
        return areaIndex >= 0 && _listDiscovered[static_cast<size_t>( areaIndex )] == SW_TRUE;
    }
} // namespace sw
