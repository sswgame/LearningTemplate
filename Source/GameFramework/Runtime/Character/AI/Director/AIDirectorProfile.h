/**
 * @file AIDirectorProfile.h
 * @brief 페이싱 감독의 데이터(`*.director.xml`)를 정의합니다.
 * @details 긴장도 모델, 단계(쌓기, 절정, 쉼)와 단계를 넘어가는 조건, 단계 곡선, 조우와 보상 풀을 담는다.
 *          장르는 모른다. 레프트 4 데드의 AI 디렉터(긴장도에 따라 쌓기, 절정 유지, 쉼),
 *          RDR2 의 무작위 조우(시각, 장소, 플레이어 상태 조건, 쿨다운), 로그라이크의 방 감독(가중 풀, 보상 밀도)이 공통으로 갖는 부분을 데이터로 적는다.
 *          무엇을 낼지(조우 id 의 뜻)는 게임이 정한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Curve/FloatCurve.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCondition.h"

namespace sw
{
    class XMLNode;

    /** @brief 긴장도 신호를 긴장도에 반영하는 방식입니다(파일에는 열거자 이름). */
    ENUM()
    enum class AIDirectorSignalKind : uint8
    {
        Impulse = 0, ///< `addSignal( 양 )` 한 번에 양 × scale 이 더해진다(입은 피해, 가까이서 쓰러뜨림)
        Rate,        ///< `setSignal( 값 )` 이 유지되는 동안 초마다 값 × scale 이 더해진다(가까운 적 수)
        Level        ///< 긴장도의 하한이다. 긴장도는 Σ 값 × scale(각각 max 까지) 아래로 내려가지 않는다(자원 부족)
    };

} // namespace sw

namespace sw
{
    /** @brief 긴장도 신호 하나(`<Signal>`)입니다. */
    REFLECT()
    struct AIDirectorSignalDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        float32 _scale{ 1.0f };
        PROPERTY( Tooltip = "Cap of what this signal adds at once (Impulse), per second (Rate) or as a floor (Level); negative is none" )
        float32 _max{ -1.0f };
        PROPERTY( Required )
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
        hashed_string   _to{};
        optional<int32> _toIndex{}; ///< 읽을 때 푼 `_to` 의 단계 번호(`resolveReferences` — 모르는 이름이면 읽기 실패)
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
        hashed_string _id{};
        PROPERTY( Name = "spawnTags" )
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
    ENUM()
    enum class AIDirectorPoolKind : uint8
    {
        Encounter = 0, ///< 조우나 사건(호드, 매복, 길가의 낯선 이)
        Reward         ///< 보상이나 보급(탄약, 회복, 상자). 예산 풀은 단계의 rewardScale 만큼 쌓인다
    };

    /** @brief 풀이 항목을 고르는 때입니다. */
    ENUM()
    enum class AIDirectorPoolTrigger : uint8
    {
        PhaseEnter = 0, ///< 단계(`phase`)에 들어설 때 `picks` 번
        Interval,       ///< `interval` 초마다 `chance` 확률로 한 번
        Budget          ///< 예산이 `perMinute` × 단계 배율 × (1 + needScale × 필요 신호) 로 쌓이고, 고른 것의 `cost` 에 닿으면 낸다
    };

} // namespace sw

namespace sw
{
    /** @brief 풀의 항목 하나(`<Encounter>`)입니다. 조우, 이벤트, 보상 중 하나이며 id 의 뜻은 게임이 압니다. 일정 조건 속성은 손으로 읽습니다. */
    REFLECT()
    struct SW_GF_API AIDirectorEncounterDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string     _id{};
        ScheduleCondition _condition{}; ///< 요일, 계절, 날씨, 하루의 때(`phases`), 플래그 식, 태그(플레이어 상태), 확률
        PROPERTY( Name = "areas" )
        vector<hashed_string> _listArea{}; ///< 지금 지역 태그 중 하나가 이것이어야 한다. 비면 어디서나
        PROPERTY( Name = "pacing" )
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
        /** @brief `_maxIntensity` 가 이 값이면(음수) 긴장도 위 끝이 없다. */
        static constexpr float32 kNoIntensityCap = -1.0f;
        /** @brief `_maxCount` 가 이 값이면 한 판에 나오는 수에 상한이 없다. */
        static constexpr int32 kUnlimitedCount = -1;

