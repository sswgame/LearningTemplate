/**
 * @file TurnOrder.h
 * @brief 턴 순서를 정합니다. 라운드제와 타임라인제를 지원합니다.
 * @details 라운드제는 매 라운드 우선도가 높은 순, 같으면 속도가 빠른 순으로 정하고, 속도도 같으면 시드 난수로 정합니다.
 *          드래곤 퀘스트와 포켓몬(기술 우선도)이 이 방식입니다.
 *          타임라인제는 속도가 빠를수록 차례가 자주 돌아오고, 지연과 미리 보기를 지원합니다. FFX 와 씨 오브 스타즈의 차례 표시가 이 방식입니다.
 *          행동 자체(피해, 효과)는 어빌리티 시스템이 처리합니다.
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

    /** @brief 턴 순서 방식입니다. */
    enum class TurnOrderMode : uint8
    {
        Rounds = 0, ///< 한 라운드에 모두 한 번씩 행동한다
        Timeline    ///< 빠른 쪽이 여러 번 행동한다. 행동할 때마다 다음 차례까지의 시간이 다시 찬다
    };

    /**
     * @class TurnOrder
     * @brief 게임이 정한 번호(`actorID`)로 행위자를 넣고 차례를 꺼냅니다.
     * @details 행위자가 쓰러지면 `removeActor`, 속도가 바뀌면 `setSpeed` 를 부릅니다. 타임라인제에서는 남은 양을 새 속도의 비율로 옮깁니다.
     */
    class SW_GF_API TurnOrder
    {
    public:
        static constexpr float32 kTimelineThreshold = 100.0f; ///< 타임라인제에서 게이지가 이만큼 차면 차례가 온다

        TurnOrder();

        void initialize( TurnOrderMode mode, uint32 seed );
        void addActor( int32 actorID, float32 speed );
        void removeActor( int32 actorID );
        void setSpeed( int32 actorID, float32 speed );
        /** @brief 라운드제에서 이번 라운드에 고른 행동의 우선도를 정합니다. 높을수록 먼저 행동하며, 다음 라운드 순서를 정할 때 씁니다. */
        void setPriority( int32 actorID, int32 priority );
        /**
         * @brief 타임라인제에서 차례를 @p amount 만큼 늦춥니다. 지연 공격이나 기절에 씁니다.
         * @param amount 0..1 범위의 비율입니다. 1 은 한 차례 전체이고, 음수면 차례를 앞당깁니다.
         */
        void delayActor( int32 actorID, float32 amount );
        /** @brief 라운드제에서 지금 라운드의 순서를 버리고 다음 `next` 에서 다시 정합니다. 모두 행동을 고른 뒤 부릅니다. */
        void restartRound();

        /** @brief 다음 차례의 행위자입니다. 아무도 없으면 -1 입니다. */
        int32 next();
        /** @brief 앞으로 올 차례 @p count 개를 미리 봅니다. 상태는 바꾸지 않습니다. */
        void  previewOrder( int32 count, vector<int32>& outListActor ) const;
        int32 getRound() const { return _round; }
        int32 getActorCount() const { return static_cast<int32>( _listCombatant.size() ); }

        /** @brief 행위자 목록, 라운드 대기열, 난수, 라운드 번호, 모드를 씁니다. 행위자마다 속도, 게이지, id, 우선도, 동률 깨기 값을 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 데이터가 깨졌으면 false 를 돌려주고 상태는 그대로 둡니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        struct Combatant
        {
            float32 _speed{ 1.0f };
            float32 _gauge{ 0.0f }; ///< 타임라인제에서 다음 차례까지 남은 양
            int32   _actorID{ -1 };
            int32   _priority{ 0 };
            uint32  _tieBreak{ 0 };
        };

        Combatant*   findCombatant( int32 actorID );
        void         makeRoundQueue( vector<int32>& outListQueue, const vector<Combatant>& listActor, GameRandom& random ) const;
        static int32 popTimeline( vector<Combatant>& listActor );

        vector<Combatant> _listCombatant;
        vector<int32>     _listRoundQueue; ///< 라운드제에서 이번 라운드에 남은 차례(앞부터)
        GameRandom        _random;
        int32             _round;
        TurnOrderMode     _mode;
    };
} // namespace sw
