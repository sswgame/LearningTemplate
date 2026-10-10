/**
 * @file AIDirector.h
 * @brief 페이싱 감독 — 긴장도를 읽어 단계(쌓기 · 절정 · 쉼)를 넘기고, 스폰 예산 · 조우/사건 고르기 · 보상 밀도를 정합니다. 장르를 모릅니다.
 * @details 레프트 4 데드 AI 디렉터의 페이싱(긴장도가 차면 절정을 잠깐 유지하고 쉼으로 — 쉬는 동안은 내지 않는다), RDR2 무작위 조우(가중 풀 ·
 *          쿨다운 · 시각 · 장소 · 플레이어 상태 조건), 로그라이크 방 감독(보상 밀도 예산)을 한 틀로 둡니다. 스폰 자체는 `SpawnDirector` 가 하고
 *          (단계가 예산 배율 · 태그를 바꾼다), 조건은 일정의 `ScheduleCondition` 을, 난수는 `GameRandom` 을 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/AI/Director/AIDirectorIntensity.h"
#include "GameFramework/Base/Actor/AI/Schedule/ScheduleCondition.h"
#include "GameFramework/Base/Actor/AI/SpawnDirector.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AIDirectorEncounterDef;
    struct AIDirectorPoolDef;

    class AIDirectorProfile;
    class Archive;
    class WorldClock;

    /** @brief 조우 조건을 판정할 때의 세계 · 플레이어 상태입니다. 게임이 바뀔 때 `AIDirector::setContext` 로 넘깁니다. */
    struct SW_GF_API AIDirectorContext
    {
        ScheduleConditionContext _condition{};   ///< 플래그 · 플레이어 태그(`_pNpcTags`) · 세계 태그 · 요일 · 계절 · 날씨 · 하루의 때
        vector<hashed_string>    _listAreaTag{}; ///< 플레이어가 있는 지역의 태그(`areas` 조건)

        /** @brief 시계에서 날 · 계절의 날 · 계절 · 하루의 때를 채웁니다(요일 · 날씨는 게임의 달력이 채운다). */
        void fillFromClock( const WorldClock& clock );
    };
} // namespace sw

namespace sw
{
    /** @brief 감독이 낸 일의 종류입니다. */
    enum class AIDirectorEventKind : uint8
    {
        PhaseChanged = 0, ///< `_id` = 새 단계, `_source` = 앞 단계(시작이면 빈 이름), `_detail` = 나간 길 번호(−1 = 시작 · 강제)
        Spawned,          ///< `_id` = 스폰 항목 — 게임이 만든다, 죽으면 `_spawnID` 로 `notifyDespawned`
        Despawned,        ///< `_id` = 스폰 항목, `_cost` = 돌려받은 예산
        Encounter,        ///< `_id` = 조우 · 사건, `_source` = 풀, `_count` · `_scale` = 데이터의 값
        Reward            ///< `_id` = 보상, `_source` = 풀, `_count` · `_scale` = 데이터의 값
    };

    SW_GF_API const utf8* toString( AIDirectorEventKind kind );

    /** @brief 감독이 낸 일 하나입니다. 추적(`dumpTrace`)도 이것을 쌓습니다. */
    struct AIDirectorEvent
    {
        hashed_string       _id{};
        hashed_string       _source{};
        float32             _time{ 0.0f };
        float32             _intensity{ 0.0f };
        float32             _scale{ 1.0f };
        float32             _cost{ 0.0f };
        uint32              _spawnID{ 0 };
        int32               _count{ 0 };
        int32               _detail{ -1 };
        AIDirectorEventKind _kind{ AIDirectorEventKind::PhaseChanged };
    };
} // namespace sw

namespace sw
{
    /** @brief 항목을 막은 까닭의 비트입니다(`AIDirector::computeBlockMask` · `explain`). */
    struct AIDirectorBlock
    {
        static constexpr uint32 kWeight    = 1u << 0; ///< 가중치 0
        static constexpr uint32 kPacing    = 1u << 1; ///< 지금 페이싱 단계가 아니다
        static constexpr uint32 kCooldown  = 1u << 2; ///< 항목 쿨다운 중
        static constexpr uint32 kMaxCount  = 1u << 3; ///< 한 판 상한에 닿았다
        static constexpr uint32 kCycle     = 1u << 4; ///< 페이싱이 덜 돌았다
        static constexpr uint32 kTime      = 1u << 5; ///< 시작 뒤 시간이 모자라다
        static constexpr uint32 kIntensity = 1u << 6; ///< 긴장도 범위 밖
        static constexpr uint32 kArea      = 1u << 7; ///< 지역 태그가 맞지 않는다
        static constexpr uint32 kCondition = 1u << 8; ///< 일정 조건(요일 · 날씨 · 때 · 플래그 · 태그 · 확률)이 거짓
        static constexpr uint32 kCost      = 1u << 9; ///< 비용이 풀의 예산 상한보다 크다(예산 풀)
        static constexpr uint32 kCount     = 10;

