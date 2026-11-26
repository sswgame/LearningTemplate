#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Scene/NavPathSolver.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SceneNavigation;

    /**
     * @class NavPathTestComponent
     * @brief 시작 자리 → 대상 오브젝트의 비동기 경로를 계속 묻는 탐침입니다. 파일 머리말 참고.
     */
    REFLECT()
    class NavPathTestComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavPathTestComponent();
        ~NavPathTestComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 질의를 내는 자리를 옮깁니다. */
        void onWorldOriginShifted( const float3& delta ) override;

        /** @brief 마지막으로 받은 경로의 길이(m)입니다. 아직 받지 못했으면 음수입니다. */
        float32 getLastPathLength() const { return _lastPathLength; }
        uint32  getResultCount() const { return _resultCount; }
        uint32  getCancelLeakCount() const { return _cancelLeakCount; }

    private:
        /** @brief 결과를 받고(첫 완전 경로면 에이전트를 보낸다), 버리기 시험을 한 칸 진행하고, 기다리는 질의가 없으면 새로 냅니다. */
        void updateProbe();

    private:
        PROPERTY( Category = "Probe", DisplayName = "Target", Tooltip = "Name of the object the probe asks a path to" )
        hashed_string _target;

        SceneNavigation*           _pSceneNavigation; ///< 등록한 씬의 내비게이션(등록 동안)
        SW_TRANSIENT float3        _origin;           ///< 플레이를 시작한 자리 — 에이전트가 움직여도 질의는 여기서 낸다
        SW_TRANSIENT NavPathTicket _probe;
        SW_TRANSIENT NavPathTicket _cancelPending; ///< 다음 프레임에 버릴 질의(그 사이 워커에 넘어간다)
        SW_TRANSIENT float32       _lastPathLength;
        SW_TRANSIENT uint32        _requestCount;
        SW_TRANSIENT uint32        _resultCount;
        SW_TRANSIENT uint32        _cancelLeakCount;
        SW_TRANSIENT uint8         _bAgentSent : 1; ///< 첫 완전 경로로 에이전트를 대상에 보냈다
        [[maybe_unused]] uint8     _reserved   : 7;
    };

    namespace
    {
        struct NavPathTestInternal
        {
            /** @brief 대상 오브젝트의 탐침입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const NavPathTestComponent* findProbe( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<NavPathTestComponent>( "NavPathTestComponent", outValue );
            }

            /** @brief 감시 값 `PlaygroundNav.TestPathLength` — 탐침이 마지막으로 받은 비동기 경로의 길이(m)입니다. 아직 받지 못했으면 값이 없습니다. */
            [[nodiscard]] static bool readPathLength( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const NavPathTestComponent* pProbe = findProbe( query, outValue );
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

            /** @brief 감시 값 `PlaygroundNav.TestResultCount` — 탐침이 받은 비동기 경로 결과 수입니다. */
            [[nodiscard]] static bool readResultCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const NavPathTestComponent* pProbe = findProbe( query, outValue );
                if ( pProbe == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pProbe->getResultCount() ) );
                return true;
            }

            /** @brief 감시 값 `PlaygroundNav.TestCancelLeaks` — 버린 질의가 수확 뒤에도 결과를 낸 수입니다(0 이어야 한다). */
            [[nodiscard]] static bool readCancelLeaks( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const NavPathTestComponent* pProbe = findProbe( query, outValue );
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
    NavPathTestComponent::NavPathTestComponent()
        : _target{}
        , _pSceneNavigation{ nullptr }
        , _origin{}
        , _probe{}
        , _cancelPending{}
        , _lastPathLength{ -1.0f }
        , _requestCount{ 0 }
        , _resultCount{ 0 }
        , _cancelLeakCount{ 0 }
        , _bAgentSent{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void NavPathTestComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _pSceneNavigation = &manager.getSubsystem<SceneNavigation>();
    }

    void NavPathTestComponent::onUnregister( GameObjectManager& manager )
    {
        _pSceneNavigation = nullptr;
        Component::onUnregister( manager );
    }

    void NavPathTestComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const SceneComponent* pScene = getOwner()->getPrimarySceneComponent();
        _origin                      = pScene != nullptr ? pScene->getWorldPosition() : float3{};
    }

    void NavPathTestComponent::onEndPlay()
    {
        // 요청자가 사라진다 — 줄에 남은 질의를 버린다. 씬의 내비게이션보다 먼저 놓는다.
        _probe.cancel();
        _cancelPending.cancel();
        Component::onEndPlay();
    }

    void NavPathTestComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _pSceneNavigation != nullptr )
            updateProbe();
    }

    void NavPathTestComponent::onWorldOriginShifted( const float3& delta )
    {
        _origin += delta;
    }

    void NavPathTestComponent::updateProbe()
    {
        NavMeshAgentComponent* pAgent    = getOwner()->getComponent<NavMeshAgentComponent>();
        const hashed_string    agentType = pAgent != nullptr ? pAgent->getAgentType() : hashed_string{};
        NavPath                path;
        // 지난 프레임에 낸 질의는 워커에 넘어갔다 — 구하는 중에 버린다. 버린 표로는 결과를 받을 수 없어야 한다(받으면 샌 것이다).
        if ( _cancelPending.isValid() )
        {
            _cancelPending.cancel();
            if ( _pSceneNavigation->takePathResult( _cancelPending, path ) != PathRequestState::Unknown )
                ++_cancelLeakCount;
        }
        if ( _probe.isValid() )
        {
            const PathRequestState state = _pSceneNavigation->takePathResult( _probe, path );
            if ( state == PathRequestState::Pending )
                return;
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
        const bool bCancelDue = ( _requestCount % 2u ) == 0 && _cancelPending.isValid() == false;
        if ( bCancelDue )
            _cancelPending = _pSceneNavigation->requestPath( agentType, _origin, end );
    }

    SW_AUTOMATION_QUERY_WATCH( playgroundNavTestPathLength, "PlaygroundNav.TestPathLength", "Length (m) of the last async navmesh path the probe on target received; absent before the first",
                               &NavPathTestInternal::readPathLength, true, false );
    SW_AUTOMATION_QUERY_WATCH( playgroundNavTestResultCount, "PlaygroundNav.TestResultCount", "Async path results the probe on target received", &NavPathTestInternal::readResultCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( playgroundNavTestCancelLeaks, "PlaygroundNav.TestCancelLeaks", "Cancelled async path queries of the probe on target that still delivered a result (must stay 0)",
                               &NavPathTestInternal::readCancelLeaks, true, false );
} // namespace sw

#include "NavPathTestComponent.cpp.gen.inl"
