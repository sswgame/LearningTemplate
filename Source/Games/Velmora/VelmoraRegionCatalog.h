/**
 * @file VelmoraRegionCatalog.h
 * @brief 벨모라 골짜기의 지역 표(`data/regions.xml`)입니다 — 지역마다 평면 위 사각 경계, 쓰러졌을 때 일어설 자리, 쉬는 자리(야영지), 불 켤 봉화, 전투(홀 · 보스 배치), 들어서면 받는 퀘스트.
 * @details 지역은 겹치지 않는다(읽을 때 거절한다) — 한 자리는 한 지역이다. 확장 팩(DLC)은 이 표에 덮어쓰기 층으로 지역을 더한다(`<Layer>` — `datacataloginheritance.xml` 의 `Region` 열쇠 `id`).
 *          전투 배치의 종 id 는 몬스터 표(`MonsterCatalog`)의 id 이고, 판을 여는 디렉터가 모두 있는지 본다(없으면 판을 열지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Rule/ActionRoom.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class XMLNode;

    /** @brief 지역 안의 이름 있는 자리 하나(야영지 · 봉화)입니다. id 가 비면 없는 자리입니다. */
    struct VelmoraSiteDef
    {
        hashed_string _id{};
        float2        _position{ 0.0f, 0.0f };

        bool isSet() const { return _id.empty() == false; }
    };

    /** @brief 지역 하나입니다(`<Region>`). */
    struct VelmoraRegionDef
    {
        hashed_string              _id{};
        string                     _name{};
        float2                     _min{ 0.0f, 0.0f }; ///< 평면(x, z) 경계의 작은 쪽(포함)
        float2                     _max{ 0.0f, 0.0f }; ///< 큰 쪽(포함)
        float2                     _respawn{ 0.0f, 0.0f };
        VelmoraSiteDef             _camp{};   ///< 쉬면 체력을 채우고 체크포인트로 저장한다
        VelmoraSiteDef             _beacon{}; ///< 불을 켜면 지도에 드러난다(퀘스트 목표 `Light`)
        vector<ActionRoomSpawnDef> _listHallSpawn{};
        vector<ActionRoomSpawnDef> _listBossSpawn{};
        vector<hashed_string>      _listStartQuest{}; ///< 들어설 때마다 받기를 시도하는 퀘스트(조건이 맞을 때 받는다)

        bool contains( const float2& position ) const { return position._x >= _min._x && position._x <= _max._x && position._y >= _min._y && position._y <= _max._y; }
        bool hasEncounter() const { return _listHallSpawn.empty() == false || _listBossSpawn.empty() == false; }
        /** @brief 이 지역의 전투 배치입니다(액션 룸에 그대로 넘긴다). */
        ActionRoomLayout makeLayout() const;
    };

    /** @brief `<Region id name min max respawn startQuest>` 속성의 스키마입니다(`XMLFields::readFields`). 이름이 비면 id 를 쓴다. */
    REFLECT()
    struct VelmoraRegionAttributeDef
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
    struct VelmoraSiteAttributeDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id;
        PROPERTY( Required )
        float2 _at{};
    };

    /** @brief `<Hall monster at/>` · `<Boss monster at/>` 의 스키마입니다. */
    REFLECT()
    struct VelmoraSpawnAttributeDef
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
     * @class VelmoraRegionCatalog
     * @brief `<VelmoraRegionCatalog><Region id min max respawn startQuest><Camp id at/><Beacon id at/><Hall monster at/><Boss monster at/></Region></VelmoraRegionCatalog>` 를 읽습니다.
     * @details 경계 · 일어설 자리는 필수이고 일어설 자리는 경계 안이어야 한다. 야영지 · 봉화 · 전투 자리도 경계 안이다. 틀린 지역은 넣지 않고 오류다.
     */
    class VelmoraRegionCatalog : public GameCatalog<VelmoraRegionDef, XMLCatalog<VelmoraRegionCatalog>>
    {
        friend class XMLCatalog<VelmoraRegionCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(게임 인스턴스가 `loadRequiredCatalog` 로 팩의 `data/regions.xml` 을 건다). */
        static constexpr const utf8*  kDataKind  = "velmoraregions";
        static constexpr AssetTypeDef kAssetType = AssetTypeDef::makeDataFile( kDataKind, AssetPathRule{ "game/", {}, "/data/regions.xml" }, {} );

        /** @brief @p position 을 품은 지역입니다. 없으면 nullptr 입니다(지역은 겹치지 않는다). */
        const VelmoraRegionDef* findAt( const float2& position ) const;

    private:
        static constexpr const utf8* kXMLRootName = "VelmoraRegionCatalog";
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           loadRegion( const XMLNode& node, string_view sourceName );
    };
} // namespace sw