        PROPERTY( Tooltip = "Negative is no upper bound" )
        float32 _maxIntensity{ kNoIntensityCap };
        PROPERTY( Tooltip = "Value handed to the game (strength, size)" )
        float32 _scale{ 1.0f };
        PROPERTY( Min = 0, Tooltip = "Count handed to the game (how many to spawn)" )
        int32 _count{ 1 };
        PROPERTY( Min = -1, Tooltip = "Cap per run; -1 is none" )
        int32 _maxCount{ kUnlimitedCount };
        PROPERTY( Min = 0, Tooltip = "Pacing cycles completed (returns to the start phase) before this can appear" )
        int32 _minCycle{ 0 };

        /** @brief 한 판에 이미 @p count 번 나왔을 때 더 나올 수 있는지입니다(상한이 없으면 늘 된다). */
        bool canTriggerAgain( int32 count ) const { return _maxCount == kUnlimitedCount || count < _maxCount; }
        /** @brief 긴장도 @p intensity 가 위 끝을 넘었는지입니다(위 끝이 없으면 늘 아니다 — 음수가 위 끝 없음이다). */
        bool isAboveMaxIntensity( float32 intensity ) const { return _maxIntensity >= 0.0f && intensity > _maxIntensity; }
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
        PROPERTY( Name = "pacing" )
        vector<hashed_string> _listPacing{}; ///< 이 페이싱 단계에서만 돈다(시계 · 예산도 멈춘다). 비면 늘
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
        float32         _needScale{ 0.0f };
        optional<int32> _phaseIndex{}; ///< `PhaseEnter` 풀이 기다리는 단계 번호(읽을 때 푼다). 다른 트리거는 비어 있다
        PROPERTY( Min = 1 )
        int32 _picks{ 1 };
        PROPERTY() ///< 적지 않으면 조우 풀이다
        AIDirectorPoolKind _kind{ AIDirectorPoolKind::Encounter };
        PROPERTY( Required )
        AIDirectorPoolTrigger _trigger{ AIDirectorPoolTrigger::Interval };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AIDirectorProfile
     * @brief `*.director.xml` 하나입니다.
     * @details 모르는 원소, 속성, 종류, 단계, 신호와 겹친 id, 단계 없음, 깨진 값과 PROPERTY( Min · Max ) 밖 값은 로드 오류이며, 오류를 남기고 false 를 반환한다. 칸은 `XMLFields::readFields` 가 읽는다.
     * @code
     *     <AIDirector startPhase="BuildUp">
     *       <Calendar weathers="sunny,rain"/>                                         <!-- 조건 이름 검사(선택) -->
     *       <Intensity max="1" decayPerSecond="0.08" decayDelay="4">
     *         <Signal id="damageTaken" kind="Impulse" scale="0.02" combat="true"/>
     *         <Signal id="enemiesNearby" kind="Rate" scale="0.01" max="0.06"/>
     *         <Signal id="lowResources" kind="Level" scale="0.3"/>
     *       </Intensity>
     *       <Phase id="BuildUp" spawnScale="1" spawnTags="Common">
     *         <Curve time="0" scale="0.5"/><Curve time="40" scale="1.5"/>
     *         <Exit to="Peak" intensityAbove="0.8" minTime="5"/><Exit to="Peak" minTime="90"/>
     *       </Phase>
     *       <Phase id="Peak" spawnScale="2"><Exit to="Relax" minTime="5"/></Phase>
     *       <Phase id="Relax" spawnScale="0" rewardScale="2"><Exit to="BuildUp" intensityBelow="0.25" calmFor="5" minTime="15"/></Phase>
     *       <Pool id="horde" kind="Encounter" trigger="PhaseEnter" phase="Peak">
     *         <Encounter id="swarm" weight="3" count="6"/><Encounter id="elite" weight="1" cooldown="120" minCycle="2"/>
     *       </Pool>
     *       <Pool id="ambient" trigger="Interval" interval="20" chance="0.5" pacing="BuildUp,Relax">
     *         <Encounter id="ambush" areas="Forest" phases="Night" tags="Player.Mounted" notTags="Player.Hidden"/>
     *       </Pool>
     *       <Pool id="supplies" kind="Reward" trigger="Budget" perMinute="2" maxBudget="4" need="lowResources" needScale="2">
     *         <Encounter id="ammo" cost="1" weight="3"/><Encounter id="medkit" cost="2" maxIntensity="0.5"/>
     *       </Pool>
     *     </AIDirector>
     * @endcode
     */
    class SW_GF_API AIDirectorProfile
    {
        friend struct AssetTypeLoader;

