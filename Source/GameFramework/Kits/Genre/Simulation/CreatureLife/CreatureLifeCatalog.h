/**
 * @file CreatureLifeCatalog.h
 * @brief 생물 생활 정의 — 서식지 레시피(칸 패턴) · 생물 종(좋아하는 서식지 · 음식 · 선물 · 시간대 · 날씨 · 능력 · 부탁) · 능력(칸 변환 규칙) · 마을 매력도 단계입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCondition.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 서식지 패턴의 칸 하나입니다. */
    struct HabitatCell
    {
        hashed_string _object{};         ///< 있어야 하는 오브젝트(비면 빈 칸이어야 한다)
        uint8         _bAny{ SW_FALSE }; ///< 무엇이든 된다(`.`) — 칸을 차지하지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 서식지 레시피 하나 — 격자 위 오브젝트 조합입니다. 90° 회전한 모양도 같은 서식지입니다.
     * @details 패턴은 `_width` × `_height` 행 우선입니다. 무엇이든 되는 칸이 아닌 칸을 서식지가 차지합니다(한 칸은 한 서식지에만).
     */
    struct SW_GF_API HabitatDef
    {
        hashed_string       _id{};
        string              _name{};
        vector<HabitatCell> _listCell{}; ///< 행 우선(y × width + x)
        int32               _width{ 0 };
        int32               _height{ 0 };
        int32               _capacity{ 1 }; ///< 이 서식지 하나에 머무는 생물 수

        const HabitatCell& getCell( int32 x, int32 y ) const { return _listCell[static_cast<size_t>( y * _width + x )]; }
        /** @brief 차지하는 칸 수입니다(무엇이든 되는 칸 제외). 큰 서식지가 먼저 맞춰집니다. */
        int32 countClaimedCells() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 능력의 칸 변환 규칙 하나 — `_from` 이 놓인 칸을 `_to` 로 바꾸고 `_yieldItem` 을 줍니다. */
    struct CreatureTileRule
    {
        hashed_string _from{};      ///< 비면 빈 칸
        hashed_string _to{};        ///< 비면 비운다
        hashed_string _yieldItem{}; ///< 바꾸며 얻는 아이템(바위 → 돌)
        int32         _yieldCount{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 생물 능력 하나입니다(나무 심기 · 물 만들기 · 바위 부수기). 규칙은 위에서부터 처음 맞는 것을 씁니다. */
    struct SW_GF_API CreatureAbilityDef
    {
        hashed_string            _id{};
        vector<CreatureTileRule> _listRule{};
        int32                    _usesPerDay{ 1 };

        const CreatureTileRule* findRule( const hashed_string& object ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 생물 한 종입니다. 마을에는 종마다 한 마리가 삽니다. */
    struct SW_GF_API CreatureSpeciesDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<hashed_string> _listHabitat{}; ///< 좋아하는 서식지(이 서식지가 생기면 찾아온다)
        vector<hashed_string> _listFood{};    ///< 좋아하는 음식(선물 점수 `_foodPoints`)
        vector<hashed_string> _listGift{};    ///< 좋아하는 선물(선물 점수 `_likedGiftPoints`)
        vector<hashed_string> _listAbility{};
        vector<hashed_string> _listRequest{};    ///< 이 생물이 하는 부탁(퀘스트 id)
        ScheduleCondition     _visitCondition{}; ///< 찾아오는 때(`phases`) · 날씨(`weathers`) — 비면 언제나. NPC 일정과 같은 조건 판정이다
        float32               _chance{ 0.5f };   ///< 조건이 맞는 시간마다 찾아올 확률

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
    /** @brief 매력도 단계 하나 — 점수가 `_minScore` 이상이면 이 단계입니다. */
    struct TownAppealTier
    {
        hashed_string _name{};
        float32       _minScore{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 마을 매력도 점수 = 서식지 종류 수 × `_diversityWeight` + 생물 수 × `_creatureWeight` + 호감도 평균 × `_friendshipWeight` 입니다. */
    struct TownAppealDef
    {
        vector<TownAppealTier> _listTier{}; ///< 낮은 점수부터
        float32                _diversityWeight{ 10.0f };
        float32                _creatureWeight{ 5.0f };
        float32                _friendshipWeight{ 0.1f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class CreatureLifeCatalog
     * @brief XML 형식입니다.
     * @code
     *     <CreatureLifeCatalog>
     *       <Habitat id="tall_grass" name="Tall Grass" capacity="1">
     *         <Key symbol="G" object="grass"/><Key symbol="T" object="tree"/><Key symbol="_" object="empty"/>
     *         <Row cells="GGT"/><Row cells="GG."/>
     *       </Habitat>
     *       <Species id="sprout" habitats="tall_grass" foods="berry" gifts="flower" phases="Dawn,Day" weathers="sunny" chance="0.6"
     *                abilities="plant_tree" requests="sprout_berries"/>
     *       <Ability id="plant_tree" uses="2"><Rule from="empty" to="tree"/></Ability>
     *       <Ability id="rock_smash" uses="1"><Rule from="rock" to="empty" yield="stone" count="1"/></Ability>
     *       <Appeal diversity="10" creature="5" friendship="0.1"><Tier name="Camp" min="0"/><Tier name="Village" min="40"/></Appeal>
     *     </CreatureLifeCatalog>
     * @endcode
     *          `.` 은 무엇이든 되는 칸, `empty` 오브젝트는 빈 칸입니다. 행 길이가 다르거나 모르는 기호가 있는 서식지는 경고하고 뺍니다.
     */
    class SW_GF_API CreatureLifeCatalog : public XmlCatalog<CreatureLifeCatalog>
    {
        friend class XmlCatalog<CreatureLifeCatalog>;

    public:
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
        /** @brief 서식지를 맞춰 볼 차례(차지 칸이 많은 것 먼저, 같으면 읽은 순서)의 자리 번호입니다. */
        const vector<int32>& getHabitatMatchOrder() const { return _listHabitatOrder; }

    private:
        static constexpr const utf8* kXmlRootName = "CreatureLifeCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );
        void                         rebuildHabitatOrder();

        GameCatalog<HabitatDef>         _habitatCatalog;
        GameCatalog<CreatureSpeciesDef> _speciesCatalog;
        GameCatalog<CreatureAbilityDef> _abilityCatalog;
        vector<int32>                   _listHabitatOrder;
        TownAppealDef                   _appeal;
    };
} // namespace sw
