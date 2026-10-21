#include "pch.h"

#include "GameFramework/Kits/Feature/World/Overworld/ZoneTracker.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

namespace sw
{
    namespace
    {
        struct ZoneTrackerInternal
        {
            /** @brief 태그 문자열을 나누는 구분자입니다. 쉼표, 공백, 탭, 줄바꿈이 해당합니다. */
            static constexpr string_view kTagSeparator = ", \t\r\n";
        };
    } // namespace
} // namespace sw

namespace sw
{
    ZoneDef::ZoneDef()
        : _id{}
        , _bounds{}
        , _listTag{}
        , _bClearGateLocked{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ZoneTracker::ZoneTracker()
        : _listZone{}
        , _activeIndex{ -1 }
    {
    }

    bool ZoneDef::hasTag( const hashed_string& tag ) const
    {
        return VectorUtil::contains( _listTag, tag );
    }

    void ZoneDef::addTag( const hashed_string& tag )
    {
        if ( tag.empty() || hasTag( tag ) )
            return;
        _listTag.push_back( tag );
    }

    void ZoneTracker::clear()
    {
        _listZone.clear();
        _activeIndex = -1;
    }

    void ZoneTracker::setFromMap( string_view mapPath, string_view mapName, int32 width, int32 height, string_view tagText )
    {
        clear();
        ZoneDef zone{};
        zone._id             = mapName.empty() ? mapPath : mapName;
        zone._bounds._min._x = 0;
        zone._bounds._min._y = 0;
        zone._bounds._max._x = width > 0 ? width - 1 : 0;
        zone._bounds._max._y = height > 0 ? height - 1 : 0;

        // 태그는 맵 데이터가 붙인다. 경로 이름에서 역할을 짐작하지 않는다.
        StringUtil::forEachToken( tagText, ZoneTrackerInternal::kTagSeparator, [&]( string_view tag )
        { zone.addTag( hashed_string( tag ) ); } );
        zone._bClearGateLocked = zone.hasTag( hashed_string( kClearGateTag ) ) ? SW_TRUE : SW_FALSE;

        _listZone.push_back( std::move( zone ) );
        _activeIndex = 0;
    }

    void ZoneTracker::activate( string_view zoneID )
    {
        for ( size_t zoneIndex = 0; zoneIndex < _listZone.size(); ++zoneIndex )
        {
            if ( _listZone[zoneIndex]._id == zoneID )
            {
                _activeIndex = static_cast<int32>( zoneIndex );
                return;
            }
        }
    }

    void ZoneTracker::setClearGateLocked( bool bLocked )
    {
        if ( _activeIndex < 0 || _activeIndex >= static_cast<int32>( _listZone.size() ) )
            return;
        _listZone[static_cast<size_t>( _activeIndex )]._bClearGateLocked = bLocked ? SW_TRUE : SW_FALSE;
    }

    bool ZoneTracker::isClearGateLocked() const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr && pZone->_bClearGateLocked != SW_FALSE;
    }

    bool ZoneTracker::hasActiveZoneTag( const hashed_string& tag ) const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr && pZone->hasTag( tag );
    }

    const ZoneDef* ZoneTracker::getActiveZone() const
    {
        if ( _activeIndex < 0 || _activeIndex >= static_cast<int32>( _listZone.size() ) )
            return nullptr;
        return &_listZone[static_cast<size_t>( _activeIndex )];
    }

    string ZoneTracker::getActiveZoneID() const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr ? pZone->_id : string{};
    }

    const ZoneBounds& ZoneTracker::getCameraBounds() const
    {
        static ZoneBounds s_empty{};
        const ZoneDef*    pZone = getActiveZone();
        return pZone != nullptr ? pZone->_bounds : s_empty;
    }
} // namespace sw