    public:
        AIDirectorProfile();

        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );
        void               clear();

        [[nodiscard]] optional<int32> findPhaseIndex( const hashed_string& phaseID ) const;
        [[nodiscard]] optional<int32> findSignalIndex( const hashed_string& signalID ) const;

        const AIDirectorIntensityDef&     getIntensity() const { return _intensity; }
        const vector<AIDirectorPhaseDef>& getPhases() const { return _listPhase; }
        const vector<AIDirectorPoolDef>&  getPools() const { return _listPool; }
        /** @brief 시작 단계 번호입니다. 읽은 프로필이면 늘 있다(없으면 읽기 실패) — 읽지 않은 프로필이면 비어 있다. */
        optional<int32> getStartPhaseIndex() const { return _startPhaseIndex; }
        /** @brief 이 프로필이 내는 모든 조우와 보상 id 를 @p outListID 에 겹치지 않게 붙입니다. 게임이 아는 id 인지 검사할 때 씁니다. */
        void collectEncounterIDs( vector<hashed_string>& outListID ) const;

    private:
        /** @brief 리소스 경로의 프로필을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<AIDirectorProfile>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool readIntensity( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool readPhase( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool readPool( const XMLNode& node, const ScheduleConditionSchema& vocabulary, string_view sourceName );
        [[nodiscard]] bool resolveReferences( string_view sourceName );
        bool               validatePacingNames( const vector<hashed_string>& listPacing, const hashed_string& ownerID, string_view sourceName ) const;

    private:
        AIDirectorIntensityDef     _intensity;
        vector<AIDirectorPhaseDef> _listPhase;
        vector<AIDirectorPoolDef>  _listPool;
        hashed_string              _startPhase;
        optional<int32>            _startPhaseIndex;
    };

    /**
     * @brief 페이싱 프로필을 경로마다 한 벌 나눠 줍니다("aidirector", 인스턴스화는 `AIDirectorProfile.cpp`).
     * @details 다시 읽기는 떼어 낸다(`Detach`) — 감독(`AIDirector`)은 국면 · 풀 번호로 프로필을 읽고 그 번호가 상태(저장 포함)에 남는다. 제자리에서 바뀌면 번호가 어긋난다.
     *          쥔 쪽은 다시 읽은 횟수가 바뀌면 새 객체를 받아 감독이 번호를 이어 옮긴다(`AIDirector::rebindProfile` — 구조가 같은 변경만, 구조가 바뀌면 그 판은 옛 객체로 돈다).
     *          언리얼 AI 감독 데이터 에셋 한 벌과 같은 자리다(기억에 의존).
     */
    extern template class SW_GF_API_EXTERN_TEMPLATE SharedAssetCache<AIDirectorProfile>;
} // namespace sw
