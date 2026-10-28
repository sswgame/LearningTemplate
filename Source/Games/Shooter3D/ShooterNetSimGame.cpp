#include "pch.h"

#include "Games/Shooter3D/ShooterNetSimGame.h"

#include "Core/Container/vector.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceID.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"

#include "Games/Shooter3D/ShooterBodyMovementComponent.h"
#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterNetPawnComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ShooterNetSimGame" );

    namespace
    {
        /**
         * @class ShooterNetSimSession
         * @brief 월드 하나의 Shooter3D 세션입니다 — 메시지는 없고, 클라이언트 월드의 몸 이동을 입력 초점에 맞춰 멈추고 푼다.
         */
        class ShooterNetSimSession final : public INetSimSession
        {
        public:
            explicit ShooterNetSimSession( GameObjectHandle pawnObject )
                : _pawnObject{ pawnObject }
            {
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
            GameObjectHandle _pawnObject; ///< 이 월드의 플레이어 몸(넷 폰 컴포넌트가 달린 오브젝트)
        };

        /** @brief 이 매니저에서 넷 폰 컴포넌트가 달린 오브젝트를 찾습니다. 없으면 빈 핸들입니다. */
        GameObjectHandle findPawnObject( GameObjectManager& manager )
        {
            vector<GameObject*> listObject;
            manager.getAllGameObjects( listObject );
            for ( GameObject* pObject : listObject )
            {
                if ( pObject->getComponent<ShooterNetPawnComponent>() != nullptr )
                    return pObject->getHandle();
            }
            return GameObjectHandle{};
        }
    } // namespace

    unique_ptr<INetSimSession> ShooterNetSimGame::createSession( NetSimWorld& world )
    {
        SceneDocument document{};
        if ( document.load( ResourceID{ kScenePath } ) == false )
        {
            SW_LOG_ERROR( "Shooter net sim: the arena scene '%#' could not be loaded - the world stays empty", kScenePath );
            return nullptr;
        }
        if ( world.getScene().instantiate( document ) == false )
        {
            SW_LOG_ERROR( "Shooter net sim: the arena scene '%#' could not be instantiated - the world stays incomplete", kScenePath );
            return nullptr;
        }
        GameObjectManager& manager = world.getObjectManager();
        if ( world.isServer() == false )
        {
            // 적 · 웨이브는 서버 몫이다 — 클라이언트 월드에 켜 두면 월드마다 다른 적이 선다.
            vector<GameObject*> listObject;
            manager.getAllGameObjects( listObject );
            for ( GameObject* pObject : listObject )
            {
                if ( ShooterDirectorComponent* pDirector = pObject->getComponent<ShooterDirectorComponent>() )
                    pDirector->setActive( false );
            }
        }
        const GameObjectHandle pawnObject = findPawnObject( manager );
        if ( manager.resolveGameObject( pawnObject ) == nullptr )
            SW_LOG_WARNING( "Shooter net sim: the arena scene has no object with a ShooterNetPawnComponent - the world has no player body" );
        return make_unique<ShooterNetSimSession>( pawnObject );
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
            ShooterNetSimRegistrar() { (void)NetSimGameRegistry::registerGame( _entry ); }
            ~ShooterNetSimRegistrar() { (void)NetSimGameRegistry::unregisterGame( _entry ); }

            NetSimGameEntry _entry{ ShooterNetSimGame::kName, &ShooterNetSimGame::create };
        };

        const ShooterNetSimRegistrar s_shooterNetSimRegistrar;
    } // namespace
} // namespace sw
