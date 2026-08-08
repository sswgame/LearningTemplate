#pragma once
/**
 * @file ZoneRuntime.h
 * @brief Activate/Pause zones with role tags and camera bounds (Dungreed room idea)
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
	enum class ZoneRole : uint8
	{
		Town = 0,
		Route,
		Center,
		Mart,
		Gym,
		Wild,
		Battle
	};

	struct ZoneBounds
	{
		int32 _minX = 0;
		int32 _minY = 0;
		int32 _maxX = 0;
		int32 _maxY = 0;
	};

	struct ZoneDef
	{
		ZoneDef();

		std::string _id;
		ZoneRole	_role	 = ZoneRole::Town;
		ZoneBounds	_bounds{};
		uint8		_bClearGateLocked : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};

	class ZoneRuntime
	{
	public:
		void clear();
		void setFromMap( const std::string& mapPath, const std::string& mapName, int32 width, int32 height );
		void activate( const std::string& zoneId );
		void setClearGateLocked( bool locked );
		bool isClearGateLocked() const;
		bool isWarpBlocked() const { return isClearGateLocked(); }

		const ZoneDef* getActiveZone() const;
		ZoneRole	   getActiveRole() const;
		const ZoneBounds& getCameraBounds() const;

	private:
		std::vector<ZoneDef> _zones;
		int32				 _activeIndex = -1;
	};

	ZoneRole zoneRoleFromMapPath( const std::string& mapPath );
} // namespace sw
