/**
 * @file AsymmetricHorrorRules.h
 * @brief 비대칭 공포의 데이터 — 생존자 속도 · 출혈 · 치료, 발전기(시간 · 인원 효율 · 스킬 체크 · 걷어차기 퇴행), 갈고리 단계, 판자 · 창틀 · 사물함, 탈출구 · 붕괴 · 해치,
 *        살인마(속도 비율 · 공격 쿨다운 · 위협 반경 · 능력), 블러드포인트 범주 · 행동 점수입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 판 규칙의 수치입니다. 시간은 초, 거리 · 속도는 m · m/s, 진행량은 0..1 입니다. */
    struct AsymmetricHorrorRules
    {
        vector<float32> _listRepairScale{}; ///< [n−1] = n 명이 고칠 때 속도 배율(사람이 늘수록 한 명 몫은 준다)
        vector<float32> _listHealScale{};   ///< [n−1] = n 명이 치료할 때 속도 배율
        StatBlock       _skillCheckBonus{}; ///< 스킬 체크 등급 → 더하는 진행량
        float32         _survivorSpeed{ 4.0f };
        float32         _crawlSpeed{ 0.7f };    ///< 빈사 기어가기
        float32         _hitHasteScale{ 1.5f }; ///< 맞은 직후 달아나는 속도 배율
        float32         _hitHasteTime{ 1.8f };
        float32         _bleedoutTime{ 240.0f }; ///< 빈사에서 혼자 죽기까지
        float32         _healTime{ 16.0f };
        float32         _repairTime{ 80.0f };
        float32         _repairSkillCheckInterval{ 10.0f };
        float32         _healSkillCheckInterval{ 8.0f };
        float32         _skillCheckLeadTime{ 1.0f };
        float32         _skillCheckFailPenalty{ 0.1f }; ///< 놓치면 깎는 진행량(폭발 · 퇴행)
        float32         _skillCheckNoiseRadius{ 60.0f };
        float32         _kickPenalty{ 0.05f };      ///< 걷어차는 순간 깎는 진행량
        float32         _kickRegression{ 0.0025f }; ///< 걷어찬 뒤 아무도 없으면 초당 줄어드는 진행량
        float32         _hookStageTime{ 60.0f };
        float32         _struggleGrace{ 1.0f }; ///< 2 단계(몸부림)에서 몸부림을 이만큼 넘게 멈추면 희생
        float32         _wiggleTime{ 16.0f };   ///< 들린 채 몸부림으로 빠져나오기까지
        float32         _wiggleStunTime{ 3.0f };
        float32         _interactRange{ 1.5f };
        float32         _palletStunTime{ 2.0f };
        float32         _palletStunRange{ 1.5f };
        float32         _palletBreakTime{ 2.6f };
        float32         _fastVaultTime{ 0.5f };
        float32         _mediumVaultTime{ 1.2f };
        float32         _killerVaultTime{ 1.7f };
        float32         _fastVaultSpeedRatio{ 0.9f }; ///< 생존자 속도의 이 비율 이상으로 달려 들어가면 빠른 넘기
        float32         _vaultNoiseRadius{ 40.0f };   ///< 빠른 넘기의 소음
        float32         _windowBlockTime{ 15.0f };
        float32         _lockerSearchTime{ 1.0f };
        float32         _gateOpenTime{ 20.0f };
        float32         _collapseTime{ 120.0f };
        float32         _collapseSlowScale{ 0.5f }; ///< 누가 빈사 · 갈고리에 있으면 붕괴가 이 배율로 흐른다
        int32           _repairMaxParticipants{ 4 };
        int32           _healMaxParticipants{ 3 };
        int32           _generatorsRequired{ 5 };
        int32           _maxHookStage{ 3 };       ///< 이 번째로 걸리면 바로 희생
        int32           _windowBlockCount{ 3 };   ///< 추격 중 같은 창을 이만큼 넘으면 막힌다
        int32           _hatchSurvivorCount{ 1 }; ///< 남은 생존자가 이만큼이면 해치가 열린다
    };
} // namespace sw

namespace sw
{
    /** @brief 살인마 하나입니다. */
    struct HorrorKillerDef
    {
        hashed_string _id{};
        string        _name{};
        float32       _speedRatio{ 1.15f }; ///< 생존자 속도 대비
        float32       _lungeRange{ 2.5f };
        float32       _lungeAngle{ 60.0f };        ///< 앞에서 이 각(도) 안만 맞는다
        float32       _hitCooldown{ 2.7f };        ///< 맞힌 뒤 칼 닦기
        float32       _missCooldown{ 1.5f };       ///< 헛방 뒤
        float32       _cooldownSpeedScale{ 0.3f }; ///< 쿨다운 동안 속도 배율
        float32       _terrorRadius{ 32.0f };      ///< 심장 소리가 들리는 거리
        float32       _abilityCooldown{ 10.0f };
        float32       _carrySpeedScale{ 0.8f };
    };
} // namespace sw

namespace sw
{
    /** @brief 행동 하나가 주는 블러드포인트입니다. 받는 점수 = `_points` × 양(수리 진행량 · 스킬 체크 창 점수 · 1). */
    struct HorrorScoreRule
    {
        hashed_string _action{};
        hashed_string _category{};
        float32       _points{ 0.0f };
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
     * @details 스킬 체크 창은 기반 `TimingJudge` 가 판정합니다(이 카탈로그가 쥐고, 판은 빌려 간다).
     *          행동 이름은 "Repair" · "SkillCheck" · "Heal" · "Unhook" · "Escape" · "PalletStun" · "Vault" · "Hit" · "Hook" · "Sacrifice" · "Kick" ·
     *          "BreakPallet" · "LockerGrab" 입니다 — 없는 행동은 점수가 없습니다.
     */
    class SW_GF_API AsymmetricHorrorRulesCatalog : public XMLCatalog<AsymmetricHorrorRulesCatalog>
    {
        friend class XMLCatalog<AsymmetricHorrorRulesCatalog>;

    public:
        AsymmetricHorrorRulesCatalog();

        const AsymmetricHorrorRules&   getRules() const { return _rules; }
        const TimingJudge&             getJudge() const { return _judge; }
        const HorrorKillerDef*         findKiller( const hashed_string& id ) const { return _catalogKiller.find( id ); }
        const vector<HorrorKillerDef>& getKillers() const { return _catalogKiller.getAll(); }
        const HorrorScoreRule*         findScoreRule( const hashed_string& action ) const;
        /** @brief 범주의 상한입니다(없으면 0 = 상한 없음). */
        float32 getCategoryCap( const hashed_string& category ) const { return _categoryCap.getValue( category, 0.0f ); }

    private:
        static constexpr const utf8* kXMLRootName = "AsymmetricHorrorRules"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        AsymmetricHorrorRules        _rules;
        TimingJudge                  _judge;
        GameCatalog<HorrorKillerDef> _catalogKiller;
        vector<HorrorScoreRule>      _listScoreRule;
        StatBlock                    _categoryCap;
    };
} // namespace sw
