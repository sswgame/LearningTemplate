/**
 * @file QuestLog.h
 * @brief 퀘스트 일지입니다. 받기(선행 퀘스트, 레벨, 반복), 목표 진행 알림, 단계 넘기기, 선택지, 시간 제한, 완료와 실패와 포기, 보상 알림을 다룹니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct QuestDef;
    struct QuestReward;
    struct QuestStage;

    class Archive;
    class QuestCatalog;

    /** @brief 퀘스트 상태입니다. */
    enum class QuestStatus : uint8
    {
        NotStarted = 0,
        Active,
        Completed,
        Failed
    };

    /** @brief 받기의 결과입니다. */
    enum class QuestStartResult : uint8
    {
        Ok = 0,
        UnknownQuest,
        AlreadyActive,
        AlreadyDone, ///< 반복할 수 없는데 이미 끝냈다
        RequirementMissing,
        LevelTooLow
    };

    SW_GF_API const utf8* toString( QuestStartResult result );

    /** @brief 진행 중이거나 끝난 퀘스트 하나입니다. */
    struct QuestProgress
    {
        hashed_string _questID{};
        hashed_string _stageID{};
        vector<int32> _listCount{}; ///< 지금 단계의 목표별 개수
        float32       _stageTime{ 0.0f };
        int32         _completedCount{ 0 };
        QuestStatus   _status{ QuestStatus::NotStarted };
        uint8         _bAwaitingChoice{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 일지에서 일어난 일입니다. 보상은 정의 쪽을 가리키므로 카탈로그가 살아 있는 동안만 유효합니다. */
    struct QuestEvent
    {
        enum class Kind : uint8
        {
            Started = 0,
            ObjectiveProgress, ///< _objective 와 _value 가 지금 개수를 나타낸다
            ObjectiveDone,
            StageEntered, ///< 보상이 있으면 `_pReward` 에 담긴다
            AwaitingChoice,
            Completed,
            Failed,
            Abandoned
        };
        hashed_string      _questID{};
        hashed_string      _stageID{};
        const QuestReward* _pReward{ nullptr };
        int32              _objective{ -1 };
        int32              _value{ 0 };
        Kind               _kind{ Kind::Started };
    };
} // namespace sw

namespace sw
{
    /**
     * @class QuestLog
     * @brief 한 플레이어의 퀘스트입니다. 게임은 일어난 일을 `notify( "Kill", "wolf" )` 로 알리기만 하면 됩니다. 어느 퀘스트의 어느 목표가 그 일을 세는지는 일지가 판단합니다.
     * @details 단계에 들어서면 그 단계의 보상이 알림으로 나간다. 경험치와 아이템을 실제로 주는 것은 게임이다. 목표가 없고 다음 단계가 있는 단계는 바로 넘어간다(대사만 있는 단계).
     *          선택지가 있는 단계는 목표를 다 한 뒤 `choose` 를 기다린다. 카탈로그는 소유하지 않고 참조만 한다.
     */
    class SW_GF_API QuestLog
    {
    public:
        static constexpr int32  kMaxChainedStages = 32; ///< 한 번에 넘어가는 단계 수의 상한. 서로를 가리키는 단계에서 멈추게 한다
        static constexpr uint32 kStateTag         = FourCcUtil::make( "QLOG" );
        static constexpr uint32 kStateVersion     = 1;

        QuestLog();

        void initialize( const QuestCatalog* pCatalog );

        QuestStartResult evaluateStart( const hashed_string& questID, int32 level ) const;
        QuestStartResult start( const hashed_string& questID, int32 level );
        /** @brief 일어난 일을 알립니다. 진행된 목표 수를 반환합니다. */
        int32 notify( const hashed_string& kind, const hashed_string& target, int32 amount = 1 );
        /** @brief 개수를 그대로 정합니다. 가진 아이템 수가 해당하며 버리면 줄어듭니다. 바뀐 목표 수를 반환합니다. */
        int32 notifyCount( const hashed_string& kind, const hashed_string& target, int32 count );
        /** @brief 선택지를 고릅니다. 그 퀘스트가 선택을 기다리고 있지 않거나 없는 선택이면 false 입니다. */
        [[nodiscard]] bool choose( const hashed_string& questID, const hashed_string& choice );
        /** @brief 실패시킵니다. 호위 대상이 죽었을 때 등에 씁니다. */
        [[nodiscard]] bool fail( const hashed_string& questID );
        [[nodiscard]] bool abandon( const hashed_string& questID );
        /** @brief 시간 제한을 셉니다. */
        void update( float32 deltaTime );

        QuestStatus          getStatus( const hashed_string& questID ) const;
        const QuestProgress* findProgress( const hashed_string& questID ) const;
        const QuestStage*    findCurrentStage( const hashed_string& questID ) const;
        /** @brief 진행 중인 퀘스트의 id 목록입니다. 받은 순서입니다. */
        void collectActive( vector<hashed_string>& outListQuest ) const;
        void drainEvents( vector<QuestEvent>& outListEvent );
        /** @brief 퀘스트마다 진행 상황(단계, 목표 개수, 단계 시간, 완료 횟수, 상태, 선택 대기)을 씁니다. */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 가 쓴 바이트로 모두 복원합니다. 알림은 없습니다. 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다.
         * @details 카탈로그(`initialize`)에 없는 퀘스트는 경고를 남기고 버린다. 데이터에서 지운 퀘스트 하나가 일지 전체를 막지 않게 하기 위해서이다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        QuestProgress* findProgressMutable( const hashed_string& questID );
        void           enterStage( QuestProgress& progress, const QuestDef& quest, const hashed_string& stageID, int32 depth );
        /** @brief 지금 단계의 필수 목표를 다 했으면 다음 단계로 넘깁니다. */
        void  tryAdvance( QuestProgress& progress );
        int32 applyNotify( const hashed_string& kind, const hashed_string& target, int32 value, bool bAbsolute );
        void  pushEvent( QuestEvent::Kind kind, const QuestProgress& progress, int32 objective = -1, int32 value = 0, const QuestReward* pReward = nullptr );

        vector<QuestProgress>   _listProgress;
        EventBuffer<QuestEvent> _eventBuffer;
        const QuestCatalog*     _pCatalog;
    };
} // namespace sw
