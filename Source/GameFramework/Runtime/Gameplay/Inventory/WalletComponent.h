/**
 * @file WalletComponent.h
 * @brief 플레이어 한 명의 지갑(통화별 잔액)을 조종자 오브젝트에 두는 컴포넌트입니다. 언리얼 `APlayerState` 에 붙는 기능 컴포넌트 자리입니다.
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
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class WalletComponent
     * @brief 플레이어 한 명의 지갑입니다. 조종자 오브젝트에 다른 플레이어 상태 컴포넌트(`InventoryComponent` · `QuestLogComponent` · `ReputationComponent`)와 함께 둡니다.
     * @details - **열기**: 카탈로그가 없어 만들 때부터 열려 있다. 새 판인지(`isFreshGame`)는 복원 바이트를 받았는지로 정한다 — 디렉터가 시작 돈을 넣을지 볼 때 묻는다.
     *          - **찾기 · 세이브**: `PlayerComponentUtil` · `DeferredStateRestore`. 정의한 .cpp 가 `StatefulComponentRegistrar` 로 모든 게임의 스냅샷에 올린다.
     */
    REFLECT( Category = "Framework", DisplayName = "Wallet", Tooltip = "One player's wallet, on the controller object (or the director object in a scene without a controller)" )
    class SW_GF_API WalletComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( WalletComponent );

        static constexpr uint32 kStateTag     = FourCCUtil::make( "WLTC" );
        static constexpr uint32 kStateVersion = 1;

        WalletComponent();
        ~WalletComponent() override;

        /** @brief 지갑 구간 하나를 씁니다. `ComponentStateStore::capture` 가 부릅니다. */
        SW_DECLARE_STATE_IO_IN_PLACE();
        /** @brief 바로 적용합니다(늘 열려 있다). `ComponentStateStore::restore` 가 부릅니다. */
        void restoreState( vector<uint8>&& bytes );

        /** @brief 늘 true 입니다 — `PlayerComponentUtil::findOpen` 의 약속을 다른 플레이어 상태 컴포넌트와 같게 둔다. */
        bool          isOpen() const { return _restore.isOpen(); }
        bool          isFreshGame() const { return _restore.isFreshGame(); }
        Wallet&       getWallet() { return _wallet; }
        const Wallet& getWallet() const { return _wallet; }

    private:
        Wallet               _wallet;
        DeferredStateRestore _restore;
    };
} // namespace sw
