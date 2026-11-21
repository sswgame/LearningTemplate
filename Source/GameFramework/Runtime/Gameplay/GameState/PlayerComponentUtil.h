/**
 * @file PlayerComponentUtil.h
 * @brief 플레이어 상태 컴포넌트(`WalletComponent` · `InventoryComponent` · `QuestLogComponent` · `ReputationComponent`)가 함께 쓰는 늦은 복원 칸과 찾기입니다.
 * @details 상태 범위 셋(언리얼 `AGameState` · `APlayerState` · 매치 상태) 가운데 플레이어 몫은 기능마다 컴포넌트 하나로 조종자 오브젝트에 함께 붙는다(Lyra 의
 *          `ULyraInventoryManagerComponent` · `ULyraQuickBarComponent` 처럼 기능마다). 조종자가 없는 디렉터 전용 씬(옛 게임 맵)은 디렉터 오브젝트에 둔다.
 *          기능이 없으면 컴포넌트가 없다 — "슬롯 0 이면 가방 없음" 같은 특수 값을 두지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/StateSectionTableUtil.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class DeferredStateRestore
     * @brief 열기(카탈로그 · 설정) 전에 받은 복원 바이트를 들고 있다가 열 때 적용하는 칸입니다. 플레이어 상태 컴포넌트가 멤버로 듭니다.
     * @details 세이브 복원(`ComponentStateStore::restore`)은 디렉터의 `startGame`(열기)보다 먼저 올 수 있고, 읽기에는 카탈로그가 있어야 한다(일지는 카탈로그에 없는
     *          퀘스트를 버린다). 열린 뒤에 받은 바이트는 바로 적용한다. 새 판인지(`isFreshGame`)는 디렉터가 시작값(시작 돈 · 첫 아이템)을 넣을지 볼 때 묻는다.
     *          상태는 구간 하나(`StateSectionTableUtil` — 월드 상태와 같은 꼴)로 싣는다. 안쪽 형식의 판 올림 사슬과 "전부 아니면 없음" 을 그 틀이 맡는다.
     */
    class DeferredStateRestore
    {
    public:
        DeferredStateRestore()
            : _heldBytes{}
            , _bOpen{ SW_FALSE }
            , _bFreshGame{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        bool isOpen() const { return _bOpen == SW_TRUE; }
        bool isFreshGame() const { return _bFreshGame == SW_TRUE; }

        /**
         * @brief 엽니다. 들고 있던 바이트가 있으면 @p component 의 `readState` 로 적용합니다. 부르는 쪽은 카탈로그 · 설정을 먼저 넣습니다.
         * @return 새 판, 복원, 이미 열림 중 하나입니다. 바이트를 읽지 못했으면 오류를 남기고 새 판(`Fresh`)입니다.
         */
        template <typename TComponent>
        [[nodiscard]] GameStateInitResult open( TComponent& component )
        {
            if ( _bOpen == SW_TRUE )
                return GameStateInitResult::AlreadyInitialized;
            _bOpen      = SW_TRUE;
            _bFreshGame = SW_TRUE;
            if ( _heldBytes.empty() || apply( component ) == false )
                return GameStateInitResult::Fresh;
            _bFreshGame = SW_FALSE;
            return GameStateInitResult::Restored;
        }

        /** @brief 복원 바이트를 받습니다. 열리기 전이면 들고 있다가 `open` 에서, 열린 뒤면 바로 적용합니다. 컴포넌트의 `restoreState` 가 부릅니다. */
        template <typename TComponent>
        void receive( TComponent& component, vector<uint8>&& bytes )
        {
            _heldBytes = std::move( bytes );
            if ( _bOpen == SW_TRUE && apply( component ) )
                _bFreshGame = SW_FALSE;
        }

        /**
         * @brief @p state 를 머리(@p tag · @p version)와 구간 하나로 쓰거나 읽습니다. 컴포넌트의 `serializeState` 가 부릅니다.
         * @details 열리기 전(복원한 직후의 두 번째 캡처)에 바이트를 들고 있으면 그 바이트가 곧 상태이다 — 빈 판을 쓰면 세이브를 잃는다.
         */
        template <typename TState>
        void serialize( StateIO& io, uint32 tag, uint32 version, TState& state, const utf8* pName ) const
        {
            const auto forEachSection = [&state, pName]( auto&& visit )
            { visit( state, pName ); };
            if ( io.isWriting() )
            {
                Archive& archive = io.getArchive();
                if ( _bOpen == SW_FALSE && _heldBytes.empty() == false )
                {
                    archive.writeBytes( _heldBytes.data(), _heldBytes.size() );
                    return;
                }
                StateSectionTableUtil::write( archive, tag, version, 1, forEachSection );
                return;
            }
            (void)StateSectionTableUtil::read( io, tag, version, 1, forEachSection, pName );
        }

    private:
        /** @brief 들고 있던 바이트를 적용하고 비웁니다. 구조가 깨졌으면 오류를 남기고 false 입니다. */
        template <typename TComponent>
        [[nodiscard]] bool apply( TComponent& component )
        {
            Archive    archive( _heldBytes.data(), _heldBytes.size() );
            const bool bRestored = component.readState( archive );
            _heldBytes.clear();
            if ( bRestored == false )
                SW_LOG_ERROR_ID( "PlayerComponent.SavedStateUnread", "%# on '%#': the saved state could not be read - it starts fresh", component.getTypeName().c_str(),
                                 component.getOwner() != nullptr ? component.getOwner()->getName().c_str() : "?" );
            return bRestored;
        }

    private:
        vector<uint8>          _heldBytes; ///< 열리기 전에 받은 복원 바이트
        uint8                  _bOpen      : 1;
        uint8                  _bFreshGame : 1;
        [[maybe_unused]] uint8 _reserved   : 6;
    };

    /**
     * @struct PlayerComponentUtil
     * @brief 플레이어 상태 컴포넌트 찾기 · 열기입니다. 컴포넌트마다 같은 찾기를 되풀이하지 않게 형식 인자로 받는다.
     * @details - **디렉터**: 같은 오브젝트에서 `findOnOwner` · `openOnOwner`(디렉터의 `startGame`).
     *          - **폰**: `findForPawnObject`(폰 → 쥔 조종자 → 그 오브젝트 — 형제 `PlayerStateComponent` 와 같은 길).
     *          - **씬 하나를 읽는 도구**(감시 값 · 치트 · 디버그 HUD · 퀘스트 목표 표시): `findOpen` — 열린 것이 둘 이상이면 오류로 멈춘다(어느 플레이어인지 등록 순서로 고르지 않는다).
     *            컴포넌트는 `onRegister` 에서 레지스트리에 올라 있어야 한다.
     */
    struct SW_GF_API PlayerComponentUtil
    {
        /** @brief @p component 와 같은 오브젝트의 @p T 입니다. 없으면 nullptr 입니다. */
        template <typename T>
        [[nodiscard]] static T* findOnOwner( const Component& component )
        {
            GameObject* const pOwner = component.getOwner();
            return pOwner != nullptr ? pOwner->getComponent<T>() : nullptr;
        }

        /** @brief @p component 와 같은 오브젝트의 @p T 입니다. 판을 열 디렉터가 반드시 쓰는 것이라 없으면 오류를 남기고 nullptr 입니다. 열기가 없는 지갑은 이것으로 찾는다. */
        template <typename T>
        [[nodiscard]] static T* requireOnOwner( const Component& component )
        {
            T* const pFound = findOnOwner<T>( component );
            if ( pFound == nullptr )
                SW_LOG_ERROR_ID( "PlayerComponent.MissingOnOwner", "no %# next to the game mode on this object - the game cannot start", T::StaticType()->_name.c_str() );
            return pFound;
        }

        /** @brief @p component 와 같은 오브젝트의 @p T 를 @p args 로 엽니다(`T::initialize`). 없거나 거절되면 오류를 남기고 nullptr 입니다. */
        template <typename T, typename... TArgs>
        [[nodiscard]] static T* openOnOwner( const Component& component, const TArgs&... args )
        {
            T* const pFound = requireOnOwner<T>( component );
            if ( pFound == nullptr )
                return nullptr;
            return pFound->initialize( args... ) != GameStateInitResult::Refused ? pFound : nullptr; // 거절한 까닭은 `initialize` 가 남겼다
        }

        /** @brief @p pawnObject 의 폰을 쥔 조종자 오브젝트의 @p T 입니다. 폰 · 조종자 · @p T 가 없으면 nullptr 입니다. */
        template <typename T>
        [[nodiscard]] static T* findForPawnObject( GameObjectManager& manager, const GameObject& pawnObject )
        {
            GameObject* const pControllerObject = findControllerObject( manager, pawnObject );
            return pControllerObject != nullptr ? pControllerObject->getComponent<T>() : nullptr;
        }

        /**
         * @brief 씬 @p pManager 의 열린(`isOpen`) @p T 입니다. 없으면 nullptr, 둘 이상이면 오류를 남기고 nullptr 입니다.
         * @details 매니저 형식은 형식 인자로 받아 이 헤더가 오브젝트 매니저를 include 하지 않는다. 부르는 쪽은 이미 include 했다. 레지스트리를 읽어 씬을 훑지 않는다.
         */
        template <typename T, typename TManager>
        [[nodiscard]] static T* findOpen( const TManager* pManager )
        {
            if ( pManager == nullptr )
                return nullptr;
            T* pFound = nullptr;
            for ( T* pComponent : pManager->getComponentRegistry().template getAll<T>() )
            {
                if ( pComponent->isOpen() == false )
                    continue;
                if ( pFound != nullptr )
                {
                    SW_LOG_ERROR_ID( "PlayerComponent.SeveralOpen", "the scene has more than one open %# ('%#', '%#') - a single-player reader cannot pick one", T::StaticType()->_name.c_str(),
                                     pFound->getOwner() != nullptr ? pFound->getOwner()->getName().c_str() : "?",
                                     pComponent->getOwner() != nullptr ? pComponent->getOwner()->getName().c_str() : "?" );
                    return nullptr;
                }
                pFound = pComponent;
            }
            return pFound;
        }

        /** @brief @p pawnObject 의 폰을 쥔 조종자의 오브젝트입니다. 폰이나 조종자가 없으면 nullptr 입니다. */
        [[nodiscard]] static GameObject* findControllerObject( GameObjectManager& manager, const GameObject& pawnObject );
    };
} // namespace sw
