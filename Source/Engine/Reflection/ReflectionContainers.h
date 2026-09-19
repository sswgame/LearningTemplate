/**
 * @file ReflectionContainers.h
 * @brief 리플렉션 컨테이너 래퍼 (시퀀스 / 맵)
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{

    /// @brief Container Kind — 목록은 PredefinedContainerKind.xxx
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
        virtual IMapContainerWrapper*      asMap() { return nullptr; }
    };

    /**
     * @brief 원소 하나를 채우는 콜백 — 인자는 **채울 원소의 주소**입니다. 실패하면 false.
     * @details 읽는 방법(바이너리 스트림 · JSON 값 · XML 노드)은 호출자가 알고, **그 값을 컨테이너에
     *          넣는 방법**은 컨테이너가 안다. 그 둘을 나누려고 둔다 (`appendElement` 참고).
     */
    using ElementFillDelegate = Delegate<bool( void* pElement )>;

    /// @brief 인덱스 시퀀스 컨테이너 래퍼
    struct ISequenceContainerWrapper : IContainerWrapper
    {
        ContainerKind              getKind() const override { return ContainerKind::Sequence; }
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

        /**
         * @brief 이미 들어 있는 원소를 **제자리에서 고쳐도 되는가.**
         * @details 연관 컨테이너는 원소가 곧 정렬 키라 안 된다 — 고치는 순간 트리가 정렬을 잃는다.
         *          역직렬화는 `appendElement` 로 이 문제를 피하지만, **인스펙터처럼 이미 들어 있는
         *          원소를 편집하는 UI** 는 먼저 이것을 물어야 한다.
         */
        virtual bool allowsInPlaceElementWrite() const { return true; }

        /**
         * @brief 원소 하나를 읽어 **뒤에 넣습니다.** 읽기는 @p fill 이, 넣는 방법은 컨테이너가 정합니다.
         *
         * @details 역직렬화는 오랫동안 "자리를 먼저 만들고(`addElementDefault`) 그 자리에 제자리로
         *          쓴다(`getElement`)" 는 한 가지 방법만 알았다. **연관 컨테이너에서는 그것이 틀렸다** —
         *          `set` 의 원소는 곧 정렬 키라, 트리에 들어간 뒤에 값을 바꾸면 정렬 불변식이 깨진다.
         *          (`SetWrapper::getElement` 가 `const_cast` 로 const 를 벗기고 있었다.) 증상은 그 자리에서
         *          나지 않고 **나중에 엉뚱한 곳에서 터진다** — 실제로 `set<int32>` 프로퍼티를 왕복시키면
         *          프로세스가 죽었다.
         *
         *          그래서 "어떻게 넣는가" 를 컨테이너에게 돌려준다. 기본 구현은 예전과 같고(연속·노드
         *          컨테이너는 제자리 쓰기가 옳다), 연관 컨테이너만 **다 읽은 뒤 insert** 하도록 재정의한다.
         *          새 컨테이너 래퍼를 더할 때 "내 컨테이너는 제자리 쓰기가 되는가" 만 답하면 된다.
         */
        virtual bool appendElement( void* pContainer, const ElementFillDelegate& fill ) const
        {
            addElementDefault( pContainer );

            const size_t elementCount = getSize( pContainer );
            if ( elementCount == 0 )
                return false; // 자리를 만들지 못했다 — 읽을 곳이 없다.

            return fill( getElement( pContainer, elementCount - 1 ) );
        }
    };

    using MapForEachDelegate = Delegate<void( const void* pKey, const void* pVal )>;

    /// @brief 키-값 맵 컨테이너 래퍼
    struct IMapContainerWrapper : IContainerWrapper
    {
        ContainerKind         getKind() const override { return ContainerKind::Map; }
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
        /** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
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

        /**
         * @brief **다 읽은 뒤에 넣습니다** — 트리에 들어간 원소를 제자리에서 고치지 않습니다.
         * @details 기본 구현(자리를 만들고 제자리 쓰기)은 연관 컨테이너에서 틀렸다. 원소가 곧 정렬
         *          키라서, 넣은 뒤에 값을 바꾸면 트리가 정렬을 잃고 이후의 삽입·조회가 무너진다.
         *          지역 변수에 읽어 `insert` 로 넘기면 컨테이너가 제자리를 스스로 정한다.
         * @note 중복 키는 `insert` 가 조용히 버린다 — 그것이 `set` 의 계약이고, 원본에 중복이 없었다면
         *       개수도 그대로다.
         */
        bool appendElement( void* pContainer, const ElementFillDelegate& fill ) const override
        {
            using ElementType = typename TContainer::value_type;

            ElementType stagedElement{};
            if ( fill( &stagedElement ) == false )
                return false;

            static_cast<TContainer*>( pContainer )->insert( std::move( stagedElement ) );
            return true;
        }
    };

    /**
     * @brief unordered_set 래퍼 — `SetWrapper` 와 **하는 일이 같아 별칭이다.**
     * @details 정렬 여부는 컨테이너의 성질이고, 이 래퍼가 하는 일(개수·비우기·순회·삽입·키/값 크기)은
     *          거기에 좌우되지 않는다. 예전에는 `SetWrapper` 를 통째로 복사해 이름만 바꾼 구현이
     *          따로 있었다 — 그러면 한쪽에 메서드를 더할 때 다른 쪽이 조용히 뒤처진다.
     *          별칭이면 **뒤처질 수가 없다.**
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
        using KeyType   = typename TContainer::key_type;
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

    /**
     * @brief unordered_map 래퍼 — `MapWrapper` 와 **하는 일이 같아 별칭이다.**
     * @details 정렬 여부는 컨테이너의 성질이고, 이 래퍼가 하는 일(개수·비우기·순회·삽입·키/값 크기)은
     *          거기에 좌우되지 않는다. 예전에는 `MapWrapper` 를 통째로 복사해 이름만 바꾼 구현이
     *          따로 있었다 — 그러면 한쪽에 메서드를 더할 때 다른 쪽이 조용히 뒤처진다.
     *          별칭이면 **뒤처질 수가 없다.**
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
        /** @brief 제로된 저장소에 빈 컨테이너를 placement-new 합니다. */
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
