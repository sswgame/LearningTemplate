#include "pch.h"

#include "GameFramework/Kits/Overworld/ZoneRuntime.h"

#include "Core/String/StringUtil.h"

namespace sw
{
	namespace
	{
		struct ZoneRuntimeInternal
		{
			static ZoneRole zoneRoleFromText( string_view roleText, string_view mapPath )
			{
				if ( roleText.empty() )
					return zoneRoleFromMapPath( mapPath );
				if ( StringUtil::equalsIgnoreCase( roleText, "boss" ) )
					return ZoneRole::Boss;
				if ( StringUtil::equalsIgnoreCase( roleText, "dungeon" ) )
					return ZoneRole::Dungeon;
				if ( StringUtil::equalsIgnoreCase( roleText, "battle" ) )
					return ZoneRole::Battle;
				if ( StringUtil::equalsIgnoreCase( roleText, "route" ) )
					return ZoneRole::Route;
				if ( StringUtil::equalsIgnoreCase( roleText, "center" ) )
					return ZoneRole::Center;
				if ( StringUtil::equalsIgnoreCase( roleText, "mart" ) )
					return ZoneRole::Mart;
				if ( StringUtil::equalsIgnoreCase( roleText, "gym" ) )
					return ZoneRole::Gym;
				if ( StringUtil::equalsIgnoreCase( roleText, "wild" ) )
					return ZoneRole::Wild;
				return ZoneRole::Town;
			}

			static bool roleUsesClearGate( ZoneRole role )
			{
				return role == ZoneRole::Gym || role == ZoneRole::Dungeon || role == ZoneRole::Boss;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	ZoneDef::ZoneDef()
		: _id{}
		, _role{ ZoneRole::Town }
		, _bounds{}
		, _listTag{}
		, _bClearGateLocked{ 0 }
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
		z._id				= mapName.empty() ? mapPath : mapName;
		z._role				= ZoneRuntimeInternal::zoneRoleFromText( roleText, mapPath );
		z._bounds._minX		= 0;
		z._bounds._minY		= 0;
		z._bounds._maxX		= width > 0 ? width - 1 : 0;
		z._bounds._maxY		= height > 0 ? height - 1 : 0;
		z._bClearGateLocked = ZoneRuntimeInternal::roleUsesClearGate( z._role ) ? 1 : 0;
		// 역할을 태그로 미러해 장르 비의존 코드가 ZoneRole 없이 조회할 수 있게 합니다.
		switch ( z._role )
		{
			case ZoneRole::Town:
				z.addTag( "town" );
				break;
			case ZoneRole::Route:
				z.addTag( "route" );
				break;
			case ZoneRole::Dungeon:
				z.addTag( "dungeon" );
				break;
			case ZoneRole::Boss:
				z.addTag( "boss" );
				break;
			case ZoneRole::Battle:
				z.addTag( "battle" );
				break;
			case ZoneRole::Center:
				z.addTag( "center" );
				break;
			case ZoneRole::Mart:
				z.addTag( "mart" );
				break;
			case ZoneRole::Gym:
				z.addTag( "gym" );
				break;
			case ZoneRole::Wild:
				z.addTag( "wild" );
				break;
		}
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
		return pZone != nullptr && pZone->_bClearGateLocked != 0;
	}

	bool ZoneRuntime::activeHasTag( string_view tag ) const
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
		const ZoneDef*	  pZone = getActiveZone();
		return pZone != nullptr ? pZone->_bounds : s_empty;
	}

	ZoneRole zoneRoleFromMapPath( string_view mapPath )
	{
		if ( mapPath.find( "dungeon_boss" ) != string::npos || mapPath.find( "boss" ) != string::npos )
			return ZoneRole::Boss;
		if ( mapPath.find( "dungeon" ) != string::npos )
			return ZoneRole::Dungeon;
		if ( mapPath.find( "battle" ) != string::npos )
			return ZoneRole::Battle;
		if ( mapPath.find( "route" ) != string::npos )
			return ZoneRole::Route;
		if ( mapPath.find( "center" ) != string::npos )
			return ZoneRole::Center;
		if ( mapPath.find( "mart" ) != string::npos )
			return ZoneRole::Mart;
		if ( mapPath.find( "gym" ) != string::npos )
			return ZoneRole::Gym;
		if ( mapPath.find( "wild" ) != string::npos )
			return ZoneRole::Wild;
		return ZoneRole::Town;
	}
} // namespace sw
