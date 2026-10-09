/**
 * @file JrpgCatalog.h
 * @brief 클래식 JRPG 의 데이터 — 직업(레벨 1 능력치 · 레벨당 성장 · 배울 주문 · 전직 조건 아이템 · 기본 공격 피해 유형), 주문 · 특기(MP · 내공 · 위력 · 피해 유형),
 *        합동기(참여 멤버 · 콤보 포인트), 무공 비급(숙련 단계 → 초식), 적(능력치 · 약점 · 시전 · 잠금), 인카운터 지역(걸음당 확률 · 유예 걸음 · 무리 가중치)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 능력치입니다. XML 속성 이름은 `hp mp str agi vit intellect luck`(성장은 `growHp` …)입니다. */
    enum class JrpgStat : uint8
    {
        MaxHp = 0,
        MaxMp,
        Strength,
        Agility,
        Vitality,
        Intellect,
        Luck,
        Count
    };

    /** @brief 능력치 수입니다. */
    static constexpr int32 kJrpgStatCount = static_cast<int32>( JrpgStat::Count );

    /** @brief 주문 · 특기가 하는 일입니다. */
    enum class JrpgSpellKind : uint8
    {
        Damage = 0,
        Heal,
        Revive
    };

    /** @brief 대상 범위입니다. */
    enum class JrpgTargetKind : uint8
    {
        One = 0,
        All
    };

    /** @brief 직업 레벨에 배우는 주문 하나입니다. */
    struct JrpgLearnEntry
    {
        hashed_string _spellId{};
        int32         _level{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 직업 하나입니다. 레벨 L 의 능력치는 레벨이 오를 때마다 성장치를 더해 갑니다(전직하면 절반에서 다시). */
    struct JrpgClassDef
    {
        hashed_string          _id{};
        string                 _name{};
        hashed_string          _attackType{};   ///< 기본 공격의 피해 유형(잠금 깨기 · 약점 — "Sword" · "Blunt")
        hashed_string          _requiredItem{}; ///< 이 직업으로 바꾸려면 가지고 있어야 하는 아이템(DQ3 현자 — 깨달음의 책)
        vector<JrpgLearnEntry> _listLearn{};
        int32                  _arrBase[kJrpgStatCount]{ 20, 0, 5, 5, 5, 5, 5 };
        int32                  _arrGrowth[kJrpgStatCount]{ 5, 0, 2, 2, 2, 1, 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 주문 · 특기 · 무공 초식 하나입니다. */
    struct JrpgSpellDef
    {
        hashed_string  _id{};
        string         _name{};
        hashed_string  _damageType{}; ///< 잠금 깨기 · 약점
        hashed_string  _manualId{};   ///< 비지 않으면 그 비급의 초식(숙련 단계로 해금 · 쓸 때마다 숙련)
        int32          _power{ 0 };
        int32          _mpCost{ 0 };
        int32          _innerCost{ 0 };       ///< 내공(무협 옵션일 때만 본다)
        int32          _proficiencyGain{ 0 }; ///< 초식을 쓸 때 오르는 비급 숙련
        JrpgSpellKind  _kind{ JrpgSpellKind::Damage };
        JrpgTargetKind _target{ JrpgTargetKind::One };
    };
} // namespace sw

namespace sw
{
    /** @brief 합동기 하나(씨 오브 스타즈) — 적힌 멤버가 모두 살아 있고 콤보 포인트가 있으면 씁니다. */
    struct JrpgComboDef
    {
        hashed_string         _id{};
        string                _name{};
        vector<hashed_string> _listMemberId{};   ///< 참여 멤버의 id(직업이 아니라 캐릭터)
        vector<hashed_string> _listDamageType{}; ///< 한 번에 여러 잠금을 깰 수 있다
        int32                 _points{ 1 };
        int32                 _power{ 0 };
        JrpgTargetKind        _target{ JrpgTargetKind::One };
    };
} // namespace sw

namespace sw
{
    /** @brief 비급 숙련 단계 하나 — 숙련이 이만큼이면 이 초식이 열립니다. */
    struct JrpgManualStage
    {
        hashed_string _techniqueId{};
        int32         _proficiency{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 무공 비급 하나(완다링 소드)입니다. 단계는 숙련 오름차순입니다. */
    struct JrpgManualDef
    {
        hashed_string           _id{};
        string                  _name{};
        vector<JrpgManualStage> _listStage{};
    };
} // namespace sw

namespace sw
{
    /** @brief 적 하나입니다. */
    struct JrpgEnemyDef
    {
        hashed_string         _id{};
        string                _name{};
        hashed_string         _attackType{};
        hashed_string         _castSpellId{}; ///< 비지 않으면 시전하는 적
        vector<hashed_string> _listWeakness{};
        vector<hashed_string> _listLock{}; ///< 시전 중 잠금(피해 유형 하나씩 — 같은 유형이 여럿일 수 있다)
        int32                 _arrStat[kJrpgStatCount]{ 10, 0, 5, 5, 5, 5, 5 };
        int32                 _exp{ 1 };
        int32                 _gold{ 1 };
        int32                 _castTurns{ 2 }; ///< 시전을 시작하고 이만큼 제 차례가 지나면 터진다
        int32                 _castEvery{ 3 }; ///< 제 차례 몇 번에 한 번 시전을 시작하는가
        bool                  _bBoss{ false }; ///< 도망칠 수 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 인카운터 무리 하나입니다. */
    struct JrpgEncounterGroup
    {
        vector<hashed_string> _listEnemyId{};
        int32                 _weight{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 인카운터 지역 하나입니다. */
    struct JrpgAreaDef
    {
        hashed_string              _id{};
        vector<JrpgEncounterGroup> _listGroup{};
        float32                    _rate{ 0.0625f }; ///< 걸음 하나의 조우 확률
        int32                      _graceSteps{ 0 }; ///< 조우 뒤 이만큼은 조우하지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class JrpgCatalog
     * @brief `<JrpgCatalog><ExperienceCurve .../><Class/><Spell/><Combo/><Manual><Stage/></Manual><Enemy/><Area><Group/></Area></JrpgCatalog>` 를 읽습니다.
     * @details 예:
     *          `<Class id="mage" hp="18" mp="12" str="4" intellect="14" growMp="4" attackType="Blunt" requires=""><Learn level="1" spell="frizz"/></Class>`
     *          `<Spell id="frizz" kind="Damage" mp="2" power="12" type="Fire" target="One"/>` · `<Spell id="pine_cut" manual="pine_sword" inner="20" power="20" type="Sword" proficiency="10"/>`
     *          `<Combo id="eclipse" members="zale,valere" points="3" power="40" types="Sun,Moon" target="All"/>`
     *          `<Manual id="pine_sword"><Stage proficiency="0" technique="pine_cut"/><Stage proficiency="30" technique="pine_storm"/></Manual>`
     *          `<Enemy id="wyrd" hp="120" str="14" agi="6" vit="8" exp="30" gold="20" attackType="Blunt" weak="Sun" cast="flame" castTurns="2" castEvery="2" locks="Sword,Moon" boss="false"/>`
     *          `<Area id="field" rate="0.0625" grace="4"><Group enemies="slime,slime" weight="3"/></Area>`
     */
    class SW_GF_API JrpgCatalog : public XmlCatalog<JrpgCatalog>
    {
        friend class XmlCatalog<JrpgCatalog>;

    public:
        JrpgCatalog();

        void clear();

        const JrpgClassDef*    findClass( const hashed_string& id ) const { return _classCatalog.find( id ); }
        const JrpgSpellDef*    findSpell( const hashed_string& id ) const { return _spellCatalog.find( id ); }
        const JrpgComboDef*    findCombo( const hashed_string& id ) const { return _comboCatalog.find( id ); }
        const JrpgManualDef*   findManual( const hashed_string& id ) const { return _manualCatalog.find( id ); }
        const JrpgEnemyDef*    findEnemy( const hashed_string& id ) const { return _enemyCatalog.find( id ); }
        const JrpgAreaDef*     findArea( const hashed_string& id ) const { return _areaCatalog.find( id ); }
        const ExperienceCurve& getCurve() const { return _curve; }

    private:
        static constexpr const utf8* kXmlRootName = "JrpgCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );
        void                         loadClass( const XmlNode& node, const utf8* pId );
        void                         loadSpell( const XmlNode& node, const utf8* pId, string_view sourceName );
        void                         loadEnemy( const XmlNode& node, const utf8* pId );

        GameCatalog<JrpgClassDef>  _classCatalog;
        GameCatalog<JrpgSpellDef>  _spellCatalog;
        GameCatalog<JrpgComboDef>  _comboCatalog;
        GameCatalog<JrpgManualDef> _manualCatalog;
        GameCatalog<JrpgEnemyDef>  _enemyCatalog;
        GameCatalog<JrpgAreaDef>   _areaCatalog;
        ExperienceCurve            _curve;
    };
} // namespace sw
