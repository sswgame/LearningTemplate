#pragma once
/**
 * @file RHIReleaseQueue.h
 * @brief Auto-generated documentation header
 */

#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

#include "Core/Utility/Delegate/Delegate.h"

namespace sw
{

	using RHIResourceReleaseDelegate = Delegate<void()>;

	class SW_API RHIReleaseQueue
	{
	public:

		/**
		 * @brief RHIReleaseQueue 처리를 수행합니다.
		 */
		explicit RHIReleaseQueue( uint32 frameLatency = 3 );
		~RHIReleaseQueue() = default;

		/**
		 * @brief enqueueRelease 처리를 수행합니다.
		 */
		void enqueueRelease( const RHIResourceReleaseDelegate& releaseDelegate );

		/**
		 * @brief tickFrame 처리를 수행합니다.
		 */
		void tickFrame();

		/**
		 * @brief flushAll 처리를 수행합니다.
		 */
		void flushAll();

		/**
		 * @brief getPendingReleaseCount 처리를 수행합니다.
		 */
		uint32 getPendingReleaseCount() const;

	private:
		struct DeferredEntry
		{
			RHIResourceReleaseDelegate _releaseDelegate;
			uint64					   _targetFrame = 0;
		};

		uint32					   _frameLatency = 3;
		uint64					   _currentFrame = 0;
		std::vector<DeferredEntry> _entries;
		mutable std::mutex		   _mutex;
	};
}
