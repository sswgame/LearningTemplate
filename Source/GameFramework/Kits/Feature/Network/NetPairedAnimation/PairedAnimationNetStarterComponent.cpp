#include "pch.h"

#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Character/PairedAnimation/PairedAnimationComponent.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    /**
     * @class PairedAnimationNetStarterComponent
     * @brief 파일 머리말 참고.
     */
    REFLECT( Category = "Network", DisplayName = "Paired Animation Net Starter", Tooltip = "Server side: restarts this object's paired animation session each time it ends (verification scene driver)" )
    class PairedAnimationNetStarterComponent : public Component
    {
    public:
        REFLECT_BODY();

        PairedAnimationNetStarterComponent();
        ~PairedAnimationNetStarterComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

    private:
        /** @brief 같은 오브젝트의 `PairedAnimationComponent` 로 세션을 시작합니다(틱 뒤에 불립니다). */
        void startSession();

        PROPERTY( Category = "Network", DisplayName = "Restart Delay", Min = 0.0, Units = s, Tooltip = "Seconds the session may stay ended before the next one starts" )
        float32 _restartDelay;

        SW_TRANSIENT float32 _endedSeconds; ///< 세션이 재생 중이 아니었던 시간
        SW_TRANSIENT uint8   _bStarts;      ///< 이 프로세스가 시작한다(클라이언트가 아니고 데이터 결함으로 멈추지 않았다)
    };

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

        GameObject*         pOwner  = getOwner();
        const NetLinkValue* pLink   = SceneNetLink::findBoundFor( *this ); // 이 컴포넌트의 월드의 넷 링크(전역 세션을 찾지 않는다)
        const bool          bClient = pLink != nullptr && pLink->_role == NetLinkRole::Client;
        if ( bClient )
        {
            _bStarts = SW_FALSE; // 클라이언트는 서버가 보낸 세션을 받기만 한다
            return;
        }
        if ( pOwner == nullptr || pOwner->getComponent<PairedAnimationComponent>() == nullptr )
        {
            SW_LOG_ERROR_ID( "PairedAnimationNetStarterComponent.PairedAnimationNetStarter", "'%#': the paired animation net starter needs a PairedAnimationComponent on its object", pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
            _bStarts = SW_FALSE;
            return;
        }
        _bStarts = SW_TRUE;
    }

    void PairedAnimationNetStarterComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*                     pOwner  = getOwner();
        const PairedAnimationComponent* pPaired = _bStarts == SW_TRUE && pOwner != nullptr ? pOwner->getComponent<PairedAnimationComponent>() : nullptr;
        if ( pPaired == nullptr )
            return;
        if ( pPaired->getSessionState() == PairedAnimationSessionState::Playing )
        {
            _endedSeconds = 0.0f;
            return;
        }
        _endedSeconds += context.getDeltaTime().count();
        if ( _endedSeconds < _restartDelay )
            return;
        // 세션은 다른 오브젝트의 애니메이터를 바꾼다 — 틱 뒤에 시작한다.
        context.deferPostTickOnComponent( *this, []( PairedAnimationNetStarterComponent& component, GameObjectManager& manager )
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
        SW_LOG_ERROR_ID( "PairedAnimationNetStarterComponent.PairedAnimationDidNot", "'%#': paired animation '%#' did not start (result %#); the net starter stops", pOwner->getName().c_str(), pPaired->getPairedAnimationPath().c_str(), static_cast<uint32>( result ) );
        _bStarts = SW_FALSE;
    }
} // namespace sw

#include "PairedAnimationNetStarterComponent.cpp.gen.inl"
