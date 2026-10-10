/**
 * @file SchedulePathing.h
 * @brief 일정이 묻는 "어디" — 자리(`ScheduleLocation`), 이동 시간 추정 · 경로(`ISchedulePathing`)와 그 구현 셋(직선 · 내비 격자 · 방 그래프)입니다.
 * @details 일정은 차원을 모릅니다. 자리는 `float3` 와 지역 id 이고, 평면(XZ = 3D 지면, XY = 2D 화면)은 경로 구현이 정합니다 —
 *          2D 농장 · 생활 게임과 3D 마을이 같은 일정 코드를 씁니다. 출발 시각(일찍 나서기)은 이 추정으로 정하고, 화면 안 NPC 의 걷는 자리는
 *          같은 출발 · 도착 시각 사이를 경로 위에서 비율로 나눕니다(화면 밖 · 안이 같은 시각에 도착한다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AreaGraph;
    class GameFlags;
    class GridPathfinder;
    class NavGrid;

    /** @brief 자리 하나 — 월드 좌표와 지역(`AreaGraph` 방 id, 모르면 빈 이름)입니다. */
    struct ScheduleLocation
    {
        float3        _position{};
        hashed_string _area{};
    };

    /** @brief 자리가 놓인 평면입니다. */
    enum class SchedulePlane : uint8
    {
        XZ = 0, ///< 3D 지면(y 가 높이) — `NavGrid` 의 평면
        XY      ///< 2D 화면(z = 0)
    };
} // namespace sw

namespace sw
{
    /**
     * @class ISchedulePathing
     * @brief 이동 시간 추정과 경로입니다. 일정은 출발 시각을 이 추정으로 정하므로 같은 입력에 늘 같은 값을 돌려줘야 합니다(결정적).
     */
    class SW_GF_API ISchedulePathing
    {
    public:
        ISchedulePathing()                                         = default;
        virtual ~ISchedulePathing()                                = default;
        ISchedulePathing( const ISchedulePathing& )                = default;
        ISchedulePathing& operator=( const ISchedulePathing& )     = default;
        ISchedulePathing( ISchedulePathing&& ) noexcept            = default;
        ISchedulePathing& operator=( ISchedulePathing&& ) noexcept = default;

        /** @brief @p from → @p to 를 분당 @p unitsPerMinute 로 걸을 때 걸리는 게임 분입니다. */
        virtual float32 estimateTravelMinutes( const ScheduleLocation& from, const ScheduleLocation& to, float32 unitsPerMinute ) const = 0;
        /** @brief 지나는 점 목록(@p from · @p to 포함)입니다. */
        virtual void makeRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>& outListPoint ) const = 0;
        /** @brief 이동의 @p fraction(0..1) 지점이 든 지역입니다. 기본은 반을 넘으면 도착 지역입니다. */
        virtual hashed_string computeAreaAt( const ScheduleLocation& from, const ScheduleLocation& to, float32 fraction ) const;
        /** @brief @p center 에서 평면 위로 (@p offsetU, @p offsetV) 만큼 옮긴 자리입니다(돌아다니기 점). */
        virtual float3 offsetInPlane( const float3& center, float32 offsetU, float32 offsetV ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 경로 계산 도우미입니다. */
    struct SW_GF_API SchedulePathingUtil
    {
        /** @brief 점 목록의 길이입니다. */
        static float32 computeLength( const vector<float3>& listPoint );
        /** @brief 점 목록을 따라 길이의 @p fraction(0..1) 만큼 간 자리입니다. 비면 원점입니다. */
        static float3 computePointAlong( const vector<float3>& listPoint, float32 fraction );
        /** @brief 평면 위로 옮깁니다. */
        static float3 offsetInPlane( SchedulePlane plane, const float3& center, float32 offsetU, float32 offsetV );
    };
} // namespace sw

namespace sw
{
    /**
     * @class StraightSchedulePathing
     * @brief 곧은 선 — 거리 / 속도입니다. 갈 길을 모를 때의 기본이고 어느 차원에서도 맞습니다.
     */
    class SW_GF_API StraightSchedulePathing : public ISchedulePathing
    {
    public:
        explicit StraightSchedulePathing( SchedulePlane plane = SchedulePlane::XZ );

