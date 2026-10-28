/**
 * @file AdventureWorldMap.h
 * @brief 야생의 숨결의 탑과 사당 규칙입니다. 탑을 깨우면 그 지역의 지도가 드러나고, 사당을 마치면 증표(빛의 구슬)를 받습니다. 정해진 수(네 개)의 증표를 하트 그릇이나 스태미나 그릇으로 바꿀 수 있습니다.
 *        깨운 탑과 찾은 사당은 순간 이동 지점이 됩니다.
 * @details 지도는 기반 `AreaGraph` 의 지역 공개(`discoverRegion`)로 드러낸다. 그릇 교환 결과는 `AdventureVitals` 에 준다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AdventureVitals;
    class AreaGraph;
    class XMLNode;

    /** @brief 지도 위 표지의 종류입니다. */
    enum class AdventureLandmarkKind : uint8
    {
        Tower = 0,
        Shrine
    };

    /** @brief 탑이나 사당 하나입니다. 종류는 원소 이름(`<Tower>` · `<Shrine>`)입니다. */
    REFLECT()
    struct AdventureLandmarkDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        hashed_string         _region{}; ///< 탑이면 드러낼 지역(탑은 반드시 적는다), 사당이면 사당이 놓인 지역
        AdventureLandmarkKind _kind{ AdventureLandmarkKind::Shrine };
    };
} // namespace sw

namespace sw
{
    /** @brief 루트 `<AdventureWorld>` 의 속성입니다. */
    REFLECT()
    struct AdventureWorldRootDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 1 )
        int32 _orbsPerExchange{ 4 };
    };
} // namespace sw

namespace sw
{
    /** @brief 증표로 바꿀 대상입니다. */
    enum class AdventureOrbReward : uint8
    {
        HeartContainer = 0,
        StaminaVessel
    };

    /** @brief 증표 교환 결과입니다. */
    ENUM()
    enum class AdventureExchangeResult : uint8
    {
        Ok = 0,
        NotEnoughOrbs,
        AtLimit ///< 하트나 스태미나가 이미 최대라서 교환하지 않았고, 증표는 그대로다
    };

    /**
     * @class AdventureWorldMap
     * @brief 다음 형식의 XML 을 읽는 카탈로그입니다. `<AdventureWorld orbsPerExchange="4"><Tower id="plateauTower" region="Plateau"/><Shrine id="oman" region="Plateau"/></AdventureWorld>`
     *        깨운 탑, 찾은 사당, 마친 사당, 증표를 가집니다.
     */
    class SW_KIT_API AdventureWorldMap : public XMLCatalog<AdventureWorldMap>
    {
        friend class XMLCatalog<AdventureWorldMap>;

    public:
        AdventureWorldMap();

        void resetState();

        /** @brief 탑을 깨웁니다. 지역의 방을 드러내고, 새로 드러난 수를 반환합니다. 탑이 아니거나 이미 깨운 탑이면 −1 을 반환합니다. */
        int32 activateTower( const hashed_string& towerID, AreaGraph& areaGraph );
        /** @brief 사당을 찾았습니다(순간 이동 지점이 됩니다). 처음이면 true 를 반환합니다. */
        bool discoverShrine( const hashed_string& shrineID );
        /** @brief 사당을 마쳤습니다. 처음이면 증표 하나를 주고 true 를 반환합니다(찾은 것으로도 처리합니다). */
        bool completeShrine( const hashed_string& shrineID );
        /** @brief 증표 `_orbsPerExchange` 개를 그릇 하나로 바꿉니다. */
        AdventureExchangeResult exchangeOrbs( AdventureOrbReward reward, AdventureVitals& vitals );

        /** @brief 순간 이동할 수 있는지(깨운 탑이나 찾은 사당인지) 반환합니다. */
        bool                                canWarpTo( const hashed_string& landmarkID ) const;
        bool                                isActivated( const hashed_string& landmarkID ) const;
        bool                                isCompleted( const hashed_string& landmarkID ) const;
        int32                               getOrbCount() const { return _orbCount; }
        int32                               getOrbsPerExchange() const { return _orbsPerExchange; }
        int32                               getCompletedShrineCount() const { return _completedShrineCount; }
        const AdventureLandmarkDef*         findLandmark( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<AdventureLandmarkDef>& getLandmarks() const { return _catalog.getAll(); }

    private:
        static constexpr const utf8* kXMLRootName = "AdventureWorld"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           readLandmarks( const XMLNode& root, const utf8* pNodeName, AdventureLandmarkKind kind, string_view sourceName, uint32& inoutCount );

        GameCatalog<AdventureLandmarkDef> _catalog;
        vector<uint8>                     _listActivated; ///< 탑이면 깨웠는지, 사당이면 찾았는지(카탈로그 순서)
        vector<uint8>                     _listCompleted; ///< 사당을 마쳤는지
        int32                             _orbCount;
        int32                             _orbsPerExchange;
        int32                             _completedShrineCount;
    };
} // namespace sw
