/**
 * @file QuestLogComponent.h
 * @brief 플레이어 한 명의 퀘스트 일지(`QuestLog`)를 조종자 오브젝트에 두는 컴포넌트입니다. 언리얼 `APlayerState` 에 붙는 기능 컴포넌트 자리입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
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
    struct AbilityOwnerDiedEvent;
    struct GimmickCueEvent;
    struct InteractionCompletedEvent;

    /**
     * @class QuestLogComponent
     * @brief 플레이어 한 명의 퀘스트 일지입니다. 조종자 오브젝트에 다른 플레이어 상태 컴포넌트와 함께 둡니다.
     * @details - **열기**: 퀘스트 카탈로그를 받아 연다(디렉터의 `startGame` — `PlayerComponentUtil::openOnOwner`). 카탈로그는 소유하지 않고 참조만 한다. 카탈로그가 없으면 퀘스트를 시작하지 못한다.
     *          - **찾기 · 세이브**: `PlayerComponentUtil` · `DeferredStateRestore`. 정의한 .cpp 가 `StatefulComponentRegistrar` 로 모든 게임의 스냅샷에 올린다.
     *          - **자동 세기**: "game" 채널의 쓰러짐(`AbilityOwnerDiedEvent`)과 상호작용 완료(`InteractionCompletedEvent`)를 구독해, 이 일지 주인이 조종하는 폰이 한 것만
     *            `notify( "Defeat", <쓰러진 대상의 태그> )` · `notify( "Interact", <상호작용 이름> )` · `notify( "Cue", <연출 신호 이름> )`(`GimmickCueEvent` — 자기 폰이 트리거에 들어가 부른 것) 로 센다(Lyra 처럼 대상은 게임플레이 태그로 맞춘다). 게임 고유의 일(보스 종 ·
     *            지역 클리어)은 디렉터가 직접 `notify` 한다.
     */
    REFLECT( Category = "Framework", DisplayName = "Quest Log", Tooltip = "One player's quest log, on the controller object (or the director object in a scene without a controller)" )
    class SW_GF_API QuestLogComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( QuestLogComponent );

        static constexpr uint32 kStateTag     = FourCCUtil::make( "QLGC" );
        static constexpr uint32 kStateVersion = 1;
        /** @brief 자동으로 세는 목표 종류 — 퀘스트 데이터의 `<Objective kind="…">` 와 같은 글자다. */
        static constexpr const utf8* kDefeatKind   = "Defeat";
        static constexpr const utf8* kInteractKind = "Interact";
        static constexpr const utf8* kCueKind      = "Cue";

        QuestLogComponent();
        ~QuestLogComponent() override;

        /**
         * @brief 일지를 엽니다. 첫 호출만 카탈로그를 쓰고, 들고 있던 복원 바이트가 있으면 그것으로 복원합니다(카탈로그에 없는 퀘스트는 버린다 — 그래서 열고 나서 읽는다).
         * @return 새 판, 복원, 이미 열림 중 하나입니다.
         */
        [[nodiscard]] GameStateInitResult initialize( const QuestCatalog& catalog );

        /** @brief 씬의 컴포넌트 레지스트리에 오르고 "game" 채널의 쓰러짐 · 상호작용 완료 · 연출 신호를 구독합니다(버스가 없는 프로세스는 구독하지 않는다). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 구독을 풀고 레지스트리에서 내립니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief `_bOpenAtBeginPlay` 면 카탈로그 없이 엽니다. */
        void onBeginPlay() override;
        /** @brief 다시 읽힌 카탈로그가 있으면 틱 뒤에 다시 묶습니다(배포는 다시 읽지 않아 틱하지 않는다). */
        void onTick( TickContext& context ) override;

        /** @brief 일지 구간 하나를 씁니다. 열리기 전이면 들고 있던 바이트를 그대로 씁니다. `ComponentStateCodec::captureObject` 가 부릅니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief 열리기 전이면 들고 있다가 `initialize` 에서, 열린 뒤면 바로 적용합니다. `ComponentStateCodec::restoreObject` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        bool            isOpen() const { return _restore.isOpen(); }
        bool            isFreshGame() const { return _restore.isFreshGame(); }
        QuestLog&       getQuestLog() { return _questLog; }
        const QuestLog& getQuestLog() const { return _questLog; }

        /** @brief 쓰러짐을 받습니다. 이 일지 주인이 조종하는 폰이 마지막으로 친 것이면 쓰러진 대상의 태그마다 `Defeat` 를 셉니다. */
        void handleOwnerDied( const AbilityOwnerDiedEvent& event );
        /** @brief 상호작용 완료를 받습니다. 이 일지 주인이 조종하는 폰이 한 것이면 상호작용 이름으로 `Interact` 를 셉니다. */
        void handleInteractionCompleted( const InteractionCompletedEvent& event );
        /** @brief 연출 신호를 받습니다. 이 일지 주인이 조종하는 폰이 부른 것(`GimmickCueEvent::_target`)이면 신호 이름으로 `Cue` 를 셉니다. */
        void handleCue( const GimmickCueEvent& event );

    private:
        /** @brief 일지를 지금 판의 카탈로그에 다시 묶습니다. */
        void rebindCatalog();
        /** @brief @p actor 가 이 일지 주인(조종자 오브젝트)이 조종하는 폰이면 true 입니다 — 다른 플레이어 · AI 가 한 일은 세지 않는다. */
        bool isOwnedPawn( GameObjectHandle actor ) const;

        SharedAssetRef<QuestCatalog>   _catalog;       ///< 연 카탈로그(다시 읽히면 새 판을 가리킨다). 카탈로그 없이 열었으면 비었다
        shared_ptr<const QuestCatalog> _pBoundCatalog; ///< 일지가 가리키는 판
        QuestLog                       _questLog;
        DeferredStateRestore           _restore;
        Subscription                   _ownerDiedSubscription;            ///< "game" 채널 쓰러짐 구독(`onRegister` 가 걸고 `onUnregister` 가 푼다)
        Subscription                   _interactionCompletedSubscription; ///< "game" 채널 상호작용 완료 구독
        Subscription                   _cueSubscription;                  ///< "game" 채널 연출 신호 구독
        PROPERTY( Tooltip = "Open without a quest catalog at begin play - only for scenes without a kit director (the hub); a director opens it with its own catalog" )
        uint8                  _bOpenAtBeginPlay : 1;
        [[maybe_unused]] uint8 _reserved         : 7;
    };
} // namespace sw
