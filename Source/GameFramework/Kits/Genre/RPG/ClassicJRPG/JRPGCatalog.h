/**
 * @file JRPGCatalog.h
 * @brief 클래식 JRPG 의 데이터입니다. 직업(레벨 1 능력치, 레벨당 성장, 배울 주문, 전직 조건 아이템, 기본 공격 피해 유형), 주문과 특기(MP, 내공, 위력, 피해 유형),
 *        합동기(참여 멤버, 콤보 포인트), 무공 비급(숙련 단계별 초식), 적(능력치, 약점, 시전, 잠금), 인카운터 지역(걸음당 확률, 유예 걸음, 무리 가중치)을 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 능력치입니다. XML 속성 이름은 `hp mp str agi vit intellect luck` 이고, 성장치는 `growHP` 처럼 씁니다. */
    enum class JRPGStat : uint8
    {
        MaxHP = 0,
        MaxMP,
        Strength,
        Agility,
        Vitality,
        Intellect,
        Luck,
        Count
    };

    /** @brief 능력치 수입니다. */
    static constexpr int32 kJRPGStatCount = static_cast<int32>( JRPGStat::Count );

    /** @brief 주문과 특기가 하는 일입니다. */
    ENUM()
    enum class JRPGSpellKind : uint8
    {
        Damage = 0,
        Heal,
        Revive
    };

    /** @brief 대상 범위입니다. */
    ENUM()
    enum class JRPGTargetKind : uint8
    {
        One = 0,
        All
    };

    /** @brief 직업 레벨에 배우는 주문 하나입니다. */
    REFLECT()
    struct JRPGLearnEntry
    {
        REFLECT_BODY();

        PROPERTY( Name = "spell", Required )
        hashed_string _spellID{};
        PROPERTY( Min = 1 )
        int32 _level{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 직업 하나입니다. 레벨 L 의 능력치는 레벨이 오를 때마다 성장치를 더해서 계산합니다(전직하면 절반에서 다시 시작합니다). */
    REFLECT()
    struct JRPGClassDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY()
        hashed_string _attackType{}; ///< 기본 공격의 피해 유형(잠금 깨기와 약점에 쓰며 "Sword", "Blunt" 같은 값이다)
        PROPERTY( Name = "requires" )
        hashed_string          _requiredItem{}; ///< 이 직업으로 바꾸려면 가지고 있어야 하는 아이템(DQ3 현자의 깨달음의 책)
        vector<JRPGLearnEntry> _listLearn{};
        int32                  _arrBase[kJRPGStatCount]{ 20, 0, 5, 5, 5, 5, 5 };
        int32                  _arrGrowth[kJRPGStatCount]{ 5, 0, 2, 2, 2, 1, 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 주문, 특기, 무공 초식 하나입니다. */
    REFLECT()
    struct JRPGSpellDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Name = "type" )
        hashed_string _damageType{}; ///< 잠금 깨기와 약점에 쓰는 피해 유형
        PROPERTY( Name = "manual" )
        hashed_string _manualID{}; ///< 비어 있지 않으면 그 비급의 초식이다(숙련 단계로 해금되고, 쓸 때마다 숙련이 오른다)
        PROPERTY( Min = 0 )
        int32 _power{ 0 };
        PROPERTY( Name = "mp", Min = 0 )
        int32 _mpCost{ 0 };
        PROPERTY( Name = "inner", Min = 0 )
        int32 _innerCost{ 0 }; ///< 내공(무협 옵션일 때만 본다)
        PROPERTY( Name = "proficiency", Min = 0 )
        int32 _proficiencyGain{ 0 }; ///< 초식을 쓸 때 오르는 비급 숙련
        PROPERTY()
        JRPGSpellKind _kind{ JRPGSpellKind::Damage };
        PROPERTY()
        JRPGTargetKind _target{ JRPGTargetKind::One };
    };
} // namespace sw

namespace sw
{
    /** @brief 합동기 하나(씨 오브 스타즈)입니다. 적힌 멤버가 모두 살아 있고 콤보 포인트가 있으면 쓸 수 있습니다. */
    REFLECT()
    struct JRPGComboDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string                _name{};
        vector<hashed_string> _listMemberID{};   ///< 참여 멤버의 id(직업이 아니라 캐릭터)
        vector<hashed_string> _listDamageType{}; ///< 한 번에 여러 잠금을 깰 수 있다
        PROPERTY( Min = 0 )
        int32 _points{ 1 };
        PROPERTY( Min = 0 )
        int32 _power{ 0 };
        PROPERTY()
        JRPGTargetKind _target{ JRPGTargetKind::One };
    };
} // namespace sw

namespace sw
{
    /** @brief 비급 숙련 단계 하나입니다. 숙련이 이 값 이상이면 이 초식이 열립니다. */
    REFLECT()
    struct JRPGManualStage
    {
        REFLECT_BODY();

