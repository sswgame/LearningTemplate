/**
 * @file RHIReleaseQueue.h
 * @brief GPU 지연 해제 큐
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

namespace sw
{

	using RHIResourceReleaseDelegate = Delegate<void()>;

	/**
	 * @class RHIReleaseQueue
	 * @brief frameLatency 프레임 뒤에 GPU 리소스 해제 콜백을 실행합니다
	 */
	class SW_API RHIReleaseQueue
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 수명 — frameLatency 뒤 해제, 소멸 시 flushAll
		// ------------------------------------------------------------------------------
		/** @brief frameLatency 프레임 뒤 해제를 수행하는 큐를 만듭니다. */
		explicit RHIReleaseQueue( uint32 frameLatency = 3 );
		/** @brief 대기 중인 해제를 모두 실행합니다. */
		~RHIReleaseQueue();

		// ------------------------------------------------------------------------------
		// 2) enqueue · tick · flush
		// ------------------------------------------------------------------------------
		/** @brief GPU 리소스 해제 콜백을 지연 큐에 넣습니다. CPU 프레임 지연입니다. */
		void enqueueRelease( const RHIResourceReleaseDelegate& releaseDelegate );

		/** @brief GPU 펜스 값이 완료된 뒤에 해제합니다. */
		void enqueueGpuRelease( const RHIResourceReleaseDelegate& releaseDelegate, uint64 fenceValue );

		/** @brief 프레임을 진행하고 만기된 해제 콜백을 실행합니다. */
		void tickFrame();

		/** @brief 완료된 GPU 펜스 이하의 해제 콜백을 실행합니다. */
		void tickCompleted( uint64 completedFence );

		/** @brief 대기 중인 해제를 모두 즉시 실행합니다. */
		void flushAll();

		/** @brief 아직 실행되지 않은 해제 항목 수를 반환합니다. */
		uint32 getPendingReleaseCount() const;

	private:
		struct FrameDeferredEntry
		{
			RHIResourceReleaseDelegate _releaseDelegate;
			uint64					   _targetFrame{ 0 };
		};

		struct GpuDeferredEntry
		{
			RHIResourceReleaseDelegate _releaseDelegate;
			uint64					   _targetFence{ 0 };
		};

		vector<FrameDeferredEntry> _listFrameEntry;
		vector<GpuDeferredEntry>   _listGpuEntry;
		mutable SpinLock		   _spinLock;
		uint64					   _currentFrame;
		uint32					   _frameLatency;
	};
} // namespace sw
