/**
 * @file ScavengerFacility.h
 * @brief 시설 — 씨앗으로 방 그래프(정문 · 방 · 문 · 잠긴 문 · 고리 통로 · 화재 출구)를 지어 기반 `AreaGraph` 에 담고, 고철(가치 · 무게 · 양손)을 방마다 놓습니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/World/Land/AreaGraph.h"
#include "GameFramework/Base/World/Query/GameFlags.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ScavengerMoonDef;

    class Archive;
    class GameRandom;
    class ScavengerCatalog;

    /** @brief 주울 수 있는 것 하나(고철 · 동료 시신)입니다. */
    struct ScavengerScrap
    {
        hashed_string _scrapId{};
        hashed_string _areaId{}; ///< 놓인 방(들려 있으면 비어 있다)
        float32       _weight{ 0.0f };
        int32         _uid{ 0 };
        int32         _value{ 0 };
        int32         _bodyOf{ -1 }; ///< 시신이면 그 사람 번호
        int32         _day{ -1 };    ///< 우주선에 실은 날(전멸 손실 · 기록)
        uint8         _bTwoHanded{ SW_FALSE };

        bool isBody() const { return _bodyOf >= 0; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScavengerFacility
     * @brief 위성 하나의 하루치 지도입니다. 방 id 는 `outside`(바깥) · `ship`(우주선) · `entrance`(정문 안) · `room1`.. 이고 연결 종류는
     *        `Ship` · `MainEntrance` · `Door` · `FireExit` 입니다. 잠긴 문은 `unlocked.<방>` 플래그 조건이라 `unlockDoor` 로 엽니다(열쇠 · 자물쇠 따개 — 플래그는 빌린 것, `setFlags`).
     * @details 방 n 은 앞선 방 하나에 이어 나무가 되고(모든 방이 정문에서 닿는다 — 잠금을 빼면), 확률로 고리 통로를 더하며, 정문에서 가장 먼 방들부터
     *          화재 출구가 바깥으로 납니다. 고철은 위성의 목록에서 `spawnWeight` 로 고르고, 가치 = 정의 범위 × 위성 배율 × 날씨 배율입니다.
     *          같은 씨앗 · 같은 위성이면 같은 지도 · 같은 고철입니다.
     */
    class SW_GF_API ScavengerFacility
    {
    public:
        static constexpr uint32 kStateTag      = FourCcUtil::make( "SCFC" );
        static constexpr uint32 kStateVersion  = 1;
        static constexpr uint32 kMinScrapBytes = 29; ///< 고철 하나가 쓰는 가장 적은 바이트(이름 둘 · 무게 · 번호 · 가치 · 시신 · 날 · 양손)

        ScavengerFacility();

        /** @brief 지도를 짓고 고철을 놓습니다. 회사 위성이면 바깥 · 우주선만 있습니다. 실패(지도 읽기)면 false 입니다. */
        [[nodiscard]] bool createLayout( const ScavengerCatalog& catalog, const ScavengerMoonDef& moon, uint32 seed, float32 valueScale );
        void               clear();

        /** @brief 잠긴 문(그 방으로 들어가는)을 엽니다. 잠겨 있었으면 true 입니다. */
        bool unlockDoor( const hashed_string& roomId );
        bool canTraverse( const hashed_string& fromId, const hashed_string& toId ) const;
        /** @brief 바닥에 놓습니다(새 고유 번호를 준다). 번호를 돌려줍니다. */
        int32 placeScrap( const ScavengerScrap& scrap, const hashed_string& areaId );
        /** @brief 바닥에서 집어 올립니다. 그 방에 없으면 false 입니다. */
        [[nodiscard]] bool tryTakeScrap( int32 uid, const hashed_string& areaId, ScavengerScrap& outScrap );

        const AreaGraph& getGraph() const { return _graph; }
        /** @brief 빌린 플래그입니다(빌리지 않았으면 빈 플래그). */
        const GameFlags& getFlags() const;
        /** @brief 문 잠금을 둘 플래그를 빌립니다(시설보다 오래 살아야 한다). 없으면 잠긴 문은 열리지 않는다. */
        void                          setFlags( GameFlags* pFlags ) { _pFlags = pFlags; }
        const vector<ScavengerScrap>& getGroundScrap() const { return _listGroundScrap; }
        const ScavengerScrap*         findGroundScrap( int32 uid ) const;
        /** @brief 바닥 고철 가치의 합입니다(시신 제외). */
        int32 computeGroundValue() const;
        int32 getRoomCount() const { return _roomCount; }
        /** @brief 잠긴 문 수(만들 때)입니다. */
        int32 getLockedDoorCount() const { return _lockedDoorCount; }

        /**
         * @brief 지도를 지은 위성 id · 씨앗, 바닥 고철, 빌린 플래그에 둔 잠금 해제 목록, 다음 고유 번호 · 방 수 · 잠긴 문 수를 씁니다.
         *        그래프는 위성 · 씨앗으로 다시 지으므로 싣지 않고, 빌린 플래그 자체도 싣지 않습니다(주인이 싣는다).
         */
        void writeState( Archive& outArchive ) const;
        /** @brief 지도를 다시 지을 카탈로그를 빌립니다 — 새 객체에 `readState` 하기 전에 묶는다(`createLayout` 도 묶는다). */
        void bindCatalog( const ScavengerCatalog* pCatalog ) { _pCatalog = pCatalog; }
        /** @brief `writeState` 의 바이트로 바꿉니다 — 그래프는 묶은 카탈로그의 위성으로 다시 짓습니다. 깨졌거나, 지은 지도가 있는데 카탈로그가 없거나 없는 위성이면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 고철 하나를 씁니다(운반 칸 · 우주선 · 바닥이 함께 쓴다). */
        static void writeScrap( Archive& outArchive, const ScavengerScrap& scrap );
        /** @brief `writeScrap` 의 바이트를 읽습니다. 깨졌으면 false 입니다. */
        [[nodiscard]] static bool readScrap( Archive& archive, ScavengerScrap& outScrap );

    private:
        AreaGraph               _graph;
        vector<ScavengerScrap>  _listGroundScrap;
        vector<hashed_string>   _listUnlockedFlag; ///< 이 시설이 빌린 플래그에 둔 것(하루가 끝나면 이것만 지운다)
        hashed_string           _layoutMoonId;     ///< 그래프를 지은 위성(비면 지은 적 없음 — `clear` 뒤에도 그래프와 함께 남는다)
        GameFlags*              _pFlags;           ///< 빌린 플래그
        const ScavengerCatalog* _pCatalog;         ///< 빌린 카탈로그(지도를 다시 짓는다)
        uint32                  _layoutSeed;       ///< 그래프를 지은 씨앗
        int32                   _nextUid;
        int32                   _roomCount;
        int32                   _lockedDoorCount;
    };
} // namespace sw