        /** @brief 비트 번호의 이름입니다. */
        static const utf8* getName( uint32 bitIndex );
    };
} // namespace sw

namespace sw
{
    /**
     * @class AIDirector
     * @brief 프로필(빌림) 하나로 도는 페이싱 감독입니다. 게임은 신호를 긴장도 모델에 넣고, `update` 뒤 `drainEvents` 로 낼 것을 받아 만듭니다.
     * @details **한 프레임의 순서**: 긴장도 모델 → 시간 → 단계 넘기기(나가는 길을 적은 순서로, 한 프레임에 최대 `kMaxTransitionsPerUpdate`) →
     *          스폰(단계 배율 × 단계 곡선을 `SpawnDirector` 예산에) → 풀(Interval · Budget). PhaseEnter 풀은 단계에 들어선 그 자리에서 고릅니다.
     *          **결정성**: 씨앗이 같고 같은 dt · 같은 신호 · 같은 문맥으로 부르면 같은 단계 · 같은 고르기를 같은 때에 냅니다(`computeStateHash`).
     *          스폰 감독의 씨앗은 감독 씨앗에서 나옵니다. 조건의 확률 절은 (씨앗 · 항목 · 고른 횟수)의 해시로 굴립니다.
     */
    class SW_GF_API AIDirector
    {
    public:
        static constexpr int32 kMaxTransitionsPerUpdate = 4;   ///< 한 프레임에 넘길 단계 수 상한(머무는 시간 0 인 단계가 돌고 돌지 않게)
        static constexpr int32 kMaxPicksPerUpdate       = 8;   ///< 풀 하나가 한 프레임에 고르는 수 상한(긴 dt)
        static constexpr int32 kMaxTraceEvent           = 256; ///< 추적이 들고 있는 최근 사건 수

        AIDirector();

        /** @brief 프로필 · 스폰 테이블(둘 다 빌림, 테이블은 없어도 된다) · 씨앗을 정하고 처음 단계로 시작합니다. */
        void initialize( const AIDirectorProfile* pProfile, const SpawnTable* pSpawnTable, uint32 seed );
        /** @brief 같은 프로필 · 씨앗으로 처음부터 다시 시작합니다(판 다시 시작). 기본 긴장도 모델도 처음으로 돌립니다. */
        void restart();
        /** @brief 게임의 긴장도 모델을 끼웁니다(빌림). nullptr 이면 기본 모델(`getBuiltinIntensityModel`)입니다. */
        void setIntensityModel( IAIDirectorIntensityModel* pModel ) { _pCustomModel = pModel; }
        /** @brief 조우 조건을 판정할 세계 · 플레이어 상태입니다(값을 복사한다 — 안의 포인터는 빌림). */
        void setContext( const AIDirectorContext& context ) { _context = context; }
        /** @brief 시간을 흘리고 단계 · 스폰 · 풀을 돌립니다. */
        void update( float32 deltaTime );
        /** @brief 게임 쪽 스폰 개체가 사라졌음을 알립니다(스폰 감독으로). 모르는 id 면 false 입니다. */
        bool notifyDespawned( uint32 spawnID );
        /** @brief 단계를 바로 바꿉니다(스크립트 사건 · 시험). 모르는 단계면 false 입니다. */
        bool forcePhase( const hashed_string& phaseID );
        /** @brief 쌓인 일을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<AIDirectorEvent>& outListEvent );

        AIDirectorIntensityModel&        getBuiltinIntensityModel() { return _builtinModel; }
        const IAIDirectorIntensityModel& getIntensityModel() const;
        const AIDirectorProfile*         getProfile() const { return _pProfile; }
        const SpawnDirector&             getSpawnDirector() const { return _spawnDirector; }
        hashed_string                    getPhase() const;
        int32                            getPhaseIndex() const { return _phaseIndex; }
        float32                          getPhaseTime() const { return _phaseTime; }
        float32                          getTime() const { return _time; }
        float32                          getIntensity() const { return _intensity; }
        /** @brief 페이싱이 시작 단계로 돌아온 수입니다(판의 "몇 번째 파도"). */
        int32   getCycle() const { return _cycle; }
        float32 getPoolBudget( int32 poolIndex ) const;
        int32   findPoolIndex( const hashed_string& poolID ) const;