        float32 estimateTravelMinutes( const ScheduleLocation& from, const ScheduleLocation& to, float32 unitsPerMinute ) const override;
        void    makeRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>& outListPoint ) const override;
        float3  offsetInPlane( const float3& center, float32 offsetU, float32 offsetV ) const override;

    private:
        SchedulePlane _plane;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NavGridSchedulePathing
     * @brief `NavGrid` + `GridPathfinder`(A*) 경로입니다. 시간은 다듬은 칸 경로의 길이 / 속도입니다.
     * @details 평면이 XY 이면 자리의 (x, y) 를 격자의 (x, z) 로 읽고 돌려줄 때 되바꿉니다 — 2D 타일 게임이 같은 격자 · 길 찾기를 씁니다.
     *          A* 는 배열을 고쳐 쓰므로 이 객체는 스레드 하나에서만 씁니다.
     */
    class SW_GF_API NavGridSchedulePathing : public ISchedulePathing
    {
    public:
        NavGridSchedulePathing( const NavGrid* pGrid, GridPathfinder* pPathfinder, SchedulePlane plane );

        float32 estimateTravelMinutes( const ScheduleLocation& from, const ScheduleLocation& to, float32 unitsPerMinute ) const override;
        void    makeRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>& outListPoint ) const override;
        float3  offsetInPlane( const float3& center, float32 offsetU, float32 offsetV ) const override;

    private:
        float3 toGrid( const float3& position ) const;
        float3 fromGrid( const float3& position ) const;
        /** @brief 칸 경로를 찾아 비용 가중 길이를 돌려줍니다. 길이 없으면 곧은 거리입니다. */
        float32 findRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>* pOutListPoint ) const;

        const NavGrid*  _pGrid;
        GridPathfinder* _pPathfinder;
        SchedulePlane   _plane;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AreaGraphSchedulePathing
     * @brief 방 그래프 경로 — 지역 단위(화면 밖 · 로드되지 않은 NPC)입니다. 다른 방이면 `AreaGraph::findPath` 로 지금 열린 문만 지나
     *        방 가운데를 잇고(`AreaDef` 의 x · y · 너비 · 높이를 평면에 놓는다), 같은 방이면 곧은 선입니다.
     * @details 길이 없으면(잠긴 문) 곧은 선으로 추정합니다 — 일정은 멈추지 않는다.
     */
    class SW_GF_API AreaGraphSchedulePathing : public ISchedulePathing
    {
    public:
        AreaGraphSchedulePathing( const AreaGraph* pGraph, const GameFlags* pFlags, SchedulePlane plane );

        float32       estimateTravelMinutes( const ScheduleLocation& from, const ScheduleLocation& to, float32 unitsPerMinute ) const override;
        void          makeRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<float3>& outListPoint ) const override;
        hashed_string computeAreaAt( const ScheduleLocation& from, const ScheduleLocation& to, float32 fraction ) const override;
        float3        offsetInPlane( const float3& center, float32 offsetU, float32 offsetV ) const override;

    private:
        /** @brief 지나는 방(출발 · 도착 포함)과 점을 채웁니다. 같은 방 · 길 없음이면 방 둘 · 점 둘입니다. */
        void   makeAreaRoute( const ScheduleLocation& from, const ScheduleLocation& to, vector<hashed_string>& outListArea, vector<float3>& outListPoint ) const;
        float3 computeAreaCenter( const hashed_string& areaID, const float3& fallback ) const;

        const AreaGraph* _pGraph;
        const GameFlags* _pFlags;
        SchedulePlane    _plane;
    };
} // namespace sw
