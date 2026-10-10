/**
 * @file MetroMapState.h
 * @brief 메트로배니아 지도 — 방 단위 탐색률(기반 `AreaGraph`), 지역 지도를 사야 그 지역이 지도에 그려지는 규칙, 아직 가 보지 않은 방의 아이템 표시,
 *        세이브 지점 · 빠른 이동 지점(열어야 쓴다), 줍기 · 수집률입니다.
 * @details 할로우 나이트처럼 지도가 없으면 지나온 방도 그려지지 않고, 지도를 사면 그 지역의 방이 모두 드러납니다(`AreaGraph::discoverRegion`).
 *          아이템 표시는 "지도에 그려진 방 · 아직 방문하지 않은 방 · 아직 줍지 않은 것" 셋을 모두 만족할 때만입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct MetroPickupDef;

    class Archive;
    class AreaGraph;
    class MetroidvaniaCatalog;
    class Wallet;

    /** @brief 지도 사기 결과입니다. */
    enum class MetroMapPurchase : uint8
    {
        Bought = 0,
        AlreadyOwned,
        NotEnoughCurrency,
        UnknownRegion
    };

    /**
     * @class MetroMapState
     * @brief 지도 · 수집 상태입니다. 방 그래프(`AreaGraph`)는 빌려 씁니다 — 방문 · 발견은 그래프가, 산 지도 · 연 정거장 · 주운 것은 여기가 듭니다.
     */
    class SW_GF_API MetroMapState
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "MMAP" );
        static constexpr uint32 kStateVersion = 1;

        MetroMapState();

        /** @brief 카탈로그와 방 그래프를 둡니다(상태는 비운다). */
        void initialize( const MetroidvaniaCatalog* pCatalog, AreaGraph* pGraph );

        /** @brief 방에 들어갑니다(그래프의 방문 · 이웃 발견). 처음 방문이면 true 입니다. */
        bool enterArea( const hashed_string& areaID );
        /**
         * @brief 지역 지도를 삽니다 — 값만큼 @p inoutCurrency 를 줄이고 그 지역의 방을 모두 드러냅니다.
         * @details 실패하면 통화는 그대로입니다.
         */
        MetroMapPurchase buyRegionMap( const hashed_string& region, Wallet& inoutWallet, const hashed_string& currency );
        bool             hasRegionMap( const hashed_string& region ) const;
        /** @brief 방이 지도에 그려지는가 — 그 지역 지도를 가졌고 방이 발견되었다입니다. */
        bool isShownOnMap( const hashed_string& areaID ) const;

        /** @brief 줍습니다. 처음이면 정의를, 이미 주웠거나 모르는 것이면 nullptr 입니다(능력 · 부적 주기는 부르는 쪽이 한다). */
        const MetroPickupDef* collectPickup( const hashed_string& pickupID );
        bool                  isCollected( const hashed_string& pickupID ) const;
        /** @brief 지도에 찍을 아이템 — 그려진 방, 아직 방문하지 않은 방, 아직 줍지 않은 것(카탈로그 순서)입니다. */
        void collectItemMarkers( vector<const MetroPickupDef*>& outListPickup ) const;

        /** @brief 지점을 엽니다(그 방을 방문한 적이 있어야 한다). 새로 열었으면 true 입니다. */
        bool activateSite( const hashed_string& siteID );
        bool isSiteActive( const hashed_string& siteID ) const;
        /** @brief 빠른 이동 — 두 지점 모두 빠른 이동 정거장이고 열려 있어야 하고 서로 달라야 합니다. */
        bool canFastTravel( const hashed_string& fromSiteID, const hashed_string& toSiteID ) const;

        /** @brief 방문한 방 / 모든 방(0..1)입니다. */
        float32 computeExplorationRatio() const;
        /** @brief 주운 것 / 모든 줍는 것(0..1)입니다. 없으면 1 입니다. */
        float32 computeCollectionRatio() const;
        /** @brief 완료율(%) — 방과 줍는 것을 같은 무게로 센 것입니다(0..100). */
        float32 computeCompletionPercent() const;

        /** @brief 세이브용 — 산 지도 · 연 지점 · 주운 것(각 카탈로그 순서)입니다. */
        void fillSaveState( vector<hashed_string>& outListRegionMap, vector<hashed_string>& outListSite, vector<hashed_string>& outListPickup ) const;
        /** @brief 세이브에서 되살립니다(그래프의 방문 · 발견은 `AreaGraph::restoreState` 로 따로). 지도를 산 지역은 다시 드러냅니다. */
        void restoreSaveState( const vector<hashed_string>& listRegionMap, const vector<hashed_string>& listSite, const vector<hashed_string>& listPickup );

        /** @brief 산 지도 · 연 지점 · 주운 것을 씁니다. 방문 · 발견은 빌린 `AreaGraph` 의 것, 카탈로그는 `initialize` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다(지도를 산 지역은 그래프에 다시 드러낸다). 카탈로그에 없는 id 거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        static bool contains( const vector<hashed_string>& listID, const hashed_string& id );

        const MetroidvaniaCatalog* _pCatalog;
        AreaGraph*                 _pGraph;
        vector<hashed_string>      _listRegionMap; ///< 산 지역 지도
        vector<hashed_string>      _listSite;      ///< 연 지점
        vector<hashed_string>      _listPickup;    ///< 주운 것
    };
} // namespace sw
