#pragma once
/**
 * @file FrameResourceRing.h
 * @brief DX12-oriented N-buffered frame resource / upload offset helper.
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/**
	 * @class FrameResourceRing
	 * @brief Tracks per-frame fence values and a linear upload bump allocator (N=3).
	 * @details Does not own GPU buffers — pair with an upload heap and advance offsets each frame.
	 */
	class SW_API FrameResourceRing
	{
	public:
		static constexpr uint32 kFrameCount = 3;

		FrameResourceRing();
		explicit FrameResourceRing( uint64 uploadCapacityBytes );

		void reset( uint64 uploadCapacityBytes );

		/**
		 * @brief Advance to the next ring slot once @p completedFenceValue covers the slot's fence.
		 * @return true if the slot was ready and the ring advanced / reset upload offset.
		 */
		bool beginFrame( uint64 completedFenceValue );

		/** @brief Force-advance (caller guarantees GPU finished the slot). Resets upload offset. */
		void advanceFrame();

		uint32 currentIndex() const { return _frameIndex; }
		uint64 getFenceValue( uint32 index ) const;
		void   setFenceValue( uint32 index, uint64 fenceValue );
		uint64& currentFenceValue();

		uint64 getUploadCapacity() const { return _uploadCapacity; }
		uint64 getUploadOffset() const { return _slots[_frameIndex]._uploadOffset; }

		/**
		 * @brief Bump-allocate from the current slot's upload region.
		 * @return false if the allocation would exceed capacity.
		 */
		bool tryAllocate( uint64 sizeBytes, uint64 alignment, uint64& outOffset );

		void resetUploadOffset();

	private:
		struct Slot
		{
			uint64 _fenceValue	 = 0;
			uint64 _uploadOffset = 0;
		};

		Slot   _slots[kFrameCount]{};
		uint32 _frameIndex	   = 0;
		uint64 _uploadCapacity = 0;
	};
} // namespace sw
