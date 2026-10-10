/**
 * @file WitcherCatalog.h
 * @brief 위쳐 RPG 의 데이터 — 괴물(도감 지식 · 약점) · 연금술 물건(물약 · 변이 혼합물 · 오일 · 폭탄) · 표식 · 전투 수치 · 스킬 색 · 변이원 · 슬롯 묶음 · 계약(단서 · 보상)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 약점의 종류입니다. */
    enum class WitcherWeaknessKind : uint8
    {
        Oil = 0,
        Bomb,
        Sign
    };

    /** @brief 도감에 적힌 약점 하나입니다. */
    struct WitcherWeakness
    {
        hashed_string       _id{};           ///< 오일 · 폭탄 · 표식 id
        int32               _knowledge{ 1 }; ///< 이 지식 단계부터 보인다
        WitcherWeaknessKind _kind{ WitcherWeaknessKind::Oil };
    };
} // namespace sw

namespace sw
{
    /** @brief 괴물 한 종입니다. */
    struct WitcherMonsterDef
    {
        hashed_string           _id{};
        hashed_string           _category{};    ///< "Necrophage" · "Specter" …(도감 분류)
        vector<hashed_string>   _listElement{}; ///< 방어 속성(기반 `ElementChart` 의 방어 쪽 — 비면 분류 하나)
        vector<WitcherWeakness> _listWeakness{};
        int32                   _maxKnowledge{ 3 };
        int32                   _readLevel{ 3 };        ///< 책을 읽으면 이 단계까지
        int32                   _killsPerLevel{ 1 };    ///< 이만큼 처치할 때마다 한 단계
        int32                   _killCap{ 2 };          ///< 처치만으로 오르는 상한
        int32                   _investigateLevel{ 1 }; ///< 흔적을 조사하면 이 단계까지
    };
} // namespace sw

namespace sw
{
    /** @brief 연금술 물건의 종류입니다. */
    enum class WitcherAlchemyKind : uint8
    {
        Potion = 0,
        Decoction, ///< 변이 혼합물 — 오래 가고 독성이 효과 동안 묶인다
        Oil,
        Bomb
    };

    /** @brief 연금술 물건 하나(인벤토리 아이템 id 와 같다)입니다. */
    struct WitcherAlchemyDef
    {
        hashed_string      _id{};
        hashed_string      _element{};         ///< 오일 · 폭탄의 공격 속성(기반 `ElementChart`)
        float32            _toxicity{ 0.0f };  ///< 마시면 오르는 독성
        float32            _duration{ 30.0f }; ///< 효과 초
        int32              _charges{ 1 };      ///< 명상으로 다시 차는 사용 횟수
        int32              _hits{ 0 };         ///< 오일 — 칼에 바른 뒤 적중 횟수
        WitcherAlchemyKind _kind{ WitcherAlchemyKind::Potion };
    };
} // namespace sw

