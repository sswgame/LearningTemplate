#pragma once
/**
 * @file TypedHandle.h
 * @brief TypedId, GenerationHandle, HandleTable, and DenseHandlePool (header-only).
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/**
	 * @brief Strongly-typed opaque id (tag prevents accidental cross-type mixing).
	 */
	template <typename Tag>
	struct TypedId
	{
		uint32 _value = 0;

		constexpr TypedId() noexcept = default;
		explicit constexpr TypedId( uint32 value ) noexcept
			: _value{ value }
		{
		}

		constexpr bool isValid() const noexcept { return _value != 0; }
		constexpr explicit operator bool() const noexcept { return isValid(); }

		constexpr bool operator==( TypedId other ) const noexcept { return _value == other._value; }
		constexpr bool operator!=( TypedId other ) const noexcept { return _value != other._value; }
	};

	/**
	 * @brief Index + generation handle for pooled reuse without ABA.
	 */
	struct GenerationHandle
	{
		uint32 _index	   = 0;
		uint32 _generation = 0;

		constexpr bool isValid() const noexcept { return _generation != 0; }
		constexpr explicit operator bool() const noexcept { return isValid(); }

		constexpr bool operator==( const GenerationHandle& other ) const noexcept
		{
			return _index == other._index && _generation == other._generation;
		}
		constexpr bool operator!=( const GenerationHandle& other ) const noexcept
		{
			return !( *this == other );
		}
	};

	/**
	 * @brief Sparse pool: allocate / free / get by GenerationHandle.
	 */
	template <typename T>
	class HandleTable
	{
	public:
		GenerationHandle allocate( T value = T{} )
		{
			uint32 index = 0;
			if ( _freeList.empty() == false )
			{
				index = _freeList.back();
				_freeList.pop_back();
				Slot& slot		= _slots[index];
				slot._value		= std::move( value );
				slot._bAlive	= true;
				++slot._generation;
				if ( slot._generation == 0 )
					slot._generation = 1;
			}
			else
			{
				index = static_cast<uint32>( _slots.size() );
				Slot slot{};
				slot._value		 = std::move( value );
				slot._generation = 1;
				slot._bAlive	 = true;
				_slots.push_back( std::move( slot ) );
			}

			GenerationHandle handle{};
			handle._index	   = index;
			handle._generation = _slots[index]._generation;
			return handle;
		}

		bool free( GenerationHandle handle )
		{
			T* ptr = get( handle );
			if ( ptr == nullptr )
				return false;

			Slot& slot	 = _slots[handle._index];
			slot._bAlive = false;
			slot._value	 = T{};
			_freeList.push_back( handle._index );
			return true;
		}

		T* get( GenerationHandle handle )
		{
			if ( handle._index >= static_cast<uint32>( _slots.size() ) )
				return nullptr;
			Slot& slot = _slots[handle._index];
			if ( slot._bAlive == false || slot._generation != handle._generation )
				return nullptr;
			return &slot._value;
		}

		const T* get( GenerationHandle handle ) const
		{
			return const_cast<HandleTable*>( this )->get( handle );
		}

		uint32 capacity() const { return static_cast<uint32>( _slots.size() ); }

		uint32 aliveCount() const
		{
			uint32 count = 0;
			for ( const Slot& slot : _slots )
			{
				if ( slot._bAlive )
					++count;
			}
			return count;
		}

	private:
		struct Slot
		{
			T	  _value{};
			uint32 _generation = 0;
			bool   _bAlive	   = false;
		};

		std::vector<Slot>	_slots;
		std::vector<uint32> _freeList;
	};

	/**
	 * @brief Dense packed T storage with GenerationHandle sparse indirection (swap-remove free).
	 */
	template <typename T>
	class DenseHandlePool
	{
	public:
		GenerationHandle allocate( T value = T{} )
		{
			uint32 sparseIndex = 0;
			if ( _freeSparse.empty() == false )
			{
				sparseIndex = _freeSparse.back();
				_freeSparse.pop_back();
			}
			else
			{
				sparseIndex = static_cast<uint32>( _sparse.size() );
				_sparse.push_back( SparseSlot{} );
			}

			SparseSlot& sparse = _sparse[sparseIndex];
			if ( sparse._generation == 0 )
				sparse._generation = 1;

			const uint32 denseIndex = static_cast<uint32>( _dense.size() );
			_dense.push_back( std::move( value ) );
			_denseToSparse.push_back( sparseIndex );
			sparse._denseIndex = denseIndex;

			GenerationHandle handle{};
			handle._index	   = sparseIndex;
			handle._generation = sparse._generation;
			return handle;
		}

		bool free( GenerationHandle handle )
		{
			T* ptr = get( handle );
			if ( ptr == nullptr )
				return false;

			const uint32 sparseIndex = handle._index;
			const uint32 denseIndex	 = _sparse[sparseIndex]._denseIndex;
			const uint32 lastDense	 = static_cast<uint32>( _dense.size() - 1 );

			if ( denseIndex != lastDense )
			{
				_dense[denseIndex]			   = std::move( _dense[lastDense] );
				const uint32 movedSparse	   = _denseToSparse[lastDense];
				_denseToSparse[denseIndex]	   = movedSparse;
				_sparse[movedSparse]._denseIndex = denseIndex;
			}

			_dense.pop_back();
			_denseToSparse.pop_back();

			SparseSlot& sparse = _sparse[sparseIndex];
			sparse._denseIndex = kInvalidDense;
			++sparse._generation;
			if ( sparse._generation == 0 )
				sparse._generation = 1;
			_freeSparse.push_back( sparseIndex );
			return true;
		}

		T* get( GenerationHandle handle )
		{
			if ( handle._index >= static_cast<uint32>( _sparse.size() ) )
				return nullptr;
			const SparseSlot& sparse = _sparse[handle._index];
			if ( sparse._generation != handle._generation || sparse._denseIndex == kInvalidDense )
				return nullptr;
			return &_dense[sparse._denseIndex];
		}

		const T* get( GenerationHandle handle ) const
		{
			return const_cast<DenseHandlePool*>( this )->get( handle );
		}

		T*	   data() { return _dense.empty() ? nullptr : _dense.data(); }
		const T* data() const { return _dense.empty() ? nullptr : _dense.data(); }
		uint32 count() const { return static_cast<uint32>( _dense.size() ); }
		bool   empty() const { return _dense.empty(); }

	private:
		static constexpr uint32 kInvalidDense = 0xffffffffu;

		struct SparseSlot
		{
			uint32 _generation = 0;
			uint32 _denseIndex = kInvalidDense;
		};

		std::vector<SparseSlot> _sparse;
		std::vector<T>			_dense;
		std::vector<uint32>		_denseToSparse;
		std::vector<uint32>		_freeSparse;
	};
} // namespace sw
