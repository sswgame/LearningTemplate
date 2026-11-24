#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        /** @brief 지갑을 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다 — 형제 `PlayerStateComponent` 와 같다). */
        const StatefulComponentRegistrar<WalletComponent> s_walletStateRegistration{ "WalletComponent" };
    } // namespace

    WalletComponent::WalletComponent()
        : _wallet{}
        , _restore{}
    {
        (void)_restore.open( *this ); // 카탈로그가 없어 늘 열려 있다 — 들고 있던 바이트가 없으니 새 판이다
    }

    WalletComponent::~WalletComponent() = default;

    void WalletComponent::serializeState( StateIO& io )
    {
        _restore.serialize( io, kStateTag, kStateVersion, _wallet, "the wallet" );
    }

    void WalletComponent::restoreState( vector<uint8>&& bytes )
    {
        _restore.receive( *this, std::move( bytes ) );
    }
} // namespace sw
