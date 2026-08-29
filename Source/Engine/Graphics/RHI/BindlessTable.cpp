#include "pch.h"

#include "Engine/Graphics/RHI/BindlessTable.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Log/Logger.h"

namespace sw
{
	BindlessTable::BindlessTable()
		: _mutex{}
		, _listTexture{}
		, _listBuffer{}
		, _listFreeTextureSlot{}
		, _listFreeBufferSlot{}
		, _maxSlots{ kMaxBindlessSlots }
	{
	}

	void BindlessTable::initialize( uint32 maxSlots )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		_maxSlots = maxSlots;
		_listTexture.clear();
		_listBuffer.clear();
		_listFreeTextureSlot.clear();
		_listFreeBufferSlot.clear();
	}

	void BindlessTable::shutdown()
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		_listTexture.clear();
		_listBuffer.clear();
		_listFreeTextureSlot.clear();
		_listFreeBufferSlot.clear();
	}

	RHIDescriptorIndex BindlessTable::allocateTextureSlot( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return kInvalidDescriptorIndex;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( _listFreeTextureSlot.empty() == false )
		{
			const RHIDescriptorIndex slot = _listFreeTextureSlot.back();
			_listFreeTextureSlot.pop_back();
			_listTexture[slot] = texture;
			return slot;
		}

		if ( _listTexture.size() >= _maxSlots )
			return kInvalidDescriptorIndex;

		const auto slot = static_cast<RHIDescriptorIndex>( _listTexture.size() );
		_listTexture.push_back( texture );
		return slot;
	}

	void BindlessTable::freeTextureSlot( RHIDescriptorIndex slotIndex )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( slotIndex < _listTexture.size() && _listTexture[slotIndex] != 0 )
		{
			_listTexture[slotIndex] = 0;
			_listFreeTextureSlot.push_back( slotIndex );
		}
	}

	RHIDescriptorIndex BindlessTable::allocateBufferSlot( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( _listFreeBufferSlot.empty() == false )
		{
			const RHIDescriptorIndex slot = _listFreeBufferSlot.back();
			_listFreeBufferSlot.pop_back();
			_listBuffer[slot] = buffer;
			return slot;
		}

		if ( _listBuffer.size() >= _maxSlots )
			return kInvalidDescriptorIndex;

		const auto slot = static_cast<RHIDescriptorIndex>( _listBuffer.size() );
		_listBuffer.push_back( buffer );
		return slot;
	}

	void BindlessTable::freeBufferSlot( RHIDescriptorIndex slotIndex )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( slotIndex < _listBuffer.size() && _listBuffer[slotIndex] != 0 )
		{
			_listBuffer[slotIndex] = 0;
			_listFreeBufferSlot.push_back( slotIndex );
		}
	}

	RHITextureHandle BindlessTable::getTexture( RHIDescriptorIndex slotIndex ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		if ( slotIndex < _listTexture.size() )
			return _listTexture[slotIndex];
		return 0;
	}

	RHIBufferHandle BindlessTable::getBuffer( RHIDescriptorIndex slotIndex ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		if ( slotIndex < _listBuffer.size() )
			return _listBuffer[slotIndex];
		return 0;
	}

	size_t BindlessTable::getActiveTextureCount() const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		return _listTexture.size() - _listFreeTextureSlot.size();
	}

	size_t BindlessTable::getActiveBufferCount() const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		return _listBuffer.size() - _listFreeBufferSlot.size();
	}
} // namespace sw
