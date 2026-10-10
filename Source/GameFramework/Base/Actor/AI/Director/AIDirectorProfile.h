/**
 * @file AIDirectorProfile.h
 * @brief 페이싱 감독의 데이터(`*.director.xml`) — 긴장도 모델 · 단계(쌓기 · 절정 · 쉼)와 넘어가는 조건 · 단계 곡선 · 조우/보상 풀입니다.
 * @details 장르를 모릅니다. 레프트 4 데드 AI 디렉터(긴장도 → 쌓기 · 절정 유지 · 쉼), RDR2 무작위 조우(시각 · 장소 · 플레이어 상태 조건 · 쿨다운),
 *          로그라이크 방 감독(가중 풀 · 보상 밀도)의 공통 부분을 데이터로 적습니다. 무엇을 낼지(조우 id 의 뜻)는 게임이 정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCondition.h"
#include "GameFramework/Base/Foundation/Data/GameCurve.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 긴장도 신호가 긴장도에 들어가는 방식입니다. */
    enum class AIDirectorSignalKind : uint8
    {
        Impulse = 0, ///< `addSignal( 양 )` 한 번에 양 × scale 이 더해진다(입은 피해 · 가까이서 쓰러뜨림)
        Rate,        ///< `setSignal( 값 )` 이 유지되는 동안 초마다 값 × scale 이 더해진다(가까운 적 수)
        Level        ///< 긴장도의 바닥 — 긴장도는 Σ 값 × scale(각각 max 까지) 아래로 내려가지 않는다(자원 부족)
    };

    SW_GF_API const utf8* toString( AIDirectorSignalKind kind );
} // namespace sw

namespace sw
{
    /** @brief 긴장도 신호 하나입니다. */
    struct AIDirectorSignalDef
    {
        hashed_string        _id{};
        float32              _scale{ 1.0f };
        float32              _max{ -1.0f }; ///< 이 신호가 한 번에(Impulse) · 초마다(Rate) · 바닥으로(Level) 더하는 상한. 음수면 없음
        AIDirectorSignalKind _kind{ AIDirectorSignalKind::Impulse };
        uint8                _bCombat{ SW_FALSE }; ///< 싸움 신호 — 들어오면 "싸움 뒤 지난 시간" 이 0 이 되고 식기가 멈춘다
    };
} // namespace sw

namespace sw
{
    /** @brief 기본 긴장도 모델(`AIDirectorIntensityModel`)의 설정입니다. */
    struct AIDirectorIntensityDef
    {
        vector<AIDirectorSignalDef> _listSignal{};
        float32                     _max{ 1.0f };
        float32                     _decayPerSecond{ 0.1f }; ///< 식는 빠르기(초마다 빠지는 양)
        float32                     _decayDelay{ 3.0f };     ///< 마지막 싸움 신호 뒤 이만큼(s) 지나야 식기 시작한다
    };
} // namespace sw

namespace sw
{
    /** @brief 단계에서 나가는 길 하나입니다. 적은 절이 모두 참이어야 나갑니다(적지 않은 절은 늘 참). */
    struct AIDirectorExitDef
    {
        hashed_string _to{};
        int32         _toIndex{ -1 };
        float32       _minTime{ 0.0f };         ///< 단계에 머문 시간(s)이 이 이상
        float32       _intensityAbove{ -1.0f }; ///< 긴장도가 이 이상(음수면 절 없음)
        float32       _intensityBelow{ -1.0f }; ///< 긴장도가 이 이하(음수면 절 없음)
        float32       _calmFor{ 0.0f };         ///< 마지막 싸움 뒤 이만큼(s) 지났다
    };
} // namespace sw

