#pragma once
/**
 * @file RHIReleaseQueue.h
 * @brief GPU 지연 해제 큐
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
		/** @brief frameLatency 프레임 뒤 해제를 수행하는 큐를 만듭니다. */
		explicit RHIReleaseQueue( uint32 frameLatency = 3 );
		~RHIReleaseQueue();

		/** @brief GPU 리소스 해제 콜백을 지연 큐에 넣습니다. */
		void enqueueRelease( const RHIResourceReleaseDelegate& releaseDelegate );

		/** @brief 프레임을 진행하고 만기된 해제 콜백을 실행합니다. */
		void tickFrame();

		/** @brief 대기 중인 해제를 모두 즉시 실행합니다. */
		void flushAll();

		/** @brief 아직 실행되지 않은 해제 항목 수를 반환합니다. */
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
} // namespace sw
