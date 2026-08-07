/**
 * @file RHIReleaseQueue.cpp
 * @brief RHI 지연 해제 큐 구현
 */
#include "pch.h"
#include "RHIReleaseQueue.h"

namespace sw
{
	RHIReleaseQueue::RHIReleaseQueue( uint32 frameLatency )
		: _frameLatency{ frameLatency }
		, _currentFrame{ 0 }
	{
	}

	RHIReleaseQueue::~RHIReleaseQueue()
	{
		flushAll();
	}

	void RHIReleaseQueue::enqueueRelease( const RHIResourceReleaseDelegate& releaseDelegate )
	{
		if ( releaseDelegate.isBound() == false )
			return;

		std::lock_guard<std::mutex> lock{ _mutex };
		DeferredEntry				entry;
		entry._releaseDelegate = releaseDelegate;
		entry._targetFrame	   = _currentFrame + static_cast<uint64>( _frameLatency );
		_entries.push_back( entry );
	}

	void RHIReleaseQueue::tickFrame()
	{
		std::vector<RHIResourceReleaseDelegate> readyToDestroy;
		{
			std::lock_guard<std::mutex> lock{ _mutex };
			_currentFrame++;

			std::vector<DeferredEntry>::iterator iter = _entries.begin();
			while ( iter != _entries.end() )
			{
				if ( iter->_targetFrame <= _currentFrame )
				{
					readyToDestroy.push_back( iter->_releaseDelegate );
					iter = _entries.erase( iter );
				}
				else
				{
					++iter;
				}
			}
		}

		for ( const RHIResourceReleaseDelegate& callback : readyToDestroy )
		{
			if ( callback.isBound() )
			{
				callback();
			}
		}
	}

	void RHIReleaseQueue::flushAll()
	{
		std::vector<DeferredEntry> entriesToFlush;
		{
			std::lock_guard<std::mutex> lock{ _mutex };
			entriesToFlush.swap( _entries );
		}

		for ( const DeferredEntry& entry : entriesToFlush )
		{
			if ( entry._releaseDelegate.isBound() )
			{
				entry._releaseDelegate();
			}
		}
	}

	uint32 RHIReleaseQueue::getPendingReleaseCount() const
	{
		std::lock_guard<std::mutex> lock{ _mutex };
		return static_cast<uint32>( _entries.size() );
	}
} // namespace sw
