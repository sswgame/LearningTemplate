/**
 * @file AdventureWorldMap.h
 * @brief 야생의 숨결의 탑 · 사당 — 탑을 깨우면 그 지역 지도가 드러나고, 사당을 마치면 증표(빛의 구슬)를 받아 정한 수(넷)를 하트 그릇이나
 *        스태미나 그릇으로 바꿉니다. 깨운 탑 · 찾은 사당은 순간 이동 지점입니다.
 * @details 지도는 기반 `AreaGraph` 의 지역 공개(`discoverRegion`)입니다. 그릇 교환은 `AdventureVitals` 에 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AdventureVitals;
    class AreaGraph;
    class XmlNode;

    /** @brief 지도 위 표지의 종류입니다. */
    enum class AdventureLandmarkKind : uint8
    {
        Tower = 0,
        Shrine
    };

    /** @brief 탑 · 사당 하나입니다. */
    struct AdventureLandmarkDef
    {
        hashed_string         _id{};
        hashed_string         _region{}; ///< 탑 — 드러내는 지역 · 사당 — 놓인 지역
        AdventureLandmarkKind _kind{ AdventureLandmarkKind::Shrine };
    };
} // namespace sw

namespace sw
{
    /** @brief 증표로 바꿀 것입니다. */
    enum class AdventureOrbReward : uint8
    {
        HeartContainer = 0,
        StaminaVessel
    };

    /** @brief 증표 교환 결과입니다. */
    enum class AdventureExchangeResult : uint8
    {
        Ok = 0,
        NotEnoughOrbs,
        AtLimit ///< 하트 · 스태미나가 이미 최대 — 증표는 그대로
    };

    SW_GF_API const utf8* toString( AdventureExchangeResult result );

    /**
     * @class AdventureWorldMap
     * @brief `<AdventureWorld orbsPerExchange="4"><Tower id="plateauTower" region="Plateau"/><Shrine id="oman" region="Plateau"/></AdventureWorld>` 를 읽고
     *        깨운 탑 · 찾은 · 마친 사당 · 증표를 듭니다.
     */
    class SW_GF_API AdventureWorldMap
    {
    public:
        AdventureWorldMap();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               resetState();

        /** @brief 탑을 깨웁니다. 지역의 방을 드러내고 새로 드러난 수입니다. 탑이 아니거나 이미 깨웠으면 −1 입니다. */
        int32 activateTower( const hashed_string& towerId, AreaGraph& areaGraph );
        /** @brief 사당을 찾았습니다(순간 이동 지점). 처음이면 true 입니다. */
        bool discoverShrine( const hashed_string& shrineId );
        /** @brief 사당을 마쳤습니다. 처음이면 증표 하나를 주고 true 입니다(찾은 것도 된다). */
        bool completeShrine( const hashed_string& shrineId );
        /** @brief 증표 `_orbsPerExchange` 개를 그릇 하나로 바꿉니다. */
        AdventureExchangeResult exchangeOrbs( AdventureOrbReward reward, AdventureVitals& vitals );

        /** @brief 순간 이동할 수 있는가(깨운 탑 · 찾은 사당)입니다. */
        bool                                canWarpTo( const hashed_string& landmarkId ) const;
        bool                                isActivated( const hashed_string& landmarkId ) const;
        bool                                isCompleted( const hashed_string& landmarkId ) const;
        int32                               getOrbCount() const { return _orbCount; }
        int32                               getOrbsPerExchange() const { return _orbsPerExchange; }
        int32                               getCompletedShrineCount() const { return _completedShrineCount; }
        const AdventureLandmarkDef*         findLandmark( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<AdventureLandmarkDef>& getLandmarks() const { return _catalog.getAll(); }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );
        void   readLandmarks( const XmlNode& root, const utf8* pNodeName, AdventureLandmarkKind kind, string_view sourceName, uint32& inoutCount );

        GameCatalog<AdventureLandmarkDef> _catalog;
        vector<uint8>                     _listActivated; ///< 탑은 깨움 · 사당은 찾음(카탈로그 순서)
        vector<uint8>                     _listCompleted; ///< 사당 — 마침
        int32                             _orbCount;
        int32                             _orbsPerExchange;
        int32                             _completedShrineCount;
    };
} // namespace sw
