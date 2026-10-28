/**
 * @file ScheduleSystem.h
 * @brief NPC 의 하루 일정을 돌리는 런타임입니다.
 * @details 시각으로 블록을 고르고, 이동 시간만큼 일찍 나서고, 끼어들기 스택이 끝나면 맞는 블록으로 돌아간다.
 *          축제가 일정을 덮고, NPC 사이의 약속을 판정하고, 화면 밖 NPC 는 지역 단위의 굵은 시간표(LOD)로 돌린다.
 *          잠으로 시간을 건너뛰고, 저장과 복원, 네트워크 요약, "왜 여기 있나" 추적도 지원한다.
 *
 *          **계획은 (그날, 계획을 세운 시각, 그때의 위치)의 함수이다.**
 *          하루가 시작될 때, 조건(날씨, 플래그, 태그)이 바뀔 때, 끼어들기가 끝날 때, 약속이 깨질 때만 계획을 다시 세운다.
 *          어느 시각의 상태(어느 블록인지, 가는 중인지, 경로 위 비율)는 그 계획에서 바로 계산한다.
 *          그래서 화면 밖 NPC 는 매 프레임 아무 일도 하지 않고, 몇 시간을 한 번에 건너뛰어도 매 분 돌린 것과 같은 위치에 선다.
 *          시각은 정수 분이고 무작위는 씨앗 해시라서 결정적이다. 위치는 `ISchedulePathing` 이 정하므로 2D 와 3D 를 가리지 않는다.
 *
 *          - 판정 위치(계획의 출발점과 끼어든 위치)는 늘 계획 경로(`setPathing` 의 첫째, 굵은 경로)에서 측정한다.
 *            화면 안(`ScheduleLOD::Near`) NPC 의 보이는 위치만 고운 경로(둘째)를 따라가므로, LOD 를 바꿔도 계획이 달라지지 않는다.
 *          - 시간은 `update( clock )` 이나 `advanceTo( 분 )` 으로만 흐른다.
 *            (지금, 목표] 안의 이벤트(날 시작, 끼어들기 만료, 약속 판정, 상태 바뀜)는 시각 순으로, 같은 시각이면 NPC 순으로 처리한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleLocator.h"
#include "GameFramework/Base/Character/AI/Schedule/SchedulePathing.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ScheduleBlockDef;
    struct ScheduleConditionContext;
    struct ScheduleNPCDef;
    struct ScheduleSaveState;

    class GameFlags;
    class ScheduleCatalog;

    /** @brief 계산을 얼마나 곱게 할지 정합니다. */
    ENUM()
    enum class ScheduleLOD : uint8
    {
        Near = 0, ///< 화면 안. 고운 경로 위 위치를 쓰고 애니메이션 훅을 부른다
        Far       ///< 화면 밖이거나 로드되지 않음. 지역 단위로 다루고 상태 사건은 굵은 시간 간격으로 낸다
    };

    /** @brief NPC 가 지금 하는 일입니다. */
    ENUM()
    enum class ScheduleNPCPhase : uint8
    {
        Traveling = 0, ///< 다음 블록의 위치로 가는 중
        Performing,    ///< 블록의 위치에서 활동 중
        Interrupted    ///< 끼어들기(말 걸기, 전투, 경보) 중. 일정 시계는 그대로 흐른다
    };

    /** @brief 계획 구간의 출처입니다. */
    enum class ScheduleSegmentSource : uint8
    {
        Idle = 0, ///< 어느 블록도 없는 시간. NPC 의 `idle` 활동(집)을 한다
        Routine,
        Event,
        Appointment
    };

    SW_GF_API const utf8* toString( ScheduleSegmentSource source );
} // namespace sw

