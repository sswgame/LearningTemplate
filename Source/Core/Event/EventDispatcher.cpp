#include "pch.h"

#include "Core/Event/EventDispatcher.h"

namespace sw
{
	IEvent::IEvent()
		: _next{ nullptr } {}

	IEvent::~IEvent()
	{
	}

	WindowResizeEvent::WindowResizeEvent() noexcept
		: _bIsResizing{ 0 }
		, _bIsMaximized{ 0 }
		, _bIsMinimized{ 0 }
		, _reservedFlags{ 0 } {}

	WindowActivateEvent::WindowActivateEvent() noexcept
		: _bIsActivate{ 1 }
		, _reservedFlags{ 0 } {}

	EventDispatcher::EventDispatcher()
		: _busSpinLock{}
		, _queueSpinLock{}
		, _mapChannelDelegates{}
		, _mapChannelDispatchTable{}
		, _mapChannelQueue{}
		, _arrFrameAllocators{ LinearAllocator{ 1024 * 64 }, LinearAllocator{ 1024 * 64 } }
		, _listOverflowAllocations{}
		, _activeAllocatorIndex{ 0 }
	{
	}

	EventDispatcher::~EventDispatcher()
	{
	}

	void EventDispatcher::processEvents()
	{
		int32									  currentAllocIdx{ 0 };
		vector<std::pair<hashed_string, IEvent*>> activeChannels;
		BLOCK( "Swap Event Queues" )
		{
			std::scoped_lock<SpinLock> lock{ _queueSpinLock };
			currentAllocIdx			= _activeAllocatorIndex.load( std::memory_order_relaxed );
			const int32 newAllocIdx = ( currentAllocIdx + 1 ) % 2;
			_activeAllocatorIndex.store( newAllocIdx, std::memory_order_relaxed );

			for ( auto& [channel, list] : _mapChannelQueue )
			{
				IEvent* pHead = list->_pHead.exchange( nullptr, std::memory_order_relaxed );
				if ( pHead != nullptr )
					activeChannels.emplace_back( channel, pHead );
			}
		}

		if ( activeChannels.empty() )
		{
			for ( void* pOverflowMem : _listOverflowAllocations[currentAllocIdx] )
			{
				if ( pOverflowMem != nullptr )
					Memory::freeMemory( pOverflowMem );
			}
			_listOverflowAllocations[currentAllocIdx].clear();
			_arrFrameAllocators[currentAllocIdx].reset();
			return;
		}

		BLOCK( "Dispatch Events" )
		{
			for ( auto& [channel, pHead] : activeChannels )
			{
				IEvent* pCurr = pHead;
				IEvent* pPrev{ nullptr };
				while ( pCurr != nullptr )
				{
					IEvent* pNext = pCurr->_next.load( std::memory_order_relaxed );
					pCurr->_next.store( pPrev, std::memory_order_relaxed );
					pPrev = pCurr;
					pCurr = pNext;
				}
				pHead = pPrev;

				for ( IEvent* pEvent = pHead; pEvent != nullptr; )
				{
					ChannelEventCallback callback;
					{
						std::scoped_lock<SpinLock>			  lock{ _busSpinLock };
						std::pair<hashed_string, EventTypeId> key( channel, pEvent->getEventType() );
						auto								  iter = _mapChannelDispatchTable.find( key );
						if ( iter != _mapChannelDispatchTable.end() )
							callback = iter->second;
					}

					if ( callback.isBound() )
						callback( *pEvent );

					IEvent* pNextEvent = pEvent->_next.load( std::memory_order_relaxed );
					pEvent->~IEvent();
					pEvent = pNextEvent;
				}
			}
		}

		BLOCK( "Cleanup Frame Allocator" )
		{
			for ( void* pOverflowMem : _listOverflowAllocations[currentAllocIdx] )
			{
				if ( pOverflowMem != nullptr )
					Memory::freeMemory( pOverflowMem );
			}
			_listOverflowAllocations[currentAllocIdx].clear();
			_arrFrameAllocators[currentAllocIdx].reset();
		}
	}

	void EventDispatcher::clear()
	{
		BLOCK( "Clear Bus Handlers" )
		{
			std::scoped_lock<SpinLock> lock{ _busSpinLock };
			_mapChannelDelegates.clear();
			_mapChannelDispatchTable.clear();
			_mapChannelDelegates.reserve( 16 );
			_mapChannelDispatchTable.reserve( 16 );
		}

		BLOCK( "Clear Event Queues" )
		{
			std::scoped_lock<SpinLock> lock{ _queueSpinLock };
			_mapChannelQueue.clear();
			for ( uint32 allocIndex = 0; allocIndex < 2; ++allocIndex )
			{
				for ( void* pOverflowMem : _listOverflowAllocations[allocIndex] )
				{
					if ( pOverflowMem != nullptr )
						Memory::freeMemory( pOverflowMem );
				}
				_listOverflowAllocations[allocIndex].clear();
			}
			_arrFrameAllocators[0].clear();
			_arrFrameAllocators[1].clear();
		}
	}
} // namespace sw
