/**
 * @file ScavengerFacility.h
 * @brief 시설 — 씨앗으로 방 그래프(정문 · 방 · 문 · 잠긴 문 · 고리 통로 · 화재 출구)를 지어 기반 `AreaGraph` 에 담고, 고철(가치 · 무게 · 양손)을 방마다 놓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/World/AreaGraph.h"
#include "GameFramework/Base/World/GameFlags.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ScavengerMoonDef;

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
     *        `Ship` · `MainEntrance` · `Door` · `FireExit` 입니다. 잠긴 문은 `unlocked_<방>` 플래그 조건이라 `unlockDoor` 로 엽니다(열쇠 · 자물쇠 따개).
     * @details 방 n 은 앞선 방 하나에 이어 나무가 되고(모든 방이 정문에서 닿는다 — 잠금을 빼면), 확률로 고리 통로를 더하며, 정문에서 가장 먼 방들부터
     *          화재 출구가 바깥으로 납니다. 고철은 위성의 목록에서 `spawnWeight` 로 고르고, 가치 = 정의 범위 × 위성 배율 × 날씨 배율입니다.
     *          같은 씨앗 · 같은 위성이면 같은 지도 · 같은 고철입니다.
     */
    class SW_GF_API ScavengerFacility
    {
    public:
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

        const AreaGraph&              getGraph() const { return _graph; }
        const GameFlags&              getFlags() const { return _flags; }
        const vector<ScavengerScrap>& getGroundScrap() const { return _listGroundScrap; }
        const ScavengerScrap*         findGroundScrap( int32 uid ) const;
        /** @brief 바닥 고철 가치의 합입니다(시신 제외). */
        int32 computeGroundValue() const;
        int32 getRoomCount() const { return _roomCount; }
        /** @brief 잠긴 문 수(만들 때)입니다. */
        int32 getLockedDoorCount() const { return _lockedDoorCount; }

    private:
        AreaGraph              _graph;
        GameFlags              _flags;
        vector<ScavengerScrap> _listGroundScrap;
        int32                  _nextUid;
        int32                  _roomCount;
        int32                  _lockedDoorCount;
    };
} // namespace sw
