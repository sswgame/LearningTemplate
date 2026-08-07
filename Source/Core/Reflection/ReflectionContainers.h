#pragma once
/**
 * @file ReflectionContainers.h
 * @brief Reflection container wrappers (sequence / map)
 */

#include "Core/CoreMinimal.h"
#include "Core/Utility/Task/TaskTypes.h"

namespace sw
{

	enum class ContainerKind
	{
		None,
		Sequence,
		Map
	};

	struct ISequenceContainerWrapper;
	struct IMapContainerWrapper;

	struct IContainerWrapper
	{
		virtual ~IContainerWrapper() = default;
		/**
		 * @brief Kind을(를) 반환합니다
		 */
		virtual ContainerKind getKind() const = 0;
		/**
		 * @brief 컨테이너 크기를 반환합니다
		 */
		virtual size_t getSize( const void* containerPtr ) const = 0;
		/**
		 * @brief 내부 상태를 비웁니다
		 */
		virtual void clear( void* containerPtr ) const = 0;

		virtual ISequenceContainerWrapper* asSequence() { return nullptr; }
		virtual IMapContainerWrapper*	   asMap() { return nullptr; }
	};

	struct ISequenceContainerWrapper : public IContainerWrapper
	{
		ContainerKind			   getKind() const override { return ContainerKind::Sequence; }
		ISequenceContainerWrapper* asSequence() override { return this; }
		/**
		 * @brief 요소를 반환합니다
		 */
		virtual void*		getElement( void* containerPtr, size_t index ) const			= 0;
		virtual const void* getElementConst( const void* containerPtr, size_t index ) const = 0;
		/**
		 * @brief 기본값 요소를 추가합니다
		 */
		virtual void addElementDefault( void* containerPtr ) const = 0;
		virtual void reserve( void*, size_t ) const {}
	};

	using MapForEachDelegate = Delegate<void( const void* keyPtr, const void* valPtr )>;

	struct IMapContainerWrapper : public IContainerWrapper
	{
		ContainerKind		  getKind() const override { return ContainerKind::Map; }
		IMapContainerWrapper* asMap() override { return this; }

		/**
		 * @brief 각 항목에 대해 실행합니다
		 */
		virtual void forEach( const void* containerPtr, const MapForEachDelegate& callback ) const = 0;
		/**
		 * @brief 키-값을 삽입합니다
		 */
		virtual void insertKeyValue( void* containerPtr, const void* keyPtr, const void* valPtr ) const = 0;

		/**
		 * @brief KeySize을(를) 반환합니다
		 */
		virtual size_t getKeySize() const = 0;
		/**
		 * @brief ValueSize을(를) 반환합니다
		 */
		virtual size_t getValueSize() const = 0;
		/**
		 * @brief 키를 기본 생성합니다
		 */
		virtual void defaultConstructKey( void* ptr ) const = 0;
		/**
		 * @brief 값을 기본 생성합니다
		 */
		virtual void defaultConstructValue( void* ptr ) const = 0;
		/**
		 * @brief Key을(를) 파괴합니다
		 */
		virtual void destroyKey( void* ptr ) const = 0;
		/**
		 * @brief Value을(를) 파괴합니다
		 */
		virtual void destroyValue( void* ptr ) const = 0;
	};

