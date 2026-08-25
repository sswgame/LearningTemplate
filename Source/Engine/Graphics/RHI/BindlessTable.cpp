#include "pch.h"

#include "Engine/Graphics/RHI/BindlessTable.h"

#include "Core/Log/Logger.h"

#include <shared_mutex>

namespace sw
{
	BindlessTable::BindlessTable()
		: _mutex{}
		, _listTextures{}
		, _listBuffers{}
		, _listFreeTextureSlots{}
		, _listFreeBufferSlots{}
		, _maxSlots{ kMaxBindlessSlots }
	{
	}

	void BindlessTable::initialize( uint32 maxSlots )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		_maxSlots = maxSlots;
		_listTextures.clear();
		_listBuffers.clear();
		_listFreeTextureSlots.clear();
		_listFreeBufferSlots.clear();
	}

	void BindlessTable::shutdown()
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		_listTextures.clear();
		_listBuffers.clear();
		_listFreeTextureSlots.clear();
		_listFreeBufferSlots.clear();
	}

	RHIDescriptorIndex BindlessTable::allocateTextureSlot( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return kInvalidDescriptorIndex;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( _listFreeTextureSlots.empty() == false )
		{
			const RHIDescriptorIndex slot = _listFreeTextureSlots.back();
			_listFreeTextureSlots.pop_back();
			_listTextures[slot] = texture;
			return slot;
		}

		if ( _listTextures.size() >= _maxSlots )
			return kInvalidDescriptorIndex;

		const auto slot = static_cast<RHIDescriptorIndex>( _listTextures.size() );
		_listTextures.push_back( texture );
		return slot;
	}

	void BindlessTable::freeTextureSlot( RHIDescriptorIndex slotIndex )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( slotIndex < _listTextures.size() && _listTextures[slotIndex] != 0 )
		{
			_listTextures[slotIndex] = 0;
			_listFreeTextureSlots.push_back( slotIndex );
		}
	}

	RHIDescriptorIndex BindlessTable::allocateBufferSlot( RHIBufferHandle buffer )
	{
		if ( buffer == 0 )
			return kInvalidDescriptorIndex;

		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( _listFreeBufferSlots.empty() == false )
		{
			const RHIDescriptorIndex slot = _listFreeBufferSlots.back();
			_listFreeBufferSlots.pop_back();
			_listBuffers[slot] = buffer;
			return slot;
		}

		if ( _listBuffers.size() >= _maxSlots )
			return kInvalidDescriptorIndex;

		const auto slot = static_cast<RHIDescriptorIndex>( _listBuffers.size() );
		_listBuffers.push_back( buffer );
		return slot;
	}

	void BindlessTable::freeBufferSlot( RHIDescriptorIndex slotIndex )
	{
		std::unique_lock<std::shared_mutex> lock{ _mutex };
		if ( slotIndex < _listBuffers.size() && _listBuffers[slotIndex] != 0 )
		{
			_listBuffers[slotIndex] = 0;
			_listFreeBufferSlots.push_back( slotIndex );
		}
	}

	RHITextureHandle BindlessTable::getTexture( RHIDescriptorIndex slotIndex ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		if ( slotIndex < _listTextures.size() )
			return _listTextures[slotIndex];
		return 0;
	}

	RHIBufferHandle BindlessTable::getBuffer( RHIDescriptorIndex slotIndex ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		if ( slotIndex < _listBuffers.size() )
			return _listBuffers[slotIndex];
		return 0;
	}

	size_t BindlessTable::getActiveTextureCount() const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		return _listTextures.size() - _listFreeTextureSlots.size();
	}

	size_t BindlessTable::getActiveBufferCount() const
	{
		std::shared_lock<std::shared_mutex> lock{ _mutex };
		return _listBuffers.size() - _listFreeBufferSlots.size();
	}
} // namespace sw
