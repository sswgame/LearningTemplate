/**
 * @file SpeciesData.h
 * @brief 종족 / 기술 테이블 + 파티 멤버 (game/<pack>/data/species.xml에서 로드)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 데이터 — 기술 정의, 종족 정의, 런타임 파티 멤버
    //    _name 기본값은 XML 폴백 리터럴 (번역하지 않음)
    // ------------------------------------------------------------------------------
    /** @brief 기술 한 칸 (위력·PP) */
    struct MoveDef
    {
        string _id{ "tackle" };
        string _name{ "Tackle" }; ///< 표시 이름 (strings.xml과 별개 폴백)
        int32  _power{ 40 };
        int32  _ppMax{ 35 };
    };

    /**
     * @brief 종족 한 행 (기초 스탯 + 기술 슬롯)
     * @details 기술 슬롯 수는 **데이터가 정한다**. 예전엔 `_move0` / `_move1` 두 칸 고정이라
     *          기술이 넷인 턴제 게임을 이 키트로 만들 수 없었다 — 장르 공통 뼈대가 게임 하나의
     *          스키마를 박아 두고 있던 셈이다. XML 은 `move0`, `move1`, ... 를 없을 때까지 읽는다.
     */
    struct SpeciesDef
    {
        string        _id{ "critter_a" };
        string        _name{ "Wild Critter" }; ///< 표시 이름 폴백
        int32         _baseHp{ 40 };
        int32         _baseAtk{ 10 };
        vector<int32> _listMoveIndex{ 0, 1 }; ///< MoveDef 인덱스 (슬롯 순서)
    };

    /** @brief 런타임 파티 멤버 (세이브에 들어감) */
    REFLECT()
    struct SW_GF_API PartyMember
    {
        REFLECT_BODY();

        PROPERTY()
        string _speciesId{ "critter_a" };
        PROPERTY()
        string _nickname{};
        PROPERTY()
        int32 _level{ 5 };
        PROPERTY()
        int32 _hp{ 40 };
        PROPERTY()
        int32 _hpMax{ 40 };
        /** @brief 슬롯별 잔여 PP. SpeciesDef::_listMoveIndex 와 같은 길이·같은 순서다. */
        PROPERTY()
        vector<int32> _listPp{ 35, 20 };
        PROPERTY()
        int32 _exp{ 0 };
        PROPERTY()
        int32 _expNext{ 50 };
    };

    // ------------------------------------------------------------------------------
    // 2) SpeciesCatalog — 종족 / 기술 카탈로그 인스턴스
    //    로드 실패 시 최소 폴백을 심어 전투가 비지 않게
    // ------------------------------------------------------------------------------
    /** @brief species.xml 종족·기술 테이블 서비스 */
    class SW_GF_API SpeciesCatalog
    {
    public:
        SpeciesCatalog();
        ~SpeciesCatalog();

        SpeciesCatalog( const SpeciesCatalog& )            = delete;
        SpeciesCatalog& operator=( const SpeciesCatalog& ) = delete;

        /** @brief 리소스 경로에서 기술/종족을 로드합니다. 실패 시 최소 폴백을 심습니다. */
        bool loadFromResource( string_view assetRelativePath );

        /** @brief ID로 종족 정의를 찾습니다. */
        const SpeciesDef* findSpecies( const utf8* pId ) const;
        /** @brief 인덱스로 기술 정의를 찾습니다. */
        const MoveDef* findMove( int32 index ) const;
        /** @brief ID로 기술 인덱스를 찾습니다. */
        int32 findMoveIndex( const utf8* pId ) const;

        /** @brief 종족의 슬롯 번호에 해당하는 기술을 찾습니다. 슬롯이 없으면 nullptr 입니다. */
        const MoveDef* findMoveAtSlot( const SpeciesDef& species, size_t slot ) const;

        /** @brief 야생 조우용 파티 멤버를 만듭니다. */
        PartyMember makeWild( const utf8* pSpeciesId, int32 level = 5 ) const;
        /** @brief 스타터 파티 멤버를 만듭니다. */
        PartyMember makeStarter( const utf8* pSpeciesId = "starter_a", int32 level = 5 ) const;

        /** @brief 로드된 카탈로그를 비웁니다. */
        void clear();

    private:
        void seedFallback();
        /** @brief _listMove / _listSpecies 로 id 조회 맵을 다시 만듭니다. */
        void rebuildLookup();

        vector<MoveDef>    _listMove;
        vector<SpeciesDef> _listSpecies;

        /**
         * @brief id → 행 인덱스. 벡터가 정본이고 이건 조회용이다.
         * @details 예전엔 id 조회가 벡터 선형 탐색 + string 비교였다. 형제 키트인 ActionCombat 의
         *          MonsterDataCatalog 는 같은 문제를 이미 hashed_string 맵으로 풀고 있었는데,
         *          한 프레임워크 안에서 같은 일을 두 방식으로 하고 있었다. 인덱스는 SpeciesDef 에
         *          적혀 직렬화되므로 **벡터의 자리는 그대로 두고** 맵만 곁에 둔다.
         */
        unordered_map<hashed_string, size_t> _mapMoveIndex;
        unordered_map<hashed_string, size_t> _mapSpeciesIndex;
    };
} // namespace sw
