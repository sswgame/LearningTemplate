/**
 * @file ScavengerFacility.h
 * @brief 시설을 구현합니다. 시드로 방 그래프(정문, 방, 문, 잠긴 문, 고리 통로, 화재 출구)를 만들어 기반 `AreaGraph` 에 담고, 고철(가치, 무게, 양손)을 방마다 놓습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"
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

    /** @brief 주울 수 있는 것 하나(고철 또는 동료 시신)입니다. */
    struct ScavengerScrap
    {
        hashed_string _scrapID{};
        hashed_string _areaID{}; ///< 놓인 방이다(들려 있으면 비어 있다)
        float32       _weight{ 0.0f };
        int32         _uid{ 0 };
        int32         _value{ 0 };
        int32         _bodyOf{ -1 }; ///< 시신이면 그 사람의 번호이다
        int32         _day{ -1 };    ///< 우주선에 실은 날이다(전멸 손실과 기록에 쓴다)
        uint8         _bTwoHanded{ SW_FALSE };

        bool isBody() const { return _bodyOf >= 0; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScavengerFacility
     * @brief 위성 하나의 하루치 지도입니다. 방 id 는 `outside`(바깥), `ship`(우주선), `entrance`(정문 안), `room1`.. 이고 연결 종류는
     *        `Ship`, `MainEntrance`, `Door`, `FireExit` 입니다. 잠긴 문은 `unlocked.<방>` 플래그 조건이라 `unlockDoor` 로 엽니다(열쇠와 자물쇠 따개를 쓰며, 플래그는 빌린 것이고 `setFlags` 로 정합니다).
     * @details 방 n 은 앞선 방 하나에 이어 붙어 나무가 되고(잠금을 빼면 모든 방이 정문에서 닿는다), 확률로 고리 통로를 더하며, 정문에서 가장 먼 방부터
     *          화재 출구가 바깥으로 난다. 고철은 위성의 목록에서 `spawnWeight` 로 고르고, 가치는 정의 범위 x 위성 배율 x 날씨 배율이다.
     *          같은 시드와 같은 위성이면 같은 지도와 같은 고철이 나온다.
     */
    class SW_KIT_API ScavengerFacility
    {
    public:
        static constexpr uint32 kStateTag      = FourCcUtil::make( "SCFC" );
        static constexpr uint32 kStateVersion  = 1;
        static constexpr uint32 kMinScrapBytes = 29; ///< 고철 하나가 차지하는 최소 바이트 수(이름 둘, 무게, 번호, 가치, 시신, 날, 양손)

        ScavengerFacility();

        /** @brief 지도를 만들고 고철을 놓습니다. 회사 위성이면 바깥과 우주선만 있습니다. 지도를 읽다 실패하면 false 를 반환합니다. */
        [[nodiscard]] bool createLayout( const ScavengerCatalog& catalog, const ScavengerMoonDef& moon, uint32 seed, float32 valueScale );
        void               clear();

        /** @brief 잠긴 문(그 방으로 들어가는 문)을 엽니다. 잠겨 있었으면 true 를 반환합니다. */
        bool unlockDoor( const hashed_string& roomID );
        bool canTraverse( const hashed_string& fromID, const hashed_string& toID ) const;
        /** @brief 바닥에 놓고 새 고유 번호를 반환합니다. */
        int32 placeScrap( const ScavengerScrap& scrap, const hashed_string& areaID );
        /** @brief 바닥에서 집어 올립니다. 그 방에 없으면 false 를 반환합니다. */
        [[nodiscard]] bool tryTakeScrap( int32 uid, const hashed_string& areaID, ScavengerScrap& outScrap );

        const AreaGraph& getGraph() const { return _graph; }
        /** @brief 빌린 플래그를 반환합니다(빌리지 않았으면 빈 플래그입니다). */
        const GameFlags& getFlags() const;
        /** @brief 문 잠금을 둘 플래그를 빌립니다. 시설보다 오래 살아야 하며, 없으면 잠긴 문은 열리지 않습니다. */
        void                          setFlags( GameFlags* pFlags ) { _pFlags = pFlags; }
        const vector<ScavengerScrap>& getGroundScrap() const { return _listGroundScrap; }
        const ScavengerScrap*         findGroundScrap( int32 uid ) const;
        /** @brief 바닥 고철 가치의 합을 반환합니다(시신은 제외합니다). */
        int32 computeGroundValue() const;
        int32 getRoomCount() const { return _roomCount; }
        /** @brief 만들 때 정해진 잠긴 문 수를 반환합니다. */
        int32 getLockedDoorCount() const { return _lockedDoorCount; }

        /**
         * @brief 지도를 만든 위성 id 와 시드, 바닥 고철, 빌린 플래그에 둔 잠금 해제 목록, 다음 고유 번호, 방 수, 잠긴 문 수를 씁니다.
         *        그래프는 위성과 시드로 다시 만들 수 있으므로 저장하지 않고, 빌린 플래그 자체도 저장하지 않습니다(주인이 저장합니다).
         */
        void writeState( Archive& outArchive ) const;
        /** @brief 지도를 다시 만들 카탈로그를 빌립니다. 새 객체에 `readState` 하기 전에 호출해야 합니다(`createLayout` 도 이 값을 설정합니다). */
        void bindCatalog( const ScavengerCatalog* pCatalog ) { _pCatalog = pCatalog; }
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 그래프는 빌린 카탈로그의 위성으로 다시 만듭니다. 데이터가 깨졌거나, 만든 지도가 있는데 카탈로그가 없거나 없는 위성이면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 고철 하나를 씁니다(운반 슬롯, 우주선, 바닥이 함께 씁니다). */
        static void writeScrap( Archive& outArchive, const ScavengerScrap& scrap );
        /** @brief `writeScrap` 이 쓴 바이트를 읽습니다. 데이터가 깨졌으면 false 를 반환합니다. */
        [[nodiscard]] static bool readScrap( Archive& archive, ScavengerScrap& outScrap );

    private:
        AreaGraph               _graph;
        vector<ScavengerScrap>  _listGroundScrap;
        vector<hashed_string>   _listUnlockedFlag; ///< 이 시설이 빌린 플래그에 둔 것이다(하루가 끝나면 이것만 지운다)
        hashed_string           _layoutMoonID;     ///< 그래프를 만든 위성이다(비어 있으면 만든 적이 없다. `clear` 뒤에도 그래프와 함께 남는다)
        GameFlags*              _pFlags;           ///< 빌린 플래그
        const ScavengerCatalog* _pCatalog;         ///< 빌린 카탈로그이다(지도를 다시 만들 때 쓴다)
        uint32                  _layoutSeed;       ///< 그래프를 만든 시드
        int32                   _nextUid;
        int32                   _roomCount;
        int32                   _lockedDoorCount;
    };
} // namespace sw
