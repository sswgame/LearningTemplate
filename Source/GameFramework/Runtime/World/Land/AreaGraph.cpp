#include "pch.h"

#include "GameFramework/Runtime/World/Land/AreaGraph.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

#include <algorithm>

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

    uint32 AreaGraph::loadRoot( const XMLNode& root, string_view sourceName )
    {
        clear();
        // 연결은 지역을 가리키므로 지역을 먼저 다 읽는다.
        using Element                          = XMLCatalogElement<AreaGraph>;
        static constexpr Element kArrElement[] = {
            Element::of<&AreaGraph::loadArea>( "Area" ),
            Element::of<&AreaGraph::loadLink>( "Link" ).uncounted(),
        };
        const uint32 loadedCount = XMLCatalogReadResult::readInTableOrder( *this, root, sourceName, kArrElement )._loadedCount;
        resetState();
        return loadedCount;
    }

    bool AreaGraph::loadArea( const XMLNode& node, string_view sourceName )
    {
        // 적지 않은 칸은 AreaDef 의 초기값이다. 깨진 값 · 음수 크기는 그 지역을 넣지 않는 오류다(자르지 않는다).
        AreaDef    area;
        const bool bValid = XMLFields::readFields( node, area, sourceName );
        if ( area._id.empty() )
            return false;
        const utf8* const pID = area._id.c_str();
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "AreaGraph.AreaInvalidFieldsSkipped", "%#: area '%#' has invalid fields - skipped", sourceName, pID );
            return false;
        }
        if ( addArea( area ) == false )
        {
            SW_LOG_WARNING_ID( "AreaGraph.DuplicateAreaSkipped", "%#: duplicate area '%#' - skipped", sourceName, pID );
            return false;
        }
        return true;
    }

    bool AreaGraph::loadLink( const XMLNode& node, string_view sourceName )
    {
        AreaLinkParams link;
        if ( XMLFields::readFields( node, link, sourceName ) == false )
            return false; // 끝점이 없거나 깨진 연결은 오류로 알리고 넣지 않는다(위 읽기가 알렸다)
        const hashed_string& from     = link._from;
        const hashed_string& to       = link._to;
        bool                 bInvalid = false;
        if ( addLink( from, to, link._kind, link._requires, link._bOneWay, bInvalid ) == false )
        {
            SW_LOG_WARNING_ID( "AreaGraph.LinkNeedsTwoDifferent", "%#: link '%#' -> '%#' needs two different known areas - skipped", sourceName, from.c_str(), to.c_str() );
            return false;
        }
        if ( bInvalid )
            SW_LOG_WARNING_ID( "AreaGraph.LinkInvalidConditionStays", "%#: link '%#' -> '%#' has an invalid condition and stays locked", sourceName, from.c_str(), to.c_str() );
        return true;
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
        if ( area._id.empty() || _catalog.findIndex( area._id ).has_value() )
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
        outbInvalidCondition               = false;
        const optional<int32> fromIndex    = _catalog.findIndex( from );
        const optional<int32> toIndex      = _catalog.findIndex( to );
        const bool            bInvalidLink = fromIndex.has_value() == false || toIndex.has_value() == false || *fromIndex == *toIndex;
        if ( bInvalidLink )
            return false;
        AreaLink link;
        link._from      = from;
        link._to        = to;
        link._kind      = kind;
        link._fromIndex = *fromIndex;
        link._toIndex   = *toIndex;
        link._bOneWay   = bOneWay ? SW_TRUE : SW_FALSE;
        // 틀린 조건은 부르는 쪽이 어느 연결인지 함께 경고한다
        if ( ConditionExpr::tryCompile( condition, link._requires ).has_value() == false )
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

    optional<int32> AreaGraph::findOtherSide( const AreaLink& link, int32 fromIndex ) const
    {
        if ( link._fromIndex == fromIndex )
            return link._toIndex;
        if ( link._toIndex == fromIndex && link._bOneWay == SW_FALSE )
            return link._fromIndex;
        return nullopt;
    }

    bool AreaGraph::isUnlocked( const AreaLink& link, const GameFlags& flags ) const
    {
        if ( link._bInvalidCondition == SW_TRUE )
            return false;
        return flags.evaluate( link._requires );
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

    bool AreaGraph::enterArea( const hashed_string& areaID )
    {
        const optional<int32> foundIndex = _catalog.findIndex( areaID );
        if ( foundIndex.has_value() == false )
            return false;
        const int32 areaIndex   = *foundIndex;
        const bool  bFirstVisit = _listVisited[static_cast<size_t>( areaIndex )] == SW_FALSE;
        markVisited( areaIndex );
        for ( const int32 linkIndex : _listAdjacency[static_cast<size_t>( areaIndex )] )
        {
            const optional<int32> otherIndex = findOtherSide( _listLink[static_cast<size_t>( linkIndex )], areaIndex );
            if ( otherIndex.has_value() )
                _listDiscovered[static_cast<size_t>( *otherIndex )] = SW_TRUE;
        }
        return bFirstVisit;
    }

    bool AreaGraph::discoverArea( const hashed_string& areaID )
    {
        const optional<int32> areaIndex = _catalog.findIndex( areaID );
        if ( areaIndex.has_value() == false || _listDiscovered[static_cast<size_t>( *areaIndex )] == SW_TRUE )
            return false;
        _listDiscovered[static_cast<size_t>( *areaIndex )] = SW_TRUE;
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

    bool AreaGraph::canTraverse( const hashed_string& fromID, const hashed_string& toID, const GameFlags& flags ) const
    {
        const optional<int32> foundFrom = _catalog.findIndex( fromID );
        const optional<int32> foundTo   = _catalog.findIndex( toID );
        if ( foundFrom.has_value() == false || foundTo.has_value() == false )
            return false;
        const int32 fromIndex = *foundFrom;
        const int32 toIndex   = *foundTo;
        for ( const int32 linkIndex : _listAdjacency[static_cast<size_t>( fromIndex )] )
        {
            const AreaLink& link = _listLink[static_cast<size_t>( linkIndex )];
            if ( findOtherSide( link, fromIndex ) == toIndex && isUnlocked( link, flags ) )
                return true;
        }
        return false;
    }

    bool AreaGraph::findPath( const hashed_string& fromID, const hashed_string& toID, const GameFlags& flags, vector<hashed_string>& outListArea ) const
    {
        outListArea.clear();
        const optional<int32> foundFrom = _catalog.findIndex( fromID );
        const optional<int32> foundTo   = _catalog.findIndex( toID );
        if ( foundFrom.has_value() == false || foundTo.has_value() == false )
            return false;
        const int32 fromIndex = *foundFrom;
        const int32 toIndex   = *foundTo;

        // 연결마다 잠금을 한 번만 평가한다(조건식은 문자열 해석이라 방문마다 다시 읽지 않게).
        vector<uint8> listUnlocked;
        listUnlocked.resize( _listLink.size() );
        for ( size_t linkIndex = 0; linkIndex < _listLink.size(); ++linkIndex )
        {
            listUnlocked[linkIndex] = isUnlocked( _listLink[linkIndex], flags ) ? SW_TRUE : SW_FALSE;
        }

        vector<int32> listParent;
        listParent.resize( _catalog.getCount(), -2 ); // −2 = 아직, −1 = 출발
        vector<int32> listQueue;
        listQueue.reserve( _catalog.getCount() );
        listParent[static_cast<size_t>( fromIndex )] = constant::invalid_index::kInt32;
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
                const optional<int32> otherIndex = findOtherSide( _listLink[static_cast<size_t>( linkIndex )], areaIndex );
                if ( otherIndex.has_value() == false || listParent[static_cast<size_t>( *otherIndex )] != -2 )
                    continue;
                listParent[static_cast<size_t>( *otherIndex )] = areaIndex;
                listQueue.push_back( *otherIndex );
            }
        }
        if ( listParent[static_cast<size_t>( toIndex )] == -2 )
            return false;
        for ( int32 areaIndex = toIndex; areaIndex >= 0; areaIndex = listParent[static_cast<size_t>( areaIndex )] )
        {
            outListArea.push_back( _catalog.getAt( static_cast<size_t>( areaIndex ) )._id );
        }
        VectorUtil::reverse( outListArea );
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
        VectorUtil::sort( outListArea, HashedStringLexicalLess{} );
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
        for ( const hashed_string& areaID : listDiscovered )
        {
            (void)discoverArea( areaID );
        }
        for ( const hashed_string& areaID : listVisited )
        {
            if ( const optional<int32> areaIndex = _catalog.findIndex( areaID ); areaIndex.has_value() )
                markVisited( *areaIndex );
        }
    }

    bool AreaGraph::isVisited( const hashed_string& areaID ) const
    {
        const optional<int32> areaIndex = _catalog.findIndex( areaID );
        return areaIndex.has_value() && _listVisited[static_cast<size_t>( *areaIndex )] == SW_TRUE;
    }

    bool AreaGraph::isDiscovered( const hashed_string& areaID ) const
    {
        const optional<int32> areaIndex = _catalog.findIndex( areaID );
        return areaIndex.has_value() && _listDiscovered[static_cast<size_t>( *areaIndex )] == SW_TRUE;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<AreaGraph>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( AreaGraph, AreaGraph, SW_GF_API, AreaGraph::kAssetType.withLoader( AssetTypeLoader::of<AreaGraph>( MemoryTag::Game ) ) );
} // namespace sw
