/**
 * @file QuestLogComponent.h
 * @brief 플레이어 한 명의 퀘스트 일지(`QuestLog`)를 조종자 오브젝트에 두는 컴포넌트입니다. 언리얼 `APlayerState` 에 붙는 기능 컴포넌트 자리입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

namespace sw
{
    class GameObjectManager;
    class QuestCatalog;

    /**
     * @class QuestLogComponent
     * @brief 플레이어 한 명의 퀘스트 일지입니다. 조종자 오브젝트에 다른 플레이어 상태 컴포넌트와 함께 둡니다.
     * @details - **열기**: 퀘스트 카탈로그를 받아 연다(디렉터의 `startGame` — `PlayerComponentUtil::openOnOwner`). 카탈로그는 소유하지 않고 참조만 한다. 카탈로그가 없으면 퀘스트를 시작하지 못한다.
     *          - **찾기 · 세이브**: `PlayerComponentUtil` · `DeferredStateRestore`. 정의한 .cpp 가 `StatefulComponentRegistrar` 로 모든 게임의 스냅샷에 올린다.
     */
    REFLECT( Category = "Framework", DisplayName = "Quest Log", Tooltip = "One player's quest log, on the controller object (or the director object in a scene without a controller)" )
    class SW_GF_API QuestLogComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "QLGC" );
        static constexpr uint32 kStateVersion = 1;

        QuestLogComponent();
        ~QuestLogComponent() override;

        /**
         * @brief 일지를 엽니다. 첫 호출만 카탈로그를 쓰고, 들고 있던 복원 바이트가 있으면 그것으로 복원합니다(카탈로그에 없는 퀘스트는 버린다 — 그래서 열고 나서 읽는다).
         * @return 새 판, 복원, 이미 열림 중 하나입니다.
         */
        [[nodiscard]] GameStateInitResult initialize( const QuestCatalog* pCatalog );

        /** @brief 씬의 컴포넌트 레지스트리에 오릅니다(`PlayerComponentUtil::findOpen`). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내립니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief `_bOpenAtBeginPlay` 면 카탈로그 없이 엽니다. */
        void onBeginPlay() override;

        /** @brief 일지 구간 하나를 씁니다. 열리기 전이면 들고 있던 바이트를 그대로 씁니다. `ComponentStateStore::capture` 가 부릅니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief 열리기 전이면 들고 있다가 `initialize` 에서, 열린 뒤면 바로 적용합니다. `ComponentStateStore::restore` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        bool            isOpen() const { return _restore.isOpen(); }
        bool            isFreshGame() const { return _restore.isFreshGame(); }
        QuestLog&       getQuestLog() { return _questLog; }
        const QuestLog& getQuestLog() const { return _questLog; }

    private:
        QuestLog             _questLog;
        DeferredStateRestore _restore;
        PROPERTY( Tooltip = "Open without a quest catalog at begin play - only for scenes without a kit director (the hub); a director opens it with its own catalog" )
        uint8                  _bOpenAtBeginPlay : 1;
        [[maybe_unused]] uint8 _reserved         : 7;
    };
} // namespace sw
