#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#include <shared_mutex>

namespace sw
{
	/**
	 * @class BindlessTable
	 * @brief 티어 3 Bindless 텍스처 및 버퍼 인덱싱 테이블 매니저
	 * @details 디스크립터 셋 재바인딩 없이 셰이더에서 슬롯 인덱스로 텍스처/버퍼에 즉시 접근할 수 있도록 슬롯을 할당/관리합니다.
	 */
	class SW_API BindlessTable
	{
	public:
		static constexpr RHIDescriptorIndex kMaxBindlessSlots = 65536;

		BindlessTable();
		~BindlessTable() = default;

		void initialize( uint32 maxSlots = kMaxBindlessSlots );
		void shutdown();

		RHIDescriptorIndex allocateTextureSlot( RHITextureHandle texture );
		void			   freeTextureSlot( RHIDescriptorIndex slotIndex );

		RHIDescriptorIndex allocateBufferSlot( RHIBufferHandle buffer );
		void			   freeBufferSlot( RHIDescriptorIndex slotIndex );

		RHITextureHandle getTexture( RHIDescriptorIndex slotIndex ) const;
		RHIBufferHandle	 getBuffer( RHIDescriptorIndex slotIndex ) const;

		size_t getActiveTextureCount() const;
		size_t getActiveBufferCount() const;

	private:
		mutable std::shared_mutex  _mutex;
		vector<RHITextureHandle>   _listTexture;
		vector<RHIBufferHandle>	   _listBuffer;
		vector<RHIDescriptorIndex> _listFreeTextureSlot;
		vector<RHIDescriptorIndex> _listFreeBufferSlot;
		uint32					   _maxSlots;
	};
} // namespace sw
