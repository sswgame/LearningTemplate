/**
 * @file ReflectionContainers.h
 * @brief 리플렉션 컨테이너 래퍼 (시퀀스 / 맵)
 */
#pragma once

#include "Engine/EngineMinimal.h"
#include "Engine/Utility/Task/TaskTypes.h"

namespace sw
{

	/// @brief Container Kind — 목록은 PredefinedContainerKind.xxx
	enum class ContainerKind : uint8
	{
#define REGISTER_CONTAINER_KIND( Name ) Name,
#include "Core/Predefined/PredefinedContainerKind.xxx"

#undef REGISTER_CONTAINER_KIND
	};

	struct ISequenceContainerWrapper;
	struct IMapContainerWrapper;

	/// @brief 시퀀스/맵 공통 컨테이너 래퍼
	struct IContainerWrapper
	{
		/** @brief 가상 소멸. */
		virtual ~IContainerWrapper() = default;
		/** @brief Sequence / Map 종류. */
		virtual ContainerKind getKind() const = 0;
		/**
		 * @brief 컨테이너 크기를 반환합니다
		 */
		virtual size_t getSize( const void* pContainer ) const = 0;
		/**
		 * @brief 내부 상태를 비웁니다
		 */
		virtual void clear( void* pContainer ) const = 0;

		/** @brief Zeroed storage에 빈 컨테이너를 placement-new 합니다. */
		virtual void constructEmpty( void* pContainer ) const { (void)pContainer; }
		/** @brief constructEmpty 로 만든 컨테이너를 파괴합니다. */
		virtual void destroyContainer( void* pContainer ) const { (void)pContainer; }

		virtual ISequenceContainerWrapper* asSequence() { return nullptr; }
		virtual IMapContainerWrapper*	   asMap() { return nullptr; }
	};

	/// @brief 인덱스 시퀀스 컨테이너 래퍼
	struct ISequenceContainerWrapper : IContainerWrapper
	{
		ContainerKind			   getKind() const override { return ContainerKind::Sequence; }
		ISequenceContainerWrapper* asSequence() override { return this; }
		/**
		 * @brief 요소를 반환합니다
		 */
		virtual void* getElement( void* pContainer, size_t index ) const = 0;
		/** @brief 인덱스 요소의 const 포인터. */
		virtual const void* getElementConst( const void* pContainer, size_t index ) const = 0;
		/**
		 * @brief 기본값 요소를 추가합니다
		 */
		virtual void addElementDefault( void* pContainer ) const = 0;
		virtual void reserve( void*, size_t ) const {}
	};

	using MapForEachDelegate = Delegate<void( const void* pKey, const void* pVal )>;

	/// @brief 키-값 맵 컨테이너 래퍼
	struct IMapContainerWrapper : IContainerWrapper
	{
		ContainerKind		  getKind() const override { return ContainerKind::Map; }
		IMapContainerWrapper* asMap() override { return this; }

		/**
		 * @brief 각 항목에 대해 실행합니다
		 */
		virtual void forEach( const void* pContainer, const MapForEachDelegate& callback ) const = 0;
		/**
		 * @brief 키-값을 삽입합니다
		 */
		virtual void insertKeyValue( void* pContainer, const void* pKey, const void* pVal ) const = 0;

		/** @brief 키 타입 바이트 크기. */
		virtual size_t getKeySize() const = 0;
		/** @brief 값 타입 바이트 크기. */
		virtual size_t getValueSize() const = 0;
		/**
		 * @brief 키를 기본 생성합니다
		 */
		virtual void defaultConstructKey( void* pPtr ) const = 0;
		/**
		 * @brief 값을 기본 생성합니다
		 */
		virtual void defaultConstructValue( void* pPtr ) const = 0;
		/** @brief 키를 파괴합니다. */
		virtual void destroyKey( void* pPtr ) const = 0;
		/** @brief 값을 파괴합니다. */
		virtual void destroyValue( void* pPtr ) const = 0;
	};

	template <typename TContainer>
	/// @brief vector 시퀀스 래퍼
	struct VectorWrapper : ISequenceContainerWrapper
	{
		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 인덱스 원소 포인터. */
		void* getElement( void* pContainer, size_t index ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			return &( ( *pContainerTyped )[index] );
		}

		/** @brief 인덱스 원소 const 포인터. */
		const void* getElementConst( const void* pContainer, size_t index ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			return &( ( *pContainerTyped )[index] );
		}

		/** @brief 비웁니다. */
		void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 기본 원소를 뒤에 추가합니다. */
		void addElementDefault( void* pContainer ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( pContainer )->emplace_back( ElementType{} );
		}

