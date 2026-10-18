/**
 * @file CreatureLifeCatalog.h
 * @brief 생물 생활 정의를 담습니다. 서식지 레시피(셀 패턴), 생물 종(좋아하는 서식지, 음식, 선물, 시간대, 날씨, 능력, 부탁), 능력(셀 변환 규칙), 마을 매력도 단계를 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCondition.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 서식지 패턴의 셀 하나입니다. */
    struct HabitatCell
    {
        hashed_string _object{};         ///< 있어야 하는 오브젝트이다(비어 있으면 빈 셀이어야 한다)
        uint8         _bAny{ SW_FALSE }; ///< 무엇이든 된다(`.`). 셀을 차지하지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서식지 레시피 하나입니다. 격자 위 오브젝트의 조합이며, 90° 회전한 모양도 같은 서식지입니다.
     * @details 패턴은 `_width` x `_height` 크기의 행 우선 배열이다. 서식지는 "무엇이든 되는 셀" 이 아닌 셀을 차지한다(한 셀은 한 서식지에만 속한다).
     */
    REFLECT()
    struct SW_KIT_API HabitatDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Min = 1 )
        int32 _capacity{ 1 }; ///< 이 서식지 하나에 머무는 생물 수

        vector<HabitatCell> _listCell{}; ///< 행 우선 배열이다(y x width + x). `<Key>` · `<Row>` 자식에서 읽는다
        int32               _width{ 0 };
        int32               _height{ 0 };

        const HabitatCell& getCell( int32 x, int32 y ) const { return _listCell[static_cast<size_t>( GridTopology( _width, _height ).toIndex( x, y ) )]; }
        /** @brief 차지하는 셀 수를 반환합니다(무엇이든 되는 셀은 제외합니다). 큰 서식지를 먼저 맞춥니다. */
        int32 countClaimedCells() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 능력의 셀 변환 규칙 하나입니다. `_from` 이 놓인 셀을 `_to` 로 바꾸고 `_yieldItem` 을 줍니다. */
    REFLECT()
    struct CreatureTileRule
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _from{}; ///< 비어 있으면 빈 셀이다(데이터는 `empty` 로 적는다)
        PROPERTY( Required )
        hashed_string _to{}; ///< 비어 있으면 비운다(데이터는 `empty` 로 적는다)
        PROPERTY( Name = "yield" )
        hashed_string _yieldItem{}; ///< 바꾸며 얻는 아이템이다(바위에서 돌)
        PROPERTY( Name = "count", Min = 1 )
        int32 _yieldCount{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 생물 능력 하나입니다(나무 심기, 물 만들기, 바위 부수기). 규칙은 위에서부터 처음 맞는 것을 씁니다. */
    REFLECT()
    struct SW_KIT_API CreatureAbilityDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "uses", Min = 1 )
        int32 _usesPerDay{ 1 };

        vector<CreatureTileRule> _listRule{}; ///< `<Rule>` 자식에서 읽는다

        const CreatureTileRule* findRule( const hashed_string& object ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 생물 한 종입니다. 마을에는 종마다 한 마리가 삽니다. */
    REFLECT()
    struct SW_KIT_API CreatureSpeciesDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _chance{ 0.5f }; ///< 조건이 맞는 시간마다 찾아올 확률

        // 아래 목록과 방문 조건은 손으로 읽는다(`habitats` · `foods` · `gifts` · `abilities` · `requests` · `phases` · `weathers`).
        vector<hashed_string> _listHabitat{}; ///< 좋아하는 서식지이다(이 서식지가 생기면 찾아온다)
        vector<hashed_string> _listFood{};    ///< 좋아하는 음식이다(선물 점수 `_foodPoints`)
        vector<hashed_string> _listGift{};    ///< 좋아하는 선물이다(선물 점수 `_likedGiftPoints`)
        vector<hashed_string> _listAbility{};
        vector<hashed_string> _listRequest{};    ///< 이 생물이 하는 부탁이다(퀘스트 id)
        ScheduleCondition     _visitCondition{}; ///< 찾아오는 때(`phases`)와 날씨(`weathers`)이며 비어 있으면 언제나 온다. NPC 일정과 같은 조건 판정을 쓴다

        bool likesHabitat( const hashed_string& habitatID ) const;
        bool likesFood( const hashed_string& itemID ) const;
        bool likesGift( const hashed_string& itemID ) const;
        bool hasAbility( const hashed_string& abilityID ) const;
        bool offersRequest( const hashed_string& questID ) const;
        bool comesIn( DayPhase phase, const hashed_string& weatherID ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 매력도 단계 하나입니다. 점수가 `_minScore` 이상이면 이 단계입니다. */
    REFLECT()
    struct TownAppealTier
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _name{};
        PROPERTY( Name = "min", Required )
        float32 _minScore{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 마을 매력도 점수는 서식지 종류 수 x `_diversityWeight` + 생물 수 x `_creatureWeight` + 호감도 평균 x `_friendshipWeight` 입니다. */
    REFLECT()
    struct TownAppealDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "diversity", Min = 0.0 )
        float32 _diversityWeight{ 10.0f };
        PROPERTY( Name = "creature", Min = 0.0 )
        float32 _creatureWeight{ 5.0f };
        PROPERTY( Name = "friendship", Min = 0.0 )
        float32 _friendshipWeight{ 0.1f };

        vector<TownAppealTier> _listTier{}; ///< 낮은 점수부터 나열한다(`<Tier>` 자식에서 읽는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class CreatureLifeCatalog
     * @brief 카탈로그의 XML 형식입니다.
     * @code
     *     <CreatureLifeCatalog>
     *       <Habitat id="tall_grass" name="Tall Grass" capacity="1">
     *         <Key symbol="G" object="grass"/><Key symbol="T" object="tree"/><Key symbol="_" object="empty"/>
     *         <Row cells="GGT"/><Row cells="GG."/>
     *       </Habitat>
     *       <Species id="sprout" name="Sprout" habitats="tall_grass" foods="berry" gifts="flower" phases="Dawn,Day" weathers="sunny" chance="0.6"
     *                abilities="plant_tree" requests="sprout_berries"/>
     *       <Ability id="plant_tree" uses="2"><Rule from="empty" to="tree"/></Ability>
     *       <Ability id="rock_smash" uses="1"><Rule from="rock" to="empty" yield="stone" count="1"/></Ability>
     *       <Appeal diversity="10" creature="5" friendship="0.1"><Tier name="Camp" min="0"/><Tier name="Village" min="40"/></Appeal>
     *     </CreatureLifeCatalog>
     * @endcode
     * @details `.` 은 무엇이든 되는 셀이고, `empty` 오브젝트는 빈 셀이다. 행 길이가 다르거나 모르는 기호가 있는 서식지, 이름 없는 서식지 · 종, 서식지 없는 종, 아무것도 바꾸지 않는 규칙이 든 능력은 그 원소를 넣지 않는 오류다.
     */
    class SW_KIT_API CreatureLifeCatalog : public XMLCatalog<CreatureLifeCatalog>
    {
        friend class XMLCatalog<CreatureLifeCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "creatures";

        CreatureLifeCatalog();

        void addHabitat( const HabitatDef& habitat );
        void addSpecies( const CreatureSpeciesDef& species ) { (void)_speciesCatalog.add( species ); }
        void addAbility( const CreatureAbilityDef& ability ) { (void)_abilityCatalog.add( ability ); }
        void setAppeal( const TownAppealDef& appeal ) { _appeal = appeal; }

        const HabitatDef*                 findHabitat( const hashed_string& id ) const { return _habitatCatalog.find( id ); }
        const CreatureSpeciesDef*         findSpecies( const hashed_string& id ) const { return _speciesCatalog.find( id ); }
        const CreatureAbilityDef*         findAbility( const hashed_string& id ) const { return _abilityCatalog.find( id ); }
        const vector<HabitatDef>&         getHabitats() const { return _habitatCatalog.getAll(); }
        const vector<CreatureSpeciesDef>& getSpecies() const { return _speciesCatalog.getAll(); }
        const TownAppealDef&              getAppeal() const { return _appeal; }
        /** @brief 서식지를 맞춰 볼 순서(차지하는 셀이 많은 것 먼저, 같으면 읽은 순서)의 index 를 반환합니다. */
        const vector<int32>& getHabitatMatchOrder() const { return _listHabitatOrder; }

    private:
        static constexpr const utf8* kXMLRootName = "CreatureLifeCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Habitat>` 하나를 스키마와 패턴으로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadHabitat( const XMLNode& node, string_view sourceName );
        /** @brief `<Ability>` 와 그 `<Rule>` 을 읽습니다. 규칙 하나라도 틀리면 능력 전체를 넣지 않고 false 입니다. */
        [[nodiscard]] bool loadAbility( const XMLNode& node, string_view sourceName );
        /** @brief `<Species>` 하나를 읽습니다. 오류면 넣지 않고 false 입니다. */
        [[nodiscard]] bool loadSpecies( const XMLNode& node, string_view sourceName );
        /** @brief `<Appeal>` 과 `<Tier>` 를 읽습니다. 오류면 지금 매력도를 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool loadAppeal( const XMLNode& node, string_view sourceName );
        void               rebuildHabitatOrder();

        GameCatalog<HabitatDef>         _habitatCatalog;
        GameCatalog<CreatureSpeciesDef> _speciesCatalog;
        GameCatalog<CreatureAbilityDef> _abilityCatalog;
        vector<int32>                   _listHabitatOrder;
        TownAppealDef                   _appeal;
    };
} // namespace sw
