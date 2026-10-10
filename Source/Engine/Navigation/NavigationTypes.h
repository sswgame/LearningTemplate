/**
 * @file NavigationTypes.h
 * @brief 내비메시의 공통 낱말 — 폴리곤 참조 · 영역 · 질의 거름 · 경로 · 레이캐스트 결과 · 군중 에이전트 값입니다.
 * @details 백엔드(`Navigation/Recast/`)의 타입은 이 파일 밖으로 나오지 않습니다. 폴리곤 참조는 64 비트 불투명 값이고(백엔드가 더 좁게 써도 된다),
 *          영역 번호는 설정 표(`NavMeshSettings::_listArea`)의 순서입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 내비메시 폴리곤 하나의 불투명 참조입니다. 0 은 없음입니다. 타일을 다시 구우면 옛 참조는 무효가 됩니다. */
    using NavPolyRef = uint64;

    /** @brief 군중 안의 에이전트 번호입니다. 음수는 없음입니다. */
    using NavCrowdAgentID = int32;

    struct NavigationConstant
    {
        /** @brief 없는 폴리곤입니다. */
        static constexpr NavPolyRef kInvalidPolyRef = 0;
        /** @brief 없는 군중 에이전트입니다. */
        static constexpr NavCrowdAgentID kInvalidAgentID = -1;
        /** @brief 영역 표의 최대 줄 수입니다(영역 하나가 폴리곤 표시 비트 하나 — 16 비트). */
        static constexpr uint32 kMaxAreaCount = 16;
        /** @brief 걸을 수 없는 영역입니다(장애물 · 구멍 — 베이크에서 그 자리를 뺀다). 표의 번호가 아닙니다. */
        static constexpr uint8 kNotWalkableArea = 0xFF;
        /** @brief 경로가 낼 수 있는 꼭짓점의 상한입니다(넘으면 부분 경로). */
        static constexpr uint32 kMaxPathPointCount = 256;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NavQueryFilter
     * @brief 질의 거름 — 영역마다 비용 배율과 지날 수 있는 영역 비트입니다(언리얼 `FNavigationQueryFilter` · 유니티 `NavMeshQueryFilter`).
     * @details 기본값은 설정 표의 영역 비용입니다(`NavMeshSettings::makeDefaultFilter`). 비트 i 는 영역 i 입니다.
     */
    struct NavQueryFilter
    {
        float32 _arrAreaCost[NavigationConstant::kMaxAreaCount]{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                                                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        uint16  _includeAreaMask{ 0xFFFF }; ///< 이 비트의 영역만 지난다
        uint16  _excludeAreaMask{ 0 };      ///< 이 비트의 영역은 지나지 않는다(포함보다 앞선다)

        /** @brief 영역 하나를 막거나 풉니다. */
        void setAreaAllowed( uint32 areaIndex, bool bAllowed )
        {
            if ( areaIndex >= NavigationConstant::kMaxAreaCount )
                return;
            const uint16 bit = static_cast<uint16>( 1u << areaIndex );
            _excludeAreaMask = bAllowed ? static_cast<uint16>( _excludeAreaMask & ~bit ) : static_cast<uint16>( _excludeAreaMask | bit );
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 경로 찾기 결과입니다. */
    enum class NavPathStatus : uint8
    {
        Failed = 0, ///< 시작이나 끝이 내비메시 위에 없다
        Complete,   ///< 끝까지 간다
        Partial,    ///< 끝에 닿지 못해 가장 가까운 곳까지 간다(막힌 곳 · 꼭짓점 상한)
    };
} // namespace sw

namespace sw
{
    /** @brief 내비메시 위의 점 하나 — 자리와 그것이 든 폴리곤입니다. */
    struct NavLocation
    {
        float3     _position{};
        NavPolyRef _poly{ NavigationConstant::kInvalidPolyRef };

        bool isValid() const { return _poly != NavigationConstant::kInvalidPolyRef; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NavPath
     * @brief 다듬은 경로(줄 당기기 — 모퉁이만 남긴 직선 경로)입니다. 첫 점은 시작, 마지막 점은 끝(부분 경로면 닿은 곳)입니다.
     */
    struct NavPath
    {
        vector<float3> _listPoint;
        NavPathStatus  _status{ NavPathStatus::Failed };
        uint32         _polyCount{ 0 }; ///< 지나는 폴리곤 수(진단)

        /** @brief 점을 이은 길이입니다. */
        float32 computeLength() const
        {
            float32 length = 0.0f;
            for ( size_t pointIndex = 1; pointIndex < _listPoint.size(); ++pointIndex )
            {
                length += ( _listPoint[pointIndex] - _listPoint[pointIndex - 1] ).getLength();
            }
            return length;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 내비메시 위 레이캐스트(시선) 결과입니다 — 걸을 면을 따라 곧게 가다 경계에 막힌 곳입니다. */
    struct NavRaycastHit
    {
        float3  _position{}; ///< 막힌 곳(안 막혔으면 끝점)
        float3  _normal{};   ///< 막은 경계의 바깥쪽 법선(XZ)
        float32 _fraction{ 1.0f };
        bool    _bHit{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 군중 에이전트의 회피 품질입니다(낮을수록 싸다 — 언리얼 `ECrowdAvoidanceQuality`). */
    ENUM()
    enum class NavAvoidanceQuality : uint8
    {
        Low = 0,
        Medium,
        Good,
        High,
    };
} // namespace sw

namespace sw
{
    /** @brief 군중 에이전트 하나의 몸 · 움직임 값입니다. */
    struct NavCrowdAgentParams
    {
        float32             _radius{ 0.4f };
        float32             _height{ 2.0f };
        float32             _maxSpeed{ 3.5f };
        float32             _maxAcceleration{ 8.0f };
        float32             _separationWeight{ 2.0f };
        float32             _collisionQueryRange{ 4.8f }; ///< 이웃 · 경계를 모으는 거리(보통 반지름 × 12)
        float32             _pathOptimizationRange{ 12.0f };
        NavAvoidanceQuality _avoidanceQuality{ NavAvoidanceQuality::Medium };
        bool                _bAvoidance{ true };
        bool                _bSeparation{ true };
        bool                _bAnticipateTurns{ true };
    };
} // namespace sw

namespace sw
{
    /** @brief 군중 에이전트의 이동 요청 상태입니다. */
    enum class NavCrowdMoveState : uint8
    {
        Idle = 0, ///< 목적지 없음
        Pending,  ///< 경로를 구하는 중
        Moving,   ///< 경로를 따라간다
        Arrived,  ///< 목적지 안에 들었다
        Failed,   ///< 목적지를 내비메시에서 찾지 못했다
    };
} // namespace sw

namespace sw
{
    /** @brief 군중 에이전트의 지금 상태입니다(군중 `update` 뒤). */
    struct NavCrowdAgentState
    {
        float3            _position{};
        float3            _velocity{};        ///< 이번 갱신의 실제 속도
        float3            _desiredVelocity{}; ///< 회피 전 경로가 원한 속도
        float3            _target{};
        float3            _nextCorner{}; ///< 다음 모퉁이(경로가 있으면)
        NavCrowdMoveState _moveState{ NavCrowdMoveState::Idle };
        bool              _bOnNavMesh{ false };
    };
} // namespace sw
