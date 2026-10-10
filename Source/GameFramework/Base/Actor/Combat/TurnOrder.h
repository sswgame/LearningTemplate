/**
 * @file TurnOrder.h
 * @brief 턴 순서 — 라운드제(매 라운드 우선도 → 속도 순, 동속은 씨앗 난수)와 타임라인제(속도만큼 빨리 차례가 돌아온다 · 지연 · 미리 보기)입니다.
 * @details 드래곤 퀘스트 · 포켓몬(기술 우선도)은 라운드제, FFX · 씨 오브 스타즈 류의 차례 표시는 타임라인제입니다. 행동 자체(피해 · 효과)는 어빌리티 시스템이 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 순서 방식입니다. */
    enum class TurnOrderMode : uint8
    {
        Rounds = 0, ///< 한 라운드에 모두 한 번씩
        Timeline    ///< 빠른 쪽이 여러 번 — 행동마다 다음 차례까지의 시간이 다시 찬다
    };

    /**
     * @class TurnOrder
     * @brief 행위자는 게임이 정한 번호(`actorID`)로 넣습니다. 쓰러지면 `removeActor`, 속도가 바뀌면 `setSpeed` 입니다(타임라인은 남은 몫을 비율로 옮긴다).
     */
    class SW_GF_API TurnOrder
    {
    public:
        static constexpr float32 kTimelineThreshold = 100.0f; ///< 타임라인 — 이만큼 차면 차례

        TurnOrder();

        void initialize( TurnOrderMode mode, uint32 seed );
        void addActor( int32 actorID, float32 speed );
        void removeActor( int32 actorID );
        void setSpeed( int32 actorID, float32 speed );
        /** @brief 라운드제 — 이번 라운드에 고른 행동의 우선도(높을수록 먼저, 다음 라운드를 짤 때 쓴다). */
        void setPriority( int32 actorID, int32 priority );
        /** @brief 타임라인제 — 차례를 @p amount 몫(0..1, 1 = 한 차례 전체)만큼 늦춥니다(지연 공격 · 기절). 음수면 앞당긴다. */
        void delayActor( int32 actorID, float32 amount );
        /** @brief 라운드제 — 지금 라운드 순서를 버리고 다음 `next` 에서 다시 짭니다(모두 행동을 고른 뒤 부른다). */
        void restartRound();

        /** @brief 다음 차례의 행위자입니다. 아무도 없으면 −1 입니다. */
        int32 next();
        /** @brief 앞으로 올 차례 @p count 개를 미리 봅니다(상태를 바꾸지 않는다). */
        void  previewOrder( int32 count, vector<int32>& outListActor ) const;
        int32 getRound() const { return _round; }
        int32 getActorCount() const { return static_cast<int32>( _listActor.size() ); }

        /** @brief 배우(속도 · 게이지 · id · 우선 · 동률깨기) · 라운드 대기열 · 난수 · 라운드 · 모드을 씁니다.  */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        struct Actor
        {
            float32 _speed{ 1.0f };
            float32 _gauge{ 0.0f }; ///< 타임라인 — 다음 차례까지 남은 양
            int32   _actorID{ -1 };
            int32   _priority{ 0 };
            uint32  _tieBreak{ 0 };
        };

        Actor*       findActor( int32 actorID );
        void         makeRoundQueue( vector<int32>& outListQueue, const vector<Actor>& listActor, GameRandom& random ) const;
        static int32 popTimeline( vector<Actor>& listActor );

        vector<Actor> _listActor;
        vector<int32> _listRoundQueue; ///< 라운드제 — 이번 라운드에 남은 차례(앞부터)
        GameRandom    _random;
        int32         _round;
        TurnOrderMode _mode;
    };
} // namespace sw
