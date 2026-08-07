#pragma once
/**
 * @file EventType.h
 * @brief 엔진 이벤트 타입 정의
 */

#include "Core/Common/Types.h"

namespace sw
{
	enum class EventType : uint8
	{
		WindowActivate,
		WindowClose,
		WindowResize,
		Key,
		Mouse,
		Count
	};

/** @brief SW_REGISTER_EVENT 매크로 정의입니다. */
#define SW_REGISTER_EVENT( eventType )                        \
	EventType getEventType() const override { return kType; } \
                                                              \
private:                                                      \
	static constexpr EventType kType = EventType::eventType;  \
	friend class EventDispatcher;                             \
	friend class EventBus

	struct IEvent
	{
		IEvent()						   = default;
		IEvent( const IEvent& )			   = default;
		IEvent& operator=( const IEvent& ) = default;
		IEvent( IEvent&& )				   = default;
		IEvent& operator=( IEvent&& )	   = default;
		virtual ~IEvent()				   = default;
		/**
		 * @brief EventType을(를) 반환합니다
		 */
		virtual EventType getEventType() const = 0;
	};

	struct WindowResizeEvent final : IEvent
	{
		WindowResizeEvent() noexcept;

		int32				   _width  = 0;
		int32				   _height = 0;
		uint8				   _bIsResizing	  : 1;
		uint8				   _bIsMaximized  : 1;
		uint8				   _bIsMinimized  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 5;

		SW_REGISTER_EVENT( WindowResize );
	};

	struct WindowCloseEvent final : IEvent
	{
		SW_REGISTER_EVENT( WindowClose );
	};

	struct WindowActivateEvent final : IEvent
	{
		WindowActivateEvent() noexcept;

		uint8				   _bIsActivate	  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;
		SW_REGISTER_EVENT( WindowActivate );
	};
} // namespace sw
