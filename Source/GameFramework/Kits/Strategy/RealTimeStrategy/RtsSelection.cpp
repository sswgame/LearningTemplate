#include "pch.h"

#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsSelection.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RtsSelectionInternal
        {
            /** @brief 유닛이 사각형과 겹치는가입니다(유닛은 몸, 건물은 자리 가운데). */
            static bool isInRect( const RtsUnit& unit, const float3& minCorner, const float3& maxCorner )
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
    void RtsSelection::writeState( Archive& outArchive ) const
    {
        const auto writeList = [&outArchive]( const vector<RtsUnitId>& listUnit )
        {
            outArchive << static_cast<uint32>( listUnit.size() );
            for ( const RtsUnitId unitId : listUnit )
            {
                outArchive << unitId.packed();
            }
        };
        writeList( _listSelected );
        for ( const vector<RtsUnitId>& listGroup : _arrGroup )
        {
            writeList( listGroup );
        }
    }

    bool RtsSelection::readState( Archive& archive )
    {
        const auto readList = [&archive]( vector<RtsUnitId>& outListUnit )
        {
            uint32 count = 0;
            if ( StateArchiveUtil::readCount( archive, sizeof( uint64 ), count ) == false )
                return false;
            outListUnit.resize( count );
            for ( RtsUnitId& unitId : outListUnit )
            {
                uint64 packed = 0;
                archive >> packed;
                unitId = RtsUnitId::fromPacked( packed );
            }
            return archive.isOk();
        };
        vector<RtsUnitId> listSelected;
        vector<RtsUnitId> arrGroup[kGroupCount];
        if ( readList( listSelected ) == false )
            return false;
        for ( vector<RtsUnitId>& listGroup : arrGroup )
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

    RtsSelection::RtsSelection()
        : _listSelected{}
        , _arrGroup{}
        , _player{ 0 }
        , _maxCount{ 0 }
    {
    }

    void RtsSelection::addUnit( RtsUnitId unitId )
    {
        if ( isSelected( unitId ) || ( _maxCount > 0 && static_cast<int32>( _listSelected.size() ) >= _maxCount ) )
            return;
        _listSelected.push_back( unitId );
    }

    void RtsSelection::selectInRect( const RtsWorld& world, const float3& cornerA, const float3& cornerB, bool bAdd )
    {
        float3 minCorner{};
        float3 maxCorner{};
        RtsSelectionInternal::sortCorners( cornerA, cornerB, minCorner, maxCorner );
        vector<RtsUnitId> listMobile;
        RtsUnitId         ownBuilding{};
        RtsUnitId         otherUnit{};
        world.forEachUnit( [&]( const RtsUnit& unit )
        {
            if ( RtsSelectionInternal::isInRect( unit, minCorner, maxCorner ) == false )
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
            const RtsUnit* pPrimary = world.findUnit( getPrimary() );
            if ( pPrimary == nullptr || pPrimary->isMobile() )
            {
                for ( const RtsUnitId unitId : listMobile )
                {
                    addUnit( unitId );
                }
                return;
            }
        }
        if ( listMobile.empty() && ownBuilding.isValid() == false && otherUnit.isValid() == false )
            return; // 빈 땅을 끌면 그대로 둔다
        _listSelected.clear();
        if ( listMobile.empty() == false )
        {
            for ( const RtsUnitId unitId : listMobile )
            {
                addUnit( unitId );
            }
        }
        else
        {
            _listSelected.push_back( ownBuilding.isValid() ? ownBuilding : otherUnit );
        }
    }

    void RtsSelection::selectUnit( const RtsWorld& world, RtsUnitId unitId, bool bToggle )
    {
        const RtsUnit* pUnit = world.findUnit( unitId );
        if ( pUnit == nullptr )
            return;
        if ( bToggle && pUnit->_owner == _player && pUnit->isMobile() && isCommandable( world ) )
        {
            const auto selectedIter = std::find( _listSelected.begin(), _listSelected.end(), unitId );
            if ( selectedIter != _listSelected.end() )
                _listSelected.erase( selectedIter );
            else
                addUnit( unitId );
            return;
        }
        _listSelected.clear();
        _listSelected.push_back( unitId );
    }

    void RtsSelection::selectSameType( const RtsWorld& world, RtsUnitId unitId, const float3& cornerA, const float3& cornerB )
    {
        const RtsUnit* pUnit = world.findUnit( unitId );
        if ( pUnit == nullptr )
            return;
        if ( pUnit->_owner != _player )
        {
            selectUnit( world, unitId, false );
            return;
        }
        float3 minCorner{};
        float3 maxCorner{};
        RtsSelectionInternal::sortCorners( cornerA, cornerB, minCorner, maxCorner );
        const hashed_string defId = pUnit->_pDef->_id;
        _listSelected.clear();
        addUnit( unitId );
        world.forEachUnit( [&]( const RtsUnit& unit )
        {
            if ( unit._owner == _player && unit._pDef->_id == defId && RtsSelectionInternal::isInRect( unit, minCorner, maxCorner ) )
                addUnit( unit._id );
        } );
    }

    void RtsSelection::assignGroup( int32 group )
    {
        if ( group < 0 || group >= kGroupCount )
            return;
        _arrGroup[group] = _listSelected;
    }

    void RtsSelection::addToGroup( int32 group )
    {
        if ( group < 0 || group >= kGroupCount )
            return;
        vector<RtsUnitId>& listGroup = _arrGroup[group];
        for ( const RtsUnitId unitId : _listSelected )
        {
            if ( std::find( listGroup.begin(), listGroup.end(), unitId ) == listGroup.end() )
                listGroup.push_back( unitId );
        }
    }

    bool RtsSelection::recallGroup( const RtsWorld& world, int32 group )
    {
        if ( group < 0 || group >= kGroupCount )
            return false;
        prune( world );
        if ( _arrGroup[group].empty() )
            return false;
        _listSelected = _arrGroup[group];
        return true;
    }

    void RtsSelection::prune( const RtsWorld& world )
    {
        const auto isDead = [&world]( RtsUnitId unitId )
        { return world.findUnit( unitId ) == nullptr; };
        _listSelected.erase( std::remove_if( _listSelected.begin(), _listSelected.end(), isDead ), _listSelected.end() );
        for ( vector<RtsUnitId>& listGroup : _arrGroup )
        {
            listGroup.erase( std::remove_if( listGroup.begin(), listGroup.end(), isDead ), listGroup.end() );
        }
    }

    const vector<RtsUnitId>& RtsSelection::getGroup( int32 group ) const
    {
        static const vector<RtsUnitId> kEmptyGroup{};
        return group >= 0 && group < kGroupCount ? _arrGroup[group] : kEmptyGroup;
    }

    bool RtsSelection::isSelected( RtsUnitId unitId ) const { return std::find( _listSelected.begin(), _listSelected.end(), unitId ) != _listSelected.end(); }

    bool RtsSelection::isCommandable( const RtsWorld& world ) const
    {
        for ( const RtsUnitId unitId : _listSelected )
        {
            const RtsUnit* pUnit = world.findUnit( unitId );
            if ( pUnit != nullptr && pUnit->_owner != _player )
                return false;
        }
        return _listSelected.empty() == false;
    }
} // namespace sw
