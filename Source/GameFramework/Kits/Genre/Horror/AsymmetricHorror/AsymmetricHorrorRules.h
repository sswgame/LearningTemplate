/**
 * @file AsymmetricHorrorRules.h
 * @brief 비대칭 공포의 데이터를 담습니다. 생존자 속도, 출혈, 치료, 발전기(시간, 인원 효율, 스킬 체크, 걷어차기 퇴행), 갈고리 단계, 판자, 창틀, 사물함, 탈출구, 붕괴, 해치,
 *        살인마(속도 비율, 공격 쿨다운, 위협 반경, 능력), 블러드포인트 범주와 행동 점수를 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/StatBlock.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 발전기 수치입니다(`<Generator>`). 시간은 초, 진행량은 0..1 입니다. */
    REFLECT()
    struct HorrorGeneratorDef
    {
        REFLECT_BODY();

        vector<float32> _listScale{}; ///< `scales` — [n-1] 이 n명이 고칠 때의 속도 배율이다(사람이 늘수록 한 명의 몫은 줄어든다)
        PROPERTY( Min = 0.1 )
        float32 _time{ 80.0f };
        PROPERTY( Min = 0.0 )
        float32 _skillCheckInterval{ 10.0f };
        PROPERTY( Min = 0.0 )
        float32 _noiseRadius{ 60.0f }; ///< 스킬 체크를 놓치면 나는 소음 반경
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _kickPenalty{ 0.05f }; ///< 걷어차는 순간 깎는 진행량
        PROPERTY( Min = 0.0 )
        float32 _kickRegression{ 0.0025f }; ///< 걷어찬 뒤 아무도 없으면 초당 줄어드는 진행량
        PROPERTY( Min = 1 )
        int32 _maxRepairers{ 4 };
    };
} // namespace sw

namespace sw
{
    /** @brief 치료 수치입니다(`<Heal>`). */
    REFLECT()
    struct HorrorHealDef
    {
        REFLECT_BODY();

        vector<float32> _listScale{}; ///< `scales` — [n-1] 이 n명이 치료할 때의 속도 배율이다
        PROPERTY( Min = 0.1 )
        float32 _time{ 16.0f };
        PROPERTY( Min = 0.0 )
        float32 _skillCheckInterval{ 8.0f };
        PROPERTY( Min = 1 )
        int32 _maxHealers{ 3 };
    };
} // namespace sw

namespace sw
{
    /** @brief 스킬 체크 수치입니다(`<SkillCheck>`). 창(`<Window grade early late score bonus>`)은 기반 `TimingJudge` 가 판정합니다. */
    REFLECT()
    struct HorrorSkillCheckDef
    {
        REFLECT_BODY();

        StatBlock _bonus{}; ///< 창의 `bonus` — 스킬 체크 등급에 따라 더하는 진행량이다
        PROPERTY( Min = 0.0 )
        float32 _leadTime{ 1.0f };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _failPenalty{ 0.1f }; ///< 놓쳤을 때 깎는 진행량이다(폭발과 퇴행)
    };
} // namespace sw

namespace sw
{
    /** @brief 갈고리 수치입니다(`<Hook>`). */
    REFLECT()
    struct HorrorHookDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.1 )
        float32 _stageTime{ 60.0f };
        PROPERTY( Min = 0.0 )
        float32 _struggleGrace{ 1.0f }; ///< 2단계(몸부림)에서 몸부림을 이만큼 넘게 멈추면 희생된다
        PROPERTY( Min = 0.1 )
        float32 _wiggleTime{ 16.0f }; ///< 들린 채 몸부림으로 빠져나오기까지의 시간
        PROPERTY( Min = 0.0 )
        float32 _wiggleSTUNTime{ 3.0f };
        PROPERTY( Min = 1 )
        int32 _stages{ 3 }; ///< 이 횟수만큼 걸리면 바로 희생된다
    };
} // namespace sw

namespace sw
{
    /** @brief 판자 수치입니다(`<Pallet>`). */
    REFLECT()
    struct HorrorPalletDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0 )
        float32 _stunTime{ 2.0f };
        PROPERTY( Min = 0.0 )
        float32 _stunRange{ 1.5f };
        PROPERTY( Min = 0.0 )
        float32 _breakTime{ 2.6f };
    };
} // namespace sw