		/** @brief 용량을 예약합니다. */
		void reserve( void* pContainer, size_t capacity ) const override { static_cast<TContainer*>( pContainer )->reserve( capacity ); }
	};

	template <typename TContainer>
	/// @brief list 시퀀스 래퍼
	struct ListWrapper : ISequenceContainerWrapper
	{
		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 인덱스 원소 포인터. */
		void* getElement( void* pContainer, size_t index ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			auto		it				= pContainerTyped->begin();
			std::advance( it, index );
			return &( *it );
		}

		/** @brief 인덱스 원소 const 포인터. */
		const void* getElementConst( const void* pContainer, size_t index ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			auto			  it			  = pContainerTyped->begin();
			std::advance( it, index );
			return &( *it );
		}

		/** @brief 비웁니다. */
		void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 기본 원소를 뒤에 추가합니다. */
		void addElementDefault( void* pContainer ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( pContainer )->emplace_back( ElementType{} );
		}
	};

	template <typename TContainer>
	/// @brief deque 시퀀스 래퍼
	struct DequeWrapper : ISequenceContainerWrapper
	{
		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 인덱스 원소 포인터. */
		void* getElement( void* pContainer, size_t index ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			return &( ( *pContainerTyped )[index] );
		}

		/** @brief 인덱스 원소 const 포인터. */
		const void* getElementConst( const void* pContainer, size_t index ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			return &( ( *pContainerTyped )[index] );
		}

		/** @brief 비웁니다. */
		void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 기본 원소를 뒤에 추가합니다. */
		void addElementDefault( void* pContainer ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( pContainer )->emplace_back( ElementType{} );
		}
	};

	template <typename TContainer>
	/// @brief set 시퀀스 래퍼 (인덱스 순회)
	struct SetWrapper : ISequenceContainerWrapper
	{
		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 인덱스 원소 포인터. */
		void* getElement( void* pContainer, size_t index ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			auto		it				= pContainerTyped->begin();
			std::advance( it, index );
			return const_cast<void*>( static_cast<const void*>( &( *it ) ) );
		}

		/** @brief 인덱스 원소 const 포인터. */
		const void* getElementConst( const void* pContainer, size_t index ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			auto			  it			  = pContainerTyped->begin();
			std::advance( it, index );
			return &( *it );
		}

		/** @brief 비웁니다. */
		void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 기본 원소를 뒤에 추가합니다. */
		void addElementDefault( void* pContainer ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( pContainer )->insert( ElementType{} );
		}
	};

	template <typename TContainer>
	/// @brief unordered_set 시퀀스 래퍼
	struct UnorderedSetWrapper : ISequenceContainerWrapper
	{
		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 인덱스 원소 포인터. */
		void* getElement( void* pContainer, size_t index ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			auto		it				= pContainerTyped->begin();
			std::advance( it, index );
			return const_cast<void*>( static_cast<const void*>( &( *it ) ) );
		}

		/** @brief 인덱스 원소 const 포인터. */
		const void* getElementConst( const void* pContainer, size_t index ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			auto			  it			  = pContainerTyped->begin();
			std::advance( it, index );
			return &( *it );
		}

		/** @brief 비웁니다. */
		void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 기본 원소를 뒤에 추가합니다. */
		void addElementDefault( void* pContainer ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( pContainer )->insert( ElementType{} );
		}
	};

	template <typename TContainer>
	/// @brief 고정 배열 시퀀스 래퍼 (add/clear 없음)
	struct ArrayWrapper : ISequenceContainerWrapper
	{
		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 인덱스 원소 포인터. */
		void* getElement( void* pContainer, size_t index ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			return &( ( *pContainerTyped )[index] );
		}

		/** @brief 인덱스 원소 const 포인터. */
		const void* getElementConst( const void* pContainer, size_t index ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			return &( ( *pContainerTyped )[index] );
		}

		/** @brief 비웁니다. */
		void clear( void* ) const override {}
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 기본 원소를 뒤에 추가합니다. */
		void addElementDefault( void* ) const override {}
	};

	template <typename TContainer>
	/// @brief map 키-값 래퍼
	struct MapWrapper : IMapContainerWrapper
	{
		using KeyType	= typename TContainer::key_type;
		using ValueType = typename TContainer::mapped_type;

		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 비웁니다. */
		void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 각 키-값에 콜백을 호출합니다. */
		void forEach( const void* pContainer, const MapForEachDelegate& callback ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			for ( const auto& pair : *pContainerTyped )
			{
				callback( &pair.first, &pair.second );
			}
		}

