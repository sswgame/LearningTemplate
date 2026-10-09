/**
 * @file ScheduleLocator.h
 * @brief 활동 자리 제공자 — `UseObject` 칸이 "벤치 하나" · "작업대 하나" 를 시간 창으로 예약하는 창구와, 데이터의 `<Spot>` 으로 나눠 주는 기본 구현입니다.
 * @details 스마트 오브젝트(점유할 수 있는 자리 — 플레이어와 AI 가 같이 쓰는 벤치 · 작업대 · 엄폐)가 생기면 그 시스템이 이 인터페이스를 구현해
 *          `ScheduleSystem::setActivityLocator` 로 끼웁니다. 예약은 같은 NPC · 종류 · 날 · 겹치는 시간 창이면 같은 예약을 돌려줘야 합니다(멱등) —
 *          계획을 다시 세워도 자리가 바뀌지 않고, 저장에서 되살린 예약을 그대로 다시 집습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/AI/Schedule/SchedulePathing.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ScheduleReservationSaveState;

    class ScheduleCatalog;

    /** @brief 예약 요청입니다. 시간은 그날의 분입니다. */
    struct ScheduleReserveRequest
    {
        hashed_string _npc{};
        hashed_string _objectKind{};
        hashed_string _area{}; ///< 이 지역의 자리를 먼저(비면 아무 지역)
        float3        _near{}; ///< 같은 조건이면 여기서 가까운 자리
        int32         _day{ 0 };
        int32         _startMinute{ 0 };
        int32         _endMinute{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IScheduleActivityLocator
     * @brief 활동 자리를 예약 · 반납합니다. 같은 요청 순서에 늘 같은 답이어야 합니다(결정적 — 화면 밖 시뮬레이션 · 저장 · 재생).
     */
    class SW_GF_API IScheduleActivityLocator
    {
    public:
        IScheduleActivityLocator()                                                 = default;
        virtual ~IScheduleActivityLocator()                                        = default;
        IScheduleActivityLocator( const IScheduleActivityLocator& )                = default;
        IScheduleActivityLocator& operator=( const IScheduleActivityLocator& )     = default;
        IScheduleActivityLocator( IScheduleActivityLocator&& ) noexcept            = default;
        IScheduleActivityLocator& operator=( IScheduleActivityLocator&& ) noexcept = default;

        /** @brief 자리를 예약합니다. 빈 자리가 없으면 false 입니다(일정은 칸의 `place` · 집으로 물러선다). 예약 id 는 0 이 아닙니다. */
        [[nodiscard]] virtual bool reserve( const ScheduleReserveRequest& request, ScheduleLocation& outLocation, uint32& outReservationId ) = 0;
        virtual void               release( uint32 reservationId )                                                                           = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScheduleSpotLocator
     * @brief 카탈로그의 `<Spot>` 을 나눠 주는 기본 제공자입니다. 같은 지역 → 가까운 자리 → 카탈로그 순으로 고르고, 겹치는 시간 창의 예약이
     *        `capacity` 만큼 차면 그 자리는 찬 것입니다.
     */
    class SW_GF_API ScheduleSpotLocator : public IScheduleActivityLocator
    {
    public:
        ScheduleSpotLocator();

        void initialize( const ScheduleCatalog* pCatalog );
        void clear();

        [[nodiscard]] bool reserve( const ScheduleReserveRequest& request, ScheduleLocation& outLocation, uint32& outReservationId ) override;
        void               release( uint32 reservationId ) override;

        void   fillState( vector<ScheduleReservationSaveState>& outListReservation ) const;
        void   restoreState( const vector<ScheduleReservationSaveState>& listReservation );
        size_t getReservationCount() const { return _listReservation.size(); }

    private:
        struct Reservation
        {
            hashed_string _npc{};
            hashed_string _kind{};
            uint32        _id{ 0 };
            int32         _spotIndex{ -1 };
            int32         _day{ 0 };
            int32         _startMinute{ 0 };
            int32         _endMinute{ 0 };
        };

        int32 countOverlapping( int32 spotIndex, int32 day, int32 startMinute, int32 endMinute ) const;

        const ScheduleCatalog* _pCatalog;
        vector<Reservation>    _listReservation;
        uint32                 _nextId;
    };
} // namespace sw
