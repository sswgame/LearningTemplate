/**
 * @file EventType.h
 * @brief 엔진 이벤트 타입 ID (게임플레이 이벤트는 GameFramework 해시 ID)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/String/StringUtil.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) EventTypeId — 엔진 예약 1..255, 게임플레이는 문자열 해시(256+)
	// ------------------------------------------------------------------------------
	/** @brief EventDispatcher 키에 쓰는 이벤트 타입 식별자 */
	using EventTypeId = uint32;

	/** @brief 엔진 예약 이벤트 ID (1..255). 0은 무효. */
	inline constexpr EventTypeId kEventInvalid		  = 0;
	inline constexpr EventTypeId kEventWindowActivate = 1;
	inline constexpr EventTypeId kEventWindowClose	  = 2;
	inline constexpr EventTypeId kEventWindowResize	  = 3;
	inline constexpr EventTypeId kEventKey			  = 4;
	inline constexpr EventTypeId kEventMouse		  = 5;

	/** @brief 컴파일타임 FNV-1a 32-bit (게임플레이 이벤트 ID용) — StringUtil과 동일 상수/알고리즘 */
	constexpr EventTypeId eventTypeIdFromString( const utf8* pStr ) noexcept
	{
		size_t len{ 0 };
		for ( const utf8* pCurrent = pStr; pCurrent != nullptr && *pCurrent != '\0'; ++pCurrent )
		{
			++len;
		}
		uint32 hash = StringUtil::computeHash32( pStr, len, false );
		// 엔진 예약 ID 구간(0..255)과 겹치지 않게 합니다.
		if ( hash < 256u )
			hash += 256u;
		return hash;
	}

/** @brief 고정 EventTypeId로 IEvent를 등록합니다. */
#define SW_REGISTER_EVENT_ID( eventTypeId )                     \
	EventTypeId getEventType() const override { return kType; } \
                                                                \
private:                                                        \
	static constexpr EventTypeId kType = ( eventTypeId );       \
	friend class EventDispatcher;                               \
	friend class EventBus

/** @brief 엔진 이벤트 등록 (kEvent##Name 상수 사용) */
#define SW_REGISTER_ENGINE_EVENT( Name ) SW_REGISTER_EVENT_ID( sw::kEvent##Name )

/** @brief 타입명 문자열 해시로 게임플레이 이벤트 등록 */
#define SW_DECLARE_GAMEPLAY_EVENT( TypeName ) SW_REGISTER_EVENT_ID( sw::eventTypeIdFromString( #TypeName ) )

	// ------------------------------------------------------------------------------
	// 2) IEvent — 큐 노드. 파생형은 SW_REGISTER_* 로 kType 을 심습니다
	// ------------------------------------------------------------------------------
	/** @brief 디스패처 큐에 올라가는 이벤트 베이스입니다. */
	struct SW_API IEvent
	{
		/** @brief 다음 포인터만 비운 채 준비합니다. */
		IEvent();
		/** @brief 큐 링크는 복사하지 않습니다. */
		IEvent( const IEvent& ) {}
		/** @brief 큐 링크는 복사하지 않고 자신을 유지합니다. */
		IEvent& operator=( const IEvent& ) { return *this; }
		/** @brief 큐 링크는 가져오지 않습니다. */
		IEvent( IEvent&& ) {}
		/** @brief 큐 링크는 가져오지 않고 자신을 유지합니다. */
		IEvent& operator=( IEvent&& ) { return *this; }
		/** @brief 가상 소멸로 파생 이벤트를 안전하게 지웁니다. */
		virtual ~IEvent();
		/** @brief 등록된 이벤트 타입 ID입니다. */
		virtual EventTypeId getEventType() const = 0;

		mutable atomic<IEvent*> _next;
	};

	// ------------------------------------------------------------------------------
	// 3) 윈도우 이벤트 — Resize / Close / Activate
	// ------------------------------------------------------------------------------
	/** @brief 클라이언트 영역 크기와 리사이즈/최대/최소 플래그입니다. */
	struct SW_API WindowResizeEvent final : IEvent
	{
		int32				   _width;
		int32				   _height;
		uint8				   _bIsResizing	  : 1;
		uint8				   _bIsMaximized  : 1;
		uint8				   _bIsMinimized  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 5;

		/** @brief 크기 0, 플래그 꺼짐으로 둡니다. */
		WindowResizeEvent() noexcept;

		SW_REGISTER_ENGINE_EVENT( WindowResize );
	};

	/** @brief 윈도우 닫기 요청입니다. 페이로드 없음. */
	struct SW_API WindowCloseEvent final : IEvent
	{
		SW_REGISTER_ENGINE_EVENT( WindowClose );
	};

	/** @brief 활성화/비활성화 전환입니다. */
	struct SW_API WindowActivateEvent final : IEvent
	{
		uint8				   _bIsActivate	  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;

		/** @brief 비활성으로 둡니다. */
		WindowActivateEvent() noexcept;

		SW_REGISTER_ENGINE_EVENT( WindowActivate );
	};
} // namespace sw
