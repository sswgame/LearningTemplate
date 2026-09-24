/**
 * @file EventType.h
 * @brief 엔진 이벤트 타입 ID 입니다(게임플레이 이벤트는 GameFramework 의 해시 ID 를 씁니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    // `SW_REGISTER_EVENT_ID` 가 `friend class sw::EventDispatcher` 를 적는다. 한정된 이름의 friend 선언은 첫 선언이 될 수
    // 없으므로 여기서 미리 선언해 둔다.
    class EventDispatcher;

    // ------------------------------------------------------------------------------
    // 1) EventTypeId — 엔진 예약은 1..255, 게임플레이는 문자열 해시(256 이상)
    // ------------------------------------------------------------------------------
    /** @brief EventDispatcher 의 키로 쓰는 이벤트 타입 식별자입니다. */
    using EventTypeId = uint32;

    /**
     * @brief 엔진 예약 이벤트 ID(1..255)입니다. 0 은 무효입니다.
     * @details 번호가 겹치지 않도록 여기 한곳에 적습니다. 이벤트 **타입**은 그 개념이 사는 층에 둡니다 — 창 이벤트는
     *          `Engine/Window/WindowEvents.h` 입니다.
     */
    inline constexpr EventTypeId kEventInvalid        = 0;
    inline constexpr EventTypeId kEventWindowActivate = 1;
    inline constexpr EventTypeId kEventWindowClose    = 2;
    inline constexpr EventTypeId kEventWindowResize   = 3;
    inline constexpr EventTypeId kEventKey            = 4;
    inline constexpr EventTypeId kEventMouse          = 5;

    /** @brief 컴파일 타임 FNV-1a 32비트 해시로 게임플레이 이벤트 ID 를 만듭니다. StringUtil 과 같은 상수 · 알고리즘입니다. */
    constexpr EventTypeId eventTypeIdFromString( const utf8* pStr ) noexcept
    {
        size_t len{ 0 };
        for ( const utf8* pCurrent = pStr; pCurrent != nullptr && *pCurrent != '\0'; ++pCurrent )
        {
            ++len;
        }
        uint32 hash = StringUtil::computeHash32( pStr, len, false );
        // 엔진 예약 ID 구간(0..255)과 겹치지 않게 한다.
        if ( hash < 256u )
            hash += 256u;
        return hash;
    }

/**
 * @brief 고정된 EventTypeId 로 IEvent 를 등록합니다.
 * @warning **이 매크로 뒤는 `private:` 입니다.** 멤버를 더하려면 매크로 **위**에 적으십시오. 아래에 적으면 조용히
 *          private 이 됩니다(`struct` 라서 기본이 public 인 것과 어긋납니다).
 * @details 이름을 모두 `sw::` 로 한정합니다. 예전에는 ID 식만 한정하고 반환 타입과 friend 는 한정하지 않아서
 *          **`namespace sw` 밖에서는 쓸 수 없었습니다.** `EventTypeId` 를 찾지 못했고, `friend class EventDispatcher` 는
 *          전역에 새 클래스를 선언해 버려 진짜 디스패처가 `kType` 에 닿지 못했습니다. ID 식이 이미 `sw::` 로 적혀 있던
 *          것을 보면 밖에서도 쓰려던 의도였습니다.
 */
#define SW_REGISTER_EVENT_ID( eventTypeId )                         \
    sw::EventTypeId getEventType() const override { return kType; } \
                                                                    \
private:                                                            \
    static constexpr sw::EventTypeId kType = ( eventTypeId );       \
    friend class sw::EventDispatcher

/** @brief 엔진 이벤트를 등록합니다(kEvent##Name 상수를 씁니다). */
#define SW_REGISTER_ENGINE_EVENT( Name ) SW_REGISTER_EVENT_ID( sw::kEvent##Name )

/** @brief 타입 이름 문자열의 해시로 게임플레이 이벤트를 등록합니다. */
#define SW_DECLARE_GAMEPLAY_EVENT( TypeName ) SW_REGISTER_EVENT_ID( sw::eventTypeIdFromString( #TypeName ) )

    // ------------------------------------------------------------------------------
    // 2) IEvent — 큐 노드. 파생 타입은 SW_REGISTER_* 로 kType 을 심는다
    // ------------------------------------------------------------------------------
    /** @brief 디스패처 큐에 올라가는 이벤트 베이스입니다. */
    struct SW_API IEvent
    {
        /** @brief 다음 포인터만 비운 상태로 만듭니다. */
        IEvent();
        /** @brief 큐 링크는 복사하지 않습니다. */
        IEvent( const IEvent& ) {}
        /** @brief 큐 링크는 복사하지 않고 자기 링크를 유지합니다. */
        IEvent& operator=( const IEvent& ) { return *this; }
        /**
         * @brief 큐 링크는 가져오지 않습니다.
         * @note `noexcept` 가 **계약의 일부**입니다. 이동이 noexcept 가 아니면 `vector` 같은 컨테이너는 재할당 때 강한 예외
         *       보장을 지키려고 **이동 대신 복사**를 씁니다. 이 두 함수는 본문이 비어 있어 예외를 던질 수 없는데도 그 표시가
         *       없었습니다.
         */
        IEvent( IEvent&& ) noexcept {}
        /** @brief 큐 링크는 가져오지 않고 자기 링크를 유지합니다. */
        IEvent& operator=( IEvent&& ) noexcept { return *this; }
        /** @brief 가상 소멸자라 파생 이벤트를 안전하게 지울 수 있습니다. */
        virtual ~IEvent();
        /** @brief 등록된 이벤트 타입 ID 입니다. */
        virtual EventTypeId getEventType() const = 0;

        mutable atomic<IEvent*> _next;
    };
} // namespace sw
