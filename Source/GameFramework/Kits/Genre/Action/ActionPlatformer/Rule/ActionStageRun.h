/**
 * @file ActionStageRun.h
 * @brief 스테이지 한 판을 진행합니다. 체크포인트(지나간 것만 인정하고 되돌아가지 않음), 목숨과 재시작, 비밀 수집품(체크포인트를 지나야 확정되고, 그 전에 죽으면 다시 놓임), 클리어 시간, 피격 수, 등급(시간, 피격, 수집의 가중 점수)을 다룹니다.
 * @details 시간은 `update` 로만 흐른다(고정 걸음이면 결정적이다). 목숨이 0이 되면 게임 오버이고, `restartStage` 로 스테이지를 처음부터 다시 시작한다(시간, 수집, 피격도 처음부터 다시 센다).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ActionStageDef;

    class ActionPlatformerCatalog;
    class Archive;

    /** @brief 한 판의 상태입니다. */
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
     * @brief 스테이지 하나의 진행 상태입니다. 정의는 카탈로그에서 빌려 씁니다.
     */
    class SW_KIT_API ActionStageRun
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "APST" );
        static constexpr uint32 kStateVersion = 1;

        ActionStageRun();

        /** @brief 스테이지를 시작합니다. 모르는 스테이지면 false 를 반환합니다. */
        [[nodiscard]] bool start( const ActionPlatformerCatalog* pCatalog, const hashed_string& stageID );
        /** @brief 처음부터 다시 시작합니다. 목숨, 시간, 수집, 피격을 모두 되돌립니다(게임 오버 뒤 이어하기). */
        void restartStage();
        /** @brief 시간을 흘립니다(진행 중일 때만). */
        void update( float32 deltaTime );

        /** @brief 체크포인트에 닿았음을 알립니다. 지금보다 뒤쪽 체크포인트면 그곳이 새 시작점이 되고, 지닌 수집품이 확정됩니다. 새로 닿았으면 true 를 반환합니다. */
        bool reachCheckpoint( const hashed_string& checkpointID );
        /** @brief 비밀 수집품을 줍습니다(아직 확정되지 않습니다). 이 스테이지의 것이고 처음 줍는 것이면 true 를 반환합니다. */
        [[nodiscard]] bool collectSecret( const hashed_string& secretID );
        /** @brief 죽지 않고 맞았음을 알립니다. */
        void registerHit();
        /**
         * @brief 죽었음을 알립니다. 목숨 하나를 잃고, 확정되지 않은 수집품은 다시 놓입니다.
         * @return 되살아날 체크포인트입니다(비어 있으면 스테이지 시작점). 목숨이 다 떨어졌으면 상태는 GameOver 입니다.
         */
        hashed_string die();
        /** @brief 스테이지를 깼음을 알립니다. 지닌 수집품을 확정하고 결과를 계산합니다. 진행 중이 아니면 false 를 반환합니다. */
        [[nodiscard]] bool clearStage();

        /** @brief 지금까지의 결과입니다(깬 뒤면 최종 결과). 점수와 등급은 카탈로그의 등급 규칙으로 계산합니다. */
        ActionStageResult computeResult() const;
        /** @brief 비밀을 찾았는지(확정했거나 지닌 것) 반환합니다. */
        bool isSecretFound( const hashed_string& secretID ) const;

        ActionStageState      getState() const { return _state; }
        int32                 getLives() const { return _lives; }
        float32               getElapsed() const { return _elapsed; }
        int32                 getCheckpointIndex() const { return _checkpointIndex; }
        int32                 getSecretCommittedCount() const { return static_cast<int32>( _listSecretCommitted.size() ); }
        int32                 getSecretPendingCount() const { return static_cast<int32>( _listSecretPending.size() ); }
        const ActionStageDef* getStage() const { return _pStage; }

        /** @brief 스테이지 id, 판 상태, 경과 시간, 목숨, 체크포인트 인덱스, 피격, 죽은 수, 확정한 것, 지닌 비밀을 씁니다. 스테이지 정의는 카탈로그의 것이라 id 만 싣습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief 스테이지를 찾을 카탈로그를 빌립니다. 새 객체에 `readState` 하기 전에 연결해야 합니다(`start` 도 연결합니다). */
        void bindCatalog( const ActionPlatformerCatalog* pCatalog ) { _pCatalog = pCatalog; }
        /** @brief `writeState` 가 쓴 바이트로 상태를 바꿉니다. 스테이지는 연결한 카탈로그에서 id 로 찾습니다. 카탈로그가 없거나, 없는 스테이지, 비밀, 체크포인트이거나, 바이트가 깨졌으면 상태를 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        void commitSecrets();

        const ActionPlatformerCatalog* _pCatalog;
        const ActionStageDef*          _pStage;
        vector<hashed_string>          _listSecretCommitted; ///< 체크포인트나 클리어로 확정된 것
        vector<hashed_string>          _listSecretPending;   ///< 주웠지만 아직 확정되지 않은 것
        float32                        _elapsed;
        int32                          _lives;
        int32                          _checkpointIndex; ///< 지난 마지막 체크포인트 인덱스(−1이면 시작점)
        int32                          _hitCount;
        int32                          _deathCount;
        ActionStageState               _state;
    };
} // namespace sw
