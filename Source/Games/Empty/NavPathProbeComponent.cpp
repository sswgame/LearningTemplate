#include "pch.h"

#include "Games/Empty/NavPathProbeComponent.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw
{
    namespace
    {
        struct NavPathProbeInternal
        {
            /** @brief 대상 오브젝트의 탐침입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const NavPathProbeComponent* findProbe( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pObject = query.findTargetObject();
                if ( pObject == nullptr )
                {
                    outValue.setText( query.getTargetProblem() );
                    return nullptr;
                }
                const NavPathProbeComponent* pProbe = pObject->getComponent<NavPathProbeComponent>();
                if ( pProbe == nullptr )
                    outValue.setText( query.describeMissing( "NavPathProbeComponent" ) );
                return pProbe;
            }

            /** @brief 감시 값 `EmptyNav.ProbePathLength` — 탐침이 마지막으로 받은 비동기 경로의 길이(m)입니다. 아직 받지 못했으면 값이 없습니다. */
            [[nodiscard]] static bool readPathLength( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const NavPathProbeComponent* pProbe = findProbe( query, outValue );
                if ( pProbe == nullptr )
                    return false;
                if ( pProbe->getLastPathLength() < 0.0f )
                {
                    outValue.setText( "no async path result yet" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pProbe->getLastPathLength() ) );
                return true;
            }

            /** @brief 감시 값 `EmptyNav.ProbeResultCount` — 탐침이 받은 비동기 경로 결과 수입니다. */
            [[nodiscard]] static bool readResultCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const NavPathProbeComponent* pProbe = findProbe( query, outValue );
                if ( pProbe == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pProbe->getResultCount() ) );
                return true;
            }

            /** @brief 감시 값 `EmptyNav.ProbeCancelLeaks` — 버린 질의가 수확 뒤에도 결과를 낸 수입니다(0 이어야 한다). */
            [[nodiscard]] static bool readCancelLeaks( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const NavPathProbeComponent* pProbe = findProbe( query, outValue );
                if ( pProbe == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pProbe->getCancelLeakCount() ) );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NavPathProbeComponent::NavPathProbeComponent()
        : _target{}
        , _pSceneNavigation{ nullptr }
        , _origin{}
        , _probe{}
        , _cancelPending{}
        , _cancelled{}
        , _lastPathLength{ -1.0f }
        , _requestCount{ 0 }
        , _resultCount{ 0 }
        , _cancelLeakCount{ 0 }
        , _bAgentSent{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void NavPathProbeComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _pSceneNavigation = &manager.getSubsystem<SceneNavigation>();
    }

    void NavPathProbeComponent::onUnregister( GameObjectManager& manager )
    {
        _pSceneNavigation = nullptr;
        Component::onUnregister( manager );
    }

    void NavPathProbeComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const SceneComponent* pScene = getOwner()->getPrimarySceneComponent();
        _origin                      = pScene != nullptr ? pScene->getWorldPosition() : float3{};
    }

    void NavPathProbeComponent::onEndPlay()
    {
        // 요청자가 사라진다 — 줄에 남은 질의를 버린다.
        if ( _pSceneNavigation != nullptr )
        {
            _pSceneNavigation->cancelPathRequest( _probe );
            _pSceneNavigation->cancelPathRequest( _cancelPending );
        }
        _probe         = PathRequestHandle{};
        _cancelPending = PathRequestHandle{};
        _cancelled     = PathRequestHandle{};
        Component::onEndPlay();
    }

    void NavPathProbeComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _pSceneNavigation != nullptr )
            updateProbe();
    }

    void NavPathProbeComponent::updateProbe()
    {
        NavMeshAgentComponent* pAgent    = getOwner()->getComponent<NavMeshAgentComponent>();
        const hashed_string    agentType = pAgent != nullptr ? pAgent->getAgentType() : hashed_string{};
        NavPath                path;
        if ( _cancelled.isValid() )
        {
            if ( _pSceneNavigation->takePathResult( _cancelled, path ) != PathRequestState::Unknown )
                ++_cancelLeakCount;
            _cancelled = PathRequestHandle{};
        }
        if ( _cancelPending.isValid() )
        {
            _pSceneNavigation->cancelPathRequest( _cancelPending );
            _cancelled     = _cancelPending;
            _cancelPending = PathRequestHandle{};
        }
        if ( _probe.isValid() )
        {
            const PathRequestState state = _pSceneNavigation->takePathResult( _probe, path );
            if ( state == PathRequestState::Pending )
                return;
            _probe = PathRequestHandle{};
            if ( state == PathRequestState::Ready )
            {
                ++_resultCount;
                _lastPathLength = path._status == NavPathStatus::Failed ? -1.0f : path.computeLength();
                // 첫 완전 경로가 오면 같은 오브젝트의 에이전트를 보낸다(요청 → 결과 → 도착). 끝점은 대상 자리를 내비메시에 붙인 곳이다.
                const bool bSendAgent = _bAgentSent == SW_FALSE && pAgent != nullptr && path._status == NavPathStatus::Complete && path._listPoint.empty() == false;
                if ( bSendAgent )
                {
                    pAgent->setDestination( path._listPoint.back() );
                    _bAgentSent = SW_TRUE;
                }
            }
        }
        // 대상은 이름으로 찾는다 — 틱 동안 구조가 얼어 있어 읽기만 한다.
        const GameObject*     pTarget      = getOwner()->getManager() != nullptr ? getOwner()->getManager()->findGameObjectByName( _target ) : nullptr;
        const SceneComponent* pTargetScene = pTarget != nullptr ? pTarget->getPrimarySceneComponent() : nullptr;
        if ( pTargetScene == nullptr )
            return;
        const float3 end = pTargetScene->getWorldPosition();
        _probe           = _pSceneNavigation->requestPath( agentType, _origin, end );
        ++_requestCount;
        // 버리기 시험은 하나 걸러 — 확인이 끝나야(셋째 프레임) 다음을 낸다.
        const bool bCancelDue = ( _requestCount % 2u ) == 0 && _cancelPending.isValid() == false && _cancelled.isValid() == false;
        if ( bCancelDue )
            _cancelPending = _pSceneNavigation->requestPath( agentType, _origin, end );
    }

    SW_AUTOMATION_QUERY_WATCH( emptyNavProbePathLength, "EmptyNav.ProbePathLength", "Length (m) of the last async navmesh path the probe on target received; absent before the first",
                               &NavPathProbeInternal::readPathLength, true, false );
    SW_AUTOMATION_QUERY_WATCH( emptyNavProbeResultCount, "EmptyNav.ProbeResultCount", "Async path results the probe on target received", &NavPathProbeInternal::readResultCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( emptyNavProbeCancelLeaks, "EmptyNav.ProbeCancelLeaks", "Cancelled async path queries of the probe on target that still delivered a result (must stay 0)",
                               &NavPathProbeInternal::readCancelLeaks, true, false );
} // namespace sw
