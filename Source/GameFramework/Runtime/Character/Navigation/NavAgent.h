/**
 * @file NavAgent.h
 * @brief 길을 따라 걷는 행위자를 정의합니다.
 * @details 경로점 따라가기, 흐름장 따라가기, 도착 감속, 이웃과 떨어지기(분리), 막힌 셀을 따라 미끄러지기, 끼임 감지를 한다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class FlowField;
    class GridPathfinder;
    class NavGrid;

    /** @brief 행위자의 몸과 움직임 설정입니다. */
    struct NavAgentSettings
    {
        float32 _radius{ 0.4f };
        float32 _maxSpeed{ 4.0f };
        float32 _maxAcceleration{ 24.0f };
        float32 _arriveDistance{ 0.2f };   ///< 이 안이면 도착
        float32 _slowDistance{ 1.2f };     ///< 이 안에서부터 줄여 멈춘다
        float32 _waypointDistance{ 0.5f }; ///< 중간 경로점은 이 안이면 다음으로
        float32 _separationWeight{ 1.6f }; ///< 이웃과 떨어지려는 힘(0 이면 끔)
        float32 _stuckTime{ 1.5f };        ///< 이만큼 거의 못 움직이면 끼었다
    };
} // namespace sw

namespace sw
{
    /** @brief 행위자의 상태입니다. */
    enum class NavAgentState : uint8
    {
        Idle = 0, ///< 갈 곳 없음
        FollowingPath,
        FollowingFlow,
        Arrived,
        Stuck ///< 오래 못 움직였다. 게임이 다시 경로를 구하거나 포기한다
    };

    /**
     * @struct Steering
     * @brief 조향 기본기(Reynolds)입니다. 모두 원하는 속도(XZ)를 반환하며, 합치는 것은 부르는 쪽의 일입니다.
     */
    struct SW_GF_API Steering
    {
        /** @brief 목표 쪽으로 최고 속도로 가는 속도를 반환합니다. */
        static float3 seek( const float3& position, const float3& target, float32 maxSpeed );
        /** @brief 목표 쪽으로 가되 @p slowDistance 안에서는 거리에 비례해 속도를 줄입니다. */
        static float3 arrive( const float3& position, const float3& target, float32 maxSpeed, float32 slowDistance );
        /**
         * @brief 이웃과 겹치지 않게 미는 속도를 반환합니다. 가까울수록 세며, 두 반지름 합 안에서만 밀어냅니다.
         * @param listNeighbor 이웃 위치이다. 자기 자신은 빼고 넘겨야 하며, 같은 위치는 무시한다.
         */
        static float3 separate( const float3& position, float32 radius, const vector<float3>& listNeighbor, float32 neighborRadius );
        /** @brief 속도를 원하는 속도로 바꿉니다. 가속 한계 안에서만 바꿉니다. */
        static float3 accelerate( const float3& velocity, const float3& desiredVelocity, float32 maxAcceleration, float32 deltaTime );
    };
} // namespace sw

namespace sw
{
    /**
     * @class NavAgent
     * @brief 한 행위자입니다. 경로(A*)나 흐름장을 받아 매 틱 조향합니다.
     * @details 언리얼의 `UPathFollowingComponent` 와 `UCrowdFollowingComponent` 의 격자 버전이다.
     *          이웃 목록은 부르는 쪽이 넘긴다. 보통 `SpatialHashGrid2D::queryCircle` 로 구한다.
     *          움직인 위치가 막힌 셀이면 X 와 Z 를 따로 시도해 벽을 따라 미끄러지고, 그것도 막히면 제자리에 선다.
     *          흐름장은 빌려 쓰므로 행위자보다 오래 살아야 한다.
     */
    class SW_GF_API NavAgent
    {
    public:
        NavAgent();

        void setSettings( const NavAgentSettings& settings ) { _settings = settings; }
        /** @brief 위치를 옮기고 멈춥니다. 소환과 순간이동에 씁니다. */
        void setPosition( const float3& position );
        /** @brief A* 로 @p target 까지의 경로를 구해 따라갑니다. 닿지 못하면 가장 가까운 곳까지 가며, 갈 수 없으면 false 입니다. */
        [[nodiscard]] bool moveTo( const NavGrid& grid, GridPathfinder& pathfinder, const float3& target );
        /** @brief 이미 구한 월드 경로를 따라갑니다. */
        void followPath( const vector<float3>& listPoint );
        /** @brief 흐름장을 따라 @p target(흐름장의 목적지 근처)까지 갑니다. */
        void followFlowField( const FlowField* pFlowField, const float3& target );
        void stop();

        /** @brief 한 틱을 돌립니다. 원하는 속도, 분리, 가속, 막힌 셀 미끄러지기, 도착과 끼임 판정 순으로 처리합니다. */
        void update( const NavGrid& grid, const vector<float3>& listNeighbor, float32 deltaTime );

        const float3&           getPosition() const { return _position; }
        const float3&           getVelocity() const { return _velocity; }
        NavAgentState           getState() const { return _state; }
        bool                    isMoving() const { return _state == NavAgentState::FollowingPath || _state == NavAgentState::FollowingFlow; }
        const float3&           getTarget() const { return _target; }
        const vector<float3>&   getPath() const { return _listPathPoint; }
        size_t                  getWaypointIndex() const { return _waypointIndex; }
        const NavAgentSettings& getSettings() const { return _settings; }

    private:
        float3 computeDesiredVelocity( const NavGrid& grid );
        void   moveWithCollision( const NavGrid& grid, const float3& delta );

        NavAgentSettings _settings;
        vector<float3>   _listPathPoint;
        const FlowField* _pFlowField;
        float3           _position;
        float3           _velocity;
        float3           _target;
        size_t           _waypointIndex;
        float32          _stuckTimer;
        NavAgentState    _state;
    };
} // namespace sw
