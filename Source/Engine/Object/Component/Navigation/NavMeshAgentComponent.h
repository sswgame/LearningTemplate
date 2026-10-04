/**
 * @file NavMeshAgentComponent.h
 * @brief 내비메시 위를 걷는 에이전트 — 목적지 → 경로 → 군중 조향(이웃 회피)으로 오브젝트를 옮기거나, 같은 오브젝트의 캐릭터 컨트롤러에 원하는 속도를 넘깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Navigation/INavMover.h"
#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class NavMeshAgentComponent;
    class SceneNavigation;

    /** @brief 에이전트를 공통 이동 창구(`INavMover`)로 보이는 묶음입니다. 컴포넌트가 하나 들고 `getMover` 로 내줍니다. */
    class SW_API NavMeshAgentMover final : public INavMover
    {
    public:
        explicit NavMeshAgentMover( NavMeshAgentComponent& agent )
            : _agent{ agent }
        {
        }

        [[nodiscard]] bool moveTo( const float3& destination ) override;
        void               stopMoving() override;
        NavMoveStatus      getMoveStatus() const override;
        float3             getMoveVelocity() const override;
        float3             getMovePosition() const override;

    private:
        NavMeshAgentComponent& _agent;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NavMeshAgentComponent
     * @brief 유니티 `NavMeshAgent` · 언리얼 `UCrowdFollowingComponent` 의 자리입니다. 씬의 내비게이션(`SceneNavigation`)에 등록되고, 그것이 틱마다
     *        에이전트 종류의 군중(DetourCrowd)을 한 번에 진행합니다 — 이 컴포넌트는 스스로 틱하지 않습니다.
     * @details 요청(`setDestination` · `stop` · `warp`)은 이 오브젝트의 틱(병렬 워커)에서 불러도 됩니다 — 적어 두었다가 다음 내비게이션 갱신
     *          (그 프레임의 PrePhysics · DuringPhysics 틱 뒤, 애니메이션 · 물리 앞)에 군중에 넣습니다. 상태 · 속도는 그 갱신이 써 둔 것을 읽습니다.
     *
     *          **움직이는 쪽.** 같은 오브젝트에 `CharacterControllerComponent` 가 있으면 군중이 원한 속도를 컨트롤러에 넘기고(`setMoveVelocity`) 군중은
     *          컨트롤러가 실제로 간 자리를 다음 갱신에서 받습니다 — 벽 · 턱 · 경사는 물리가 맡습니다. 없으면 이 컴포넌트가 오브젝트 자리를 직접 옮기고
     *          (`_bUpdatePosition`) 진행 방향으로 몸을 돌립니다(`_bUpdateRotation`). 바깥이 오브젝트를 멀리 옮기면 순간이동으로 봅니다.
     */
    REFLECT( Category = "Navigation", DisplayName = "NavMesh Agent", Tooltip = "Walks the navmesh to a destination with crowd avoidance; drives the transform or a character controller" )
    class SW_API NavMeshAgentComponent : public Component
    {
    public:
        REFLECT_BODY();

        NavMeshAgentComponent();
        ~NavMeshAgentComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onEndPlay() override;

        /** @brief 목적지를 겁니다(다음 내비게이션 갱신에 든다). 이 오브젝트의 틱에서 불러도 됩니다. */
        void setDestination( const float3& destination );
        /** @brief 목적지를 지우고 멈춥니다. */
        void stop();
        /** @brief 순간이동합니다(경로를 버린다). */
        void warp( const float3& position );

        /** @brief 지금 상태입니다(마지막 내비게이션 갱신). */
        NavMoveStatus getMoveStatus() const { return _moveStatus; }
        /** @brief 목적지를 걸어 두었으면(요청 대기 포함) true 입니다. */
        bool          hasDestination() const { return _bHasDestination == SW_TRUE; }
        const float3& getDestination() const { return _destination; }
        /** @brief 군중이 낸 실제 속도입니다. */
        const float3& getVelocity() const { return _velocity; }
        /** @brief 회피 전 경로가 원한 속도입니다. */
        const float3& getDesiredVelocity() const { return _desiredVelocity; }
        /** @brief 군중 안의 자리(발)입니다. 등록 전이면 오브젝트 자리입니다. */
        const float3& getAgentPosition() const { return _agentPosition; }
        /** @brief 다음 모퉁이입니다. */
        const float3& getNextCorner() const { return _nextCorner; }
        /** @brief 군중에 들어가 내비메시 위에 있으면 true 입니다. */
        bool isOnNavMesh() const { return _bOnNavMesh == SW_TRUE; }
        /** @brief 공통 이동 창구입니다. */
        INavMover& getMover() { return _mover; }

        const hashed_string& getAgentType() const { return _agentType; }
        void                 setAgentType( const hashed_string& agentType ) { _agentType = agentType; }
        float32              getMaxSpeed() const { return _maxSpeed; }
        void                 setMaxSpeed( float32 maxSpeed );
        float32              getStoppingDistance() const { return _stoppingDistance; }
        void                 setStoppingDistance( float32 distance ) { _stoppingDistance = distance; }
        float32              getRadius() const { return _radius; }
        void                 setRadius( float32 radius );
        bool                 isUpdatingPosition() const { return _bUpdatePosition; }
        void                 setUpdatePosition( bool bUpdate ) { _bUpdatePosition = bUpdate; }
        /** @brief 군중에 넘기는 몸 · 움직임 값입니다. */
        NavCrowdAgentParams makeCrowdParams() const;

    private:
        friend class SceneNavigation;

        PROPERTY( Category = "Agent", DisplayName = "Agent Type", Tooltip = "Navmesh agent kind from the navigation settings (empty = default)" )
        hashed_string _agentType;
        PROPERTY( Category = "Agent", Min = 0.0, Tooltip = "Top speed", Meta = "Units=m/s" )
        float32 _maxSpeed;
        PROPERTY( Category = "Agent", Min = 0.0, Tooltip = "How fast the speed changes", Meta = "Units=m/s^2" )
        float32 _maxAcceleration;
        PROPERTY( Category = "Agent", Min = 0.0, Tooltip = "Arrives and stops inside this distance of the destination", Meta = "Units=m" )
        float32 _stoppingDistance;
        PROPERTY( Category = "Agent", Min = 0.0, Tooltip = "Body radius used for avoidance (0 = the agent type's)", Meta = "Units=m" )
        float32 _radius;
        PROPERTY( Category = "Avoidance", Min = 0.0, Tooltip = "How hard agents push apart" )
        float32 _separationWeight;
        PROPERTY( Category = "Avoidance", Tooltip = "Sampling effort of the velocity obstacle avoidance" )
        NavAvoidanceQuality _avoidanceQuality;
        PROPERTY( Category = "Movement", Min = 0.0, Tooltip = "How fast the body turns to face its velocity", Meta = "Units=rad/s" )
        float32 _turnRate;
        PROPERTY( Category = "Movement", Tooltip = "Moves the object (off when a character controller or game code moves it)" )
        bool _bUpdatePosition;
        PROPERTY( Category = "Movement", Tooltip = "Turns the object to face its velocity" )
        bool _bUpdateRotation;

        NavMeshAgentMover _mover;
        SceneNavigation*  _pSceneNavigation; ///< 등록한 씬의 내비게이션(등록 동안)
        float3            _destination;
        float3            _pendingWarp;
        float3            _velocity;
        float3            _desiredVelocity;
        float3            _agentPosition;
        float3            _nextCorner;
        float3            _writtenPosition; ///< 마지막으로 오브젝트에 쓴 자리(바깥의 순간이동 판정)
        NavCrowdAgentId   _crowdAgentId;
        uint32            _navIndex;   ///< `SceneNavigation` 의 목록 자리
        uint32            _crowdIndex; ///< 든 군중(에이전트 종류) 자리
        NavMoveStatus     _moveStatus;
        uint8             _bHasDestination   : 1;
        uint8             _bDestinationDirty : 1; ///< 다음 갱신에 군중에 넣을 목적지 · 멈춤
        uint8             _bWarpPending      : 1;
        uint8             _bOnNavMesh        : 1;
        uint8             _bHasWritten       : 1;
        uint8             _bParamsDirty      : 1;
        uint8             _reserved          : 2;
    };
} // namespace sw
