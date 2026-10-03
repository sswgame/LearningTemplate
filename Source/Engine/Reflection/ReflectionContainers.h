/**
 * @file ReflectionContainers.h
 * @brief 리플렉션 컨테이너 래퍼 (시퀀스 / 맵)
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{

    /// @brief 컨테이너 종류입니다. 목록은 PredefinedContainerKind.xxx 에 있습니다.
    enum class ContainerKind : uint8
    {
#define REGISTER_CONTAINER_KIND( Name ) Name,
#include "Core/Predefined/PredefinedContainerKind.xxx"

#undef REGISTER_CONTAINER_KIND
    };

    struct IMapContainerWrapper;
    struct ISequenceContainerWrapper;

    /// @brief 시퀀스/맵 공통 컨테이너 래퍼
    struct IContainerWrapper
    {
        /** @brief 가상 소멸. */
        virtual ~IContainerWrapper() = default;
        /** @brief Sequence / Map 종류를 반환합니다. */
        virtual ContainerKind getKind() const = 0;
        /**
         * @brief 컨테이너 크기를 반환합니다
         */
        virtual size_t getSize( const void* pContainer ) const = 0;
        /**
         * @brief 컨테이너를 비웁니다
         */
        virtual void clear( void* pContainer ) const = 0;

        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
        virtual void constructEmpty( void* pContainer ) const { (void)pContainer; }
        /** @brief constructEmpty 로 만든 컨테이너를 파괴합니다. */
        virtual void destroyContainer( void* pContainer ) const { (void)pContainer; }

        virtual ISequenceContainerWrapper* asSequence() { return nullptr; }
        virtual IMapContainerWrapper*      asMap() { return nullptr; }
    };

    /**
     * @brief 원소 하나를 채우는 콜백입니다. 인자는 **채울 원소의 주소**입니다. 실패하면 false 입니다.
     * @details 읽는 방법(바이너리 스트림 · JSON 값 · XML 노드)은 부르는 쪽이 알고, **그 값을 컨테이너에
     *          넣는 방법**은 컨테이너가 압니다. 그 둘을 나누려고 둡니다(`appendElement` 참고).
     */
    using ElementFillDelegate = Delegate<bool( void* pElement )>;

    /// @brief 인덱스 시퀀스 컨테이너 래퍼
    struct ISequenceContainerWrapper : IContainerWrapper
    {
        ContainerKind              getKind() const override { return ContainerKind::Sequence; }
        ISequenceContainerWrapper* asSequence() override { return this; }
        /**
         * @brief 인덱스의 원소 포인터를 반환합니다
         */
        virtual void* getElement( void* pContainer, size_t index ) const = 0;
        /** @brief 인덱스 요소의 const 포인터. */
        virtual const void* getElementConst( const void* pContainer, size_t index ) const = 0;
        /**
         * @brief 기본값 요소를 추가합니다
         */
        virtual void addElementDefault( void* pContainer ) const = 0;
        virtual void reserve( void*, size_t ) const {}

        /**
         * @brief 이미 들어 있는 원소를 **제자리에서 고쳐도 되는지** 반환합니다.
         * @details 연관 컨테이너는 원소가 곧 정렬 키라 안 됩니다. 고치는 순간 트리가 정렬을 잃습니다.
         *          역직렬화는 `appendElement` 로 이 문제를 피하지만, **인스펙터처럼 이미 들어 있는
         *          원소를 편집하는 UI** 는 먼저 이것을 물어야 합니다.
         */
        virtual bool allowsInPlaceElementWrite() const { return true; }

        /** @brief @p index 번째 원소를 지웁니다. 범위 밖이거나 지울 수 없는 컨테이너(고정 배열)면 false 입니다. */
        virtual bool eraseAt( void* pContainer, size_t index ) const = 0;

        /**
         * @brief @p index 번째 원소를 @p fill 로 고칩니다. 범위 밖이거나 @p fill 이 false 면 false 이고 컨테이너는 그대로입니다.
         * @details 기본은 제자리 쓰기입니다. 원소가 곧 정렬 · 해시 키인 컨테이너(`set`)는 원소를 꺼내 고친 뒤 **지우고 다시 넣습니다** — 고친 값의
         *          자리는 컨테이너가 정하므로 원소의 순번이 바뀔 수 있고, 같은 값이 이미 있으면 하나로 합쳐집니다.
         */
        virtual bool replaceElement( void* pContainer, size_t index, const ElementFillDelegate& fill ) const
        {
            if ( getSize( pContainer ) <= index )
                return false;
            return fill( getElement( pContainer, index ) );
        }

        /**
         * @brief 읽은 순서로 @p elementIndex 번째 원소 하나를 컨테이너에 넣습니다. 읽기는 @p fill 이, 넣는 방법은 컨테이너가 정합니다.
         *
         * @details 기본 구현은 자리를 먼저 만들고(`addElementDefault`) 그 자리에 제자리로 씁니다(연속 · 노드 컨테이너).
         *          연관 컨테이너(`set`)는 원소가 곧 정렬 키라 트리에 들어간 원소를 고치면 정렬 불변식이 깨지므로 **다 읽은 뒤
         *          insert** 하도록 재정의하고, 고정 배열은 자라지 않으므로 **@p elementIndex 칸을 채우도록** 재정의합니다.
         *          새 컨테이너 래퍼를 더할 때 "내 컨테이너에 원소를 어떻게 넣는가" 만 답하면 됩니다.
         * @param elementIndex 이번 읽기에서 이 원소의 순번(0 부터). 부르는 쪽은 비운(`clear`) 뒤 0 부터 하나씩 늘려 부릅니다.
         */
        virtual bool appendElement( void* pContainer, size_t elementIndex, const ElementFillDelegate& fill ) const
        {
            (void)elementIndex;
            addElementDefault( pContainer );

            const size_t elementCount = getSize( pContainer );
            if ( elementCount == 0 )
                return false; // 자리를 만들지 못했다. 읽어 넣을 곳이 없다.

            return fill( getElement( pContainer, elementCount - 1 ) );
        }
    };

    using MapForEachDelegate = Delegate<void( const void* pKey, const void* pVal )>;
    /** @brief 맵 항목마다 부르는 콜백입니다. 키는 정렬 · 해시 키라 const, 값은 고쳐 써도 됩니다. */
    using MapForEachMutableDelegate = Delegate<void( const void* pKey, void* pVal )>;

    /// @brief 키-값 맵 컨테이너 래퍼
    struct IMapContainerWrapper : IContainerWrapper
    {
        ContainerKind         getKind() const override { return ContainerKind::Map; }
        IMapContainerWrapper* asMap() override { return this; }

        /**
         * @brief 항목마다 callback 을 부릅니다
         */
        virtual void forEach( const void* pContainer, const MapForEachDelegate& callback ) const = 0;
        /** @brief 항목마다 callback 을 부르며 값을 고쳐 쓸 수 있게 줍니다(인스펙터의 맵 값 편집). 콜백 안에서 항목을 더하거나 지우지 않습니다. */
        virtual void forEachMutable( void* pContainer, const MapForEachMutableDelegate& callback ) const = 0;
        /** @brief 순회 순서로 @p ordinal 번째 항목을 지웁니다. 범위 밖이면 false 입니다. */
        virtual bool eraseAt( void* pContainer, size_t ordinal ) const = 0;
        /**
         * @brief 키-값을 삽입합니다
         */
        virtual void insertKeyValue( void* pContainer, const void* pKey, const void* pVal ) const = 0;

        /** @brief 키 타입의 바이트 크기입니다. */
        virtual size_t getKeySize() const = 0;
        /** @brief 값 타입의 바이트 크기입니다. */
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
        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
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

        /** @brief @p index 번째 원소를 지웁니다. */
        bool eraseAt( void* pContainer, size_t index ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            if ( pContainerTyped->size() <= index )
                return false;
            auto it = pContainerTyped->begin();
            for ( size_t step = 0; step < index; ++step )
            {
                ++it;
            }
            pContainerTyped->erase( it );
            return true;
        }
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
            auto        it              = pContainerTyped->begin();
            std::advance( it, index );
            return &( *it );
        }

        /** @brief 인덱스 원소 const 포인터. */
        const void* getElementConst( const void* pContainer, size_t index ) const override
        {
            const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
            auto              it              = pContainerTyped->begin();
            std::advance( it, index );
            return &( *it );
        }

        /** @brief 비웁니다. */
        void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
        void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
        void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
        /** @brief 기본 원소를 뒤에 추가합니다. */
        void addElementDefault( void* pContainer ) const override
        {
            using ElementType = typename TContainer::value_type;
            static_cast<TContainer*>( pContainer )->emplace_back( ElementType{} );
        }

        /** @brief @p index 번째 원소를 지웁니다. */
        bool eraseAt( void* pContainer, size_t index ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            if ( pContainerTyped->size() <= index )
                return false;
            auto it = pContainerTyped->begin();
            for ( size_t step = 0; step < index; ++step )
            {
                ++it;
            }
            pContainerTyped->erase( it );
            return true;
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
        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
        void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
        void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
        /** @brief 기본 원소를 뒤에 추가합니다. */
        void addElementDefault( void* pContainer ) const override
        {
            using ElementType = typename TContainer::value_type;
            static_cast<TContainer*>( pContainer )->emplace_back( ElementType{} );
        }

        /** @brief @p index 번째 원소를 지웁니다. */
        bool eraseAt( void* pContainer, size_t index ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            if ( pContainerTyped->size() <= index )
                return false;
            auto it = pContainerTyped->begin();
            for ( size_t step = 0; step < index; ++step )
            {
                ++it;
            }
            pContainerTyped->erase( it );
            return true;
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
            auto        it              = pContainerTyped->begin();
            std::advance( it, index );
            return const_cast<void*>( static_cast<const void*>( &( *it ) ) );
        }

        /** @brief 인덱스 원소 const 포인터. */
        const void* getElementConst( const void* pContainer, size_t index ) const override
        {
            const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
            auto              it              = pContainerTyped->begin();
            std::advance( it, index );
            return &( *it );
        }

        /** @brief 비웁니다. */
        void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
        void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
        void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
        /** @brief 기본 원소를 뒤에 추가합니다. */
        void addElementDefault( void* pContainer ) const override
        {
            using ElementType = typename TContainer::value_type;
            static_cast<TContainer*>( pContainer )->insert( ElementType{} );
        }

        /** @brief 원소가 곧 정렬 키라 제자리에서 고칠 수 없습니다. */
        bool allowsInPlaceElementWrite() const override { return false; }

        /** @brief @p index 번째 원소를 지웁니다(순회 순서의 번호). */
        bool eraseAt( void* pContainer, size_t index ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            if ( pContainerTyped->size() <= index )
                return false;
            auto it = pContainerTyped->begin();
            for ( size_t step = 0; step < index; ++step )
            {
                ++it;
            }
            const typename TContainer::value_type key = *it;
            pContainerTyped->erase( key );
            return true;
        }

        /** @brief 원소를 꺼내 @p fill 로 고친 뒤 지우고 다시 넣습니다(`ISequenceContainerWrapper::replaceElement`). */
        bool replaceElement( void* pContainer, size_t index, const ElementFillDelegate& fill ) const override
        {
            using ElementType           = typename TContainer::value_type;
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            if ( pContainerTyped->size() <= index )
                return false;
            auto it = pContainerTyped->begin();
            for ( size_t step = 0; step < index; ++step )
            {
                ++it;
            }
            const ElementType original = *it;
            ElementType       staged   = original;
            if ( fill( &staged ) == false )
                return false;
            pContainerTyped->erase( original );
            pContainerTyped->insert( std::move( staged ) );
            return true;
        }

        /**
         * @brief **다 읽은 뒤에 넣습니다.** 트리에 들어간 원소를 제자리에서 고치지 않습니다.
         * @details 기본 구현(자리를 만들고 제자리 쓰기)은 연관 컨테이너에서 틀립니다. 원소가 곧 정렬
         *          키라서, 넣은 뒤에 값을 바꾸면 트리가 정렬을 잃고 이후의 삽입 · 조회가 무너집니다.
         *          지역 변수에 읽어 `insert` 로 넘기면 컨테이너가 제자리를 스스로 정합니다.
         * @note 중복 키는 `insert` 가 조용히 버립니다. 그것이 `set` 의 계약이고, 원본에 중복이 없었다면
         *       개수도 그대로입니다.
         */
        bool appendElement( void* pContainer, size_t elementIndex, const ElementFillDelegate& fill ) const override
        {
            (void)elementIndex;
            using ElementType = typename TContainer::value_type;

            ElementType stagedElement{};
            if ( fill( &stagedElement ) == false )
                return false;

            static_cast<TContainer*>( pContainer )->insert( std::move( stagedElement ) );
            return true;
        }
    };

    /**
     * @brief unordered_set 래퍼입니다. `SetWrapper` 와 **하는 일이 같아 별칭입니다.**
     * @details 정렬 여부는 컨테이너의 성질이고, 이 래퍼가 하는 일(개수 · 비우기 · 순회 · 삽입 · 키/값 크기)은
     *          거기에 좌우되지 않습니다. 구현을 복사해 두면 한쪽에 메서드를 더할 때 다른 쪽이 조용히 뒤처지고,
     *          별칭이면 **뒤처질 수가 없습니다.**
     */
    template <typename TContainer>
    using UnorderedSetWrapper = SetWrapper<TContainer>;

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

        /** @brief 비웁니다. 고정 배열은 크기가 줄지 않으므로 할 일이 없습니다. */
        void clear( void* ) const override {}
        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
        void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
        void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
        /** @brief 고정 배열은 **자랄 수 없으므로** 아무 일도 하지 않습니다. */
        void addElementDefault( void* ) const override {}

        /**
         * @brief @p elementIndex 칸을 채웁니다. 칸이 없으면(파일의 원소가 배열보다 많다) 채우지 않고 false 입니다.
         * @details 고정 배열은 자라지 않으므로 기본 구현("뒤에 자리를 만들고 마지막 칸에 쓴다")을 물려받으면 모든 원소가 마지막 칸
         *          하나에 덮어써집니다. 파일의 원소가 칸보다 적으면 남은 칸은 그대로입니다(`clear` 가 할 일이 없다).
         */
        bool appendElement( void* pContainer, size_t elementIndex, const ElementFillDelegate& fill ) const override
        {
            if ( getSize( pContainer ) <= elementIndex )
                return false;
            return fill( getElement( pContainer, elementIndex ) );
        }

        /** @brief 고정 배열은 원소를 지울 수 없습니다. */
        bool eraseAt( void*, size_t ) const override { return false; }
    };

    template <typename TContainer>
    /// @brief map 키-값 래퍼
    struct MapWrapper : IMapContainerWrapper
    {
        using KeyType   = typename TContainer::key_type;
        using ValueType = typename TContainer::mapped_type;

        /** @brief 원소 개수. */
        size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
        /** @brief 비웁니다. */
        void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
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

        /** @brief 각 키-값에 콜백을 호출합니다. 값은 고쳐 써도 됩니다. */
        void forEachMutable( void* pContainer, const MapForEachMutableDelegate& callback ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            for ( auto& pair : *pContainerTyped )
            {
                callback( &pair.first, &pair.second );
            }
        }

        /** @brief 순회 순서로 @p ordinal 번째 항목을 지웁니다. */
        bool eraseAt( void* pContainer, size_t ordinal ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            if ( pContainerTyped->size() <= ordinal )
                return false;
            auto it = pContainerTyped->begin();
            for ( size_t step = 0; step < ordinal; ++step )
            {
                ++it;
            }
            const KeyType key = it->first;
            pContainerTyped->erase( key );
            return true;
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

    /**
     * @brief unordered_map 래퍼입니다. `MapWrapper` 와 **하는 일이 같아 별칭입니다.**
     * @details 정렬 여부는 컨테이너의 성질이고, 이 래퍼가 하는 일(개수 · 비우기 · 순회 · 삽입 · 키/값 크기)은
     *          거기에 좌우되지 않습니다. 구현을 복사해 두면 한쪽에 메서드를 더할 때 다른 쪽이 조용히 뒤처지고,
     *          별칭이면 **뒤처질 수가 없습니다.**
     */
    template <typename TContainer>
    using UnorderedMapWrapper = MapWrapper<TContainer>;

    template <typename TContainer>
    /// @brief sparse_set 키-값 래퍼
    struct SparseSetWrapper : IMapContainerWrapper
    {
        using KeyType   = typename TContainer::key_type;
        using ValueType = typename TContainer::mapped_type;

        /** @brief 원소 개수. */
        size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
        /** @brief 비웁니다. */
        void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->clear(); }
        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
        void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
        void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }
        /** @brief 각 키-값에 콜백을 호출합니다. */
        void forEach( const void* pContainer, const MapForEachDelegate& callback ) const override
        {
            const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
            for ( auto tuple : *pContainerTyped )
            {
                const KeyType&   key = std::get<0>( tuple );
                const ValueType& val = std::get<1>( tuple );
                callback( &key, &val );
            }
        }

        /** @brief 각 키-값에 콜백을 호출합니다. 값은 고쳐 써도 됩니다. */
        void forEachMutable( void* pContainer, const MapForEachMutableDelegate& callback ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            for ( auto tuple : *pContainerTyped )
            {
                const KeyType key = std::get<0>( tuple );
                ValueType&    val = std::get<1>( tuple );
                callback( &key, &val );
            }
        }

        /** @brief 순회 순서로 @p ordinal 번째 항목을 지웁니다. */
        bool eraseAt( void* pContainer, size_t ordinal ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            if ( pContainerTyped->size() <= ordinal )
                return false;
            auto it = pContainerTyped->begin();
            for ( size_t step = 0; step < ordinal; ++step )
            {
                ++it;
            }
            const KeyType key = std::get<0>( *it );
            pContainerTyped->erase( key );
            return true;
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
