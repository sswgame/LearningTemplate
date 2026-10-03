/**
 * @file MatchState.h
 * @brief 한 판의 규칙 — 팀 · 참가자(역할), 점수 · 시간 제한, 부활 대기 · 부활 비용(팀 전력 게이지), 도움 판정, 탈락 · 순위, 끝내기입니다.
 * @details 장르 공통입니다 — 팀 데스매치(점수), 기체 대전(코스트 게이지), 배틀로얄(부활 없음 · 순위), 비대칭 대전(역할이 다른 두 팀)이 설정만 바꿔 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 판 설정입니다. 0 은 "없음" 입니다. */
    struct MatchSettings
    {
        float32 _warmupTime{ 0.0f };
        float32 _timeLimit{ 0.0f };
        float32 _respawnDelay{ 5.0f };
        float32 _assistWindow{ 10.0f }; ///< 죽기 전 이 초 안에 피해를 준 다른 사람이 도움
        int32   _scoreLimit{ 0 };
        int32   _scorePerKill{ 1 };
        uint8   _bRespawn{ SW_TRUE };
        uint8   _bLastTeamStandingWins{ SW_FALSE }; ///< 살아 있는(부활할 수 있는) 팀이 하나 남으면 끝(배틀로얄 · 섬멸전)
    };

    /** @brief 판의 단계입니다. */
    enum class MatchPhase : uint8
    {
        Waiting = 0, ///< `start` 전
        Warmup,
        InProgress,
        Ended
    };

    /** @brief 팀 하나입니다. */
    struct MatchTeam
    {
        hashed_string _name{};
        int32         _score{ 0 };
        int32         _costPool{ 0 };  ///< 부활 비용을 내는 전력 게이지(0 으로 시작하면 무제한)
        int32         _placement{ 0 }; ///< 탈락 · 끝에서 정한 순위(1 = 우승, 0 = 아직)
        uint8         _bUnlimitedCost{ SW_TRUE };
        uint8         _bEliminated{ SW_FALSE };
    };

    /** @brief 참가자 하나입니다. */
    struct MatchParticipant
    {
        struct DamageRecord
        {
            int32   _attacker{ -1 };
            float32 _time{ 0.0f };
        };

        hashed_string        _role{}; ///< "Killer" · "Survivor" · "Striker" — 게임이 정한다
        vector<DamageRecord> _listDamage{};
        float32              _respawnTimer{ 0.0f };
        float32              _damageDealt{ 0.0f };
        int32                _team{ -1 };
        int32                _kills{ 0 };
        int32                _deaths{ 0 };
        int32                _assists{ 0 };
        int32                _respawnCost{ 0 }; ///< 죽으면 팀 게이지에서 빠지는 값(기체 코스트)
        uint8                _bAlive{ SW_TRUE };
        uint8                _bEliminated{ SW_FALSE }; ///< 더는 부활하지 않는다
    };

    /** @brief 판에서 생긴 일입니다. */
    struct MatchEvent
    {
        enum class Kind : uint8
        {
            PhaseChanged = 0, ///< _value = MatchPhase
            Killed,           ///< _participant = 죽은 쪽, _other = 죽인 쪽(−1 = 환경)
            Assisted,         ///< _participant = 도운 쪽, _other = 죽은 쪽
            Respawned,
            ParticipantEliminated,
            TeamEliminated, ///< _team, _value = 순위
            ScoreChanged,   ///< _team, _value = 새 점수
            MatchEnded      ///< _team = 이긴 팀(−1 = 무승부)
        };
        int32 _participant{ -1 };
        int32 _other{ -1 };
        int32 _team{ -1 };
        int32 _value{ 0 };
        Kind  _kind{ Kind::PhaseChanged };
    };

    /**
     * @class MatchState
     * @brief 판의 점수판입니다. 누가 누구를 죽였는지는 게임이 `reportKill` 로 알립니다 — 여기는 몸 · 체력을 모릅니다.
     * @details 부활 비용은 죽은 참가자의 팀 게이지에서 빠지고, 게이지가 0 이하가 되면 그 팀은 진 것으로 봅니다(SD 건담 캡슐파이터의 전력 게이지,
     *          G 제네레이션 코스트제). 배틀로얄은 `_bRespawn = false` + `_bLastTeamStandingWins` 로, 팀이 떨어질 때마다 남은 팀 수 + 1 을 순위로 받습니다.
     */
    class SW_GF_API MatchState
    {
    public:
        MatchState();

        void initialize( const MatchSettings& settings );
        /** @brief 팀을 더합니다. @p costPool 이 0 이면 부활 비용이 없습니다. */
        int32 addTeam( const hashed_string& name, int32 costPool = 0 );
        int32 addParticipant( int32 team, const hashed_string& role, int32 respawnCost = 0 );
        void  start();
        void  update( float32 deltaTime );

        /** @brief 피해를 기록합니다(도움 판정 · 통계). */
        void reportDamage( int32 attacker, int32 victim, float32 amount );
        /** @brief 죽음을 알립니다. @p killer −1 은 환경(낙사 · 자기장). */
        void reportKill( int32 victim, int32 killer );
        /** @brief 부활 없이 탈락시킵니다(배틀로얄 기절 후 확인 사살 · 비대칭 대전의 희생). */
        void eliminate( int32 participant );
        /** @brief 팀 점수를 더합니다(거점 · 목표 · 탈출). */
        void addScore( int32 team, int32 amount );
        /** @brief 판을 바로 끝냅니다(목표 달성 — 탈출구 · 화물 운반). @p winningTeam −1 은 무승부. */
        void endMatch( int32 winningTeam );

        MatchPhase              getPhase() const { return _phase; }
        float32                 getElapsed() const { return _elapsed; }
        float32                 getRemaining() const;
        int32                   getWinningTeam() const { return _winningTeam; }
        const MatchTeam*        findTeam( int32 team ) const;
        const MatchParticipant* findParticipant( int32 participant ) const;
        int32                   getTeamCount() const { return static_cast<int32>( _listTeam.size() ); }
        int32                   getParticipantCount() const { return static_cast<int32>( _listParticipant.size() ); }
        /** @brief 살아 있거나 부활할 수 있는 팀원 수입니다. */
        int32 countStanding( int32 team ) const;
        void  drainEvents( vector<MatchEvent>& outListEvent );

    private:
        bool isValidParticipant( int32 participant ) const { return participant >= 0 && participant < static_cast<int32>( _listParticipant.size() ); }
        bool isValidTeam( int32 team ) const { return team >= 0 && team < static_cast<int32>( _listTeam.size() ); }
        void setPhase( MatchPhase phase );
        void eliminateTeam( int32 team );
        void resolveEndConditions();
        void finishByScore();
        void pushEvent( MatchEvent::Kind kind, int32 participant, int32 other, int32 team, int32 value );

        vector<MatchTeam>        _listTeam;
        vector<MatchParticipant> _listParticipant;
        vector<MatchEvent>       _listEvent;
        MatchSettings            _settings;
        float32                  _elapsed;
        float32                  _phaseTime;
        int32                    _winningTeam;
        MatchPhase               _phase;
    };
} // namespace sw
