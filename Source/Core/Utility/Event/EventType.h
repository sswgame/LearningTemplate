#pragma once
/**
 * @file EventType.h
 * @brief Auto-generated documentation header
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
		IEvent()							   = default;
		IEvent( const IEvent& )				   = default;
		IEvent& operator=( const IEvent& )	   = default;
		IEvent( IEvent&& )					   = default;
		IEvent& operator=( IEvent&& )		   = default;
		virtual ~IEvent()					   = default;
		/**
		 * @brief getEventType 처리를 수행합니다.
		 */
		virtual EventType getEventType() const = 0;
	};

	struct WindowResizeEvent final : IEvent
	{
		int32 _width  = 0;
		int32 _height = 0;
		uint8 _bIsResizing	: 1;
		uint8 _bIsMaximized : 1;
		uint8 _bIsMinimized : 1;

		WindowResizeEvent()
			: _bIsResizing{ 0 }
			, _bIsMaximized{ 0 }
			, _bIsMinimized{ 0 }
		{
		}

		SW_REGISTER_EVENT( WindowResize );
	};

	struct WindowCloseEvent final : IEvent
	{
		SW_REGISTER_EVENT( WindowClose );
	};

	struct WindowActivateEvent final : IEvent
	{
		bool _bIsActivate = true;
		SW_REGISTER_EVENT( WindowActivate );
	};
}
