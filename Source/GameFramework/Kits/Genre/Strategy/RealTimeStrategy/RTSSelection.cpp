#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSSelection.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RTSSelectionInternal
        {
            /** @brief 유닛이 사각형과 겹치는가입니다(유닛은 몸, 건물은 자리 가운데). */
            static bool isInRect( const RTSUnit& unit, const float3& minCorner, const float3& maxCorner )
            {
                const float32 radius = unit.isMobile() ? unit._pDef->_radius : 0.0f;
                return unit._position._x + radius >= minCorner._x && unit._position._x - radius <= maxCorner._x && unit._position._z + radius >= minCorner._z &&
                       unit._position._z - radius <= maxCorner._z;
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
    void RTSSelection::writeState( Archive& outArchive ) const
    {
        const auto writeList = [&outArchive]( const vector<RTSUnitID>& listUnit )
        {
            outArchive << static_cast<uint32>( listUnit.size() );
            for ( const RTSUnitID unitID : listUnit )
            {
                outArchive << unitID.packed();
            }
        };
        writeList( _listSelected );
        for ( const vector<RTSUnitID>& listGroup : _arrGroup )
        {
            writeList( listGroup );
        }
    }

    bool RTSSelection::readState( Archive& archive )
    {
        const auto readList = [&archive]( vector<RTSUnitID>& outListUnit )
        {
            uint32 count = 0;
            if ( StateArchiveUtil::readCount( archive, sizeof( uint64 ), count ) == false )
                return false;
            outListUnit.resize( count );
            for ( RTSUnitID& unitID : outListUnit )
            {
                uint64 packed = 0;
                archive >> packed;
                unitID = RTSUnitID::fromPacked( packed );
            }
            return archive.isOk();
        };
        vector<RTSUnitID> listSelected;
        vector<RTSUnitID> arrGroup[kGroupCount];
        if ( readList( listSelected ) == false )
            return false;
        for ( vector<RTSUnitID>& listGroup : arrGroup )
        {
            if ( readList( listGroup ) == false )
                return false;
        }
        _listSelected = std::move( listSelected );
        for ( int32 group = 0; group < kGroupCount; ++group )
        {
            _arrGroup[group] = std::move( arrGroup[group] );
        }
        return true;
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
            if ( unit._owner == _player && unit.isMobile() )
                listMobile.push_back( unit._id );
            else if ( unit._owner == _player && ownBuilding.isValid() == false )
                ownBuilding = unit._id;
            else if ( unit._owner != _player && otherUnit.isValid() == false && world.isVisibleTo( _player, unit._id ) )
                otherUnit = unit._id;
        } );
        // 더하기는 내 움직이는 유닛끼리만 — 건물 · 남의 것은 혼자 고른다.
        if ( bAdd && listMobile.empty() == false && isCommandable( world ) )
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
        if ( listMobile.empty() && ownBuilding.isValid() == false && otherUnit.isValid() == false )
            return; // 빈 땅을 끌면 그대로 둔다
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
        if ( bToggle && pUnit->_owner == _player && pUnit->isMobile() && isCommandable( world ) )
        {
            const auto selectedIter = std::find( _listSelected.begin(), _listSelected.end(), unitID );
            if ( selectedIter != _listSelected.end() )
                _listSelected.erase( selectedIter );
            else
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
            if ( unit._owner == _player && unit._pDef->_id == defID && RTSSelectionInternal::isInRect( unit, minCorner, maxCorner ) )
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
            if ( std::find( listGroup.begin(), listGroup.end(), unitID ) == listGroup.end() )
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
        _listSelected.erase( std::remove_if( _listSelected.begin(), _listSelected.end(), isDead ), _listSelected.end() );
        for ( vector<RTSUnitID>& listGroup : _arrGroup )
        {
            listGroup.erase( std::remove_if( listGroup.begin(), listGroup.end(), isDead ), listGroup.end() );
        }
    }

    const vector<RTSUnitID>& RTSSelection::getGroup( int32 group ) const
    {
        static const vector<RTSUnitID> kEmptyGroup{};
        return group >= 0 && group < kGroupCount ? _arrGroup[group] : kEmptyGroup;
    }

    bool RTSSelection::isSelected( RTSUnitID unitID ) const { return std::find( _listSelected.begin(), _listSelected.end(), unitID ) != _listSelected.end(); }

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