namespace sw
{
    /** @brief 창틀 수치입니다(`<Window>`). */
    REFLECT()
    struct HorrorWindowDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0 )
        float32 _fastTime{ 0.5f };
        PROPERTY( Min = "_fastTime" )
        float32 _mediumTime{ 1.2f };
        PROPERTY( Min = 0.0 )
        float32 _killerTime{ 1.7f };
        PROPERTY( Min = 0.0 )
        float32 _fastSpeedRatio{ 0.9f }; ///< 생존자 속도의 이 비율 이상으로 달려 들어가면 빠른 넘기이다
        PROPERTY( Min = 0.0 )
        float32 _noiseRadius{ 40.0f }; ///< 빠른 넘기가 내는 소음 반경
        PROPERTY( Min = 0.0 )
        float32 _blockTime{ 15.0f };
        PROPERTY( Min = 1 )
        int32 _blockCount{ 3 }; ///< 추격 중 같은 창을 이만큼 넘으면 막힌다
    };
} // namespace sw

namespace sw
{
    /** @brief 사물함 수치입니다(`<Locker>`). */
    REFLECT()
    struct HorrorLockerDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0 )
        float32 _searchTime{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 탈출 수치입니다(`<Endgame>`). */
    REFLECT()
    struct HorrorEndgameDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.1 )
        float32 _gateTime{ 20.0f };
        PROPERTY( Min = 0.1 )
        float32 _collapseTime{ 120.0f };
        PROPERTY( Min = 0.0 )
        float32 _slowScale{ 0.5f }; ///< 누가 빈사나 갈고리 상태이면 붕괴가 이 배율로 흐른다
        PROPERTY( Min = 0 )
        int32 _hatchRemaining{ 1 }; ///< 남은 생존자가 이만큼이면 해치가 열린다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 판 규칙의 수치입니다. 시간은 초, 거리와 속도는 m 와 m/s, 진행량은 0..1 입니다.
     * @details 루트 속성이 이 스키마이고, 원소마다 하위 Def(발전기, 치료, 스킬 체크, 갈고리, 판자, 창틀, 사물함, 탈출)가 따로 스키마다 —
     *          원소 하나가 스키마 하나다(같은 속성 이름 `time` 이 원소마다 다른 칸이다).
     */
    REFLECT()
    struct AsymmetricHorrorRules
    {
        REFLECT_BODY();

        HorrorGeneratorDef  _generator{};
        HorrorHealDef       _heal{};
        HorrorSkillCheckDef _skillCheck{};
        HorrorHookDef       _hook{};
        HorrorPalletDef     _pallet{};
        HorrorWindowDef     _window{};
        HorrorLockerDef     _locker{};
        HorrorEndgameDef    _endgame{};
        PROPERTY( Min = 0.1 )
        float32 _survivorSpeed{ 4.0f };
        PROPERTY( Min = 0.0 )
        float32 _crawlSpeed{ 0.7f }; ///< 빈사 상태에서 기어가는 속도이다
        PROPERTY( Min = 1.0 )
        float32 _hitHasteScale{ 1.5f }; ///< 맞은 직후 달아나는 속도 배율
        PROPERTY( Min = 0.0 )
        float32 _hitHasteTime{ 1.8f };
        PROPERTY( Min = 0.1 )
        float32 _bleedoutTime{ 240.0f }; ///< 빈사 상태에서 혼자 죽기까지의 시간
        PROPERTY( Min = 0.1 )
        float32 _interactRange{ 1.5f };
        PROPERTY( Min = 1 )
        int32 _generatorsRequired{ 5 };
    };
} // namespace sw

