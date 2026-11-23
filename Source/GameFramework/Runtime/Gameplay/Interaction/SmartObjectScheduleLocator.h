/**
 * @file SmartObjectScheduleLocator.h
 * @brief NPC 하루 일정의 `UseObject` 활동이 씬의 스마트 오브젝트(`SmartObjectComponent`) 슬롯을 시간 창으로 예약하는 어댑터입니다.
 * @details 후보 슬롯은 씬을 훑지 않고 컴포넌트 레지스트리의 스마트 오브젝트 목록에서 모은다.
 *          일정은 계획을 세울 때 슬롯을 예약하고(`reserve`) 블록이 시작될 때 그 슬롯으로 간다. 스마트 오브젝트는 지금 **점유**(claim)만 하므로, 어댑터가 시간 창 예약 테이블을
 *          보관하고 계획에는 예약한 슬롯을 돌려주며, 블록이 시작될 때 게임이 `claimReserved` 로 실제 점유한다(끝나면 `releaseClaim`).
 *          `objectKind` 는 스마트 오브젝트 정의의 id(`<SmartObject id="Bench">`)와 같은 이름이고, 슬롯은 슬롯 하나가 한 번에 한 NPC 입니다.
 *          같은 요청 순서에 늘 같은 답이다. 후보를 (오브젝트 id, 슬롯 번호) 순으로 정렬해 같은 조건이면 먼저 오는 쪽을 고른다(결정성).
 *          기반 폴더의 층 규칙상 상호작용(Gameplay)이 일정(Actor)을 읽는 방향이라 이 어댑터는 일정 쪽이 아니라 여기에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleLocator.h"

namespace sw
{
    struct ScheduleReservationSaveState;

    class GameObject;
    class GameObjectManager;

    /**
     * @class SmartObjectScheduleLocator
     * @brief 씬의 스마트 오브젝트 슬롯을 일정의 슬롯으로 나눠 주는 제공자입니다. 게임 스레드에서만 부릅니다.
     * @details 일정의 자리(카탈로그 · 요청 · 돌려주는 위치)는 절대라, 로컬인 슬롯 자리를 월드 원점으로 옮겨 견주고 돌려줍니다(`WorldOrigin`).
     */
    class SW_GF_API SmartObjectScheduleLocator final : public IScheduleActivityLocator
    {
    public:
        SmartObjectScheduleLocator();

        /** @brief 슬롯을 찾을 씬의 오브젝트 매니저입니다(소유하지 않음). nullptr 이면 늘 슬롯이 없습니다. */
        void initialize( GameObjectManager* pManager );
        /** @brief 예약을 모두 지웁니다(점유는 건드리지 않습니다). */
        void clear();
        /**
         * @brief 요청에 지역이 없을 때 돌려주는 위치의 지역입니다. 스마트 오브젝트는 지역을 모르므로, 길찾기가 지역 단위(`AreaGraphSchedulePathing`)라면 게임이 정합니다.
         */
        void setDefaultArea( const hashed_string& area ) { _defaultArea = area; }

        [[nodiscard]] bool reserve( const ScheduleReserveRequest& request, ScheduleLocation& outLocation, uint32& outReservationID ) override;
        void               release( uint32 reservationID ) override;

        /**
         * @brief 예약한 슬롯을 실제로 점유합니다(블록이 시작될 때). 다른 쪽(플레이어)이 그 슬롯을 차지했거나 오브젝트가 사라졌으면 false 이고, 게임은 물러서는 활동을 고릅니다.
         */
        [[nodiscard]] bool claimReserved( uint32 reservationID, const GameObject& npc );
        /** @brief 점유를 비웁니다(블록이 끝날 때). 예약은 그대로이며 `release` 가 지웁니다. 그 NPC 가 점유한 슬롯이 없으면 false 입니다. */
        bool releaseClaim( uint32 reservationID, const GameObject& npc );

        void   fillState( vector<ScheduleReservationSaveState>& outListReservation ) const override;
        void   restoreState( const vector<ScheduleReservationSaveState>& listReservation ) override;
        size_t getReservationCount() const { return _listReservation.size(); }

    private:
        struct Reservation
        {
            hashed_string _npc{};
            hashed_string _kind{};
            uint32        _id{ 0 };
            uint64        _objectID{ 0 };
            int32         _slot{ 0 };
            int32         _day{ 0 };
            int32         _startMinute{ 0 };
            int32         _endMinute{ 0 };
        };

        /** @brief 후보 슬롯 하나입니다. */
        struct Candidate
        {
            float3 _position{};
            uint64 _objectID{ 0 };
            int32  _slot{ 0 };
        };

        /** @brief 예약의 슬롯 슬롯(월드 위치)를 구합니다. 오브젝트가 사라졌으면 false 입니다. */
        bool computeReservationPosition( const Reservation& reservation, float3& outPosition ) const;
        /** @brief 그 슬롯에 겹치는 시간 창을 든 다른 NPC 의 예약이 있는지입니다. */
        bool isTakenByOther( const hashed_string& npc, uint64 objectID, int32 slot, int32 day, int32 startMinute, int32 endMinute ) const;
        /** @brief 정의 id 가 @p kind 인 스마트 오브젝트의 슬롯을 (오브젝트 id, 슬롯) 순으로 모읍니다. */
        void collectCandidates( const hashed_string& kind, vector<Candidate>& outListCandidate ) const;

    private:
        GameObjectManager*  _pManager;
        hashed_string       _defaultArea;
        vector<Reservation> _listReservation;
        uint32              _nextID;
    };
} // namespace sw