namespace sw
{
    /** @brief 그날 계획의 구간 하나입니다. 시각은 그날의 분입니다. */
    struct ScheduleSegment
    {
        ScheduleLocation      _target{};
        hashed_string         _activity{};
        hashed_string         _animation{};
        hashed_string         _sourceID{}; ///< 루틴, 행사, 약속의 id
        uint32                _reservationID{ 0 };
        int32                 _startMinute{ 0 };
        int32                 _endMinute{ 0 };
        int32                 _departMinute{ 0 }; ///< 이 구간의 위치로 나서는 시각. 일찍 나서므로 시작보다 앞설 수 있다
        int32                 _travelMinutes{ 0 };
        int32                 _priority{ 0 };
        int32                 _ownerIndex{ -1 }; ///< 루틴(NPC 의 것), 행사, 약속의 인덱스
        int32                 _blockIndex{ -1 };
        ScheduleSegmentSource _source{ ScheduleSegmentSource::Idle };
        uint8                 _bLeaveEarly{ SW_TRUE }; ///< 돌아다니기의 둘째 위치부터는 그 시각에 나선다
        uint8                 _bPast{ SW_FALSE };      ///< 계획을 세운 시각보다 앞에 끝난 구간(추적 표시용)

        int32 getArriveMinute() const { return _departMinute + _travelMinutes; }
    };
} // namespace sw

namespace sw
{
    /** @brief 게임이 읽는 NPC 의 지금 모습입니다. */
    struct ScheduleNPCView
    {
        ScheduleLocation _location{}; ///< Near 면 고운 경로 위, Far 면 계획 경로 위
        ScheduleLocation _target{};
        hashed_string    _activity{};
        hashed_string    _animation{};
        hashed_string    _interruption{}; ///< 맨 위 끼어들기(없으면 빈 이름)
        int32            _segmentIndex{ -1 };
        float32          _travelFraction{ 1.0f };
        ScheduleNPCPhase _phase{ ScheduleNPCPhase::Performing };
        ScheduleLOD      _lod{ ScheduleLOD::Far };
    };
} // namespace sw

namespace sw
{
    /** @brief 일정 이벤트입니다(`drainEvents`). */
    struct ScheduleEvent
    {
        enum class Kind : uint8
        {
            Departed = 0,      ///< 구간의 위치로 나섰다
            Arrived,           ///< 구간의 위치에 닿았다
            ActivityStarted,   ///< 활동을 시작했다(`_animation` 을 재생)
            ActivityEnded,     ///< 활동을 끝냈다
            Interrupted,       ///< 끼어들기가 시작됐다(`_sourceID` = 끼어들기 id)
            Resumed,           ///< 끼어들기가 모두 끝나 일정으로 돌아갔다
            AppointmentMet,    ///< 약속의 참가자가 모두 모였다(`_sourceID` = 약속 id, NPC 는 마지막에 온 이)
            AppointmentBroken, ///< 기다려도 모두 오지 않아 그날 약속이 깨졌다
            Snapped            ///< 시간을 건너뛰었거나 화면 안으로 들어와 그 시각의 위치로 옮겨졌다
        };
        hashed_string _activity{};
        hashed_string _animation{};
        hashed_string _sourceID{};
        int32         _npcIndex{ -1 };
        int32         _minute{ 0 }; ///< 0 일 0:00 부터 센 분
        Kind          _kind{ Kind::Departed };
    };

    SW_GF_API const utf8* toString( ScheduleEvent::Kind kind );
} // namespace sw

