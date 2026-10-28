#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetPairedAnimation/PairedAnimationNetStarterComponent.h"

#include "Engine/Character/PairedAnimation/PairedAnimationComponent.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "PairedAnimationNetStarterComponent" );

    PairedAnimationNetStarterComponent::PairedAnimationNetStarterComponent()
        : _restartDelay{ 0.0f }
        , _endedSeconds{ 0.0f }
        , _bStarts{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    void PairedAnimationNetStarterComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _endedSeconds = 0.0f;

        GameObject*         pOwner   = getOwner();
        GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneNetLink* pLink    = pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr; // 이 컴포넌트의 월드의 넷 링크(전역 세션을 찾지 않는다)
        const bool          bClient  = pLink != nullptr && pLink->isBound() && pLink->get()._role == NetLinkRole::Client;
        if ( bClient )
        {
            _bStarts = SW_FALSE; // 클라이언트는 서버가 보낸 세션을 받기만 한다
            return;
        }
        if ( pOwner == nullptr || pOwner->getComponent<PairedAnimationComponent>() == nullptr )
        {
            SW_LOG_ERROR( "'%#': the paired animation net starter needs a PairedAnimationComponent on its object", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
            _bStarts = SW_FALSE;
            return;
        }
        _bStarts = SW_TRUE;
    }

    void PairedAnimationNetStarterComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                     pOwner   = getOwner();
        const PairedAnimationComponent* pPaired  = _bStarts == SW_TRUE && pOwner != nullptr ? pOwner->getComponent<PairedAnimationComponent>() : nullptr;
        GameObjectManager*              pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pPaired == nullptr || pManager == nullptr )
            return;
        if ( pPaired->getSessionState() == PairedAnimationSessionState::Playing )
        {
            _endedSeconds = 0.0f;
            return;
        }
        _endedSeconds += deltaTime;
        if ( _endedSeconds < _restartDelay )
            return;
        // 세션은 다른 오브젝트의 애니메이터를 바꾼다 — 틱 뒤에 시작한다.
        pManager->executeOrDeferPostTickOnComponent( *this, []( PairedAnimationNetStarterComponent& component, GameObjectManager& manager )
        {
            (void)manager;
            component.startSession();
        } );
    }

    void PairedAnimationNetStarterComponent::startSession()
    {
        GameObject*               pOwner  = getOwner();
        PairedAnimationComponent* pPaired = pOwner != nullptr ? pOwner->getComponent<PairedAnimationComponent>() : nullptr;
        if ( pPaired == nullptr )
            return;
        const PairedAnimationStartResult result = pPaired->startSession( {} );
        if ( result == PairedAnimationStartResult::Started )
        {
            _endedSeconds = 0.0f;
            return;
        }
        if ( result == PairedAnimationStartResult::ParticipantNotReady )
            return; // 클립을 읽는 중 — 다음 틱에 다시 시도한다
        SW_LOG_ERROR( "'%#': paired animation '%#' did not start (result %#); the net starter stops", pOwner->getName().c_str(), pPaired->getPairedAnimationPath().c_str(), static_cast<uint32>( result ) );
        _bStarts = SW_FALSE;
    }
} // namespace sw
