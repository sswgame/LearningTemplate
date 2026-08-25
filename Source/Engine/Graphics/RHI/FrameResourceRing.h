/**
 * @file FrameResourceRing.h
 * @brief DX12용 N-버퍼 프레임 리소스 / 업로드 오프셋 헬퍼
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/**
	 * @class FrameResourceRing
	 * @brief 프레임별 펜스 값과 선형 업로드 bump allocator (N=3)
	 * @details GPU 버퍼는 소유하지 않습니다 — 업로드 힙과 짝을 이루고 매 프레임 오프셋을 진행합니다.
	 */
	class SW_API FrameResourceRing
	{
	public:
		static constexpr uint32 kFrameCount = 3;

		// ------------------------------------------------------------------------------
		// 1) 수명 — 업로드 용량, reset
		// ------------------------------------------------------------------------------
		/** @brief 용량 0인 링. */
		FrameResourceRing();
		/** @brief 업로드 용량을 지정한 링. */
		explicit FrameResourceRing( uint64 uploadCapacityBytes );

		/** @brief 업로드 용량을 다시 잡고 슬롯을 초기화합니다. */
		void reset( uint64 uploadCapacityBytes );

		// ------------------------------------------------------------------------------
		// 2) 프레임 진행 — 펜스가 슬롯을 덮으면 다음 슬롯, 업로드 오프셋 리셋
		// ------------------------------------------------------------------------------
		/**
		 * @brief @p completedFenceValue가 슬롯 펜스를 덮으면 다음 링 슬롯으로 진행합니다.
		 * @return 슬롯이 준비되어 링이 진행되고 업로드 오프셋이 리셋되면 true.
		 */
		bool beginFrame( uint64 completedFenceValue );

		/** @brief 강제 진행 (호출자가 GPU 완료를 보장). 업로드 오프셋을 리셋합니다. */
		void advanceFrame();

		// ------------------------------------------------------------------------------
		// 3) 펜스 · 업로드 오프셋
		// ------------------------------------------------------------------------------
		/** @brief 현재 링 슬롯 인덱스를 반환합니다. */
		uint32 currentIndex() const { return _frameIndex; }
		/** @brief 슬롯의 펜스 값을 반환합니다. */
		uint64 getFenceValue( uint32 index ) const;
		/** @brief 슬롯의 펜스 값을 설정합니다. */
		void setFenceValue( uint32 index, uint64 fenceValue );
		/** @brief 현재 슬롯 펜스 값에 대한 참조를 반환합니다. */
		uint64& currentFenceValue();

		/** @brief 업로드 영역 용량을 반환합니다. */
		uint64 getUploadCapacity() const { return _uploadCapacity; }
		/** @brief 현재 슬롯의 업로드 bump 오프셋을 반환합니다. */
		uint64 getUploadOffset() const { return _arrSlots[_frameIndex]._uploadOffset; }

		/**
		 * @brief 현재 슬롯 업로드 영역에서 bump 할당합니다.
		 * @return 용량을 넘으면 false.
		 */
		bool tryAllocate( uint64 sizeBytes, uint64 alignment, uint64& outOffset );

		/** @brief 현재 슬롯의 업로드 오프셋을 0으로 되돌립니다. */
		void resetUploadOffset();

	private:
		struct Slot
		{
			uint64 _fenceValue{ 0 };
			uint64 _uploadOffset{ 0 };
		};

		Slot   _arrSlots[kFrameCount]{};
		uint32 _frameIndex{ 0 };
		uint64 _uploadCapacity{ 0 };
	};
} // namespace sw
