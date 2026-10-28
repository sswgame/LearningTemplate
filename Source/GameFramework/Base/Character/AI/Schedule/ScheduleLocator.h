/**
 * @file ScheduleLocator.h
 * @brief 활동 슬롯 제공자입니다. `UseObject` 블록이 "벤치 하나", "작업대 하나" 를 시간 창으로 예약하는 인터페이스와, 데이터의 `<Spot>` 으로 나눠 주는 기본 구현을 둡니다.
 * @details 스마트 오브젝트는 점유할 수 있는 슬롯입니다. 플레이어와 AI 가 같이 쓰는 벤치, 작업대, 엄폐물이 그 예입니다.
 *          스마트 오브젝트 시스템이 이 인터페이스를 구현해 `ScheduleSystem::setActivityLocator` 로 등록합니다.
 *          예약은 멱등이어야 합니다. 같은 NPC, 종류, 날, 겹치는 시간 창이면 같은 예약을 돌려줍니다.
 *          그래서 계획을 다시 세워도 슬롯이 바뀌지 않고, 저장에서 복원한 예약을 그대로 다시 고릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Character/AI/Schedule/SchedulePathing.h"
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
        hashed_string _area{}; ///< 이 지역의 슬롯을 먼저 고른다. 비면 아무 지역
        float3        _near{}; ///< 같은 조건이면 여기서 가까운 슬롯을 고른다
        int32         _day{ 0 };
        int32         _startMinute{ 0 };
        int32         _endMinute{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class IScheduleActivityLocator
     * @brief 활동 슬롯을 예약하고 반납합니다. 같은 요청 순서에 늘 같은 답이어야 합니다. 화면 밖 시뮬레이션, 저장, 재생이 이 결정성에 기댑니다.
     */
    class IScheduleActivityLocator
    {
    public:
        IScheduleActivityLocator()                                                 = default;
        virtual ~IScheduleActivityLocator()                                        = default;
        IScheduleActivityLocator( const IScheduleActivityLocator& )                = default;
        IScheduleActivityLocator& operator=( const IScheduleActivityLocator& )     = default;
        IScheduleActivityLocator( IScheduleActivityLocator&& ) noexcept            = default;
        IScheduleActivityLocator& operator=( IScheduleActivityLocator&& ) noexcept = default;

        /** @brief 슬롯을 예약합니다. 빈 슬롯이 없으면 false 이고, 일정은 블록의 `place` 나 집으로 물러섭니다. 예약 id 는 0 이 아닙니다. */
        [[nodiscard]] virtual bool reserve( const ScheduleReserveRequest& request, ScheduleLocation& outLocation, uint32& outReservationID ) = 0;
        virtual void               release( uint32 reservationID )                                                                           = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScheduleSpotLocator
     * @brief 카탈로그의 `<Spot>` 을 나눠 주는 기본 제공자입니다.
     * @details 같은 지역, 가까운 슬롯, 카탈로그 순서로 고릅니다. 겹치는 시간 창의 예약이 `capacity` 만큼 차면 그 슬롯은 찬 것입니다.
     */
    class SW_GF_API ScheduleSpotLocator : public IScheduleActivityLocator
    {
    public:
        ScheduleSpotLocator();

        void initialize( const ScheduleCatalog* pCatalog );
        void clear();

        [[nodiscard]] bool reserve( const ScheduleReserveRequest& request, ScheduleLocation& outLocation, uint32& outReservationID ) override;
        void               release( uint32 reservationID ) override;

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
        uint32                 _nextID;
    };
} // namespace sw