namespace sw
{
    /** @brief 표식 하나입니다. */
    struct WitcherSignDef
    {
        hashed_string _id{};
        hashed_string _element{};  ///< 속성(상성 · 상태이상 — 기반 `ElementChart`)
        hashed_string _altSkill{}; ///< 이 스킬 랭크가 1 이상이면 대체 시전(기반 `SkillTreeState`)
        float32       _cost{ 30.0f };
        float32       _altCost{ 60.0f };
        float32       _basePower{ 20.0f };
        float32       _altPowerScale{ 1.5f };
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 수치입니다. */
    struct WitcherCombatSettings
    {
        hashed_string _intensityStat{ "signIntensity" }; ///< 표식 위력(%)을 읽는 능력치 이름
        float32       _stamina{ 100.0f };
        float32       _staminaRegen{ 10.0f };
        float32       _staminaDelay{ 1.0f };
        float32       _fastCost{ 0.0f };
        float32       _strongCost{ 10.0f };
        float32       _dodgeCost{ 5.0f };
        float32       _rollCost{ 10.0f };
        float32       _adrenalineMax{ 3.0f };
        float32       _adrenalinePerHit{ 0.1f };
        float32       _adrenalineLossOnHit{ 1.0f };
        float32       _adrenalineDamageBonus{ 0.1f }; ///< 쌓인 포인트(정수) 하나마다 피해 배율에 더한다
    };
} // namespace sw

namespace sw
{
    /** @brief 연금술 수치입니다. */
    struct WitcherAlchemySettings
    {
        vector<hashed_string> _listAlcohol{}; ///< 명상에 쓰는 술(앞에서부터 찾는다)
        float32               _maxToxicity{ 100.0f };
        float32               _toxicityDecay{ 2.0f }; ///< 초당 — 묶이지 않은 독성만
    };
} // namespace sw

namespace sw
{
    /** @brief 스킬 하나의 색(변이원 색 맞춤)입니다. */
    struct WitcherSkillColorDef
    {
        hashed_string _id{}; ///< 스킬 id
        hashed_string _color{};
    };
} // namespace sw

namespace sw
{
    /** @brief 변이원 하나입니다. */
    struct WitcherMutagenDef
    {
        hashed_string _id{};
        hashed_string _color{};
        hashed_string _stat{};
        float32       _value{ 0.0f };      ///< 끼우면 주는 능력치
        float32       _matchValue{ 0.0f }; ///< 같은 묶음에 색이 맞는 스킬마다 더
    };
} // namespace sw

namespace sw
{
    /** @brief 스킬 슬롯 묶음 하나(변이원 슬롯 하나를 공유)입니다. */
    struct WitcherSlotGroupDef
    {
        int32 _requiredLevel{ 1 };
        int32 _slotCount{ 3 };
    };
} // namespace sw

namespace sw
{
    /** @brief 계약의 단서 하나입니다. */
    struct WitcherClueDef
    {
        hashed_string _id{};
        float3        _position{};
        float32       _radius{ 2.0f }; ///< 이 안에서 조사할 수 있다
        int32         _order{ 0 };     ///< 같은 단계에서 이보다 작은 순서의 단서를 모두 찾아야 열린다
    };
} // namespace sw

namespace sw
{
    /** @brief 계약의 조사 단계 하나입니다. */
    struct WitcherContractStepDef
    {
        hashed_string          _id{};
        vector<WitcherClueDef> _listClue{};
    };
} // namespace sw

namespace sw
{
    /** @brief 계약 하나입니다. */
    struct WitcherContractDef
    {
        hashed_string                  _id{};
        hashed_string                  _questID{};
        vector<WitcherContractStepDef> _listStep{};
        int32                          _reward{ 100 };         ///< 처음 제시하는 보상
        float32                        _limitRatio{ 1.3f };    ///< 의뢰인이 받아들이는 최대 배율
        float32                        _angerMax{ 1.0f };      ///< 분노가 여기 닿으면 결렬
        float32                        _angerScale{ 2.0f };    ///< 한도를 넘긴 몫(기본 보상 대비) × 이만큼 분노
        float32                        _angerPerRound{ 0.1f }; ///< 흥정할 때마다
    };
} // namespace sw

namespace sw
{
    /**
     * @class WitcherCatalog
     * @brief 위쳐 키트의 XML 하나를 읽습니다.
     * @code
     *     <WitcherCatalog>
     *       <Alchemy maxToxicity="100" toxicityDecay="2" alcohol="dwarven_spirit,white_gull"/>
     *       <Combat stamina="100" regen="10" delay="1" fast="0" strong="10" dodge="5" roll="10" adrenalineMax="3" adrenalinePerHit="0.1"
     *               adrenalineLoss="1" adrenalineBonus="0.1" intensityStat="signIntensity"/>
     *       <Monster id="drowner" category="Necrophage" elements="Necrophage" maxKnowledge="3" readLevel="3" killsPerLevel="1" killCap="2" investigateLevel="1">
     *         <Weakness kind="Oil" id="necrophage_oil" knowledge="1"/><Weakness kind="Sign" id="igni" knowledge="2"/>
     *       </Monster>
     *       <Item id="swallow" kind="Potion" toxicity="20" duration="20" charges="3"/>
     *       <Item id="necrophage_oil" kind="Oil" element="NecrophageOil" hits="20"/>
     *       <Sign id="igni" element="Fire" cost="30" altCost="80" power="20" altPowerScale="1.5" altSkill="firestream"/>
     *       <SkillColor skill="muscle_memory" color="Red"/>
     *       <Mutagen id="red_lesser" color="Red" stat="attackPower" value="5" matchValue="5"/>
     *       <SlotGroup level="1" slots="3"/>
     *       <Contract id="griffin" quest="contract_griffin" reward="200" limit="1.4" angerMax="1" angerScale="2" angerPerRound="0.1">
     *         <Step id="tracks"><Clue id="blood" x="0" z="0" radius="2" order="0"/></Step>
     *       </Contract>
     *     </WitcherCatalog>
     * @endcode
     */
    class SW_GF_API WitcherCatalog : public XMLCatalog<WitcherCatalog>
    {
        friend class XMLCatalog<WitcherCatalog>;

    public:
        WitcherCatalog();

        const WitcherMonsterDef*  findMonster( const hashed_string& id ) const { return _monsterCatalog.find( id ); }
        const WitcherAlchemyDef*  findAlchemy( const hashed_string& id ) const { return _alchemyCatalog.find( id ); }
        const WitcherSignDef*     findSign( const hashed_string& id ) const { return _signCatalog.find( id ); }
        const WitcherMutagenDef*  findMutagen( const hashed_string& id ) const { return _mutagenCatalog.find( id ); }
        const WitcherContractDef* findContract( const hashed_string& id ) const { return _contractCatalog.find( id ); }
        /** @brief 스킬의 색입니다. 정하지 않은 스킬은 빈 이름(어느 변이원과도 맞지 않는다)입니다. */
        hashed_string getSkillColor( const hashed_string& skillID ) const;

        const vector<WitcherAlchemyDef>&   getAlchemyItems() const { return _alchemyCatalog.getAll(); }
        const vector<WitcherSlotGroupDef>& getSlotGroups() const { return _listSlotGroup; }
        const WitcherCombatSettings&       getCombat() const { return _combat; }
        const WitcherAlchemySettings&      getAlchemy() const { return _alchemy; }

    private:
        static constexpr const utf8* kXMLRootName = "WitcherCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        void                         loadMonster( const XMLNode& node, const hashed_string& id, string_view sourceName );
        void                         loadContract( const XMLNode& node, const hashed_string& id );

        GameCatalog<WitcherMonsterDef>    _monsterCatalog;
        GameCatalog<WitcherAlchemyDef>    _alchemyCatalog;
        GameCatalog<WitcherSignDef>       _signCatalog;
        GameCatalog<WitcherMutagenDef>    _mutagenCatalog;
        GameCatalog<WitcherContractDef>   _contractCatalog;
        GameCatalog<WitcherSkillColorDef> _skillColorCatalog;
        vector<WitcherSlotGroupDef>       _listSlotGroup; ///< 읽은 순서(묶음 번호)
        WitcherCombatSettings             _combat;
        WitcherAlchemySettings            _alchemy;
    };
} // namespace sw