namespace sw
{
    /** @brief 페이싱 단계 하나입니다(쌓기 · 절정 · 쉼 — 이름과 수는 데이터가 정한다). */
    struct AIDirectorPhaseDef
    {
        hashed_string             _id{};
        vector<hashed_string>     _listSpawnTag{}; ///< 이 단계에서 낼 스폰 항목의 태그(`SpawnDirector::setAllowedTags`). 비면 모두
        vector<AIDirectorExitDef> _listExit{};     ///< 적은 순서로 본다 — 먼저 맞는 길로 나간다
        GameCurve                 _spawnCurve{};   ///< 단계 안 시간 → 스폰 배율(곡선이 없으면 1)
        float32                   _spawnScale{ 1.0f };
        float32                   _rewardScale{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 풀이 내는 것의 종류입니다 — 게임이 받는 사건의 갈래입니다. */
    enum class AIDirectorPoolKind : uint8
    {
        Encounter = 0, ///< 조우 · 사건(호드 · 매복 · 길가의 낯선 이)
        Reward         ///< 보상 · 보급(탄약 · 회복 · 상자) — 예산 풀은 단계의 rewardScale 로 쌓인다
    };

    /** @brief 풀이 고르는 때입니다. */
    enum class AIDirectorPoolTrigger : uint8
    {
        PhaseEnter = 0, ///< 단계(`phase`)에 들어설 때 `picks` 번
        Interval,       ///< `interval` 초마다 `chance` 확률로 한 번
        Budget          ///< 예산이 `perMinute` × 단계 배율 × (1 + needScale × 필요 신호) 로 쌓이고, 고른 것의 `cost` 에 닿으면 낸다
    };

    SW_GF_API const utf8* toString( AIDirectorPoolKind kind );
    SW_GF_API const utf8* toString( AIDirectorPoolTrigger trigger );
} // namespace sw

namespace sw
{
    /** @brief 풀의 항목 하나 — 조우 · 사건 · 보상입니다. id 의 뜻은 게임이 압니다. */
    struct SW_GF_API AIDirectorEncounterDef
    {
        hashed_string         _id{};
        ScheduleCondition     _condition{};  ///< 요일 · 계절 · 날씨 · 하루의 때(`phases`) · 플래그 식 · 태그(플레이어 상태) · 확률
        vector<hashed_string> _listArea{};   ///< 지금 지역 태그 중 하나가 이것이어야 한다. 비면 어디서나
        vector<hashed_string> _listPacing{}; ///< 이 페이싱 단계에서만. 비면 늘
        float32               _weight{ 1.0f };
        float32               _cost{ 1.0f }; ///< 예산 풀에서 쓰는 값
        float32               _cooldown{ 0.0f };
        float32               _minTime{ 0.0f }; ///< 감독 시작 뒤 이 시간(s) 전에는 내지 않는다
        float32               _minIntensity{ 0.0f };
        float32               _maxIntensity{ -1.0f }; ///< 음수면 상한 없음
        float32               _scale{ 1.0f };         ///< 게임에 넘기는 값(세기 · 크기)
        int32                 _count{ 1 };            ///< 게임에 넘기는 수(낼 개체 수)
        int32                 _maxCount{ -1 };        ///< 한 판에 낼 상한. −1 이면 없음
        int32                 _minCycle{ 0 };         ///< 페이싱이 이만큼 돌았어야(시작 단계로 돌아온 수)
    };
} // namespace sw

namespace sw
{
    /** @brief 가중 풀 하나입니다. */
    struct AIDirectorPoolDef
    {
        hashed_string                  _id{};
        vector<AIDirectorEncounterDef> _listEncounter{};
        vector<hashed_string>          _listPacing{}; ///< 이 페이싱 단계에서만 돈다(시계 · 예산도 멈춘다). 비면 늘
        hashed_string                  _phase{};      ///< PhaseEnter 의 단계
        hashed_string                  _needSignal{}; ///< Budget 이 읽는 필요 신호(긴장도 모델의 신호 값)
        float32                        _interval{ 10.0f };
        float32                        _chance{ 1.0f };
        float32                        _cooldown{ 0.0f }; ///< 이 풀에서 두 번 고르는 사이의 최소 시간(s)
        float32                        _perMinute{ 1.0f };
        float32                        _maxBudget{ 5.0f };
        float32                        _needScale{ 0.0f };
        int32                          _phaseIndex{ -1 };
        int32                          _picks{ 1 };
        AIDirectorPoolKind             _kind{ AIDirectorPoolKind::Encounter };
        AIDirectorPoolTrigger          _trigger{ AIDirectorPoolTrigger::Interval };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AIDirectorProfile
     * @brief `*.director.xml` 하나입니다. 모르는 원소 · 속성 · 종류 · 단계 · 신호, 겹친 id, 단계 없음은 로드 오류입니다(경고하고 false).
     * @code
     *     <AIDirector startPhase="BuildUp">
     *       <Calendar weathers="sunny,rain"/>                                         <!-- 조건 이름 검사(선택) -->
     *       <Intensity max="1" decayPerSecond="0.08" decayDelay="4">
     *         <Signal id="damageTaken" kind="impulse" scale="0.02" combat="true"/>
     *         <Signal id="enemiesNearby" kind="rate" scale="0.01" max="0.06"/>
     *         <Signal id="lowResources" kind="level" scale="0.3"/>
     *       </Intensity>
     *       <Phase id="BuildUp" spawnScale="1" spawnTags="Common">
     *         <Curve time="0" scale="0.5"/><Curve time="40" scale="1.5"/>
     *         <Exit to="Peak" intensityAbove="0.8" minTime="5"/><Exit to="Peak" minTime="90"/>
     *       </Phase>
     *       <Phase id="Peak" spawnScale="2"><Exit to="Relax" minTime="5"/></Phase>
     *       <Phase id="Relax" spawnScale="0" rewardScale="2"><Exit to="BuildUp" intensityBelow="0.25" calmFor="5" minTime="15"/></Phase>
     *       <Pool id="horde" kind="encounter" trigger="phaseEnter" phase="Peak">
     *         <Encounter id="swarm" weight="3" count="6"/><Encounter id="elite" weight="1" cooldown="120" minCycle="2"/>
     *       </Pool>
     *       <Pool id="ambient" trigger="interval" interval="20" chance="0.5" pacing="BuildUp,Relax">
     *         <Encounter id="ambush" areas="Forest" phases="Night" tags="Player.Mounted" notTags="Player.Hidden"/>
     *       </Pool>
     *       <Pool id="supplies" kind="reward" trigger="budget" perMinute="2" maxBudget="4" need="lowResources" needScale="2">
     *         <Encounter id="ammo" cost="1" weight="3"/><Encounter id="medkit" cost="2" maxIntensity="0.5"/>
     *       </Pool>
     *     </AIDirector>
     * @endcode
     */
    class SW_GF_API AIDirectorProfile
    {
    public:
        AIDirectorProfile();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );
        void               clear();

