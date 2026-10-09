/**
 * @file InteractionProgress.h
 * @brief 진행형 상호작용 — 발전기 수리 · 동료 부활 · 문 따기 · 조리 · 시체 뒤지기처럼 "붙어 있는 동안 차오르는" 일입니다.
 * @details 여럿이 붙으면 빨라지고(인원 배율), 끊기면 처음부터 또는 유지, 아무도 없으면 줄어들고(데드 바이 데이라이트 발전기 퇴행),
 *          붙어 있는 동안 가끔 스킬 체크가 뜹니다. 판정은 `TimingJudge` 를 빌려 쓰고, 간격 · 대상은 `GameRandom` 씨앗으로 정해 되풀이됩니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/StatBlock.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
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
        vector<float32> _listParticipantScale{};        ///< [n−1] = n 명일 때 속도 배율. 비면 `1 + (n−1) × _extraParticipantScale`. 목록보다 많으면 마지막 값
        StatBlock       _gradeBonus{};                  ///< 스킬 체크 등급 → 더하는 진행량(0..1). 없는 등급은 `_skillCheckBonus`
        float32         _duration{ 10.0f };             ///< 혼자 0 → 1 까지 걸리는 초
        float32         _extraParticipantScale{ 0.8f }; ///< 목록이 없을 때 한 명 늘 때마다 더하는 배율
        float32         _regressionRate{ 0.0f };        ///< 아무도 없을 때 초당 줄어드는 진행량(0..1). 0 이면 그대로
        float32         _skillCheckInterval{ 0.0f };    ///< 스킬 체크 사이 평균 초(±50 %). 0 이면 없음
        float32         _skillCheckLeadTime{ 1.0f };    ///< 체크가 뜬 뒤 목표 시각까지의 초(바늘이 판정 구역에 닿는 때)
        float32         _skillCheckBonus{ 0.0f };       ///< 맞혔을 때 기본 보너스 진행량
        float32         _skillCheckPenalty{ 0.1f };     ///< 놓쳤을 때 깎는 진행량
        int32           _maxParticipants{ 1 };
        uint8           _bResetOnInterrupt{ SW_FALSE };   ///< 모두 떠나면 처음부터(문 따기) — 아니면 유지(발전기)
        uint8           _bSkillCheckFailNoise{ SW_TRUE }; ///< 놓치면 소음 알림(살인마에게 위치가 드러난다)

        /** @brief @p participantCount 명일 때 속도 배율입니다. 0 명이면 0 입니다. */
        float32 computeParticipantScale( int32 participantCount ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 상호작용에 생긴 일입니다. */
    struct InteractionEvent
    {
        enum class Kind : uint8
        {
            Joined = 0,
            Left,
            Interrupted, ///< 마지막 사람이 끝내기 전에 떠났다(`_value` = 남은 진행량)
            Completed,
            SkillCheckStarted, ///< `_actorId` 가 `_value` 시각(이 상호작용의 시계)에 눌러야 한다
            SkillCheckSucceeded,
            SkillCheckFailed, ///< `_bNoise` 면 소음
            RegressionStarted ///< 아무도 없어 줄기 시작했다
        };
        hashed_string _grade{}; ///< 스킬 체크 성공 등급
        float32       _value{ 0.0f };
        uint32        _actorId{ 0 };
        Kind          _kind{ Kind::Joined };
        uint8         _bNoise{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class InteractionProgress
     * @brief 진행량 0..1 하나와 붙어 있는 사람 목록입니다. 시간은 `update` 로 흘리고, 스킬 체크 누름 시각은 `getTime()` 과 같은 시계로 넘깁니다.
     * @details 스킬 체크는 붙어 있는 사람 중 하나에게 뜹니다(씨앗으로). 응답이 없거나 판정 창 밖이면 실패로 깎고, 맞히면 등급 보너스를 더합니다.
     *          판정기가 없거나 간격이 0 이면 스킬 체크는 뜨지 않습니다. 끝나면 더 움직이지 않습니다(`reset` 으로 다시).
     */
    class SW_GF_API InteractionProgress
    {
    public:
        InteractionProgress();

        /** @brief 규칙 · 판정기(빌림, nullptr 가능) · 씨앗을 정하고 처음 상태로 돌립니다. */
        void initialize( const InteractionConfig& config, const TimingJudge* pJudge, uint32 seed );
        /** @brief 진행량 · 참가자 · 시계를 처음으로 돌립니다(씨앗도 처음 것으로). */
        void reset();

        /** @brief 붙습니다. 이미 붙었거나 가득 찼거나 끝났으면 false 입니다. */
        bool join( uint32 actorId );
        /** @brief 떠납니다. 붙어 있지 않았으면 false 입니다. */
        bool leave( uint32 actorId );
        void update( float32 deltaTime );
        /**
         * @brief 스킬 체크에 응답합니다. @p pressTime 은 `getTime()` 시계입니다.
         * @return 그 사람에게 뜬 체크가 있어 판정했으면 true(성공 · 실패는 이벤트로). 체크가 없으면 false.
         */
        bool respondSkillCheck( uint32 actorId, float32 pressTime );
        /** @brief 진행량을 바로 정합니다(세이브 · 디버그). */
        void setProgress( float32 progress );

        float32                  getProgress() const { return _progress; }
        float32                  getTime() const { return _time; }
        bool                     isCompleted() const { return _bCompleted == SW_TRUE; }
        bool                     hasParticipant( uint32 actorId ) const;
        int32                    getParticipantCount() const { return static_cast<int32>( _listParticipant.size() ); }
        bool                     hasPendingSkillCheck() const { return _bSkillCheckPending == SW_TRUE; }
        float32                  getSkillCheckTargetTime() const { return _skillCheckTarget; }
        uint32                   getSkillCheckActor() const { return _skillCheckActor; }
        const InteractionConfig& getConfig() const { return _config; }
        /** @brief 쌓인 일을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<InteractionEvent>& outListEvent );

        /** @brief 참가자 · 난수 · 씨앗 · 진행량 · 시계 · 스킬 체크(대상 · 남은 시간 · 목표 시각 · 대기) · 끝남 · 퇴행을 씁니다. 규칙 · 판정기는 `initialize` 의 것, 알림은 읽을 때 비웁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 참가자가 규칙의 상한을 넘거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void addProgress( float32 delta );
        void scheduleSkillCheck();
        void resolveSkillCheck( bool bSuccess, const hashed_string& grade );
        void pushEvent( InteractionEvent::Kind kind, uint32 actorId, float32 value = 0.0f );

        InteractionConfig             _config;
        vector<uint32>                _listParticipant; ///< 붙은 순서
        EventBuffer<InteractionEvent> _eventBuffer;
        const TimingJudge*            _pJudge;
        GameRandom                    _random;
        uint32                        _seed;
        uint32                        _skillCheckActor;
        float32                       _progress;
        float32                       _time;
        Countdown                     _skillCheckCountdown; ///< 다음 체크까지(참가자가 있는 동안만 준다)
        float32                       _skillCheckTarget;
        uint8                         _bCompleted;
        uint8                         _bSkillCheckPending;
        uint8                         _bRegressing;
    };
} // namespace sw
