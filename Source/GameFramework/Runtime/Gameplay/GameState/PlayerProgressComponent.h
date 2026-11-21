/**
 * @file PlayerProgressComponent.h
 * @brief 플레이어 한 명의 진행 상태(지갑 · 가방 · 퀘스트 일지 · 평판)를 조종자 오브젝트에 두는 컴포넌트입니다. 언리얼 `APlayerState` 의 게임플레이 칸 자리입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

namespace sw
{
    class Archive;
    class DebugHUDSectionWriter;
    class GameFlags;
    class GameObject;
    class GameObjectManager;
    class ItemCatalog;
    class QuestCatalog;
    class ReputationCatalog;

    /**
     * @brief 플레이어 진행을 여는 데 필요한 설정입니다. 카탈로그는 소유하지 않고 참조만 하므로 컴포넌트보다 오래 살아 있어야 합니다. 보통 게임 인스턴스가 카탈로그를 보관합니다.
     * @details 슬롯 수의 범위는 `PROPERTY( Min )` 이고, 벗어나면 `PlayerProgressComponent::initialize` 가 오류로 거절한다(`ConfigRangeUtil` — 음수를 조용히 고치지 않는다).
     *          카탈로그 포인터는 리플렉션 필드가 아니다. 카탈로그가 없으면 그 기능(퀘스트 시작 · 평판 세력 · 아이템 정의)이 비어 있다.
     */
    REFLECT()
    struct SW_GF_API PlayerProgressSettings
    {
        REFLECT_BODY();
        const QuestCatalog*      _pQuestCatalog{ nullptr };
        const ReputationCatalog* _pReputationCatalog{ nullptr };
        const ItemCatalog*       _pItemCatalog{ nullptr };
        PROPERTY( Min = 0 )
        int32 _inventorySlotCount{ 0 }; ///< 플레이어 가방의 슬롯 수. 0 이면 가방이 없고 `findInventory` 의 가방 포인터가 nullptr 이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class PlayerProgressComponent
     * @brief 플레이어 한 명의 지갑 · 가방 · 퀘스트 일지 · 평판입니다. 조종자 오브젝트에 `PlayerStateComponent`(진행 플래그)와 형제로 둡니다.
     * @details - **자리**: 상태 범위 셋(언리얼 `AGameState` · `APlayerState` · 매치 상태) 가운데 플레이어 몫이다. 월드 상태(시계 · 날씨 · 월드 플래그 · 땅)는 씬에 하나인
     *            `GameStateComponent`, 미니게임 한 판의 임시 상태는 디렉터가 결과로만 내보낸다. 조종자가 없는 디렉터 전용 씬(옛 게임 맵)은 디렉터 오브젝트에 둔다.
     *          - **찾기**: 폰에서는 `findForPawnObject`(폰 → 쥔 조종자 → 그 오브젝트), 디렉터는 같은 오브젝트에서 `findOnOwner`, 씬 하나를 읽는 도구(감시 값 · 치트 · 디버그 HUD)는
     *            `findOpen`(씬에 열린 것이 둘 이상이면 오류로 멈춘다 — 어느 플레이어인지 등록 순서로 정하지 않는다).
     *          - **틱**: 날이 넘어가면(월드 상태의 시계 알림) 평판을 한 번 식힌다. 월드 상태와 같은 오브젝트면 같은 `PrePhysics` 에서 그 뒤에, 다른 오브젝트(조종자)면
     *            `PostPhysics` 에서 읽는다 — 다른 오브젝트의 같은 그룹 틱은 동시에 돌아 시계 알림을 읽으면 데이터 경쟁이다.
     *          - **세이브**: `writeState` 는 상태마다 구간 하나(`StateSectionTableUtil` — 월드 상태와 같은 꼴)를 쓴다. 이 타입은 정의한 .cpp 가 `StatefulComponentRegistrar` 로
     *            모든 게임의 스냅샷에 올린다(형제 `PlayerStateComponent` 와 같다).
     */
    REFLECT( Category = "Framework", DisplayName = "Player Progress", Tooltip = "One player's wallet, inventory, quest log and reputation, on the controller object (or the director object in a scene without a controller)" )
    class SW_GF_API PlayerProgressComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "PPRG" );
        static constexpr uint32 kStateVersion = 1;

        PlayerProgressComponent();
        ~PlayerProgressComponent() override;

        /**
         * @brief 플레이어 진행을 엽니다. 첫 호출만 설정을 쓰고, 보관하고 있던 복원 바이트가 있으면 그것으로 복원합니다.
         * @return 새 판, 복원, 이미 열림, 거절(설정이 범위 밖 — 오류를 남겼다) 중 하나입니다. 복원 바이트를 읽지 못했으면 오류를 남기고 새 판(`Fresh`)입니다.
         */
        [[nodiscard]] GameStateInitResult initialize( const PlayerProgressSettings& settings );

        /** @brief 씬의 컴포넌트 레지스트리에 오릅니다(`findOpen`). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내립니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 틱 그룹을 정하고(월드 상태와 같은 오브젝트면 `PrePhysics`, 아니면 `PostPhysics`), `_bOpenAtBeginPlay` 면 기본 설정으로 엽니다. */
        void onBeginPlay() override;
        /** @brief 열렸으면 이번 틱에 월드 상태의 날이 넘어간 수만큼 평판을 식힙니다. */
        void onTick( TickContext& context ) override;

        /** @brief 구간 넷(지갑, 일지, 평판, 가방)을 씁니다. 열리기 전이면 보관하고 있던 바이트를 그대로 씁니다. `ComponentStateStore::capture` 가 부릅니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief 열리기 전이면 보관해 두었다가 `initialize` 에서 적용하고, 열린 뒤면 바로 적용합니다. `ComponentStateStore::restore` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        bool isInitialized() const { return _bInitialized == SW_TRUE; }
        /** @brief 새 판으로 열렸으면 true, 복원한 판이면 false 입니다. 디렉터가 시작값(시작 돈 · 첫 아이템)을 넣을지 판단할 때 묻습니다. */
        bool             isFreshGame() const { return _bFreshGame == SW_TRUE; }
        Wallet&          getWallet() { return _wallet; }
        const Wallet&    getWallet() const { return _wallet; }
        Inventory&       getInventory() { return _inventory; }
        const Inventory& getInventory() const { return _inventory; }
        /** @brief 슬롯이 있으면(설정 `_inventorySlotCount` > 0) 가방이고, 아니면 nullptr 입니다 — 가방을 선택으로 빌리는 키트(`T*`)에 넘긴다. */
        [[nodiscard]] Inventory* findInventory() { return _inventory.getSlotCount() > 0 ? &_inventory : nullptr; }
        QuestLog&                getQuestLog() { return _questLog; }
        const QuestLog&          getQuestLog() const { return _questLog; }
        ReputationState&         getReputation() { return _reputation; }
        const ReputationState&   getReputation() const { return _reputation; }

        /**
         * @brief 진행 조건 디버거(HUD 섹션 `progress`)의 줄을 씁니다 — 읽기만 한다. 월드 플래그(@p pWorldFlags, 없으면 건너뜀) · 지갑 · 가방(빈 칸 제외) 순이고 @p maxLines 에서 끊는다.
         * @details 퀘스트의 "왜 못 받나" 는 `QuestLog::writeDebugLines`(섹션 `quest`)가 적는다. 카탈로그 정의값이 아니라 이 판의 값이다.
         */
        void writeProgressDebugLines( DebugHUDSectionWriter& writer, const GameFlags* pWorldFlags, uint32 maxLines ) const;

        /** @brief @p component 와 같은 오브젝트에 있는 플레이어 진행입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static PlayerProgressComponent* findOnOwner( const Component& component );
        /**
         * @brief @p component 와 같은 오브젝트의 플레이어 진행을 찾아 엽니다. 디렉터의 `startGame` 이 부릅니다. 없거나 설정이 거절되면 오류를 남기고 nullptr 입니다.
         * @param outResult 새 판의 시작값을 넣을 첫 디렉터인지 볼 때 쓰며(`Fresh`), 실패하면 `Refused` 입니다.
         */
        [[nodiscard]] static PlayerProgressComponent* openOnOwner( const Component& component, const PlayerProgressSettings& settings, GameStateInitResult& outResult );
        /** @brief `openOnOwner` 의 결과를 보지 않는 버전입니다. 시작값은 `isFreshGame` 으로 봅니다. */
        [[nodiscard]] static PlayerProgressComponent* openOnOwner( const Component& component, const PlayerProgressSettings& settings );
        /** @brief @p pawnObject 의 폰을 쥔 조종자 오브젝트의 플레이어 진행입니다. 폰 · 조종자 · 플레이어 진행이 없으면 nullptr 입니다. */
        [[nodiscard]] static PlayerProgressComponent* findForPawnObject( GameObjectManager& manager, const GameObject& pawnObject );
        /**
         * @brief 씬 @p pManager 의 열린 플레이어 진행입니다. 없으면 nullptr, 둘 이상이면 오류를 남기고 nullptr 입니다(어느 플레이어인지 등록 순서로 고르지 않는다).
         * @details 씬 하나를 읽는 도구(감시 값 `Session.Money` · `Quest.Status`, 치트, 디버그 HUD, 퀘스트 목표 표시)가 부른다. 레지스트리를 읽어 씬을 훑지 않는다.
         */
        [[nodiscard]] static PlayerProgressComponent* findOpen( const GameObjectManager* pManager );

    private:
        /** @brief 보관하고 있던 복원 바이트를 적용합니다. 구조가 깨졌으면 오류를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool applyPendingState();

    private:
        Wallet          _wallet;
        Inventory       _inventory;
        QuestLog        _questLog;
        ReputationState _reputation;
        vector<uint8>   _pendingStateBytes; ///< 열리기 전에 받은 복원 바이트
        uint8           _bInitialized : 1;
        uint8           _bFreshGame   : 1;
        PROPERTY( Tooltip = "Open with default settings at begin play - only for scenes without a kit director (the hub, showcases); a director opens it with its own settings" )
        uint8                  _bOpenAtBeginPlay : 1;
        [[maybe_unused]] uint8 _reserved         : 5;
    };
} // namespace sw