        int32 findPhaseIndex( const hashed_string& phaseID ) const;
        int32 findSignalIndex( const hashed_string& signalID ) const;

        const AIDirectorIntensityDef&     getIntensity() const { return _intensity; }
        const vector<AIDirectorPhaseDef>& getPhases() const { return _listPhase; }
        const vector<AIDirectorPoolDef>&  getPools() const { return _listPool; }
        int32                             getStartPhaseIndex() const { return _startPhaseIndex; }
        /** @brief 이 프로필이 내는 모든 조우 · 보상 id 를 @p outListID 에 붙입니다(겹치지 않게) — 게임이 아는 id 인지 검사할 때 씁니다. */
        void collectEncounterIDs( vector<hashed_string>& outListID ) const;

    private:
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool readIntensity( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool readPhase( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool readPool( const XMLNode& node, const ScheduleConditionVocabulary& vocabulary, string_view sourceName );
        bool               resolveReferences( string_view sourceName );
        bool               validatePacingNames( const vector<hashed_string>& listPacing, const hashed_string& ownerID, string_view sourceName ) const;

        AIDirectorIntensityDef     _intensity;
        vector<AIDirectorPhaseDef> _listPhase;
        vector<AIDirectorPoolDef>  _listPool;
        hashed_string              _startPhase;
        int32                      _startPhaseIndex;
    };
} // namespace sw
