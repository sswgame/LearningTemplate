/**
 * @file ZoneRuntime.cpp
 */
#include "ZoneRuntime.h"

namespace sw
{
	ZoneDef::ZoneDef()
		: _bClearGateLocked{ 0 }
		, _reserved{ 0 }
	{
	}

	ZoneRole zoneRoleFromMapPath( const std::string& mapPath )
	{
		if ( mapPath.find( "battle" ) != std::string::npos )
			return ZoneRole::Battle;
		if ( mapPath.find( "route" ) != std::string::npos )
			return ZoneRole::Route;
		if ( mapPath.find( "center" ) != std::string::npos )
			return ZoneRole::Center;
		if ( mapPath.find( "mart" ) != std::string::npos )
			return ZoneRole::Mart;
		if ( mapPath.find( "gym" ) != std::string::npos )
			return ZoneRole::Gym;
		if ( mapPath.find( "wild" ) != std::string::npos )
			return ZoneRole::Wild;
		return ZoneRole::Town;
	}

	void ZoneRuntime::clear()
	{
		_zones.clear();
		_activeIndex = -1;
	}

	void ZoneRuntime::setFromMap( const std::string& mapPath, const std::string& mapName, int32 width, int32 height )
	{
		clear();
		ZoneDef z{};
		z._id				 = mapName.empty() ? mapPath : mapName;
		z._role				 = zoneRoleFromMapPath( mapPath );
		z._bounds._minX		 = 0;
		z._bounds._minY		 = 0;
		z._bounds._maxX		 = width > 0 ? width - 1 : 0;
		z._bounds._maxY		 = height > 0 ? height - 1 : 0;
		z._bClearGateLocked	 = ( z._role == ZoneRole::Gym ) ? 1 : 0;
		_zones.push_back( std::move( z ) );
		_activeIndex = 0;
	}

	void ZoneRuntime::activate( const std::string& zoneId )
	{
		for ( size_t i = 0; i < _zones.size(); ++i )
		{
			if ( _zones[i]._id == zoneId )
			{
				_activeIndex = static_cast<int32>( i );
				return;
			}
		}
	}

	void ZoneRuntime::setClearGateLocked( bool locked )
	{
		if ( _activeIndex < 0 || _activeIndex >= static_cast<int32>( _zones.size() ) )
			return;
		_zones[static_cast<size_t>( _activeIndex )]._bClearGateLocked = locked ? 1 : 0;
	}

	bool ZoneRuntime::isClearGateLocked() const
	{
		const ZoneDef* z = getActiveZone();
		return z != nullptr && z->_bClearGateLocked != 0;
	}

	const ZoneDef* ZoneRuntime::getActiveZone() const
	{
		if ( _activeIndex < 0 || _activeIndex >= static_cast<int32>( _zones.size() ) )
			return nullptr;
		return &_zones[static_cast<size_t>( _activeIndex )];
	}

	ZoneRole ZoneRuntime::getActiveRole() const
	{
		const ZoneDef* z = getActiveZone();
		return z != nullptr ? z->_role : ZoneRole::Town;
	}

	const ZoneBounds& ZoneRuntime::getCameraBounds() const
	{
		static ZoneBounds s_empty{};
		const ZoneDef*	  z = getActiveZone();
		return z != nullptr ? z->_bounds : s_empty;
	}
} // namespace sw