        PROPERTY( Name = "technique", Required )
        hashed_string _techniqueID{};
        PROPERTY( Min = 0 )
        int32 _proficiency{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 무공 비급 하나(완다링 소드)입니다. 단계는 숙련 오름차순입니다. */
    REFLECT()
    struct JRPGManualDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string                  _name{};
        vector<JRPGManualStage> _listStage{};
    };
} // namespace sw

namespace sw
{
    /** @brief 적 하나입니다. */
    REFLECT()
    struct JRPGEnemyDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY()
        hashed_string _attackType{};
        PROPERTY( Name = "cast" )
        hashed_string         _castSpellID{}; ///< 비어 있지 않으면 시전하는 적이다
        vector<hashed_string> _listWeakness{};
        vector<hashed_string> _listLock{}; ///< 시전 중에 적용되는 잠금(피해 유형 하나씩이며, 같은 유형이 여럿일 수 있다)
        int32                 _arrStat[kJRPGStatCount]{ 10, 0, 5, 5, 5, 5, 5 };
        PROPERTY( Min = 0 )
        int32 _exp{ 1 };
        PROPERTY( Min = 0 )
        int32 _gold{ 1 };
        PROPERTY( Min = 1 )
        int32 _castTurns{ 2 }; ///< 시전을 시작하고 제 차례가 이만큼 지나면 터진다
        PROPERTY( Min = 1 )
        int32 _castEvery{ 3 }; ///< 제 차례 몇 번에 한 번 시전을 시작하는지
        PROPERTY()
        bool _bBoss{ false }; ///< 도망칠 수 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 인카운터 무리 하나입니다. */
    REFLECT()
    struct JRPGEncounterGroup
    {
        REFLECT_BODY();

        vector<hashed_string> _listEnemyID{};
        PROPERTY( Min = 0 )
        int32 _weight{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 인카운터 지역 하나입니다. */
    REFLECT()
    struct JRPGAreaDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string              _id{};
        vector<JRPGEncounterGroup> _listGroup{};
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _rate{ 0.0625f }; ///< 걸음 하나당 조우 확률
        PROPERTY( Name = "grace", Min = 0 )
        int32 _graceSteps{ 0 }; ///< 조우한 뒤 이 걸음 수 동안은 조우하지 않는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class JRPGCatalog
     * @brief 다음 구조의 XML 을 읽는 카탈로그입니다. `<JRPGCatalog><ExperienceCurve .../><Class/><Spell/><Combo/><Manual><Stage/></Manual><Enemy/><Area><Group/></Area></JRPGCatalog>`
     * @details 칸은 각 Def 의 `PROPERTY` 가 스키마다(기본값 = 초기값, 범위 = Min · Max, 이름 `name` 은 필수). 능력치(`hp` ~ `luck`, `growHP` ~ `growLuck`)는
     *          0 이상의 정수이고 적지 않으면 Def 의 초기값이다. 예시는 다음과 같다.
     *          `<Class id="mage" name="Mage" hp="18" mp="12" str="4" intellect="14" growMP="4" attackType="Blunt" requires=""><Learn level="1" spell="frizz"/></Class>`
     *          `<Spell id="frizz" kind="Damage" mp="2" power="12" type="Fire" target="One"/>`, `<Spell id="pine_cut" manual="pine_sword" inner="20" power="20" type="Sword" proficiency="10"/>`
     *          `<Combo id="eclipse" members="zale,valere" points="3" power="40" types="Sun,Moon" target="All"/>`
     *          `<Manual id="pine_sword"><Stage proficiency="0" technique="pine_cut"/><Stage proficiency="30" technique="pine_storm"/></Manual>`
     *          `<Enemy id="wyrd" hp="120" str="14" agi="6" vit="8" exp="30" gold="20" attackType="Blunt" weak="Sun" cast="flame" castTurns="2" castEvery="2" locks="Sword,Moon" boss="false"/>`
     *          `<Area id="field" rate="0.0625" grace="4"><Group enemies="slime,slime" weight="3"/></Area>`
     */
    class SW_KIT_API JRPGCatalog : public XMLCatalog<JRPGCatalog>
    {
        friend class XMLCatalog<JRPGCatalog>;

    public:
        JRPGCatalog();

        void clear();

        const JRPGClassDef*    findClass( const hashed_string& id ) const { return _classCatalog.find( id ); }
        const JRPGSpellDef*    findSpell( const hashed_string& id ) const { return _spellCatalog.find( id ); }
        const JRPGComboDef*    findCombo( const hashed_string& id ) const { return _comboCatalog.find( id ); }
        const JRPGManualDef*   findManual( const hashed_string& id ) const { return _manualCatalog.find( id ); }
        const JRPGEnemyDef*    findEnemy( const hashed_string& id ) const { return _enemyCatalog.find( id ); }
        const JRPGAreaDef*     findArea( const hashed_string& id ) const { return _areaCatalog.find( id ); }
        const ExperienceCurve& getCurve() const { return _curve; }

    private:
        static constexpr const utf8* kXMLRootName = "JRPGCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief 원소 하나를 이름에 맞는 스키마로 읽습니다. 오류면 그 원소를 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadElement( const XMLNode& node, string_view name, string_view sourceName );
        [[nodiscard]] bool loadClass( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadCombo( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadManual( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadEnemy( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool loadArea( const XMLNode& node, string_view sourceName );

        GameCatalog<JRPGClassDef>  _classCatalog;
        GameCatalog<JRPGSpellDef>  _spellCatalog;
        GameCatalog<JRPGComboDef>  _comboCatalog;
        GameCatalog<JRPGManualDef> _manualCatalog;
        GameCatalog<JRPGEnemyDef>  _enemyCatalog;
        GameCatalog<JRPGAreaDef>   _areaCatalog;
        ExperienceCurve            _curve;
    };
} // namespace sw
