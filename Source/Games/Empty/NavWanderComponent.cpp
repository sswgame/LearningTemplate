#include "pch.h"

#include "Games/Empty/NavWanderComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw
{
    namespace
    {
        struct NavWanderInternal
        {
            /** @brief 떠돌기 수열의 씨앗을 섞는 값입니다(같은 씨앗을 쓰는 다른 수열과 겹치지 않게). */
            static constexpr uint32 kWanderSalt = 0x57414e44u; // "WAND"

            /** @brief 씬의 떠돌이마다 @p read 로 읽은 수를 더합니다. 씬이 없으면 false 입니다. */
            template <uint32 ( NavWanderComponent::*Read )() const>
            [[nodiscard]] static bool sumWanderers( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint64 total = 0;
                pManager->forEachComponentOfType<NavWanderComponent>( [&total]( const NavWanderComponent* pWander )
                { total += ( pWander->*Read )(); } );
                outValue = static_cast<float64>( total );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    NavWanderComponent::NavWanderComponent()
        : _homeCenter{}
        , _homeHalfExtent{ 10.0f }
        , _wanderRadius{ 6.0f }
        , _seed{ 1 }
        , _probeInterval{ 4 }
        , _random{}
        , _pSceneNavigation{ nullptr }
        , _probe{}
        , _cancelPending{}
        , _cancelled{}
        , _retargetCount{ 0 }
        , _arrivalCount{ 0 }
        , _probeResultCount{ 0 }
        , _cancelLeakCount{ 0 }
    {
        setCanEverTick( true );
    }

    void NavWanderComponent::setWander( const float3& homeCenter, float32 homeHalfExtent, float32 wanderRadius, uint32 seed, uint32 probeInterval )
    {
        _homeCenter     = homeCenter;
        _homeHalfExtent = homeHalfExtent;
        _wanderRadius   = wanderRadius;
        _seed           = seed;
        _probeInterval  = probeInterval;
    }

    void NavWanderComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _pSceneNavigation = &manager.getSubsystem<SceneNavigation>();
    }

    void NavWanderComponent::onUnregister( GameObjectManager& manager )
    {
        _pSceneNavigation = nullptr;
        Component::onUnregister( manager );
    }

    void NavWanderComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _random.setSeed( DeterministicRandom32::deriveSeed( _seed, NavWanderInternal::kWanderSalt ) );
    }

    void NavWanderComponent::onEndPlay()
    {
        // 요청자가 사라진다 — 줄에 남은 질의를 버린다(언리얼 `AbortAsyncFindPathRequest`).
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

    void NavWanderComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        NavMeshAgentComponent* pAgent = getOwner()->getComponent<NavMeshAgentComponent>();
        if ( pAgent == nullptr || _pSceneNavigation == nullptr )
            return;
        collectProbeResults();
        const NavMoveStatus status = pAgent->getMoveStatus();
        if ( status == NavMoveStatus::Moving )
            return;
        if ( status == NavMoveStatus::Arrived )
            ++_arrivalCount;
        retarget( *pAgent );
    }

    void NavWanderComponent::collectProbeResults()
    {
        NavPath path;
        // 버린 질의는 워커가 풀었더라도 수확에서 버려져야 한다 — 결과가 보이면 샌 것이다.
        if ( _cancelled.isValid() )
        {
            if ( _pSceneNavigation->takePathResult( _cancelled, path ) != PathRequestState::Unknown )
                ++_cancelLeakCount;
            _cancelled = PathRequestHandle{};
        }
        // 지난 프레임에 낸 질의는 이제 워커에 넘어갔다 — 구하는 중에 버린다.
        if ( _cancelPending.isValid() )
        {
            _pSceneNavigation->cancelPathRequest( _cancelPending );
            _cancelled     = _cancelPending;
            _cancelPending = PathRequestHandle{};
        }
        if ( _probe.isValid() == false )
            return;
        const PathRequestState state = _pSceneNavigation->takePathResult( _probe, path );
        if ( state == PathRequestState::Pending )
            return;
        if ( state == PathRequestState::Ready )
            ++_probeResultCount;
        _probe = PathRequestHandle{};
    }

    void NavWanderComponent::retarget( NavMeshAgentComponent& agent )
    {
        const float3  from    = agent.getAgentPosition();
        const float32 minX    = _homeCenter._x - _homeHalfExtent;
        const float32 maxX    = _homeCenter._x + _homeHalfExtent;
        const float32 minZ    = _homeCenter._z - _homeHalfExtent;
        const float32 maxZ    = _homeCenter._z + _homeHalfExtent;
        const float32 offsetX = _random.nextRange( -_wanderRadius, _wanderRadius );
        const float32 offsetZ = _random.nextRange( -_wanderRadius, _wanderRadius );
        const float3  destination{ MathUtil::clamp( from._x + offsetX, minX, maxX ), from._y, MathUtil::clamp( from._z + offsetZ, minZ, maxZ ) };
        agent.setDestination( destination );
        ++_retargetCount;
        const bool bProbeDue = _probeInterval > 0 && ( _retargetCount % _probeInterval ) == 0 && _probe.isValid() == false;
        if ( bProbeDue == false )
            return;
        _probe = _pSceneNavigation->requestPath( agent.getAgentType(), from, destination );
        // 버리기 시험은 하나 걸러 — 셋째 프레임에 확인이 끝나야 다음을 낸다.
        const bool bCancelDue = ( ( _retargetCount / _probeInterval ) % 2u ) == 0 && _cancelPending.isValid() == false && _cancelled.isValid() == false;
        if ( bCancelDue )
            _cancelPending = _pSceneNavigation->requestPath( agent.getAgentType(), from, destination );
    }

    SW_AUTOMATION_WATCH( emptyNavWanderArrivals, "EmptyNav.WanderArrivals", "Destinations the wandering navmesh agents reached so far (all wanderers)",
                         &NavWanderInternal::sumWanderers<&NavWanderComponent::getArrivalCount> );
    SW_AUTOMATION_WATCH( emptyNavWanderProbeResults, "EmptyNav.WanderProbeResults", "Async path queries the wanderers received a result for (all wanderers)",
                         &NavWanderInternal::sumWanderers<&NavWanderComponent::getProbeResultCount> );
    SW_AUTOMATION_WATCH( emptyNavWanderCancelLeaks, "EmptyNav.WanderCancelLeaks", "Cancelled async path queries that still delivered a result (must stay 0)",
                         &NavWanderInternal::sumWanderers<&NavWanderComponent::getCancelLeakCount> );
} // namespace sw
