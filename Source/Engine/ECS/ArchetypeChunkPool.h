#pragma once

#include "Core/Memory/Memory.h"
#include "Engine/EngineMinimal.h"

namespace sw
{
	using ComponentDestructFn	= void ( * )( void* pElement );
	using ComponentMoveAssignFn = void ( * )( void* pDst, void* pSrc );

	/**
	 * @struct ComponentColumnLayout
	 * @brief SoA 청크 내 각 컴포넌트 열(Column)의 메모리 레이아웃 정보 및 라이프사이클 델리게이트
	 */
	struct SW_API ComponentColumnLayout
	{
		uint64				  _typeHash{ 0 };
		size_t				  _elementSize{ 0 };
		size_t				  _alignment{ 16 };
		ComponentDestructFn	  _destructFn{ nullptr };
		ComponentMoveAssignFn _moveAssignFn{ nullptr };
	};

	template <typename T>
	/** @brief 컴포넌트 타입 T의 Type Traits를 분석하여 자동 라이프사이클 함수를 연결한 레이아웃 생성 */
	inline ComponentColumnLayout makeColumnLayout( uint64 typeHash = 0 )
	{
		ComponentColumnLayout layout{};
		layout._typeHash	= typeHash;
		layout._elementSize = sizeof( T );
		layout._alignment	= alignof( T );

		if constexpr ( std::is_trivially_destructible_v<T> == false )
		{
			layout._destructFn = []( void* pElement )
			{
				static_cast<T*>( pElement )->~T();
			};
		}

		if constexpr ( std::is_trivially_copyable_v<T> == false )
		{
			layout._moveAssignFn = []( void* pDst, void* pSrc )
			{
				*static_cast<T*>( pDst ) = std::move( *static_cast<T*>( pSrc ) );
			};
		}

		return layout;
	}

	/**
	 * @class ArchetypeChunk
	 * @brief 캐시 친화적 연속 메모리 블록 (SoA 레이아웃)
	 * @details 엔티티 ID와 복수 컴포넌트 데이터를 단일 64KB 연속 블록에 인터리빙 없이 SoA 형태로 배치합니다.
	 */
	class SW_API ArchetypeChunk
	{
	public:
		static constexpr size_t kChunkCapacity = 512; // 청크당 엔티티 수용량

		explicit ArchetypeChunk( const vector<ComponentColumnLayout>& listLayouts );
		~ArchetypeChunk();

		ArchetypeChunk( const ArchetypeChunk& )			   = delete;
		ArchetypeChunk& operator=( const ArchetypeChunk& ) = delete;

		ArchetypeChunk( ArchetypeChunk&& rhs ) noexcept;
		ArchetypeChunk& operator=( ArchetypeChunk&& rhs ) noexcept;

		bool   isFull() const { return _count >= kChunkCapacity; }
		size_t getCount() const { return _count; }

		size_t allocateRow( uint64 entityId );
		bool   freeRow( size_t rowIndex );

		uint64		getEntityId( size_t rowIndex ) const;
		void*		getComponentColumn( size_t columnIndex );
		const void* getComponentColumn( size_t columnIndex ) const;

		void*		getComponent( size_t columnIndex, size_t rowIndex );
		const void* getComponent( size_t columnIndex, size_t rowIndex ) const;

	private:
		vector<ComponentColumnLayout> _listLayouts;
		vector<size_t>				  _listColumnOffsets;
		vector<uint64>				  _listEntityIds;
		uint8*						  _pChunkMemory;
		size_t						  _chunkMemorySize;
		size_t						  _count;
	};

	/**
	 * @class ArchetypeChunkPool
	 * @brief 동일한 컴포넌트 시그니처를 가진 엔티티들의 SoA 청크 풀 매니저
	 */
	class SW_API ArchetypeChunkPool
	{
	public:
		explicit ArchetypeChunkPool( const vector<ComponentColumnLayout>& listLayouts );
		~ArchetypeChunkPool() = default;

		ArchetypeChunkPool( const ArchetypeChunkPool& )			   = delete;
		ArchetypeChunkPool& operator=( const ArchetypeChunkPool& ) = delete;

		ArchetypeChunkPool( ArchetypeChunkPool&& ) noexcept			   = default;
		ArchetypeChunkPool& operator=( ArchetypeChunkPool&& ) noexcept = default;

		uint64 allocateEntity( uint64 entityId, size_t& outChunkIndex, size_t& outRowIndex );
		bool   freeEntity( size_t chunkIndex, size_t rowIndex );

		size_t getTotalEntities() const { return _totalEntities; }
		size_t getChunkCount() const { return _listChunks.size(); }

		ArchetypeChunk*		  getChunk( size_t chunkIndex );
		const ArchetypeChunk* getChunk( size_t chunkIndex ) const;

	private:
		vector<ComponentColumnLayout>		   _listLayouts;
		vector<sw::unique_ptr<ArchetypeChunk>> _listChunks;
		size_t								   _totalEntities;
	};
} // namespace sw
