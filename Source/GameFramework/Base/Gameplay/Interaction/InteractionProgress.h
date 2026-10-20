/**
 * @file InteractionProgress.h
 * @brief 진행형 상호작용입니다. 발전기 수리, 동료 부활, 문 따기, 조리, 시체 뒤지기처럼 "붙어 있는 동안 차오르는" 일을 다룹니다.
 * @details 여럿이 붙으면 빨라지고(인원 배율), 끊기면 처음부터 다시 시작하거나 유지하며, 아무도 없으면 줄어든다(데드 바이 데이라이트의 발전기 퇴행).
 *          붙어 있는 동안 가끔 스킬 체크가 뜬다. 판정은 `TimingJudge` 를 참조해서 하고, 간격과 대상은 `GameRandom` 시드로 정하므로 같은 시드에서 같은 결과가 반복된다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class TimingJudge;

    /** @brief 상호작용 하나의 규칙입니다. */
    struct SW_GF_API InteractionConfig
    {
        vector<float32> _listParticipantScale{};        ///< [n-1] 이 n 명일 때의 속도 배율이다. 비어 있으면 `1 + (n-1) × _extraParticipantScale` 를 쓰고, 목록보다 많으면 마지막 값을 쓴다
        StatBlock       _gradeBonus{};                  ///< 스킬 체크 등급에 따라 더하는 진행량(0..1). 없는 등급은 `_skillCheckBonus` 를 쓴다
        float32         _duration{ 10.0f };             ///< 혼자서 0 에서 1 까지 채우는 데 걸리는 초
        float32         _extraParticipantScale{ 0.8f }; ///< 목록이 없을 때 한 명 늘 때마다 더하는 배율
        float32         _regressionRate{ 0.0f };        ///< 아무도 없을 때 초당 줄어드는 진행량(0..1). 0 이면 그대로 유지한다
        float32         _skillCheckInterval{ 0.0f };    ///< 스킬 체크 사이의 평균 초(±50 %). 0 이면 스킬 체크가 없다
        float32         _skillCheckLeadTime{ 1.0f };    ///< 체크가 뜬 뒤 목표 시각까지의 초. 바늘이 판정 구역에 닿는 때이다
        float32         _skillCheckBonus{ 0.0f };       ///< 맞혔을 때의 기본 보너스 진행량
        float32         _skillCheckPenalty{ 0.1f };     ///< 놓쳤을 때 깎는 진행량
        int32           _maxParticipants{ 1 };
        uint8           _bResetOnInterrupt{ SW_FALSE };   ///< 모두 떠나면 처음부터 다시 시작할지 여부(문 따기). 아니면 유지한다(발전기)
        uint8           _bSkillCheckFailNoise{ SW_TRUE }; ///< 놓치면 소음을 알린다. 살인마에게 위치가 드러난다

        /** @brief @p participantCount 명일 때의 속도 배율입니다. 0 명이면 0 입니다. */
        float32 computeParticipantScale( int32 participantCount ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 상호작용에서 일어난 일입니다. */
    struct InteractionEvent
    {
        enum class Kind : uint8
        {
            Joined = 0,
            Left,
            Interrupted, ///< 마지막 사람이 끝내기 전에 떠났다(`_value` 는 남은 진행량)
            Completed,
            SkillCheckStarted, ///< `_actorID` 가 `_value` 시각(이 상호작용의 시계)에 눌러야 한다
            SkillCheckSucceeded,
            SkillCheckFailed, ///< `_bNoise` 가 켜져 있으면 소음이 난다
            RegressionStarted ///< 아무도 없어서 줄기 시작했다
        };
        hashed_string _grade{}; ///< 스킬 체크 성공 등급
        float32       _value{ 0.0f };
        uint32        _actorID{ 0 };
        Kind          _kind{ Kind::Joined };
        uint8         _bNoise{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class InteractionProgress
     * @brief 진행량 0..1 하나와 붙어 있는 사람 목록을 담습니다. 시간은 `update` 로 진행하고, 스킬 체크를 누른 시각은 `getTime()` 과 같은 시계로 넘깁니다.
     * @details 스킬 체크는 붙어 있는 사람 중 하나에게 뜬다. 누구에게 뜰지는 시드로 정한다. 응답이 없거나 판정 창 밖이면 실패로 처리해 진행량을 깎고, 맞히면 등급 보너스를 더한다.
     *          판정기가 없거나 간격이 0 이면 스킬 체크는 뜨지 않는다. 끝나면 더 움직이지 않으며, `reset` 으로 다시 시작할 수 있다.
     */
    class SW_GF_API InteractionProgress
    {
    public:
        InteractionProgress();

        /** @brief 규칙, 판정기, 시드를 정하고 처음 상태로 되돌립니다. 판정기는 소유하지 않고 참조만 하며 nullptr 일 수 있습니다. */
        void initialize( const InteractionConfig& config, const TimingJudge* pJudge, uint32 seed );
        /** @brief 진행량, 참가자, 시계를 처음으로 되돌립니다. 시드도 처음 값으로 되돌립니다. */
        void reset();

        /** @brief 붙습니다. 이미 붙었거나, 가득 찼거나, 끝났으면 false 입니다. */
        bool join( uint32 actorID );
        /** @brief 떠납니다. 붙어 있지 않았으면 false 입니다. */
        bool leave( uint32 actorID );
        void update( float32 deltaTime );
        /**
         * @brief 스킬 체크에 응답합니다. @p pressTime 은 `getTime()` 시계의 시각입니다.
         * @return 그 사람에게 뜬 체크가 있어서 판정했으면 true 입니다. 성공과 실패는 이벤트로 알립니다. 체크가 없으면 false 입니다.
         */
        bool respondSkillCheck( uint32 actorID, float32 pressTime );
        /** @brief 진행량을 바로 정합니다. 세이브와 디버그에 씁니다. */
        void setProgress( float32 progress );

        float32                  getProgress() const { return _progress; }
        float32                  getTime() const { return _time; }
        bool                     isCompleted() const { return _bCompleted == SW_TRUE; }
        bool                     hasParticipant( uint32 actorID ) const;
        int32                    getParticipantCount() const { return static_cast<int32>( _listParticipant.size() ); }
        bool                     hasPendingSkillCheck() const { return _bSkillCheckPending == SW_TRUE; }
        float32                  getSkillCheckTargetTime() const { return _skillCheckTarget; }
        uint32                   getSkillCheckActor() const { return _skillCheckActor; }
        const InteractionConfig& getConfig() const { return _config; }
        /** @brief 쌓인 이벤트를 @p outListEvent 뒤에 추가하고 비웁니다. */
        void drainEvents( vector<InteractionEvent>& outListEvent );

        /** @brief 참가자, 난수, 시드, 진행량, 시계, 스킬 체크(대상, 남은 시간, 목표 시각, 대기), 끝남, 퇴행을 씁니다. 규칙과 판정기는 `initialize` 가 받은 것이므로 저장하지 않고, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 참가자가 규칙의 상한을 넘거나 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void addProgress( float32 delta );
        void scheduleSkillCheck();
        void resolveSkillCheck( bool bSuccess, const hashed_string& grade );
        void pushEvent( InteractionEvent::Kind kind, uint32 actorID, float32 value = 0.0f );

        InteractionConfig             _config;
        vector<uint32>                _listParticipant; ///< 붙은 순서
        EventBuffer<InteractionEvent> _eventBuffer;
        const TimingJudge*            _pJudge;
        GameRandom                    _random;
        uint32                        _seed;
        uint32                        _skillCheckActor;
        float32                       _progress;
        float32                       _time;
        Countdown                     _skillCheckCountdown; ///< 다음 체크까지 남은 시간. 참가자가 있는 동안에만 줄어든다
        float32                       _skillCheckTarget;
        uint8                         _bCompleted;
        uint8                         _bSkillCheckPending;
        uint8                         _bRegressing;
    };
} // namespace sw
