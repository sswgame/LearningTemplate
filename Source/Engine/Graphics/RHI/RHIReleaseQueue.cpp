#include "pch.h"

#include "Engine/Graphics/RHI/RHIReleaseQueue.h"

namespace sw
{
	RHIReleaseQueue::RHIReleaseQueue( uint32 frameLatency )
		: _frameLatency{ frameLatency }
		, _currentFrame{ 0 }
		, _listFrameEntries{}
		, _listGpuEntries{}
		, _listReadyToDestroyBuffer{}
		, _spinLock{} {}

	RHIReleaseQueue::~RHIReleaseQueue()
	{
		flushAll();
	}

	void RHIReleaseQueue::enqueueRelease( const RHIResourceReleaseDelegate& releaseDelegate )
	{
		if ( releaseDelegate.isBound() == false )
			return;

		std::scoped_lock<SpinLock> lock{ _spinLock };
		FrameDeferredEntry		   entry{};
		entry._releaseDelegate = releaseDelegate;
		entry._targetFrame	   = _currentFrame + static_cast<uint64>( _frameLatency );
		_listFrameEntries.push_back( entry );
	}

	void RHIReleaseQueue::enqueueGpuRelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue )
	{
		if ( releaseDelegate.isBound() == false )
			return;

		std::scoped_lock<SpinLock> lock{ _spinLock };
		GpuDeferredEntry		   entry{};
		entry._releaseDelegate = releaseDelegate;
		entry._targetFence	   = fenceValue;
		_listGpuEntries.push_back( entry );
	}

	void RHIReleaseQueue::tickFrame()
	{
		_listReadyToDestroyBuffer.clear();
		{
			std::scoped_lock<SpinLock> lock{ _spinLock };
			_currentFrame++;

			const uint64 currentFrame  = _currentFrame;
			auto		 partitionIter = std::stable_partition( _listFrameEntries.begin(), _listFrameEntries.end(), [currentFrame]( const FrameDeferredEntry& entry )
			{
				return entry._targetFrame > currentFrame;
			} );

			for ( auto iter = partitionIter; iter != _listFrameEntries.end(); ++iter )
			{
				_listReadyToDestroyBuffer.push_back( iter->_releaseDelegate );
			}
			_listFrameEntries.erase( partitionIter, _listFrameEntries.end() );
		}

		for ( const RHIResourceReleaseDelegate& callback : _listReadyToDestroyBuffer )
		{
			if ( callback.isBound() )
				callback();
		}
	}

	void RHIReleaseQueue::tickCompleted( uint64 completedFence )
	{
		_listReadyToDestroyBuffer.clear();
		{
			std::scoped_lock<SpinLock> lock{ _spinLock };

			auto partitionIter = std::stable_partition( _listGpuEntries.begin(), _listGpuEntries.end(), [completedFence]( const GpuDeferredEntry& entry )
			{
				return entry._targetFence > completedFence;
			} );

			for ( auto iter = partitionIter; iter != _listGpuEntries.end(); ++iter )
			{
				_listReadyToDestroyBuffer.push_back( iter->_releaseDelegate );
			}
			_listGpuEntries.erase( partitionIter, _listGpuEntries.end() );
		}

		for ( const RHIResourceReleaseDelegate& callback : _listReadyToDestroyBuffer )
		{
			if ( callback.isBound() )
				callback();
		}
	}

	void RHIReleaseQueue::flushAll()
	{
		vector<FrameDeferredEntry> listFrameFlush;
		vector<GpuDeferredEntry>   listGpuFlush;
		{
			std::scoped_lock<SpinLock> lock{ _spinLock };
			listFrameFlush.swap( _listFrameEntries );
			listGpuFlush.swap( _listGpuEntries );
		}

		for ( const FrameDeferredEntry& entry : listFrameFlush )
		{
			if ( entry._releaseDelegate.isBound() )
				entry._releaseDelegate();
		}
		for ( const GpuDeferredEntry& entry : listGpuFlush )
		{
			if ( entry._releaseDelegate.isBound() )
				entry._releaseDelegate();
		}
	}

	uint32 RHIReleaseQueue::getPendingReleaseCount() const
	{
		std::scoped_lock<SpinLock> lock{ _spinLock };
		return static_cast<uint32>( _listFrameEntries.size() + _listGpuEntries.size() );
	}
} // namespace sw