		/** @brief 키-값을 삽입하거나 덮어씁니다. */
		void insertKeyValue( void* pContainer, const void* pKey, const void* pVal ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			( *pContainerTyped )[*static_cast<const KeyType*>( pKey )] =
				*static_cast<const ValueType*>( pVal );
		}

		/** @brief 키 바이트 크기. */
		size_t getKeySize() const override { return sizeof( KeyType ); }
		/** @brief 값 바이트 크기. */
		size_t getValueSize() const override { return sizeof( ValueType ); }
		/** @brief 키를 기본 생성합니다. */
		void defaultConstructKey( void* pPtr ) const override { sw_placement_new( pPtr ) KeyType{}; }
		void defaultConstructValue( void* pPtr ) const override { sw_placement_new( pPtr ) ValueType{}; }
		void destroyKey( void* pPtr ) const override { static_cast<KeyType*>( pPtr )->~KeyType(); }
		/** @brief 값을 파괴합니다. */
		void destroyValue( void* pPtr ) const override { static_cast<ValueType*>( pPtr )->~ValueType(); }
	};

	template <typename TContainer>
	/// @brief unordered_map 키-값 래퍼
	struct UnorderedMapWrapper : IMapContainerWrapper
	{
		using KeyType	= typename TContainer::key_type;
		using ValueType = typename TContainer::mapped_type;

		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 비웁니다. */
		void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 각 키-값에 콜백을 호출합니다. */
		void forEach( const void* pContainer, const MapForEachDelegate& callback ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			for ( const auto& pair : *pContainerTyped )
			{
				callback( &pair.first, &pair.second );
			}
		}

		/** @brief 키-값을 삽입하거나 덮어씁니다. */
		void insertKeyValue( void* pContainer, const void* pKey, const void* pVal ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			( *pContainerTyped )[*static_cast<const KeyType*>( pKey )] =
				*static_cast<const ValueType*>( pVal );
		}

		/** @brief 키 바이트 크기. */
		size_t getKeySize() const override { return sizeof( KeyType ); }
		/** @brief 값 바이트 크기. */
		size_t getValueSize() const override { return sizeof( ValueType ); }
		/** @brief 키를 기본 생성합니다. */
		void defaultConstructKey( void* pPtr ) const override { sw_placement_new( pPtr ) KeyType{}; }
		void defaultConstructValue( void* pPtr ) const override { sw_placement_new( pPtr ) ValueType{}; }
		void destroyKey( void* pPtr ) const override { static_cast<KeyType*>( pPtr )->~KeyType(); }
		/** @brief 값을 파괴합니다. */
		void destroyValue( void* pPtr ) const override { static_cast<ValueType*>( pPtr )->~ValueType(); }
	};

	template <typename TContainer>
	/// @brief sparse_set 키-값 래퍼
	struct SparseSetWrapper : IMapContainerWrapper
	{
		using KeyType	= typename TContainer::key_type;
		using ValueType = typename TContainer::mapped_type;

		/** @brief 원소 개수. */
		size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
		/** @brief 비웁니다. */
		void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
		/** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
		void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
		void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
		/** @brief 각 키-값에 콜백을 호출합니다. */
		void forEach( const void* pContainer, const MapForEachDelegate& callback ) const override
		{
			const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
			for ( auto tuple : *pContainerTyped )
			{
				const KeyType&	 key = std::get<0>( tuple );
				const ValueType& val = std::get<1>( tuple );
				callback( &key, &val );
			}
		}

		/** @brief 키-값을 삽입하거나 덮어씁니다. */
		void insertKeyValue( void* pContainer, const void* pKey, const void* pVal ) const override
		{
			TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
			pContainerTyped->insert( *static_cast<const KeyType*>( pKey ), *static_cast<const ValueType*>( pVal ) );
		}

		/** @brief 키 바이트 크기. */
		size_t getKeySize() const override { return sizeof( KeyType ); }
		/** @brief 값 바이트 크기. */
		size_t getValueSize() const override { return sizeof( ValueType ); }
		/** @brief 키를 기본 생성합니다. */
		void defaultConstructKey( void* pPtr ) const override { sw_placement_new( pPtr ) KeyType{}; }
		void defaultConstructValue( void* pPtr ) const override { sw_placement_new( pPtr ) ValueType{}; }
		void destroyKey( void* pPtr ) const override { static_cast<KeyType*>( pPtr )->~KeyType(); }
		/** @brief 값을 파괴합니다. */
		void destroyValue( void* pPtr ) const override { static_cast<ValueType*>( pPtr )->~ValueType(); }
	};

} // namespace sw