	template <typename TContainer>
	struct VectorWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			return &( ( *container )[index] );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			return &( ( *container )[index] );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->emplace_back( ElementType{} );
		}
		void reserve( void* containerPtr, size_t capacity ) const override
		{
			static_cast<TContainer*>( containerPtr )->reserve( capacity );
		}
	};

	template <typename TContainer>
	struct ListWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			auto  it		= container->begin();
			std::advance( it, index );
			return &( *it );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			auto  it		= container->begin();
			std::advance( it, index );
			return &( *it );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->emplace_back( ElementType{} );
		}
	};

	template <typename TContainer>
	struct DequeWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			return &( ( *container )[index] );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			return &( ( *container )[index] );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->emplace_back( ElementType{} );
		}
	};

	template <typename TContainer>
	struct SetWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			auto  it		= container->begin();
			std::advance( it, index );
			return const_cast<void*>( static_cast<const void*>( &( *it ) ) );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			auto  it		= container->begin();
			std::advance( it, index );
			return &( *it );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->insert( ElementType{} );
		}
	};

	template <typename TContainer>
	struct UnorderedSetWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			auto  it		= container->begin();
			std::advance( it, index );
			return const_cast<void*>( static_cast<const void*>( &( *it ) ) );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			auto  it		= container->begin();
			std::advance( it, index );
			return &( *it );
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void addElementDefault( void* containerPtr ) const override
		{
			using ElementType = typename TContainer::value_type;
			static_cast<TContainer*>( containerPtr )->insert( ElementType{} );
		}
	};

	template <typename TContainer>
	struct ArrayWrapper : public ISequenceContainerWrapper
	{
		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void* getElement( void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<TContainer*>( const_cast<void*>( containerPtr ) );
			return &( ( *container )[index] );
		}
		const void* getElementConst( const void* containerPtr, size_t index ) const override
		{
			auto* container = static_cast<const TContainer*>( containerPtr );
			return &( ( *container )[index] );
		}
		void clear( void* ) const override
		{
		}
		void addElementDefault( void* ) const override
		{
		}
	};

	template <typename TContainer>
	struct MapWrapper : public IMapContainerWrapper
	{
		using KeyType	= typename TContainer::key_type;
		using ValueType = typename TContainer::mapped_type;

		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void forEach( const void* containerPtr, const MapForEachDelegate& callback ) const override
		{
			const auto* container = static_cast<const TContainer*>( containerPtr );
			for ( const auto& pair : *container )
				callback( &pair.first, &pair.second );
		}
		void insertKeyValue( void* containerPtr, const void* keyPtr, const void* valPtr ) const override
		{
			auto* container = static_cast<TContainer*>( containerPtr );
			( *container )[*static_cast<const KeyType*>( keyPtr )] =
				*static_cast<const ValueType*>( valPtr );
		}
		size_t getKeySize() const override { return sizeof( KeyType ); }
		size_t getValueSize() const override { return sizeof( ValueType ); }
		void   defaultConstructKey( void* ptr ) const override { new ( ptr ) KeyType{}; }
		void   defaultConstructValue( void* ptr ) const override { new ( ptr ) ValueType{}; }
		void   destroyKey( void* ptr ) const override { static_cast<KeyType*>( ptr )->~KeyType(); }
		void   destroyValue( void* ptr ) const override { static_cast<ValueType*>( ptr )->~ValueType(); }
	};

	template <typename TContainer>
	struct UnorderedMapWrapper : public IMapContainerWrapper
	{
		using KeyType	= typename TContainer::key_type;
		using ValueType = typename TContainer::mapped_type;

		size_t getSize( const void* containerPtr ) const override
		{
			return static_cast<const TContainer*>( containerPtr )->size();
		}
		void clear( void* containerPtr ) const override
		{
			static_cast<TContainer*>( const_cast<void*>( containerPtr ) )->clear();
		}
		void forEach( const void* containerPtr, const MapForEachDelegate& callback ) const override
		{
			const auto* container = static_cast<const TContainer*>( containerPtr );
			for ( const auto& pair : *container )
				callback( &pair.first, &pair.second );
		}
		void insertKeyValue( void* containerPtr, const void* keyPtr, const void* valPtr ) const override
		{
			auto* container = static_cast<TContainer*>( containerPtr );
			( *container )[*static_cast<const KeyType*>( keyPtr )] =
				*static_cast<const ValueType*>( valPtr );
		}
		size_t getKeySize() const override { return sizeof( KeyType ); }
		size_t getValueSize() const override { return sizeof( ValueType ); }
		void   defaultConstructKey( void* ptr ) const override { new ( ptr ) KeyType{}; }
		void   defaultConstructValue( void* ptr ) const override { new ( ptr ) ValueType{}; }
		void   destroyKey( void* ptr ) const override { static_cast<KeyType*>( ptr )->~KeyType(); }
		void   destroyValue( void* ptr ) const override { static_cast<ValueType*>( ptr )->~ValueType(); }
	};

} // namespace sw
