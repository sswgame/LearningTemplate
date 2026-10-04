/**
 * @file ActionStageRun.h
 * @brief 스테이지 한 판 — 체크포인트(지나간 것만, 되돌아가지 않는다) · 목숨과 재시작 · 비밀 수집품(체크포인트를 지나야 확정, 그 전에 죽으면 다시 놓인다) ·
 *        클리어 시간 · 피격 수 · 등급(시간 · 피격 · 수집 가중 점수)입니다.
 * @details 시간은 `update` 로만 흐릅니다(고정 걸음이면 결정적). 목숨이 0 이 되면 게임 오버 — `restartStage` 로 스테이지 처음부터(시간 · 수집 · 피격도 처음부터)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ActionStageDef;

    class ActionPlatformerCatalog;

    /** @brief 판 상태입니다. */
    enum class ActionStageState : uint8
    {
        NotStarted = 0,
        Playing,
        Cleared,
        GameOver
    };

    /** @brief 깬 결과입니다. */
    struct ActionStageResult
    {
        hashed_string _grade{};
        float32       _clearTime{ 0.0f };
        float32       _score{ 0.0f }; ///< 0..100
        float32       _timeScore{ 0.0f };
        float32       _hitScore{ 0.0f };
        float32       _collectScore{ 0.0f };
        int32         _hitCount{ 0 };
        int32         _deathCount{ 0 };
        int32         _secretFound{ 0 };
        int32         _secretTotal{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ActionStageRun
     * @brief 스테이지 하나의 진행입니다. 정의는 카탈로그에서 빌려 씁니다.
     */
    class SW_GF_API ActionStageRun
    {
    public:
        ActionStageRun();

        /** @brief 스테이지를 시작합니다. 모르는 스테이지면 false 입니다. */
        [[nodiscard]] bool start( const ActionPlatformerCatalog* pCatalog, const hashed_string& stageId );
        /** @brief 처음부터 다시 — 목숨 · 시간 · 수집 · 피격을 모두 되돌립니다(게임 오버 뒤 이어하기). */
        void restartStage();
        /** @brief 시간을 흘립니다(진행 중일 때만). */
        void update( float32 deltaTime );

        /** @brief 체크포인트에 닿았습니다. 지금보다 뒤의 것이면 그곳이 새 시작점이 되고 지닌 수집품이 확정됩니다. 새로 닿았으면 true 입니다. */
        bool reachCheckpoint( const hashed_string& checkpointId );
        /** @brief 비밀 수집품을 줍습니다(아직 확정 아님). 이 스테이지의 것이고 처음 줍는 것이면 true 입니다. */
        bool collectSecret( const hashed_string& secretId );
        /** @brief 맞았습니다(죽지 않은 피격). */
        void registerHit();
        /**
         * @brief 죽었습니다 — 목숨 하나를 잃고 확정되지 않은 수집품은 다시 놓입니다.
         * @return 되살아날 체크포인트(비면 스테이지 시작점). 목숨이 다 했으면 상태는 GameOver.
         */
        hashed_string die();
        /** @brief 깼습니다 — 지닌 수집품을 확정하고 결과를 셉니다. 진행 중이 아니면 false 입니다. */
        [[nodiscard]] bool clearStage();

        /** @brief 지금까지의 결과(깬 뒤면 최종)입니다. 점수 · 등급은 카탈로그의 등급 규칙입니다. */
        ActionStageResult computeResult() const;
        /** @brief 비밀을 찾았는가(확정 또는 지닌 것)입니다. */
        bool isSecretFound( const hashed_string& secretId ) const;

        ActionStageState      getState() const { return _state; }
        int32                 getLives() const { return _lives; }
        float32               getElapsed() const { return _elapsed; }
        int32                 getCheckpointIndex() const { return _checkpointIndex; }
        int32                 getSecretCommittedCount() const { return static_cast<int32>( _listSecretCommitted.size() ); }
        int32                 getSecretPendingCount() const { return static_cast<int32>( _listSecretPending.size() ); }
        const ActionStageDef* getStage() const { return _pStage; }

    private:
        void        commitSecrets();
        static bool contains( const vector<hashed_string>& listId, const hashed_string& id );

        const ActionPlatformerCatalog* _pCatalog;
        const ActionStageDef*          _pStage;
        vector<hashed_string>          _listSecretCommitted; ///< 체크포인트 · 클리어로 확정된 것
        vector<hashed_string>          _listSecretPending;   ///< 주웠지만 아직 확정되지 않은 것
        float32                        _elapsed;
        int32                          _lives;
        int32                          _checkpointIndex; ///< 지난 마지막 체크포인트 자리(−1 = 시작점)
        int32                          _hitCount;
        int32                          _deathCount;
        ActionStageState               _state;
    };
} // namespace sw
