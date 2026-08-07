/**
 * @file EventDispatcher.cpp
 * @brief Auto-generated documentation header
 */
#include "pch.h"

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"

#include "Core/Utility/Event/EventDispatcher.h"

namespace sw
{
	void EventDispatcher::processEvents()
	{
		{
			std::lock_guard<std::mutex> lock( _queueMutex );
			if ( _channelQueue.empty() )
				return;
			_swapBatchMap.clear();
			_swapBatchMap.swap( _channelQueue );
		}

		for ( const auto& [channel, events] : _swapBatchMap )
		{
			for ( const std::unique_ptr<IEvent>& event : events )
			{
				if ( event == nullptr )
					continue;

				ChannelEventCallback callback;
				{
					std::lock_guard<std::mutex>			lock( _busMutex );
					std::pair<hashed_string, EventType> key( channel, event->getEventType() );
					auto								iter = _channelDispatchTable.find( key );
					if ( iter != _channelDispatchTable.end() )
					{
						callback = iter->second;
					}
				}

				if ( callback.isBound() )
				{
					callback( *event );
				}
			}
		}
		_swapBatchMap.clear();
	}

	void EventDispatcher::clear()
	{
		{
			std::lock_guard<std::mutex> lock( _busMutex );
			_channelDelegates.clear();
			_channelDispatchTable.clear();
			_channelDelegates.reserve( 16 );
			_channelDispatchTable.reserve( 16 );
		}
		{
			std::lock_guard<std::mutex> lock( _queueMutex );
			_channelQueue.clear();
			_swapBatchMap.clear();
			_channelQueue.reserve( 16 );
			_swapBatchMap.reserve( 16 );
		}
	}
}
