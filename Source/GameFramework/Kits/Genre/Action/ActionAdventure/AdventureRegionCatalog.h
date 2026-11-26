/**
 * @file AdventureRegionCatalog.h
 * @brief 액션 어드벤처 한 판의 지역 표(`data/regions.xml`)입니다 — 지역마다 평면 위 사각 경계, 쓰러졌을 때 일어설 자리, 쉬는 자리(야영지), 불 켤 봉화, 전투(홀 · 보스 배치), 들어서면 받는 퀘스트.
 * @details 지역은 겹치지 않는다(읽을 때 거절한다). 한 자리는 한 지역이다. 확장 팩(DLC)은 이 표에 덮어쓰기 층으로 지역을 더한다(`<Layer>` — `datacataloginheritance.xml` 의 `Region` 열쇠 `id`).
 *          전투 배치의 종 id 는 전투를 도는 쪽(게임 — 몬스터 표를 아는 키트를 섞는다)의 종 id 이고, 그쪽이 판을 열 때 모두 있는지 본다. 이 키트는 종의 뜻을 모른다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /** @brief 지역 안의 이름 있는 자리 하나(야영지 · 봉화)입니다. id 가 비면 없는 자리입니다. */
    struct AdventureSiteDef
    {
        hashed_string _id{};
        float2        _position{ 0.0f, 0.0f };

        bool isSet() const { return _id.empty() == false; }
    };

    /** @brief 지역 전투에 세울 적 하나입니다. 종 id 와 평면 위 자리(m)입니다. */
    struct AdventureSpawnDef
    {
        hashed_string _monsterID{};
        float2        _position{ 0.0f, 0.0f };
    };

    /** @brief 지역 하나입니다(`<Region>`). */
    struct AdventureRegionDef
    {
        hashed_string             _id{};
        string                    _name{};
        float2                    _min{ 0.0f, 0.0f }; ///< 평면(x, z) 경계의 작은 쪽(포함)
        float2                    _max{ 0.0f, 0.0f }; ///< 큰 쪽(포함)
        float2                    _respawn{ 0.0f, 0.0f };
        AdventureSiteDef          _camp{};   ///< 쉬면 체력을 채우고 체크포인트로 저장한다
        AdventureSiteDef          _beacon{}; ///< 불을 켜면 지도에 드러난다(퀘스트 목표 `Light`)
        vector<AdventureSpawnDef> _listHallSpawn{};
        vector<AdventureSpawnDef> _listBossSpawn{};
        vector<hashed_string>     _listStartQuest{}; ///< 들어설 때마다 받기를 시도하는 퀘스트(조건이 맞을 때 받는다)

        bool contains( const float2& position ) const { return position._x >= _min._x && position._x <= _max._x && position._y >= _min._y && position._y <= _max._y; }
        bool hasEncounter() const { return _listHallSpawn.empty() == false || _listBossSpawn.empty() == false; }
    };

    /** @brief `<Region id name min max respawn startQuest>` 속성의 스키마입니다(`XMLFields::readProperties`). 이름이 비면 id 를 쓴다. */
    REFLECT()
    struct AdventureRegionAttributeDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id;
        PROPERTY()
        string _name; ///< 보이는 이름(비면 id)
        PROPERTY( Required )
        float2 _min{}; ///< 평면(x, z) 경계의 작은 쪽 — `x,z`
        PROPERTY( Required )
        float2 _max{};
        PROPERTY( Required )
        float2 _respawn{};
        PROPERTY()
        string _startQuest; ///< 받기를 시도하는 퀘스트 id 목록(`a, b`)
    };

    /** @brief `<Camp id at/>` · `<Beacon id at/>` 의 스키마입니다. */
    REFLECT()
    struct AdventureSiteAttributeDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id;
        PROPERTY( Required )
        float2 _at{};
    };

    /** @brief `<Hall monster at/>` · `<Boss monster at/>` 의 스키마입니다. */
    REFLECT()
    struct AdventureSpawnAttributeDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _monster;
        PROPERTY( Required )
        float2 _at{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class AdventureRegionCatalog
     * @brief `<AdventureRegionCatalog><Region id min max respawn startQuest><Camp id at/><Beacon id at/><Hall monster at/><Boss monster at/></Region></AdventureRegionCatalog>` 를 읽습니다.
     * @details 경계 · 일어설 자리는 필수이고 일어설 자리는 경계 안이어야 한다. 야영지 · 봉화 · 전투 자리도 경계 안이다. 틀린 지역은 넣지 않고 오류다.
     */
    class SW_KIT_API AdventureRegionCatalog : public GameCatalog<AdventureRegionDef, XMLCatalog<AdventureRegionCatalog>>
    {
        friend class XMLCatalog<AdventureRegionCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(게임 기능 `ActionAdventure` 가 미니게임 범위에 팩의 `data/regions.xml` 을 건다). */
        static constexpr const utf8*  kDataKind  = "adventureregions";
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( kDataKind, AssetPathRule{ "game/", {}, "/data/regions.xml" }, {} );

        /** @brief @p position 을 품은 지역입니다. 없으면 nullptr 입니다(지역은 겹치지 않는다). */
        const AdventureRegionDef* findAt( const float2& position ) const;

    private:
        static constexpr const utf8* kXMLRootName = "AdventureRegionCatalog";
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadRegion( const XMLNode& node, string_view sourceName );
    };

    /** @brief 이 종류의 공유 캐시입니다 — 인스턴스화는 `AdventureRegionCatalog.cpp` 의 정의 줄이 한다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, AdventureRegionCatalog );
} // namespace sw
