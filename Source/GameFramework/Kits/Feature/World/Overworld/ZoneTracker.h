/**
 * @file ZoneTracker.h
 * @brief 맵이 붙인 태그, 카메라 경계, 클리어 게이트로 활성 존(룸 개념)을 고릅니다.
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
        int2 _min{ 0, 0 }; ///< 정수 AABB 이다. float 버전은 AABB2D 가 같은 구조로 제공한다
        int2 _max{ 0, 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 존의 ID, 경계, 태그, 클리어 게이트입니다. */
    struct SW_KIT_API ZoneDef
    {
        string                 _id; ///< 안정적인 존 ID 이다. 맵 이름이나 경로를 쓴다
        ZoneBounds             _bounds;
        vector<hashed_string>  _listTag;              ///< 맵 데이터가 붙인 태그이다(예: "gym", "indoors", "no_encounter"). 대소문자를 가리지 않는다
        uint8                  _bClearGateLocked : 1; ///< 잠기면 워프를 막는다
        [[maybe_unused]] uint8 _reserved         : 7;

        /** @brief 게이트가 해제되고 태그가 없는 상태로 시작합니다. */
        ZoneDef();

        /** @brief 태그가 있는지 반환합니다. */
        bool hasTag( const hashed_string& tag ) const;
        /** @brief 태그를 추가합니다. 빈 이름이거나 이미 있는 태그는 무시합니다. */
        void addTag( const hashed_string& tag );
    };
} // namespace sw

namespace sw
{
    /**
     * @class ZoneTracker
     * @brief 런타임 존 상태(경계, 태그, 클리어 게이트)를 관리합니다. 맵 하나가 기본 존 하나가 됩니다(`setFromMap`).
     * @details 존이 마을, 체육관, 던전 중 무엇인지는 열거형이 아니라 맵이 붙인 태그로 구분한다. UE 의 GameplayTag 컨테이너와 비슷한 구조이다.
     *          장르 코드는 `hasActiveZoneTag( "dungeon" )` 으로 확인한다.
     */
    class SW_KIT_API ZoneTracker
    {
    public:
        /** @brief 이 태그가 붙은 존은 들어갈 때 클리어 게이트가 잠깁니다. 체육관, 던전, 보스 방이 해당하며 맵 데이터가 태그를 붙입니다. */
        static constexpr const utf8* kClearGateTag = "clear_gate";

        ZoneTracker();

        /** @brief 존 목록과 활성 인덱스를 비웁니다. */
        void clear();

        /**
         * @brief 맵 크기로 단일 기본 존을 만들고 활성화합니다.
         * @param tagText 맵의 역할 문자열(`TileMap::getRole`, `<role>`)입니다. 쉼표나 공백으로 나눈 태그 목록이며(예: "gym, clear_gate") 비면 태그가 없습니다.
         */
        void setFromMap( string_view mapPath, string_view mapName, int32 width, int32 height, string_view tagText = "" );

        /** @brief 지정 존을 활성화합니다. */
        void activate( string_view zoneID );

        /** @brief 활성 존의 클리어 게이트를 잠그거나 해제합니다. */
        void setClearGateLocked( bool bLocked );
        /** @brief 활성 존의 클리어 게이트가 잠겨 있는지 반환합니다. */
        bool isClearGateLocked() const;
        /** @brief 활성 존이 태그를 갖는지 반환합니다. */
        bool hasActiveZoneTag( const hashed_string& tag ) const;

        /** @brief 활성 존 정의를 반환합니다. 없으면 nullptr 입니다. */
        const ZoneDef* getActiveZone() const;
        /** @brief 활성 존의 ID 입니다. 없으면 빈 문자열입니다. */
        string getActiveZoneID() const;
        /** @brief 카메라 경계를 반환합니다. */
        const ZoneBounds& getCameraBounds() const;

    private:
        vector<ZoneDef> _listZone;
        int32           _activeIndex;
    };
} // namespace sw
