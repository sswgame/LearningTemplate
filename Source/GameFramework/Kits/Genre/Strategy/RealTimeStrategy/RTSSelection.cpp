#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSSelection.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RTSSelectionInternal
        {
            /** @brief 유닛이 사각형과 겹치는지 반환합니다. 유닛은 몸 기준이고 건물은 중심 기준입니다. */
            static bool isInRect( const RTSUnit& unit, const float3& minCorner, const float3& maxCorner )
            {
                const float32 radius = unit.isMobile() ? unit._pDef->_radius : 0.0f;
                return unit._position._x + radius >= minCorner._x && unit._position._x - radius <= maxCorner._x && unit._position._z + radius >= minCorner._z &&
                       unit._position._z - radius <= maxCorner._z;
            }

            static void serializeList( StateIO& io, vector<RTSUnitID>& listUnit )
            {
                vector<uint64> listPacked;
                listPacked.reserve( listUnit.size() );
                for ( const RTSUnitID unitID : listUnit )
                {
                    listPacked.push_back( unitID.packed() );
                }
                io.list( listPacked );
                if ( io.isWriting() || io.isOk() == false )
                    return;
                // 선택과 부대는 유닛마다 한 번이다(`addUnit`). 꾸민 바이트의 긴 목록이 제곱 시간을 쓰지 않게 정렬해 본다.
                vector<uint64> listSorted = listPacked;
                VectorUtil::sort( listSorted );
                if ( io.check( std::adjacent_find( listSorted.begin(), listSorted.end() ) == listSorted.end() ) == false )
                    return;
                listUnit.clear();
                listUnit.reserve( listPacked.size() );
                for ( const uint64 packed : listPacked )
                {
                    listUnit.push_back( RTSUnitID::fromPacked( packed ) );
                }
            }

            static void sortCorners( const float3& cornerA, const float3& cornerB, float3& outMin, float3& outMax )
            {
                outMin = float3{ MathUtil::min( cornerA._x, cornerB._x ), 0.0f, MathUtil::min( cornerA._z, cornerB._z ) };
                outMax = float3{ MathUtil::max( cornerA._x, cornerB._x ), 0.0f, MathUtil::max( cornerA._z, cornerB._z ) };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void RTSSelection::serializeState( StateIO& io )
    {
        RTSSelectionInternal::serializeList( io, _listSelected );
        io.check( _maxCount <= 0 || static_cast<int32>( _listSelected.size() ) <= _maxCount );
        for ( vector<RTSUnitID>& listGroup : _arrGroup )
        {
            RTSSelectionInternal::serializeList( io, listGroup );
        }
    }

    RTSSelection::RTSSelection()
        : _listSelected{}
        , _arrGroup{}
        , _player{ 0 }
        , _maxCount{ 0 }
    {
    }

    void RTSSelection::addUnit( RTSUnitID unitID )
    {
        if ( isSelected( unitID ) || ( _maxCount > 0 && static_cast<int32>( _listSelected.size() ) >= _maxCount ) )
            return;
        _listSelected.push_back( unitID );
    }

    void RTSSelection::selectInRect( const RTSWorld& world, const float3& cornerA, const float3& cornerB, bool bAdd )
    {
        float3 minCorner{};
        float3 maxCorner{};
        RTSSelectionInternal::sortCorners( cornerA, cornerB, minCorner, maxCorner );
        vector<RTSUnitID> listMobile;
        RTSUnitID         ownBuilding{};
        RTSUnitID         otherUnit{};
        world.forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( RTSSelectionInternal::isInRect( unit, minCorner, maxCorner ) == false )
                return;
            const bool bFirstVisibleOtherUnit = unit._owner != _player && otherUnit.isValid() == false && world.isVisibleTo( _player, unit._id );
            if ( unit._owner == _player && unit.isMobile() )
                listMobile.push_back( unit._id );
            else if ( unit._owner == _player && ownBuilding.isValid() == false )
                ownBuilding = unit._id;
            else if ( bFirstVisibleOtherUnit )
                otherUnit = unit._id;
        } );
        // 추가 선택은 내 움직이는 유닛끼리만 한다. 건물과 남의 유닛은 혼자만 고른다.
        const bool bExtendSelection = bAdd && listMobile.empty() == false && isCommandable( world );
        if ( bExtendSelection )
        {
            const RTSUnit* pPrimary = world.findUnit( getPrimary() );
            if ( pPrimary == nullptr || pPrimary->isMobile() )
            {
                for ( const RTSUnitID unitID : listMobile )
                {
                    addUnit( unitID );
                }
                return;
            }
        }
        const bool bEmptyDrag = listMobile.empty() && ownBuilding.isValid() == false && otherUnit.isValid() == false;
        if ( bEmptyDrag )
            return; // 빈 땅을 끌면 선택을 그대로 둔다
        _listSelected.clear();
        if ( listMobile.empty() == false )
        {
            for ( const RTSUnitID unitID : listMobile )
            {
                addUnit( unitID );
            }
        }
        else
        {
            _listSelected.push_back( ownBuilding.isValid() ? ownBuilding : otherUnit );
        }
    }

    void RTSSelection::selectUnit( const RTSWorld& world, RTSUnitID unitID, bool bToggle )
    {
        const RTSUnit* pUnit = world.findUnit( unitID );
        if ( pUnit == nullptr )
            return;
        const bool bToggleSelection = bToggle && pUnit->_owner == _player && pUnit->isMobile() && isCommandable( world );
        if ( bToggleSelection )
        {
            if ( VectorUtil::removeFirst( _listSelected, unitID ) == 0 )
                addUnit( unitID );
            return;
        }
        _listSelected.clear();
        _listSelected.push_back( unitID );
    }

    void RTSSelection::selectSameType( const RTSWorld& world, RTSUnitID unitID, const float3& cornerA, const float3& cornerB )
    {
        const RTSUnit* pUnit = world.findUnit( unitID );
        if ( pUnit == nullptr )
            return;
        if ( pUnit->_owner != _player )
        {
            selectUnit( world, unitID, false );
            return;
        }
        float3 minCorner{};
        float3 maxCorner{};
        RTSSelectionInternal::sortCorners( cornerA, cornerB, minCorner, maxCorner );
        const hashed_string defID = pUnit->_pDef->_id;
        _listSelected.clear();
        addUnit( unitID );
        world.forEachUnit( [&]( const RTSUnit& unit )
        {
            const bool bSameDefInRect = unit._owner == _player && unit._pDef->_id == defID && RTSSelectionInternal::isInRect( unit, minCorner, maxCorner );
            if ( bSameDefInRect )
                addUnit( unit._id );
        } );
    }

    void RTSSelection::assignGroup( int32 group )
    {
        if ( group < 0 || group >= kGroupCount )
            return;
        _arrGroup[group] = _listSelected;
    }

    void RTSSelection::addToGroup( int32 group )
    {
        if ( group < 0 || group >= kGroupCount )
            return;
        vector<RTSUnitID>& listGroup = _arrGroup[group];
        for ( const RTSUnitID unitID : _listSelected )
        {
            if ( VectorUtil::contains( listGroup, unitID ) == false )
                listGroup.push_back( unitID );
        }
    }

    bool RTSSelection::recallGroup( const RTSWorld& world, int32 group )
    {
        if ( group < 0 || group >= kGroupCount )
            return false;
        prune( world );
        if ( _arrGroup[group].empty() )
            return false;
        _listSelected = _arrGroup[group];
        return true;
    }

    void RTSSelection::prune( const RTSWorld& world )
    {
        const auto isDead = [&world]( RTSUnitID unitID )
        { return world.findUnit( unitID ) == nullptr; };
        VectorUtil::removeIf( _listSelected, isDead );
        for ( vector<RTSUnitID>& listGroup : _arrGroup )
        {
            VectorUtil::removeIf( listGroup, isDead );
        }
    }

    const vector<RTSUnitID>& RTSSelection::getGroup( int32 group ) const
    {
        static const vector<RTSUnitID> kEmptyGroup{};
        return group >= 0 && group < kGroupCount ? _arrGroup[group] : kEmptyGroup;
    }

    bool RTSSelection::isSelected( RTSUnitID unitID ) const
    {
        return VectorUtil::contains( _listSelected, unitID );
    }

    bool RTSSelection::isCommandable( const RTSWorld& world ) const
    {
        for ( const RTSUnitID unitID : _listSelected )
        {
            const RTSUnit* pUnit = world.findUnit( unitID );
            if ( pUnit != nullptr && pUnit->_owner != _player )
                return false;
        }
        return _listSelected.empty() == false;
    }
} // namespace sw
