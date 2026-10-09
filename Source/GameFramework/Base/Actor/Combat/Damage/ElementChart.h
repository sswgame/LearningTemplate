/**
 * @file ElementChart.h
 * @brief 속성 상성표 — 공격 속성 × 방어 속성의 피해 배율(복합 타입은 곱), 면역(0), 속성마다의 상태이상 부여 확률입니다.
 * @details 포켓몬(타입 상성 · 복합 타입 · 화상 확률), 위쳐(기름 · 표식), 젤다(불 · 얼음 · 번개), 소울라이크(속성 내성)가 같은 표를 씁니다.
 *          같은 속성 보너스(포켓몬 STAB — 기술 속성이 쓰는 쪽의 타입과 같으면 1.5 배)는 표가 아니라 쓰는 쪽의 속성이 필요하므로 키트가 곱합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameRandom;
    class XmlNode;

    /** @brief 속성 하나가 거는 상태이상 하나의 확률입니다. */
    struct ElementStatusChance
    {
        hashed_string _element{};
        hashed_string _status{};
        float32       _chance{ 0.0f }; ///< 0..1
    };
} // namespace sw

namespace sw
{
    /**
     * @class ElementChart
     * @brief `<ElementChart><Element id="Fire"/>...<Rule attack="Water" defend="Fire" multiplier="2"/>...<Status element="Fire" status="Burn" chance="0.1"/></ElementChart>`
     *        를 읽습니다. 규칙이 없는 쌍은 1 배입니다. 선언하지 않은 속성을 쓰는 규칙 · 상태이상은 경고하고 버립니다(오타를 잡는다).
     */
    class SW_GF_API ElementChart : public XmlCatalog<ElementChart>
    {
        friend class XmlCatalog<ElementChart>;

    public:
        ElementChart();

        void clear();

        void addElement( const hashed_string& element );
        bool hasElement( const hashed_string& element ) const;
        /** @brief 쌍의 배율을 둡니다(같은 쌍이면 바꾼다). 음수는 0 으로 자릅니다. */
        void setMultiplier( const hashed_string& attack, const hashed_string& defend, float32 multiplier );
        /** @brief 상태이상 확률을 더합니다(한 속성에 여럿 — 적은 순서대로 굴린다). */
        void addStatusChance( const ElementStatusChance& statusChance );

        /** @brief 쌍 하나의 배율입니다. 규칙이 없으면 1 입니다. */
        float32 getMultiplier( const hashed_string& attack, const hashed_string& defend ) const;
        /** @brief 방어 속성 모두의 배율을 곱합니다(복합 타입). 방어 속성이 없으면 1 입니다. */
        float32 computeMultiplier( const hashed_string& attack, const vector<hashed_string>& listDefend ) const;
        /** @brief 곱이 0 인가(한 방어 속성이라도 면역)입니다. */
        bool isImmune( const hashed_string& attack, const vector<hashed_string>& listDefend ) const;
        /**
         * @brief @p element 의 상태이상을 굴립니다. 적은 순서대로 하나씩 굴려 처음 걸린 것을 돌려주고, 없으면 빈 이름입니다.
         * @details 항목마다 난수를 하나씩 씁니다(걸리면 거기서 멈춘다) — 씨앗이 같으면 같은 결과입니다.
         */
        hashed_string rollStatus( const hashed_string& element, GameRandom& random ) const;

        const vector<hashed_string>&       getElements() const { return _listElement; }
        const vector<ElementStatusChance>& getStatusChances() const { return _listStatusChance; }

    private:
        static constexpr const utf8* kXmlRootName = "ElementChart"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        vector<hashed_string>                                               _listElement;      ///< 읽은 순서
        unordered_map<hashed_string, unordered_map<hashed_string, float32>> _mapRule;          ///< 공격 → 방어 → 배율
        vector<ElementStatusChance>                                         _listStatusChance; ///< 적은 순서
    };
} // namespace sw
