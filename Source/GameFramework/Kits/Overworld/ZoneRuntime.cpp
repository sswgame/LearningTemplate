#include "pch.h"

#include "GameFramework/Kits/Overworld/ZoneRuntime.h"

#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 역할과 이름의 **정본 표 하나.**
         * @details 예전에는 같은 대응이 세 곳에 각각 적혀 있었다 — 글자에서 역할을 읽는 곳,
         *          맵 경로에서 역할을 읽는 곳, 역할을 태그로 미러하는 `switch`. 역할을 하나
         *          더하려면 세 곳을 고쳐야 하고, **한 곳만 고치면 그때부터 조용히 어긋난다.**
         *          실제로 이미 어긋나 있었다: 글자로 묻는 쪽은 대소문자를 무시하는데 경로로
         *          묻는 쪽은 맨 `find` 라서 구별했다 — `Dungeon_01` 은 던전이 아니었다.
         *
         *          **줄 순서가 곧 우선순위다.** 경로에 여러 이름이 들어 있으면 위의 것이 이긴다
         *          (`dungeon_boss` 는 보스다). 그래서 `Town` 은 맨 아래이자 기본값이다.
         *          예전의 `find( "dungeon_boss" )` 줄은 바로 다음 `find( "boss" )` 가 이미
         *          같은 것을 잡으므로 **한 번도 혼자 참인 적이 없었다** — 지웠다.
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

        struct ZoneRuntimeInternal
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

    ZoneRuntime::ZoneRuntime()
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

    void ZoneRuntime::clear()
    {
        _listZone.clear();
        _activeIndex = -1;
    }

    void ZoneRuntime::setFromMap( string_view mapPath, string_view mapName, int32 width, int32 height,
                                  string_view roleText )
    {
        clear();
        ZoneDef z{};
        z._id               = mapName.empty() ? mapPath : mapName;
        z._role             = ZoneRuntimeInternal::zoneRoleFromText( roleText, mapPath );
        z._bounds._min._x   = 0;
        z._bounds._min._y   = 0;
        z._bounds._max._x   = width > 0 ? width - 1 : 0;
        z._bounds._max._y   = height > 0 ? height - 1 : 0;
        z._bClearGateLocked = ZoneRuntimeInternal::roleUsesClearGate( z._role ) ? 1 : 0;
        // 역할을 태그로 미러해 장르 비의존 코드가 ZoneRole 없이 조회할 수 있게 합니다.
        // 이름은 위의 표 하나에서 온다 — 여기에 `switch` 로 다시 적으면 세 번째 사본이 된다.
        z.addTag( zoneRoleToTag( z._role ) );
        _listZone.push_back( std::move( z ) );
        _activeIndex = 0;
    }

    void ZoneRuntime::activate( string_view zoneId )
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

    void ZoneRuntime::setClearGateLocked( bool locked )
    {
        if ( _activeIndex < 0 || _activeIndex >= static_cast<int32>( _listZone.size() ) )
            return;
        _listZone[static_cast<size_t>( _activeIndex )]._bClearGateLocked = locked ? 1 : 0;
    }

    bool ZoneRuntime::isClearGateLocked() const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr && pZone->_bClearGateLocked != SW_FALSE;
    }

    bool ZoneRuntime::hasActiveZoneTag( string_view tag ) const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr && pZone->hasTag( tag );
    }

    const ZoneDef* ZoneRuntime::getActiveZone() const
    {
        if ( _activeIndex < 0 || _activeIndex >= static_cast<int32>( _listZone.size() ) )
            return nullptr;
        return &_listZone[static_cast<size_t>( _activeIndex )];
    }

    string ZoneRuntime::getActiveZoneId() const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr ? pZone->_id : string{};
    }

    ZoneRole ZoneRuntime::getActiveRole() const
    {
        const ZoneDef* pZone = getActiveZone();
        return pZone != nullptr ? pZone->_role : ZoneRole::Town;
    }

    const ZoneBounds& ZoneRuntime::getCameraBounds() const
    {
        static ZoneBounds s_empty{};
        const ZoneDef*    pZone = getActiveZone();
        return pZone != nullptr ? pZone->_bounds : s_empty;
    }

    ZoneRole zoneRoleFromMapPath( string_view mapPath )
    {
        // 표의 **줄 순서가 우선순위**다. 글자로 묻는 짝과 같은 표를 보고, 같이 대소문자를
        // 무시한다 — 예전에는 여기만 맨 `find` 여서 `Dungeon_01` 이 던전이 아니었다.
        for ( const ZoneRoleName& entry : kArrZoneRoleName )
        {
            if ( StringUtil::contains( mapPath, entry._pName, true ) )
                return entry._role;
        }
        return ZoneRole::Town;
    }
} // namespace sw