        /** @brief 풀 @p poolIndex 의 항목 @p encounterIndex 를 지금 막는 까닭(`AIDirectorBlock` 비트)입니다. 0 이면 고를 수 있습니다. */
        uint32 computeBlockMask( int32 poolIndex, int32 encounterIndex ) const;
        /** @brief "왜 지금 이 단계 · 이 항목인가" — 단계 · 긴장도 · 나가는 길마다 막은 절 · 풀마다 항목의 `+`/`-` 와 까닭입니다. */
        void explain( string& outText ) const;
        /** @brief 최근 사건(최대 `kMaxTraceEvent`)을 시간 순으로 한 줄씩 씁니다. */
        void dumpTrace( string& outText ) const;
        /** @brief 결정성 확인용 상태 해시입니다(시간 · 단계 · 긴장도 · 난수 · 풀 · 항목 · 스폰 상태). */
        uint64 computeStateHash() const;
        /**
         * @brief 감독 상태를 씁니다(표 'AIDR' · 판 1) — 시간 · 단계 · 순환 · 긴장도 · 고른 횟수 · 난수 · 프로필 모양 · 풀 · 항목 · 기본 긴장도 모델 · 스폰 감독.
         * @details 핫 리로드 · 세이브가 웨이브를 잇는 자리입니다. 게임 긴장도 모델(`setIntensityModel`) · 추적 · 쌓인 사건은 싣지 않습니다.
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 의 바이트로 이어 갑니다. 먼저 같은 프로필 · 테이블로 `initialize` 한 감독에 부릅니다.
         * @details 프로필 모양(단계 · 풀 · 항목 · 신호 수)이 다르거나 끝까지 읽지 못하면 false 이고 감독은 그대로입니다(부른 쪽은 처음부터 돈다). 쌓인 사건 · 추적은 비웁니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 항목 하나의 런타임 상태입니다. */
        struct EncounterState
        {
            float32 _lastTime{ -1.0f }; ///< 음수면 아직 고른 적 없음
            int32   _count{ 0 };
        };

        /** @brief 풀 하나의 런타임 상태입니다. */
        struct PoolState
        {
            vector<EncounterState> _listEncounter{};
            float32                _timer{ 0.0f };
            float32                _budget{ 0.0f };
            float32                _lastPickTime{ -1.0f };
            int32                  _pendingIndex{ -1 }; ///< 예산 풀이 미리 골라 둔 항목
        };

        IAIDirectorIntensityModel& getModel();
        bool                       isPoolActive( const AIDirectorPoolDef& pool ) const;
        bool                       isPoolCooledDown( const AIDirectorPoolDef& pool, const PoolState& state ) const;
        int32                      findSatisfiedExit() const;
        void                       enterPhase( int32 phaseIndex, int32 exitIndex );
        void                       updateSpawns( float32 deltaTime );
        void                       updatePools( float32 deltaTime );
        void                       updateBudgetPool( int32 poolIndex, float32 deltaTime );
        int32                      pickEncounter( int32 poolIndex );
        void                       emitPick( int32 poolIndex, int32 encounterIndex );
        void                       drainSpawnEvents();
        void                       pushEvent( const AIDirectorEvent& event );
        uint32                     computeChanceKey( int32 poolIndex, int32 encounterIndex ) const;

        AIDirectorContext            _context;
        AIDirectorIntensityModel     _builtinModel;
        SpawnDirector                _spawnDirector;
        vector<PoolState>            _listPoolState;
        EventBuffer<AIDirectorEvent> _eventBuffer;
        vector<AIDirectorEvent>      _listTrace; ///< 고리 — `_traceHead` 가 가장 오래된 자리
        vector<float32>              _listScratchWeight;
        vector<SpawnEvent>           _listScratchSpawnEvent;
        const AIDirectorProfile*     _pProfile;
        const SpawnTable*            _pSpawnTable;
        IAIDirectorIntensityModel*   _pCustomModel;
        GameRandom                   _random;
        float32                      _time;
        float32                      _phaseTime;
        float32                      _intensity;
        uint32                       _seed;
        uint32                       _pickSerial; ///< 고른 횟수(확률 절의 열쇠)
        int32                        _phaseIndex;
        int32                        _cycle;
        int32                        _traceHead;
    };
} // namespace sw