namespace sw
{
    /** @brief 살인마 하나입니다. */
    REFLECT()
    struct HorrorKillerDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        string _name{}; ///< 보여 줄 이름(적지 않으면 비어 있다)
        PROPERTY( Min = 0.1 )
        float32 _speedRatio{ 1.15f }; ///< 생존자 속도에 대한 비율이다
        PROPERTY( Min = 0.1 )
        float32 _lungeRange{ 2.5f };
        PROPERTY( Min = 0.0175, Max = 3.1416, Units = "rad" )
        float32 _lungeAngle{ 1.0471976f }; ///< 앞에서 이 각(라디안, 60°) 안의 대상만 맞는다
        PROPERTY( Min = 0.0 )
        float32 _hitCooldown{ 2.7f }; ///< 맞힌 뒤 칼을 닦는 시간이다
        PROPERTY( Min = 0.0 )
        float32 _missCooldown{ 1.5f }; ///< 헛방 뒤의 쿨다운이다
        PROPERTY( Min = 0.0 )
        float32 _cooldownSpeedScale{ 0.3f }; ///< 쿨다운 동안의 속도 배율이다
        PROPERTY( Min = 0.0 )
        float32 _terrorRadius{ 32.0f }; ///< 심장 소리가 들리는 거리이다
        PROPERTY( Min = 0.0 )
        float32 _abilityCooldown{ 10.0f };
        PROPERTY( Min = 0.0 )
        float32 _carrySpeedScale{ 0.8f };
    };
} // namespace sw

namespace sw
{
    /** @brief 행동 하나가 주는 블러드포인트입니다. 받는 점수는 `_points` x 양(수리 진행량, 스킬 체크 창 점수, 또는 1)입니다. */
    REFLECT()
    struct HorrorScoreRule
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _action{};
        PROPERTY( Required )
        hashed_string _category{};
        PROPERTY( Required )
        float32 _points{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief `<Category id="Objectives" cap="8000"/>` — 블러드포인트 범주 하나의 상한입니다. */
    REFLECT()
    struct HorrorScoreCategoryDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required, Min = 0.0 )
        float32 _cap{ 0.0f }; ///< 0 이면 상한이 없다
    };
} // namespace sw

namespace sw
{
    /**
     * @class AsymmetricHorrorRulesCatalog
     * @brief `<AsymmetricHorrorRules survivorSpeed="4" generatorsRequired="5"><Generator time="80" scales="1,1.7,2.1,2.4" .../><Heal .../>
     *        <SkillCheck leadTime="1"><Window grade="Great" early="0.04" late="0.04" score="300" bonus="0.01"/></SkillCheck><Hook .../><Pallet .../>
     *        <Window .../><Endgame .../><Killer id="trapper" speedRatio="1.15" .../><Category id="Objectives" cap="8000"/>
     *        <Score action="Repair" category="Objectives" points="1250"/></AsymmetricHorrorRules>` 를 읽습니다.
     * @details 스킬 체크 창은 기반 `TimingJudge` 가 판정한다(이 카탈로그가 가지고 있고, 판은 빌려 쓴다).
     *          행동 이름은 "Repair", "SkillCheck", "Heal", "Unhook", "Escape", "PalletSTUN", "Vault", "Hit", "Hook", "Sacrifice", "Kick",
     *          "BreakPallet", "LockerGrab" 이며, 목록에 없는 행동은 점수가 없다.
     */
    class SW_KIT_API AsymmetricHorrorRulesCatalog : public XMLCatalog<AsymmetricHorrorRulesCatalog>
    {
        friend class XMLCatalog<AsymmetricHorrorRulesCatalog>;

    public:
        AsymmetricHorrorRulesCatalog();

        const AsymmetricHorrorRules&   getRules() const { return _rules; }
        const TimingJudge&             getJudge() const { return _judge; }
        const HorrorKillerDef*         findKiller( const hashed_string& id ) const { return _catalogKiller.find( id ); }
        const vector<HorrorKillerDef>& getKillers() const { return _catalogKiller.getAll(); }
        const HorrorScoreRule*         findScoreRule( const hashed_string& action ) const;
        /** @brief 범주의 상한을 반환합니다(없으면 0 이며 상한이 없다는 뜻입니다). */
        float32 getCategoryCap( const hashed_string& category ) const { return _categoryCap.getValue( category ); }

    private:
        static constexpr const utf8* kXMLRootName = "AsymmetricHorrorRules"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief 원소 하나를 이름에 맞는 스키마로 읽습니다. 오류면 그 원소를 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadElement( const XMLNode& node, string_view name, string_view sourceName );
        [[nodiscard]] bool loadSkillCheck( const XMLNode& node, string_view sourceName );

        AsymmetricHorrorRules        _rules;
        TimingJudge                  _judge;
        GameCatalog<HorrorKillerDef> _catalogKiller;
        vector<HorrorScoreRule>      _listScoreRule;
        StatBlock                    _categoryCap;
    };
} // namespace sw
