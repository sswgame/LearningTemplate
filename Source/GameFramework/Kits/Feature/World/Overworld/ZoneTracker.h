/**
 * @file ZoneTracker.h
 * @brief 맵이 붙인 태그 · 카메라 경계 · 클리어 게이트로 활성 존을 고릅니다(룸 개념).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 카메라가 머물 타일 경계입니다. */
    struct ZoneBounds
    {
        int2 _min{ 0, 0 }; ///< 정수판 AABB. float 판은 AABB2D 가 같은 모양이다
        int2 _max{ 0, 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 존의 ID · 경계 · 태그 · 클리어 게이트입니다. */
    struct SW_GF_API ZoneDef
    {
        string                 _id; ///< 안정적인 존 ID (맵 이름 / 경로)
        ZoneBounds             _bounds;
        vector<hashed_string>  _listTag;              ///< 맵 데이터가 붙인 태그(예: "gym", "indoors", "no_encounter"). 대소문자를 가리지 않는다
        uint8                  _bClearGateLocked : 1; ///< 잠기면 워프 차단
        [[maybe_unused]] uint8 _reserved         : 7;

        /** @brief 게이트 해제, 태그 없음으로 시작합니다. */
        ZoneDef();

        /** @brief 태그가 있는지 반환합니다. */
        bool hasTag( const hashed_string& tag ) const;
        /** @brief 태그를 추가합니다. 빈 이름 · 이미 있는 태그는 무시합니다. */
        void addTag( const hashed_string& tag );
    };
} // namespace sw

namespace sw
{
    /**
     * @class ZoneTracker
     * @brief 런타임 존 상태입니다(경계, 태그, 클리어 게이트). 1 개 맵 = 1 개 기본 존(`setFromMap`)입니다.
     * @details 존이 무엇인지(마을 · 체육관 · 던전 …)는 열거가 아니라 맵이 붙인 태그입니다(UE GameplayTag 컨테이너 모양). 장르 코드는 `hasActiveZoneTag( "dungeon" )` 로 묻습니다.
     */
    class SW_GF_API ZoneTracker
    {
    public:
        /** @brief 이 태그가 붙은 존은 들어갈 때 클리어 게이트가 잠깁니다(체육관 · 던전 · 보스 방 — 맵 데이터가 붙인다). */
        static constexpr const utf8* kClearGateTag = "clear_gate";

        ZoneTracker();

        /** @brief 존 목록과 활성 인덱스를 비웁니다. */
        void clear();

        /**
         * @brief 맵 크기로 단일 기본 존을 만들고 활성화합니다.
         * @param tagText 맵의 역할 글(`TileMap::getRole` — `<role>`)입니다. 쉼표 · 공백으로 나눈 태그 목록이고(예: "gym, clear_gate"), 비면 태그가 없습니다.
         */
        void setFromMap( string_view mapPath, string_view mapName, int32 width, int32 height, string_view tagText = "" );

        /** @brief 지정 존을 활성화합니다. */
        void activate( string_view zoneID );

        /** @brief 활성 존의 클리어 게이트를 잠그거나 풉니다. */
        void setClearGateLocked( bool bLocked );
        /** @brief 활성 존의 클리어 게이트가 잠겨 있는지 반환합니다. */
        bool isClearGateLocked() const;
        /** @brief 활성 존이 태그를 갖는지 반환합니다. */
        bool hasActiveZoneTag( const hashed_string& tag ) const;

        /** @brief 활성 존 정의를 반환합니다. 없으면 nullptr 입니다. */
        const ZoneDef* getActiveZone() const;
        /** @brief 활성 존의 ID 입니다(없으면 빈 문자열). */
        string getActiveZoneID() const;
        /** @brief 카메라 경계를 반환합니다. */
        const ZoneBounds& getCameraBounds() const;

    private:
        vector<ZoneDef> _listZone;
        int32           _activeIndex;
    };
} // namespace sw
