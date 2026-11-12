/**
 * @file ShooterNetSimGame.cpp
 * @brief Shooter3D 를 한 프로세스 가상 서버(`NetSimHarness`, 에디터 Multiplayer 창의 In process 탭)에서 돌리는 `INetSimGame` 입니다(ServerDebug S8).
 * @details 월드마다 아레나 씬(`game/shooter3d/maps/arena.scene.xml`)을 그 월드의 씬에 풀어 넣는다. 씬 안의 `ShooterNetPawnComponent` 는 전역 세션이 아니라
 *          월드가 올린 넷 링크(`SceneNetLink`)로 서버 · 클라이언트 역할을 받으므로, 서버 월드 하나와 클라이언트 월드 N 이 서로 다른 역할로 같은 컴포넌트를 돌린다.
 *          - 서버 월드 · 클라이언트 월드 모두 같은 아레나 씬이다. 적은 서버 몫이고 클라이언트 월드의 감독은 넷 링크 역할을 보고 복제 모습만 그린다(`ShooterGameModeComponent`).
 *          - 입력은 입력 초점이 있는 월드의 폰만 읽는다(`NetSimWorld::hasInputFocus`). 나머지 월드의 몸 이동은 멈춤(`setSuspended`)이라 입력이 0 이다.
 *          - 이 게임의 세션은 가진 메시지가 없다 — 복제는 컴포넌트가 넷 링크의 라우터에 직접 단 키트(`ReplicationServer` · `ReplicationClient`)가 한다.
 *          게임 모듈이 올라오면 `NetSimGameRegistry` 에 등록하고, 내려가기 전에 뺀다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Character/Hit/MeleeSweepHistory.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

#include "Games/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Shooter3D/ShooterNetPawnComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

namespace sw
{
    /** @brief Shooter3D 의 한 프로세스 재생 게임입니다. 파일 머리말 참고. */
    class ShooterNetSimGame final : public INetSimGame
    {
    public:
        static constexpr const utf8* kName      = "Shooter3D: arena"; ///< `NetSimGameRegistry` 에 등록하는 이름
        static constexpr const utf8* kScenePath = "game/shooter3d/maps/arena.scene.xml";

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override;
        /** @brief 아레나를 비스듬히 내려다보는 카메라를 알립니다(씬이 자기 바닥을 가지므로 바닥은 깔지 않는다). */
        bool describePreviewFraming( NetSimPreviewFraming& outFraming ) const override;

        /** @brief `NetSimGameRegistry` 에 등록할 생성 함수입니다. */
        static unique_ptr<INetSimGame> create();
    };
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @class ShooterNetSimSession
         * @brief 월드 하나의 Shooter3D 세션입니다 — 메시지는 없고, 클라이언트 월드의 몸 이동을 입력 초점에 맞춰 멈추고 푼다.
         */
        class ShooterNetSimSession final : public INetSimSession
        {
        public:
            ShooterNetSimSession( GameObjectManager& manager, GameObjectHandle pawnObject )
                : _pManager{ &manager }
                , _pawnObject{ pawnObject }
            {
            }

            /** @brief 시나리오가 보는 이 월드의 값입니다(`Editor.NetSimValue`). */
            bool readValue( string_view key, float64& outValue ) const override
            {
                const GameObject*              pPawn    = _pManager->resolveGameObject( _pawnObject );
                const ShooterPlayerComponent*  pPlayer  = pPawn != nullptr ? pPawn->getComponent<ShooterPlayerComponent>() : nullptr;
                const ShooterNetPawnComponent* pNetPawn = pPawn != nullptr ? pPawn->getComponent<ShooterNetPawnComponent>() : nullptr;
                if ( pPlayer == nullptr || pNetPawn == nullptr )
                    return false;
                if ( key == "ShotCount" )
                    outValue = static_cast<float64>( pPlayer->getShotCount() );
                else if ( key == "HitCount" )
                    outValue = static_cast<float64>( pPlayer->getHitCount() );
                else if ( key == "RewoundShots" )
                    outValue = static_cast<float64>( pNetPawn->getRewoundShotCount() );
                else if ( key == "RewoundHits" )
                    outValue = static_cast<float64>( pNetPawn->getRewoundHitCount() );
                else if ( key == "RewindTicks" )
                    outValue = static_cast<float64>( pNetPawn->getLastRewindTicks() );
                else if ( key == "Corrections" )
                    outValue = static_cast<float64>( pNetPawn->getCorrectionCount() );
                else if ( key == "ClaimsSent" )
                    outValue = static_cast<float64>( pNetPawn->getSentClaimCount() );
                else if ( key == "ClaimsConfirmed" )
                    outValue = static_cast<float64>( pNetPawn->getConfirmedClaimCount() );
                else if ( key == "ClaimsRejected" )
                    outValue = static_cast<float64>( pNetPawn->getRejectedClaimCount() );
                else if ( key == "MeleeSweeps" )
                    outValue = static_cast<float64>( countMeleeSweeps() );
                else if ( key == "EnemyCount" )
                    outValue = static_cast<float64>( countEnemies( *pPlayer ) );
                else
                    return false;
                return true;
            }

            void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
            {
                (void)deltaTime;
                // 서버 월드의 몸은 클라이언트 입력으로 움직인다. 클라이언트 월드는 입력 초점이 있는 하나만 입력(의도)을 읽는다.
                if ( world.isServer() )
                    return;
                GameObject*                   pPawn     = world.getObjectManager().resolveGameObject( _pawnObject );
                ShooterBodyMovementComponent* pMovement = pPawn != nullptr ? pPawn->getComponent<ShooterBodyMovementComponent>() : nullptr;
                if ( pMovement != nullptr )
                    pMovement->setSuspended( world.hasInputFocus() == false );
            }

        private:
            size_t countMeleeSweeps() const
            {
                const MeleeSweepHistory* pLedger = MeleeSweepHistory::find( *_pManager );
                return pLedger != nullptr ? pLedger->getRecordCount() : 0u;
            }

            size_t countEnemies( const ShooterPlayerComponent& player ) const
            {
                const GameObject*               pGameModeObject = _pManager->resolveGameObject( player.getGameMode() );
                const ShooterGameModeComponent* pGameMode       = pGameModeObject != nullptr ? pGameModeObject->getComponent<ShooterGameModeComponent>() : nullptr;
                return pGameMode != nullptr ? pGameMode->getEnemyViews().size() : 0u;
            }

        private:
            GameObjectManager* _pManager;   ///< 이 월드의 매니저(월드가 세션보다 오래 산다)
            GameObjectHandle   _pawnObject; ///< 이 월드의 플레이어 몸(넷 폰 컴포넌트가 달린 오브젝트)
        };

        /** @brief 이 매니저에서 넷 폰 컴포넌트가 달린 오브젝트를 찾습니다. 없으면 빈 핸들입니다. */
        GameObjectHandle findPawnObject( GameObjectManager& manager )
        {
            for ( const ShooterNetPawnComponent* pPawn : manager.getComponentRegistry().getAll<ShooterNetPawnComponent>() )
            {
                const GameObject* pObject = pPawn->getOwner();
                if ( pObject != nullptr )
                    return pObject->getHandle();
            }
            return GameObjectHandle{};
        }
    } // namespace

    unique_ptr<INetSimSession> ShooterNetSimGame::createSession( NetSimWorld& world )
    {
        SceneDocument document{};
        if ( document.load( AssetID{ kScenePath } ) == false )
        {
            SW_LOG_ERROR_ID( "ShooterNetSimGame.ShooterNetSimArena", "Shooter net sim: the arena scene '%#' could not be loaded - the world stays empty", kScenePath );
            return nullptr;
        }
        if ( world.getScene().instantiate( document ) == false )
        {
            SW_LOG_ERROR_ID( "ShooterNetSimGame.ShooterNetSimArena2", "Shooter net sim: the arena scene '%#' could not be instantiated - the world stays incomplete", kScenePath );
            return nullptr;
        }
        GameObjectManager&     manager    = world.getObjectManager();
        const GameObjectHandle pawnObject = findPawnObject( manager );
        if ( manager.resolveGameObject( pawnObject ) == nullptr )
            SW_LOG_WARNING_ID( "ShooterNetSimGame.ShooterNetSimArena3", "Shooter net sim: the arena scene has no object with a ShooterNetPawnComponent - the world has no player body" );
        return make_unique<ShooterNetSimSession>( manager, pawnObject );
    }

    bool ShooterNetSimGame::describePreviewFraming( NetSimPreviewFraming& outFraming ) const
    {
        outFraming._target    = float3{ 0.0f, 0.0f, 0.0f };
        outFraming._distance  = 40.0f;
        outFraming._yaw       = 0.0f;
        outFraming._pitch     = 0.9f;
        outFraming._floorSize = 0.0f;
        return true;
    }

    unique_ptr<INetSimGame> ShooterNetSimGame::create()
    {
        return make_unique<ShooterNetSimGame>();
    }

    namespace
    {
        /**
         * @brief 게임 모듈이 로드된 동안 이 게임을 한 프로세스 재생의 게임 목록에 올립니다. 등록과 해제를 한 객체의 생성자 · 소멸자에 두어 한쪽만 하는 경로가 없다
         *        (목록 항목의 만드는 함수가 이 모듈의 코드라 모듈이 내려가기 전에 빼야 한다 — `NetSimGameRegistry`).
         */
        struct ShooterNetSimRegistrar
        {

            NetSimGameEntry _entry{ ShooterNetSimGame::kName, &ShooterNetSimGame::create };
            ShooterNetSimRegistrar() { (void)NetSimGameRegistry::registerGame( _entry ); }
            ~ShooterNetSimRegistrar() { (void)NetSimGameRegistry::unregisterGame( _entry ); }
        };

        const ShooterNetSimRegistrar s_shooterNetSimRegistrar;
    } // namespace
} // namespace sw
