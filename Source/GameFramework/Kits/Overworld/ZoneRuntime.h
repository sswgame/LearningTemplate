/**
 * @file ZoneRuntime.h
 * @brief 역할 태그와 카메라 경계로 존을 활성화/일시정지합니다 (룸 개념)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) ZoneRole · 경계 · 정의
	//    클리어 게이트가 잠기면 워프를 막음 (액션 룸)
	// ------------------------------------------------------------------------------
	/** @brief 맵 역할 (BGM·조우·액션 룸 분기) */
	enum class ZoneRole : uint8
	{
		Town = 0,
		Route,
		Center,
		Mart,
		Gym,
		Wild,
		Battle,
		Dungeon,
		Boss
	};

	/** @brief 카메라가 머물 타일 경계 */
	struct ZoneBounds
	{
		int32 _minX{ 0 };
		int32 _minY{ 0 };
		int32 _maxX{ 0 };
		int32 _maxY{ 0 };
	};

	/** @brief 한 존의 ID·역할·경계·태그 */
	struct ZoneDef
	{
		string				   _id; ///< 안정적인 존 ID (맵 이름 / 경로)
		ZoneRole			   _role;
		ZoneBounds			   _bounds;
		vector<string>		   _listTag;			  ///< 장르 비의존 태그 (예: "indoors", "no_encounter")
		uint8				   _bClearGateLocked : 1; ///< 잠기면 워프 차단
		[[maybe_unused]] uint8 _reserved		 : 7;

		/** @brief 게이트 해제, 태그 없음으로 시작합니다. */
		ZoneDef();

		/** @brief 태그가 있는지 반환합니다. */
		bool hasTag( string_view tag ) const;
		/** @brief 태그를 추가합니다. */
		void addTag( string_view tag );
	};

	// ------------------------------------------------------------------------------
	// 2) ZoneRuntime — 존 목록 + 활성 존 조회
	//    1개 맵 = 1개 기본 존 (setFromMap) 또는 타일맵 메타에서 다중 존 (loadFromXml)
	// ------------------------------------------------------------------------------
	/** @brief 런타임 존 상태 (경계, 역할, 클리어 게이트, 태그) */
	class SW_GF_API ZoneRuntime
	{
	public:
		ZoneRuntime();

		/** @brief 존 목록과 활성 인덱스를 비웁니다. */
		void clear();

		/** @brief 맵 크기로 단일 기본 존을 만듭니다. roleText 가 비면 경로에서 추론합니다. */
		void setFromMap( string_view mapPath, string_view mapName, int32 width, int32 height,
						 string_view roleText = "" );

		/** @brief 지정 존을 활성화합니다. */
		void activate( string_view zoneId );

		/** @brief 플레이어 타일 위치에 맞는 첫 번째 존을 활성화합니다. 변경되면 true 를 반환합니다. */
		bool updateActiveZone( int32 playerX, int32 playerY );

		/** @brief 활성 존의 클리어 게이트를 잠그거나 풉니다. */
		void setClearGateLocked( bool bLocked );
		/** @brief 활성 존의 클리어 게이트가 잠겨 있는지 반환합니다. */
		bool isClearGateLocked() const;
		/** @brief 활성 존이 태그를 갖는지 반환합니다. */
		bool activeHasTag( string_view tag ) const;

		/** @brief 활성 존 정의를 반환합니다. */
		const ZoneDef* getActiveZone() const;
		/** @brief getActiveZone()->_id의 별칭입니다 (없으면 빈 문자열). */
		string getActiveZoneId() const;
		/** @brief 활성 존 역할을 반환합니다. */
		ZoneRole getActiveRole() const;
		/** @brief 카메라 경계를 반환합니다. */
		const ZoneBounds& getCameraBounds() const;

	private:
		vector<ZoneDef> _listZone;
		int32			_activeIndex;
	};

	// ------------------------------------------------------------------------------
	// 3) 맵 경로 → ZoneRole (setFromMap 폴백)
	// ------------------------------------------------------------------------------
	/** @brief 맵 경로에서 존 역할을 추론합니다. */
	ZoneRole zoneRoleFromMapPath( string_view mapPath );
} // namespace sw
