/**
 * @file AIDirectorProfile.h
 * @brief 페이싱 감독의 데이터(`*.director.xml`)를 정의합니다.
 * @details 긴장도 모델, 단계(쌓기, 절정, 쉼)와 단계를 넘어가는 조건, 단계 곡선, 조우와 보상 풀을 담는다.
 *          장르는 모른다. 레프트 4 데드의 AI 디렉터(긴장도에 따라 쌓기, 절정 유지, 쉼),
 *          RDR2 의 무작위 조우(시각, 장소, 플레이어 상태 조건, 쿨다운), 로그라이크의 방 감독(가중 풀, 보상 밀도)이 공통으로 갖는 부분을 데이터로 적는다.
 *          무엇을 낼지(조우 id 의 뜻)는 게임이 정한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Utility/FloatCurve.h"

#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCondition.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 긴장도 신호를 긴장도에 반영하는 방식입니다. */
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
    /** @brief 긴장도 신호 하나(`<Signal>`)입니다. `kind` 는 손으로 읽습니다(이름 표 `toString`). */
    REFLECT()
    struct AIDirectorSignalDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        float32 _scale{ 1.0f };
        PROPERTY( Tooltip = "Cap of what this signal adds at once (Impulse), per second (Rate) or as a floor (Level); negative is none" )
        float32              _max{ -1.0f };
        AIDirectorSignalKind _kind{ AIDirectorSignalKind::Impulse };
        PROPERTY( Tooltip = "A combat signal resets the time since combat and stops the decay" )
        bool _bCombat{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 기본 긴장도 모델(`AIDirectorIntensityModel`)의 설정(`<Intensity>`)입니다. 신호는 `<Signal>` 자식에서 읽습니다. */
    REFLECT()
    struct AIDirectorIntensityDef
    {
        REFLECT_BODY();

        vector<AIDirectorSignalDef> _listSignal{};
        PROPERTY( Min = 0.0 )
        float32 _max{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Decay per second" )
        float32 _decayPerSecond{ 0.1f };
        PROPERTY( Min = 0.0, Tooltip = "Decay starts this long after the last combat signal", Units = s )
        float32 _decayDelay{ 3.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 단계에서 나가는 길 하나(`<Exit>`)입니다. 적은 절이 모두 참이어야 나가며, 적지 않은 절은 늘 참으로 봅니다. */
    REFLECT()
    struct AIDirectorExitDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _to{};
        int32         _toIndex{ -1 };
        PROPERTY( Min = 0.0, Tooltip = "Time spent in the phase is at least this", Units = s )
        float32 _minTime{ 0.0f };
        PROPERTY( Tooltip = "Intensity is at least this; negative is no clause" )
        float32 _intensityAbove{ -1.0f };
        PROPERTY( Tooltip = "Intensity is at most this; negative is no clause" )
        float32 _intensityBelow{ -1.0f };
        PROPERTY( Min = 0.0, Tooltip = "This long has passed since the last combat", Units = s )
        float32 _calmFor{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 페이싱 단계 하나(`<Phase>`)입니다. 쌓기, 절정, 쉼 같은 이름과 단계의 수는 데이터가 정합니다. `spawnTags` 와 `<Curve>` · `<Exit>` 는 손으로 읽습니다. */
    REFLECT()
    struct AIDirectorPhaseDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string             _id{};
        vector<hashed_string>     _listSpawnTag{}; ///< 이 단계에서 낼 스폰 항목의 태그(`SpawnDirector::setAllowedTags`). 비면 모두
        vector<AIDirectorExitDef> _listExit{};     ///< 적은 순서로 본다 — 먼저 맞는 길로 나간다
        FloatCurve                _spawnCurve{};   ///< 단계 안 시간 → 스폰 배율(곡선이 없으면 1)
        PROPERTY( Min = 0.0 )
        float32 _spawnScale{ 1.0f };
        PROPERTY( Min = 0.0 )
        float32 _rewardScale{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 풀이 내는 것의 종류입니다. 게임이 받는 이벤트의 종류와 같습니다. */
    enum class AIDirectorPoolKind : uint8
    {
        Encounter = 0, ///< 조우 · 사건(호드 · 매복 · 길가의 낯선 이)
        Reward         ///< 보상 · 보급(탄약 · 회복 · 상자) — 예산 풀은 단계의 rewardScale 로 쌓인다
    };

    /** @brief 풀이 항목을 고르는 때입니다. */
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
    /** @brief 풀의 항목 하나(`<Encounter>`)입니다. 조우, 이벤트, 보상 중 하나이며 id 의 뜻은 게임이 압니다. `areas` · `pacing` 과 일정 조건 속성은 손으로 읽습니다. */
    REFLECT()
    struct SW_GF_API AIDirectorEncounterDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string         _id{};
        ScheduleCondition     _condition{};  ///< 요일 · 계절 · 날씨 · 하루의 때(`phases`) · 플래그 식 · 태그(플레이어 상태) · 확률
        vector<hashed_string> _listArea{};   ///< 지금 지역 태그 중 하나가 이것이어야 한다. 비면 어디서나
        vector<hashed_string> _listPacing{}; ///< 이 페이싱 단계에서만. 비면 늘
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Cost in a budget pool" )
        float32 _cost{ 1.0f };
        PROPERTY( Min = 0.0, Units = s )
        float32 _cooldown{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Not before this long after the director starts", Units = s )
        float32 _minTime{ 0.0f };
        PROPERTY()
        float32 _minIntensity{ 0.0f };
        PROPERTY( Tooltip = "Negative is no upper bound" )
        float32 _maxIntensity{ -1.0f };
        PROPERTY( Tooltip = "Value handed to the game (strength, size)" )
        float32 _scale{ 1.0f };
        PROPERTY( Min = 0, Tooltip = "Count handed to the game (how many to spawn)" )
        int32 _count{ 1 };
        PROPERTY( Tooltip = "Cap per run; -1 is none" )
        int32 _maxCount{ -1 };
        PROPERTY( Min = 0, Tooltip = "Pacing cycles completed (returns to the start phase) before this can appear" )
        int32 _minCycle{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 가중 풀 하나(`<Pool>`)입니다. `kind` · `trigger` · `pacing` 은 손으로 읽습니다. */
    REFLECT()
    struct AIDirectorPoolDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string                  _id{};
        vector<AIDirectorEncounterDef> _listEncounter{};
        vector<hashed_string>          _listPacing{}; ///< 이 페이싱 단계에서만 돈다(시계 · 예산도 멈춘다). 비면 늘
        PROPERTY( Tooltip = "Phase of a PhaseEnter pool" )
        hashed_string _phase{};
        PROPERTY( Name = "need", Tooltip = "Need signal a Budget pool reads (a signal of the intensity model)" )
        hashed_string _needSignal{};
        PROPERTY( Min = 0.01, Units = s )
        float32 _interval{ 10.0f };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _chance{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Least time between two picks from this pool", Units = s )
        float32 _cooldown{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _perMinute{ 1.0f };
        PROPERTY( Min = 0.0 )
        float32 _maxBudget{ 5.0f };
        PROPERTY()
        float32 _needScale{ 0.0f };
        int32   _phaseIndex{ -1 };
        PROPERTY( Min = 1 )
        int32                 _picks{ 1 };
        AIDirectorPoolKind    _kind{ AIDirectorPoolKind::Encounter };
        AIDirectorPoolTrigger _trigger{ AIDirectorPoolTrigger::Interval };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AIDirectorProfile
     * @brief `*.director.xml` 하나입니다.
     * @details 모르는 원소, 속성, 종류, 단계, 신호와 겹친 id, 단계 없음, 깨진 값과 PROPERTY( Min · Max ) 밖 값은 로드 오류이며, 오류를 남기고 false 를 반환한다. 칸은 `GameDataXML::readFields` 가 읽는다.
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
        /** @brief 이 프로필이 내는 모든 조우와 보상 id 를 @p outListID 에 겹치지 않게 붙입니다. 게임이 아는 id 인지 검사할 때 씁니다. */
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