namespace sw
{
    /** @brief 애니메이션 훅에 넘기는 활동 신호입니다. */
    struct ScheduleActivityCue
    {
        ScheduleLocation _location{};
        hashed_string    _activity{};
        hashed_string    _animation{};
        hashed_string    _npc{};
        int32            _npcIndex{ -1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IScheduleActivityAnimator
     * @brief 활동 애니메이션을 재생하는 훅입니다.
     * @details 화면 안(Near) NPC 가 활동을 시작하거나 끝낼 때만 불린다. 화면 밖 NPC 는 이벤트만 남는다.
     *          애니메이션 시스템(스켈레탈 애니메이터)이 생기면 그 컴포넌트를 찾아 클립과 그래프 상태를 고르는 구현을 끼운다.
     *          구현이 없으면 아무것도 재생하지 않고 이벤트(`ActivityStarted` 의 `_animation`)만 남는다.
     */
    class IScheduleActivityAnimator
    {
    public:
        IScheduleActivityAnimator()                                                  = default;
        virtual ~IScheduleActivityAnimator()                                         = default;
        IScheduleActivityAnimator( const IScheduleActivityAnimator& )                = default;
        IScheduleActivityAnimator& operator=( const IScheduleActivityAnimator& )     = default;
        IScheduleActivityAnimator( IScheduleActivityAnimator&& ) noexcept            = default;
        IScheduleActivityAnimator& operator=( IScheduleActivityAnimator&& ) noexcept = default;

        virtual void onActivityStarted( const ScheduleActivityCue& cue ) = 0;
        virtual void onActivityEnded( const ScheduleActivityCue& cue )   = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 네트워크로 보내는 NPC 한 명의 요약입니다. 바이트로는 26 바이트 고정이며, 같은 데이터를 가진 클라이언트가 보간과 표시에 씁니다. */
    struct ScheduleNetSummary
    {
        float3 _position{};
        uint32 _areaHash{ 0 };
        uint32 _activityHash{ 0 };
        uint16 _npcIndex{ 0 };
        int16  _segmentIndex{ -1 };
        uint8  _phase{ 0 };
        uint8  _interruptionDepth{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 시스템 설정입니다.
     * @details 범위는 필드의 `PROPERTY( Min )` 이고(시계 설정 안까지), 벗어나면 `ScheduleSystem::initialize` 가 오류로 거절한다(`ConfigRangeUtil`).
     *          예전에는 계절 날 수와 화면 밖 간격을 조용히 1 로 올렸다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_GF_API ScheduleSystemSettings
    {
        REFLECT_BODY();
        PROPERTY()
        WorldClockSettings _clock{}; ///< 조건이 쓰는 계절과 하루의 때. 게임의 `WorldClock` 과 같은 값이다
        PROPERTY()
        uint32 _seed{ 0x5c4ed01eu }; ///< 확률 조건과 돌아다니기 위치의 시드
        PROPERTY( Min = 1 )
        int32 _farStepMinutes{ 15 }; ///< 화면 밖 NPC 의 상태 사건 간격(계획 · 판정에는 영향이 없다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScheduleSystem
     * @brief 카탈로그의 NPC 모두의 하루 일정을 돌립니다.
     * @code
     *     ScheduleSystem schedules;
     *     if ( schedules.initialize( &catalog, settings, clock.getDay() * kScheduleMinutesPerDay + clockMinute ) == false )
     *         return false; // 범위 밖 설정은 오류로 남았다
     *     schedules.setPathing( &areaPathing, &navPathing );           // 굵은 계획 경로 · 화면 안 고운 경로
     *     schedules.setFlags( &flags );
     *     // 매 프레임
     *     schedules.setWeather( weather.getCurrent() );
     *     schedules.setNPCLOD( npcIndex, bVisible ? ScheduleLOD::Near : ScheduleLOD::Far );
     *     schedules.update( clock );
     *     const ScheduleNPCView view = schedules.getNPCView( npcIndex ); // 위치, 활동, 애니메이션
     * @endcode
     */
    class SW_GF_API ScheduleSystem
    {
    public:
        ScheduleSystem();
        ~ScheduleSystem();
        ScheduleSystem( const ScheduleSystem& )            = delete;
        ScheduleSystem& operator=( const ScheduleSystem& ) = delete;

        /**
         * @brief 카탈로그의 NPC 를 모두 집에 두고 @p absoluteMinute(0 일 0:00 부터 센 분)의 계획을 세웁니다.
         * @return 설정이 선언된 범위 밖이거나 @p absoluteMinute 가 음수이면 오류를 남기고 false 입니다. 시스템은 그대로입니다.
         */
        [[nodiscard]] bool initialize( const ScheduleCatalog* pCatalog, const ScheduleSystemSettings& settings, int32 absoluteMinute );
        void               shutdown();

        /** @brief 시계의 지금까지 시간을 흘립니다. 화면 밖 디버그 출력(`gv_scheduleTrace`)도 여기서 처리합니다. */
        void update( const WorldClock& clock );
        /** @brief @p absoluteMinute 까지 시간을 흘립니다. 뒤로는 가지 않습니다. */
        void advanceTo( int32 absoluteMinute );
        /** @brief 잠을 잡니다. 끼어들기를 모두 지우고 @p absoluteMinute 로 건너뛰며, 그 사이의 이벤트는 버리고 NPC 마다 `Snapped` 를 냅니다. */
        void skipTo( int32 absoluteMinute );

        /**
         * @brief 경로 제공자를 정합니다. 바꾸면 계획을 다시 세웁니다.
         * @details @p pPlanning 은 출발 시각, 판정 위치, 지역을 계산하는 굵은 경로입니다. @p pFine 은 화면 안 NPC 의 보이는 위치를 계산하는 경로이고, 비면 계획 경로를 씁니다.
         */
        void setPathing( const ISchedulePathing* pPlanning, const ISchedulePathing* pFine );
        /** @brief 스마트 오브젝트 슬롯 제공자를 정합니다. 비면 카탈로그 `<Spot>` 의 기본 제공자를 씁니다. */
        void setActivityLocator( IScheduleActivityLocator* pLocator );
        void setActivityAnimator( IScheduleActivityAnimator* pAnimator ) { _pAnimator = pAnimator; }
        /** @brief 플래그 조건이 읽는 플래그입니다. 리비전이 바뀌면 다음 시간 흐름에서 계획을 다시 세웁니다. */
        void setFlags( const GameFlags* pFlags );
        /** @brief 세계 태그입니다. 내용이 바뀌면 `notifyConditionsChanged` 를 부릅니다. */
        void setWorldTags( const TagContainer* pTags );
        void setWeather( const hashed_string& weatherID );
        /** @brief 게임이 조건 입력(세계 태그 등)을 바꿨음을 알립니다. 지금 시각에 계획을 모두 다시 세웁니다. */
        void notifyConditionsChanged() { _bConditionsDirty = SW_TRUE; }

        void        setNPCLOD( int32 npcIndex, ScheduleLOD lod );
        ScheduleLOD getNPCLOD( int32 npcIndex ) const;
        void        addNPCTag( int32 npcIndex, const TagID& tag );
        void        removeNPCTag( int32 npcIndex, const TagID& tag );
        /**
         * @brief NPC 의 호감도(하트 수 같은 정수 관계 단계)를 정합니다. 조건의 `minAffinity` 와 `maxAffinity` 가 읽습니다. 값이 바뀌면 그 NPC 의 계획을 다시 세웁니다.
         * @details 기본은 0 입니다. 저장 상태에 실립니다(`ScheduleNPCSaveState::_affinity`).
         */
        void  setNPCAffinity( int32 npcIndex, int32 affinity );
        int32 getNPCAffinity( int32 npcIndex ) const;

        /** @brief 끼어들기(카탈로그 `<Interrupt>` id)를 쌓습니다. 모르는 id 면 경고하고 false, 이미 있으면 만료만 다시 셉니다. */
        bool pushInterruption( int32 npcIndex, const hashed_string& interruptID );
        /** @brief 끼어들기를 뺍니다. 모두 빠지면 지금 시각의 블록으로 돌아갑니다. 없던 것이면 false 입니다. */
        bool popInterruption( int32 npcIndex, const hashed_string& interruptID );
        /** @brief 끼어든 동안 몸이 옮겨진 위치를 알립니다(도망, 전투). 끼어든 중이 아니면 무시합니다. */
        void reportInterruptedLocation( int32 npcIndex, const ScheduleLocation& location );

        int32           getNPCCount() const { return static_cast<int32>( _listNPC.size() ); }
        int32           findNPCIndex( const hashed_string& npcID ) const;
        hashed_string   getNPCID( int32 npcIndex ) const;
        ScheduleNPCView getNPCView( int32 npcIndex ) const;
        /** @brief 오늘 계획입니다. */
        const vector<ScheduleSegment>& getPlan( int32 npcIndex ) const;
        bool                           isAppointmentMet( const hashed_string& appointmentID ) const;
        bool                           isAppointmentBroken( const hashed_string& appointmentID ) const;
        int32                          getMinute() const { return _minute; }
        const ScheduleCatalog*         getCatalog() const { return _pCatalog; }
        int32                          getDay() const;
        void                           drainEvents( vector<ScheduleEvent>& outListEvent );

        void fillSaveState( ScheduleSaveState& outState ) const;
        /** @brief 저장 상태로 되돌립니다. `initialize` 한 뒤 같은 카탈로그로 부릅니다. 모르는 NPC id 는 건너뜁니다. */
        void restoreSaveState( const ScheduleSaveState& state );

        void fillNetSummary( vector<ScheduleNetSummary>& outListSummary ) const;
        /** @brief 요약을 바이트로 씁니다. 머리에 분(int32)과 수(uint16)를 쓰고, 그 뒤에 NPC 마다 고정 크기를 리틀 엔디언으로 씁니다. */
        static void               encodeNetSummary( int32 minute, const vector<ScheduleNetSummary>& listSummary, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodeNetSummary( const vector<uint8>& bytes, int32& outMinute, vector<ScheduleNetSummary>& outListSummary );
        /** @brief 상태 해시입니다. 같은 데이터, 씨앗, 입력이면 같은 값이 나오므로 결정성 확인과 동기 어긋남 감지에 씁니다. */
        uint64 computeStateHash() const;

        /** @brief "왜 여기 있나"에 답하는 추적 문자열입니다. 지금 구간과 그 출처, 후보 루틴/행사/약속의 조건 결과, 끼어들기 스택을 담습니다. */
        void explainNPC( int32 npcIndex, string& outText ) const;
        /** @brief 오늘 계획의 시간표(출발, 도착, 구간, 출처)입니다. 에디터와 콘솔에 그대로 보여 줍니다. */
        void dumpTimeline( int32 npcIndex, string& outText ) const;
        /** @brief 지금 살아 있는 일정 시스템을 모두 모읍니다. 생성자가 목록에 등록하고 소멸자가 해제합니다. 에디터 일정 디버그 창이 같은 스레드에서 읽기 전용으로 씁니다. */
        static void collectLiveSystems( vector<const ScheduleSystem*>& outListSystem );

    private:
        struct Interruption
        {
            hashed_string _id{};
            int32         _priority{ 0 };
            int32         _startMinute{ 0 };
            int32         _expireMinute{ -1 };
        };

        struct NPCRuntime
        {
            TagContainer            _tags{};
            ScheduleLocation        _origin{};
            ScheduleLocation        _held{};
            vector<ScheduleSegment> _listSegment{};
            vector<Interruption>    _listInterruption{}; ///< 우선순위 오름차순이다. 맨 뒤가 스택의 맨 위
            vector<float3>          _listRoutePoint{};   ///< Near 의 고운 경로(지금 구간)
            ScheduleSegment         _emittedSegment{};   ///< 마지막으로 사건을 낸 구간
            uint64                  _routeKey{ 0 };
            int32                   _defIndex{ -1 };
            int32                   _originMinute{ 0 }; ///< 0 일 0:00 부터
            int32                   _planDay{ -1 };
            int32                   _affinity{ 0 }; ///< 호감도(관계 단계). 바뀌면 `_bTagsDirty` 로 계획을 다시 세운다
            int32                   _wakeMinute{ 0 };
            ScheduleNPCPhase        _emittedPhase{ ScheduleNPCPhase::Performing };
            ScheduleLOD             _lod{ ScheduleLOD::Far };
            uint8                   _bTagsDirty{ SW_FALSE }; ///< 태그나 호감도가 바뀌었다. 다음 시간 흐름에서 계획을 다시 세운다
            uint8                   _bEmitted{ SW_FALSE };   ///< `_emittedSegment` 가 유효한가
        };

        /** @brief 계획 구간 후보 하나입니다. 루틴이나 행사의 블록입니다. */
        struct BlockCandidate
        {
            const ScheduleBlockDef* _pBlock{ nullptr };
            hashed_string           _sourceID{};
            int32                   _startMinute{ 0 };
            int32                   _endMinute{ 0 };
            int32                   _priority{ 0 };
            int32                   _order{ 0 };
            int32                   _ownerIndex{ -1 };
            int32                   _blockIndex{ -1 };
            ScheduleSegmentSource   _source{ ScheduleSegmentSource::Routine };
        };

        /** @brief 그날의 약속 판정 하나입니다. 시작에 기다림을 더한 시각에 모두 왔는지 봅니다. */
        struct AppointmentCheck
        {
            int32 _appointmentIndex{ -1 };
            int32 _minute{ 0 };
            uint8 _bDone{ SW_FALSE };
        };

        // 계획
        void                     planNPC( int32 npcIndex, int32 originMinute, const ScheduleLocation& origin );
        void                     replanNPC( int32 npcIndex, int32 minute, bool bKeepOriginIfSame );
        void                     makePlan( int32 npcIndex, int32 day, int32 originMinuteOfDay, const ScheduleLocation& origin, vector<ScheduleSegment>& outListSegment );
        void                     collectCandidates( int32 npcIndex, int32 day, vector<BlockCandidate>& outListCandidate ) const;
        void                     resolveTarget( int32 npcIndex, int32 day, const BlockCandidate* pCandidate, int32 startMinute, int32 endMinute, vector<ScheduleSegment>& outListSegment );
        void                     planTravel( int32 npcIndex, int32 originMinuteOfDay, const ScheduleLocation& origin, vector<ScheduleSegment>& inoutListSegment ) const;
        void                     releaseUnused( const vector<ScheduleSegment>& listOld, const vector<ScheduleSegment>& listNew );
        void                     startDay( int32 day );
        void                     rebuildAppointmentChecks( int32 day );
        ScheduleConditionContext makeContext( int32 npcIndex, int32 day, uint32 chanceKey ) const;

        // 상태
        ScheduleLocation        computeLogicLocation( int32 npcIndex, int32 absoluteMinute ) const;
        ScheduleNPCPhase        computePhase( const NPCRuntime& npc, int32 segmentIndex, float32 minuteOfDay, float32& outFraction ) const;
        ScheduleLocation        computePlanningLocationOnRoute( const NPCRuntime& npc, int32 segmentIndex, float32 fraction ) const;
        bool                    isAttendeePresent( int32 npcIndex, int32 appointmentIndex ) const;
        bool                    isValidNPC( int32 npcIndex ) const { return 0 <= npcIndex && npcIndex < getNPCCount(); }
        const ScheduleLocation& getStartLocation( const NPCRuntime& npc, int32 segmentIndex ) const;

        // 시간 · 사건
        void  processMinute( int32 minute );
        void  processAppointmentCheck( AppointmentCheck& inoutCheck );
        void  refreshNPC( int32 npcIndex );
        int32 computeWakeMinute( int32 npcIndex ) const;
        void  emitEvent( ScheduleEvent::Kind kind, int32 npcIndex, const ScheduleSegment* pSegment, const hashed_string& sourceID );
        void  emitSnapped( int32 npcIndex );
        void  applyConditionChanges();
        void  updateNearRoutes();
        void  popExpiredInterruptions( int32 npcIndex, int32 minute );
        void  resumeNPC( int32 npcIndex );
        void  logTraceRequest();

        const ScheduleCatalog*     _pCatalog;
        const ISchedulePathing*    _pPlanning;
        const ISchedulePathing*    _pFine;
        IScheduleActivityLocator*  _pLocator;
        IScheduleActivityAnimator* _pAnimator;
        const GameFlags*           _pFlags;
        const TagContainer*        _pWorldTags;
        ScheduleSpotLocator        _defaultLocator; ///< 카탈로그 `<Spot>` 제공자
        StraightSchedulePathing    _defaultPathing; ///< 곧은 선
        ScheduleSystemSettings     _settings;
        vector<NPCRuntime>         _listNPC;
        EventBuffer<ScheduleEvent> _eventBuffer;
        vector<AppointmentCheck>   _listAppointmentCheck;
        vector<int32>              _listBrokenAppointment; ///< 오늘 깨진 약속의 인덱스
        vector<int32>              _listMetAppointment;    ///< 오늘 모인 약속의 인덱스. 사건을 한 번만 내려고 보관한다
        hashed_string              _weather;
        string                     _lastTraceRequest;
        float32                    _minuteFraction; ///< `update` 가 준 시간 중 분 아래 나머지. 보이는 위치를 보간할 때 쓴다
        int32                      _minute;
        uint32                     _flagsRevision;
        uint8                      _bConditionsDirty;
        uint8                      _bSuppressEvents;
    };
} // namespace sw
