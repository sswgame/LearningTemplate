/**
 * @file ElementChart.h
 * @brief 속성 상성 테이블입니다. 공격 속성과 방어 속성의 피해 배율(복합 타입은 곱), 면역(0), 속성마다의 상태이상 부여 확률을 담습니다.
 * @details 포켓몬(타입 상성, 복합 타입, 화상 확률), 위쳐(기름, 표식), 젤다(불, 얼음, 번개), 소울라이크(속성 내성)가 같은 테이블을 씁니다.
 *          같은 속성 보너스(포켓몬 STAB: 기술 속성이 쓰는 쪽의 타입과 같으면 1.5 배)는 쓰는 쪽의 속성이 필요하므로 테이블이 아니라 키트가 곱합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class GameRandom;
    class XMLNode;

    /** @brief 속성 하나가 거는 상태이상 하나의 확률입니다. */
    REFLECT()
    struct SW_GF_API ElementStatusChance
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _element{};
        PROPERTY( Required )
        hashed_string _status{};
        PROPERTY( Required, Min = 0.0, Max = 1.0, Units = ratio )
        float32 _chance{ 0.0f }; ///< 0..1
    };

    /** @brief 속성 선언(`<Element id/>`)입니다. */
    REFLECT()
    struct SW_GF_API ElementDeclParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
    };

    /** @brief 상성 규칙(`<Rule attack defend multiplier/>`)입니다. 규칙 줄은 배율을 적으려고 있는 것이다 — 빠진 배율을 1 로 두지 않는다. */
    REFLECT()
    struct SW_GF_API ElementRuleParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _attack{};
        PROPERTY( Required )
        hashed_string _defend{};
        PROPERTY( Required, Min = 0.0 )
        float32 _multiplier{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ElementChart
     * @brief `<ElementChart><Element id="Fire"/>...<Rule attack="Water" defend="Fire" multiplier="2"/>...<Status element="Fire" status="Burn" chance="0.1"/></ElementChart>`
     *        를 읽습니다. 규칙이 없는 쌍은 1 배입니다. 선언하지 않은 속성을 쓰는 규칙 · 상태이상은 경고하고 버립니다(오타를 잡는다).
     */
    class SW_GF_API ElementChart : public XMLCatalog<ElementChart>
    {
        friend class XMLCatalog<ElementChart>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<ElementChart>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".elementchart.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `ElementChart.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "ElementChart", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        ElementChart();

        void clear();

        void addElement( const hashed_string& element );
        bool hasElement( const hashed_string& element ) const;
        /** @brief 쌍의 배율을 둡니다(같은 쌍이면 바꾼다). 음수이면 오류를 남기고 false 입니다(0 으로 자르지 않습니다). */
        [[nodiscard]] bool setMultiplier( const hashed_string& attack, const hashed_string& defend, float32 multiplier );
        /** @brief 상태이상 확률을 더합니다. 한 속성에 여럿을 둘 수 있고, 적은 순서대로 굴립니다. */
        void addStatusChance( const ElementStatusChance& statusChance );

        /** @brief 쌍 하나의 배율입니다. 규칙이 없으면 1 입니다. */
        float32 getMultiplier( const hashed_string& attack, const hashed_string& defend ) const;
        /** @brief 방어 속성 모두의 배율을 곱합니다(복합 타입). 방어 속성이 없으면 1 입니다. */
        float32 computeMultiplier( const hashed_string& attack, const vector<hashed_string>& listDefend ) const;
        /** @brief 곱이 0 인가(한 방어 속성이라도 면역)입니다. */
        bool isImmune( const hashed_string& attack, const vector<hashed_string>& listDefend ) const;
        /**
         * @brief @p element 의 상태이상을 굴립니다. 적은 순서대로 하나씩 굴려 처음 걸린 것을 돌려주고, 없으면 빈 이름입니다.
         * @details 항목마다 난수를 하나씩 쓰고, 걸리면 거기서 멈춥니다. 시드가 같으면 같은 결과입니다.
         */
        hashed_string rollStatus( const hashed_string& element, GameRandom& random ) const;

        const vector<hashed_string>&       getElements() const { return _listElement; }
        const vector<ElementStatusChance>& getStatusChances() const { return _listStatusChance; }

    private:
        static constexpr const utf8* kXMLRootName = "ElementChart"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadElement( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadRule( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadStatus( const XMLNode& node, string_view sourceName );

    private:
        vector<hashed_string>                                               _listElement;      ///< 읽은 순서
        unordered_map<hashed_string, unordered_map<hashed_string, float32>> _mapRule;          ///< 공격 → 방어 → 배율
        vector<ElementStatusChance>                                         _listStatusChance; ///< 적은 순서
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<ElementChart>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_GF_API_EXTERN_TEMPLATE, ElementChart );
} // namespace sw
