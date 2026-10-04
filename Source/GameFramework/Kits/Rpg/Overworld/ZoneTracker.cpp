#include "pch.h"

#include "GameFramework/Kits/Rpg/Overworld/ZoneTracker.h"

#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 역할과 이름의 **기준 표 하나입니다.**
         * @details 글자에서 역할을 읽는 곳, 맵 경로에서 역할을 읽는 곳, 역할을 태그로 미러하는 곳이
         *          모두 이 표를 봅니다. 대응을 따로 적으면 **한 곳만 고쳤을 때 조용히 어긋납니다**
         *          (대소문자를 무시하는 쪽과 맨 `find` 쪽이 갈라져 `Dungeon_01` 이 던전이 아니게 되는 식).
         *
         *          **줄 순서가 곧 우선순위입니다.** 경로에 여러 이름이 들어 있으면 위의 것이 이깁니다
         *          (`dungeon_boss` 는 `boss` 줄이 잡아 보스입니다). 그래서 `Town` 은 맨 아래이자 기본값입니다.
         */
        struct ZoneRoleName
        {
            ZoneRole    _role;
            const utf8* _pName;
        };

        constexpr ZoneRoleName kArrZoneRoleName[]{
            {   ZoneRole::Boss,    "boss"},
            {ZoneRole::Dungeon, "dungeon"},
            { ZoneRole::Battle,  "battle"},
            {  ZoneRole::Route,   "route"},
            { ZoneRole::Center,  "center"},
            {   ZoneRole::Mart,    "mart"},
            {    ZoneRole::Gym,     "gym"},
            {   ZoneRole::Wild,    "wild"},
            {   ZoneRole::Town,    "town"},
        };

        struct ZoneTrackerInternal
        {
            static ZoneRole zoneRoleFromText( string_view roleText, string_view mapPath )
            {
                if ( roleText.empty() )
                    return zoneRoleFromMapPath( mapPath );
                for ( const ZoneRoleName& entry : kArrZoneRoleName )
                {
                    if ( StringUtil::equals( roleText, entry._pName, true ) )
                        return entry._role;
                }
                return ZoneRole::Town;
            }

            static bool roleUsesClearGate( ZoneRole role )
            {
                return role == ZoneRole::Gym || role == ZoneRole::Dungeon || role == ZoneRole::Boss;
            }
        };
    } // namespace

    const utf8* zoneRoleToTag( ZoneRole role )
    {
        for ( const ZoneRoleName& entry : kArrZoneRoleName )
        {
            if ( entry._role == role )
                return entry._pName;
        }
        return "town";
    }
} // namespace sw

namespace sw
{
    ZoneDef::ZoneDef()
        : _id{}
        , _role{ ZoneRole::Town }
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

    bool ZoneDef::hasTag( string_view tag ) const
    {
        for ( const string& existingTag : _listTag )
        {
            if ( existingTag == tag )
                return true;
        }
        return false;
    }

    void ZoneDef::addTag( string_view tag )
    {
        if ( tag.empty() || hasTag( tag ) )
            return;
        _listTag.emplace_back( tag );
    }

    void ZoneTracker::clear()
    {
        _listZone.clear();
        _activeIndex = -1;
    }

    void ZoneTracker::setFromMap( string_view mapPath, string_view mapName, int32 width, int32 height,
                                  string_view roleText )
    {
        clear();
        ZoneDef zone{};
        zone._id               = mapName.empty() ? mapPath : mapName;
        zone._role             = ZoneTrackerInternal::zoneRoleFromText( roleText, mapPath );
        zone._bounds._min._x   = 0;
        zone._bounds._min._y   = 0;
        zone._bounds._max._x   = width > 0 ? width - 1 : 0;
        zone._bounds._max._y   = height > 0 ? height - 1 : 0;
        zone._bClearGateLocked = ZoneTrackerInternal::roleUsesClearGate( zone._role ) ? SW_TRUE : SW_FALSE;
        // 역할을 태그로 미러해 장르 비의존 코드가 ZoneRole 없이 조회할 수 있게 한다.
        // 이름은 위의 표 하나에서 온다. 여기에 `switch` 로 다시 적으면 세 번째 사본이 된다.
        zone.addTag( zoneRoleToTag( zone._role ) );
        _listZone.push_back( std::move( zone ) );
        _activeIndex = 0;
    }

    void ZoneTracker::activate( string_view zoneId )
    {
        for ( size_t zoneIndex = 0; zoneIndex < _listZone.size(); ++zoneIndex )
        {
            if ( _listZone[zoneIndex]._id == zoneId )
            {
                _activeIndex = static_cast<int32>( zoneIndex );
                return;
            }
        }
    }

    void ZoneTracker::setClearGateLocked( bool locked )
    {
        if ( _activeIndex < 0 || _activeIndex >= static_cast<int32>( _listZone.size() ) )
            return;
        _listZone[static_cast<size_t>( _activeIndex )]._bClearGateLocked = locked ? SW_TRUE : SW_FALSE;
    }

    bool ZoneTracker::isClearGateLocked() const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr && pZone->_bClearGateLocked != SW_FALSE;
    }

    bool ZoneTracker::hasActiveZoneTag( string_view tag ) const
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

    string ZoneTracker::getActiveZoneId() const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr ? pZone->_id : string{};
    }

    ZoneRole ZoneTracker::getActiveRole() const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr ? pZone->_role : ZoneRole::Town;
    }

    const ZoneBounds& ZoneTracker::getCameraBounds() const
    {
        static ZoneBounds s_empty{};
        const ZoneDef*    pZone = getActiveZone();
        return pZone != nullptr ? pZone->_bounds : s_empty;
    }

    ZoneRole zoneRoleFromMapPath( string_view mapPath )
    {
        // 표의 **줄 순서가 우선순위**다. 글자로 묻는 짝과 같은 표를 보고, 같이 대소문자를
        // 무시한다(`Dungeon_01` 도 던전이다).
        for ( const ZoneRoleName& entry : kArrZoneRoleName )
        {
            if ( StringUtil::contains( mapPath, entry._pName, true ) )
                return entry._role;
        }
        return ZoneRole::Town;
    }
} // namespace sw
