/**
 * @file MetroidvaniaMapState.h
 * @brief 메트로배니아 지도를 다룹니다. 방 단위 탐색률(기반 `AreaGraph`), 지역 지도를 사야 그 지역이 지도에 그려지는 규칙, 아직 가 보지 않은 방의 아이템 표시,
 *        세이브 지점과 빠른 이동 지점(열어야 쓸 수 있음), 줍기와 수집률을 관리합니다.
 * @details 할로우 나이트처럼 지도가 없으면 지나온 방도 그려지지 않고, 지도를 사면 그 지역의 방이 모두 드러난다(`AreaGraph::discoverRegion`).
 *          아이템 표시는 "지도에 그려진 방", "아직 방문하지 않은 방", "아직 줍지 않은 것"을 모두 만족할 때만 나온다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct MetroidvaniaPickupDef;

    class Archive;
    class AreaGraph;
    class MetroidvaniaCatalog;
    class Wallet;

    /** @brief 지도 사기 결과입니다. */
    enum class MetroidvaniaMapPurchase : uint8
    {
        Bought = 0,
        AlreadyOwned,
        NotEnoughCurrency,
        UnknownRegion
    };

    /**
     * @class MetroidvaniaMapState
     * @brief 지도와 수집 상태입니다. 방 그래프(`AreaGraph`)는 빌려 씁니다. 방문과 발견은 그래프가, 산 지도, 연 정거장, 주운 것은 이 클래스가 가집니다.
     */
    class SW_KIT_API MetroidvaniaMapState
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "MMAP" );
        static constexpr uint32 kStateVersion = 1;

        MetroidvaniaMapState();

        /** @brief 카탈로그와 방 그래프를 설정합니다(상태는 비웁니다). */
        void initialize( const MetroidvaniaCatalog* pCatalog, AreaGraph* pGraph );

        /** @brief 방에 들어갑니다(그래프의 방문과 이웃 발견). 처음 방문이면 true 를 반환합니다. */
        bool enterArea( const hashed_string& areaID );
        /**
         * @brief 지역 지도를 삽니다. 값만큼 @p inoutCurrency 를 줄이고 그 지역의 방을 모두 드러냅니다.
         * @details 실패하면 통화는 그대로다.
         */
        MetroidvaniaMapPurchase buyRegionMap( const hashed_string& region, Wallet& inoutWallet, const hashed_string& currency );
        bool                    hasRegionMap( const hashed_string& region ) const;
        /** @brief 방이 지도에 그려지는지 반환합니다. 그 지역 지도를 가졌고 방이 발견되었으면 true 입니다. */
        bool isShownOnMap( const hashed_string& areaID ) const;

        /** @brief 줍습니다. 처음이면 정의를 반환하고, 이미 주웠거나 모르는 것이면 nullptr 을 반환합니다(능력과 부적을 주는 일은 호출하는 쪽이 합니다). */
        const MetroidvaniaPickupDef* collectPickup( const hashed_string& pickupID );
        bool                         isCollected( const hashed_string& pickupID ) const;
        /** @brief 지도에 찍을 아이템을 카탈로그 순서로 반환합니다. 그려진 방에 있고, 아직 방문하지 않은 방이며, 아직 줍지 않은 것이어야 합니다. */
        void collectItemMarkers( vector<const MetroidvaniaPickupDef*>& outListPickup ) const;

        /** @brief 지점을 엽니다. 그 방을 방문한 적이 있어야 하며, 새로 열었으면 true 를 반환합니다. */
        bool activateSite( const hashed_string& siteID );
        bool isSiteActive( const hashed_string& siteID ) const;
        /** @brief 빠른 이동을 합니다. 두 지점 모두 빠른 이동 정거장이고 열려 있어야 하며, 서로 달라야 합니다. */
        bool canFastTravel( const hashed_string& fromSiteID, const hashed_string& toSiteID ) const;

        /** @brief 방문한 방의 비율(방문한 방 / 모든 방, 0..1)입니다. */
        float32 computeExplorationRatio() const;
        /** @brief 주운 것의 비율(주운 것 / 모든 줍는 것, 0..1)입니다. 줍는 것이 없으면 1 입니다. */
        float32 computeCollectionRatio() const;
        /** @brief 완료율(%)입니다. 방과 줍는 것을 같은 무게로 센 값입니다(0..100). */
        float32 computeCompletionPercent() const;

        /** @brief 세이브용으로 산 지도, 연 지점, 주운 것을 각각 카탈로그 순서로 반환합니다. */
        void fillSaveState( vector<hashed_string>& outListRegionMap, vector<hashed_string>& outListSite, vector<hashed_string>& outListPickup ) const;
        /** @brief 세이브에서 복원합니다(그래프의 방문과 발견은 `AreaGraph::restoreState` 로 따로 복원합니다). 지도를 산 지역은 다시 드러냅니다. */
        void restoreSaveState( const vector<hashed_string>& listRegionMap, const vector<hashed_string>& listSite, const vector<hashed_string>& listPickup );

        /** @brief 산 지도, 연 지점, 주운 것을 씁니다. 방문과 발견은 빌려 온 `AreaGraph` 의 것이고 카탈로그는 `initialize` 에서 받은 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다(지도를 산 지역은 그래프에 다시 드러냅니다). 카탈로그에 없는 id 이거나 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        const MetroidvaniaCatalog* _pCatalog;
        AreaGraph*                 _pGraph;
        vector<hashed_string>      _listRegionMap; ///< 산 지역 지도
        vector<hashed_string>      _listSite;      ///< 연 지점
        vector<hashed_string>      _listPickup;    ///< 주운 것
    };
} // namespace sw
