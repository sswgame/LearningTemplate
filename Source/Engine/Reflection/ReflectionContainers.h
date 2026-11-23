/**
 * @file ReflectionContainers.h
 * @brief 리플렉션 컨테이너 래퍼(시퀀스와 맵)입니다.
 */
#pragma once
#include "Core/Container/EnumArray.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ContainerKind.h"

namespace sw
{

    struct IMapContainerWrapper;
    struct ISequenceContainerWrapper;

    /// @brief 시퀀스와 맵이 공통으로 쓰는 컨테이너 래퍼입니다.
    struct IContainerWrapper
    {
        /** @brief 가상 소멸자입니다. */
        virtual ~IContainerWrapper() = default;
        /** @brief Sequence 인지 Map 인지 종류를 반환합니다. */
        virtual ContainerKind getKind() const = 0;
        /** @brief 컨테이너 크기를 반환합니다. */
        virtual size_t getSize( const void* pContainer ) const = 0;
        /** @brief 컨테이너를 비웁니다. */
        virtual void clear( void* pContainer ) const = 0;

        /** @brief 0 으로 채운 저장소에 빈 컨테이너를 placement new 로 만듭니다. */
        virtual void constructEmpty( void* pContainer ) const { (void)pContainer; }
        /** @brief constructEmpty 로 만든 컨테이너를 파괴합니다. */
        virtual void destroyContainer( void* pContainer ) const { (void)pContainer; }

        // 래퍼는 상태가 없다 — 생성 코드가 서술자 표에서 가리키는 정적 `const` 객체이므로 연산은 모두 const 다.
        virtual const ISequenceContainerWrapper* asSequence() const { return nullptr; }
        virtual const IMapContainerWrapper*      asMap() const { return nullptr; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 원소 하나를 채우는 콜백입니다. 인자는 **채울 원소의 주소**입니다. 실패하면 false 를 반환합니다.
     * @details 읽는 방법(바이너리 스트림, JSON 값, XML 노드)은 부르는 쪽이 알고, **그 값을 컨테이너에
     *          넣는 방법**은 컨테이너가 안다. 이 둘을 나누려고 만든 타입이다(`appendElement` 참고).
     */
    using ElementFillDelegate = Delegate<bool( void* pElement )>;

    /// @brief 인덱스로 접근하는 시퀀스 컨테이너 래퍼입니다.
    struct ISequenceContainerWrapper : IContainerWrapper
    {
        ContainerKind                    getKind() const override { return ContainerKind::Sequence; }
        const ISequenceContainerWrapper* asSequence() const override { return this; }
        /** @brief 인덱스 위치의 원소 포인터를 반환합니다. */
        virtual void* getElement( void* pContainer, size_t index ) const = 0;
        /** @brief 인덱스 요소의 const 포인터. */
        virtual const void* getElementConst( const void* pContainer, size_t index ) const = 0;
        /** @brief 기본값 원소를 추가합니다. */
        virtual void addElementDefault( void* pContainer ) const = 0;
        virtual void reserve( void*, size_t ) const {}

        /**
         * @brief 이미 들어 있는 원소를 **제자리에서 고쳐도 되는지** 반환합니다.
         * @details 연관 컨테이너는 원소가 곧 정렬 키라 고치면 안 된다. 고치는 순간 트리가 정렬을 잃는다.
         *          역직렬화는 `appendElement` 로 이 문제를 피하지만, **인스펙터처럼 이미 들어 있는
         *          원소를 편집하는 UI** 는 먼저 이 함수로 확인해야 한다.
         */
        virtual bool allowsInPlaceElementWrite() const = 0;

        /** @brief 크기가 고정된 컨테이너(고정 배열)이면 true 입니다. 인스펙터가 추가와 비우기 버튼을 그리지 않습니다. */
        virtual bool isFixedSize() const = 0;

        /** @brief @p index 번째 원소를 지웁니다. 범위 밖이거나 지울 수 없는 컨테이너(고정 배열)이면 false 입니다. */
        virtual bool eraseAt( void* pContainer, size_t index ) const = 0;

        /**
         * @brief @p index 번째 원소를 @p fill 로 고칩니다. 범위 밖이거나 @p fill 이 false 를 반환하면 false 를 반환하고 컨테이너는 그대로입니다.
         * @details 기본은 제자리 쓰기다. 원소가 곧 정렬 키나 해시 키인 컨테이너(`set`)는 원소를 꺼내 고친 뒤 **지우고 다시 넣는다.**
         *          고친 값의 위치는 컨테이너가 정하므로 원소의 순번이 바뀔 수 있고, 같은 값이 이미 있으면 하나로 합쳐진다.
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
         * @details 기본 구현은 슬롯을 먼저 만들고(`addElementDefault`) 그 슬롯에 제자리로 쓴다(연속 컨테이너와 노드 컨테이너).
         *          연관 컨테이너(`set`)는 원소가 곧 정렬 키라 트리에 들어간 원소를 고치면 정렬 불변식이 깨지므로 **다 읽은 뒤
         *          insert** 하도록 재정의하고, 고정 배열은 자라지 않으므로 **@p elementIndex 번째 슬롯을 채우도록** 재정의한다.
         *          새 컨테이너 래퍼를 추가할 때는 "내 컨테이너에 원소를 어떻게 넣는가"만 답하면 된다.
         * @param elementIndex 이번 읽기에서 이 원소의 순번(0 부터). 부르는 쪽은 컨테이너를 비운(`clear`) 뒤 0 부터 하나씩 늘려 부릅니다.
         */
        virtual bool appendElement( void* pContainer, size_t elementIndex, const ElementFillDelegate& fill ) const
        {
            (void)elementIndex;
            addElementDefault( pContainer );

            const size_t elementCount = getSize( pContainer );
            if ( elementCount == 0 )
                return false; // 슬롯을 만들지 못했다. 읽어 넣을 곳이 없다.

            return fill( getElement( pContainer, elementCount - 1 ) );
        }
    };
} // namespace sw

namespace sw
{
    using MapForEachDelegate = Delegate<void( const void* pKey, const void* pVal )>;
    /** @brief 맵 항목마다 부르는 콜백입니다. 키는 정렬 키나 해시 키이므로 const 이고, 값은 고쳐 써도 됩니다. */
    using MapForEachMutableDelegate = Delegate<void( const void* pKey, void* pVal )>;

    /// @brief 키-값 맵 컨테이너 래퍼입니다.
    struct IMapContainerWrapper : IContainerWrapper
    {
        ContainerKind               getKind() const override { return ContainerKind::Map; }
        const IMapContainerWrapper* asMap() const override { return this; }

        /** @brief 항목마다 callback 을 부릅니다. */
        virtual void forEach( const void* pContainer, const MapForEachDelegate& callback ) const = 0;
        /** @brief 항목마다 callback 을 부르며 값을 고쳐 쓸 수 있게 넘깁니다(인스펙터의 맵 값 편집). 콜백 안에서 항목을 추가하거나 지우면 안 됩니다. */
        virtual void forEachMutable( void* pContainer, const MapForEachMutableDelegate& callback ) const = 0;
        /** @brief 순회 순서로 @p ordinal 번째 항목을 지웁니다. 범위 밖이면 false 입니다. */
        virtual bool eraseAt( void* pContainer, size_t ordinal ) const = 0;
        /** @brief 키-값을 삽입합니다. */
        virtual void insertKeyValue( void* pContainer, const void* pKey, const void* pVal ) const = 0;
        /** @brief 키 집합이 타입으로 정해진 맵(`EnumArray` — 열거자마다 한 칸)이면 true 입니다. 인스펙터가 항목 추가 · 지우기를 그리지 않습니다. */
        virtual bool isFixedSize() const = 0;

        /** @brief 키 타입의 바이트 크기입니다. */
        virtual size_t getKeySize() const = 0;
        /** @brief 값 타입의 바이트 크기입니다. */
        virtual size_t getValueSize() const = 0;
        /** @brief 키를 기본 생성합니다. */
        virtual void defaultConstructKey( void* pPtr ) const = 0;
        /** @brief 값을 기본 생성합니다. */
        virtual void defaultConstructValue( void* pPtr ) const = 0;
        /** @brief 키를 파괴합니다. */
        virtual void destroyKey( void* pPtr ) const = 0;
        /** @brief 값을 파괴합니다. */
        virtual void destroyValue( void* pPtr ) const = 0;
        /**
         * @brief @p pSrc 의 값을 @p pDst(만들어 둔 값)에 복사합니다. 복사할 수 없는 래퍼이면 false 를 반환합니다.
         * @details 키를 바꿔 다시 넣어야 할 때(로드 배치가 맵 키의 저장 id 를 이번 실행의 id 로 바꾸는 경우) 값을 잠시 밖에 보관하는 데 쓴다.
         */
        [[nodiscard]] virtual bool copyValue( void* pDst, const void* pSrc ) const
        {
            (void)pDst;
            (void)pSrc;
            return false;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 시퀀스 래퍼의 공통 기반입니다(CRTP). 모양을 묻는 가상 함수를 `final` 로 두고 파생 형식의 정적 값(`kInPlaceElementWrite` · `kFixedSize`)을 돌려준다.
     * @details 런타임 답(가상 함수)과 컴파일 답(정적 값 — 표와 대조하는 `ContainerWrapperOf::matchesTable`)이 한 값이라 어긋날 수 없다. 파생은 다른 값일 때만 가린다.
     */
    template <typename TDerived>
    struct SequenceContainerWrapperBase : ISequenceContainerWrapper
    {
        static constexpr bool kInPlaceElementWrite = true;
        static constexpr bool kFixedSize           = false;

        bool allowsInPlaceElementWrite() const final { return TDerived::kInPlaceElementWrite; }
        bool isFixedSize() const final { return TDerived::kFixedSize; }
    };

    /** @brief 맵 래퍼의 공통 기반입니다(CRTP) — `isFixedSize` 는 파생의 `kFixedSize` 다. 맵은 원소 차례가 정해지지 않아 제자리 쓰기 시퀀스가 아니다. */
    template <typename TDerived>
    struct MapContainerWrapperBase : IMapContainerWrapper
    {
        static constexpr bool kInPlaceElementWrite = false;
        static constexpr bool kFixedSize           = false;

        bool isFixedSize() const final { return TDerived::kFixedSize; }
    };

    template <typename TContainer>
    /// @brief vector 시퀀스 래퍼입니다. 원소 주소를 `&container[index]` 로 잡습니다. 비트로 묶는 `vector<bool>` 은 `VectorElementCheck` 가 막아 이 주소는 늘 원소 자체입니다.
    struct VectorWrapper : SequenceContainerWrapperBase<VectorWrapper<TContainer>>
    {
        /** @brief 원소 개수를 반환합니다. */
        size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
        /** @brief 인덱스 위치의 원소 포인터를 반환합니다. */
        void* getElement( void* pContainer, size_t index ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            return &( ( *pContainerTyped )[index] );
        }

        /** @brief 인덱스 위치의 원소 const 포인터를 반환합니다. */
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
} // namespace sw

namespace sw
{
    template <typename TContainer>
    /// @brief deque 시퀀스 래퍼입니다.
    struct DequeWrapper : SequenceContainerWrapperBase<DequeWrapper<TContainer>>
    {
        /** @brief 원소 개수를 반환합니다. */
        size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
        /** @brief 인덱스 위치의 원소 포인터를 반환합니다. */
        void* getElement( void* pContainer, size_t index ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            return &( ( *pContainerTyped )[index] );
        }

        /** @brief 인덱스 위치의 원소 const 포인터를 반환합니다. */
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
            pContainerTyped->erase( pContainerTyped->begin() + static_cast<std::ptrdiff_t>( index ) ); // 링 deque 는 임의 접근이다
            return true;
        }
    };
} // namespace sw

namespace sw
{
    template <typename TContainer>
    /// @brief set 시퀀스 래퍼입니다. 인덱스 순서로 순회합니다.
    struct SetWrapper : SequenceContainerWrapperBase<SetWrapper<TContainer>>
    {
        /** @brief 원소 개수를 반환합니다. */
        size_t getSize( const void* pContainer ) const override { return static_cast<const TContainer*>( pContainer )->size(); }
        /** @brief 인덱스 위치의 원소 포인터를 반환합니다. */
        void* getElement( void* pContainer, size_t index ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            auto        it              = pContainerTyped->begin();
            std::advance( it, index );
            return const_cast<void*>( static_cast<const void*>( &( *it ) ) );
        }

        /** @brief 인덱스 위치의 원소 const 포인터를 반환합니다. */
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
        static constexpr bool kInPlaceElementWrite = false;

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
         * @details 기본 구현(슬롯을 만들고 제자리에 쓰기)은 연관 컨테이너에서 틀린다. 원소가 곧 정렬
         *          키라서, 넣은 뒤에 값을 바꾸면 트리가 정렬을 잃고 이후의 삽입과 조회가 무너진다.
         *          지역 변수에 읽어 `insert` 로 넘기면 컨테이너가 위치를 스스로 정한다.
         * @note 중복 키는 `insert` 가 조용히 버린다. 그것이 `set` 의 계약이고, 원본에 중복이 없었다면
         *       개수도 그대로다.
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
} // namespace sw

namespace sw
{
    /**
     * @brief unordered_set 래퍼입니다. `SetWrapper` 와 **하는 일이 같아 별칭으로 둡니다.**
     * @details 정렬 여부는 컨테이너의 성질이고, 이 래퍼가 하는 일(개수, 비우기, 순회, 삽입, 키와 값 크기)은
     *          거기에 좌우되지 않는다. 구현을 복사해 두면 한쪽에 메서드를 추가할 때 다른 쪽이 조용히 뒤처지고,
     *          별칭이면 **뒤처질 수가 없다.**
     */
    template <typename TContainer>
    using UnorderedSetWrapper = SetWrapper<TContainer>;

    template <typename TContainer>
    /// @brief 고정 배열 시퀀스 래퍼입니다. add 와 clear 가 없고, `std::array` 와 C 배열(`T[N]`)을 모두 받습니다.
    struct ArrayWrapper : SequenceContainerWrapperBase<ArrayWrapper<TContainer>>
    {
        /** @brief 원소 개수를 반환합니다. */
        size_t getSize( const void* pContainer ) const override { return std::size( *static_cast<const TContainer*>( pContainer ) ); }
        /** @brief 자라지도 줄지도 않습니다. 인스펙터가 추가와 비우기 버튼을 그리지 않습니다. */
        static constexpr bool kFixedSize = true;
        /** @brief 인덱스 위치의 원소 포인터를 반환합니다. */
        void* getElement( void* pContainer, size_t index ) const override
        {
            TContainer* pContainerTyped = static_cast<TContainer*>( pContainer );
            return &( ( *pContainerTyped )[index] );
        }

        /** @brief 인덱스 위치의 원소 const 포인터를 반환합니다. */
        const void* getElementConst( const void* pContainer, size_t index ) const override
        {
            const TContainer* pContainerTyped = static_cast<const TContainer*>( pContainer );
            return &( ( *pContainerTyped )[index] );
        }

        /** @brief 비웁니다. 고정 배열은 크기가 줄지 않으므로 할 일이 없습니다. */
        void clear( void* ) const override {}
        /**
         * @brief 0 으로 채운 저장소에 빈 컨테이너를 만듭니다.
         * @details C 배열에는 배열 placement new 를 쓰지 않는다. 구현이 앞에 원소 개수를 둘 수 있어 주소가 어긋난다. 원소마다 값 초기화한다.
         */
        void constructEmpty( void* pContainer ) const override
        {
            if constexpr ( std::is_array_v<TContainer> )
            {
                TContainer& arr = *static_cast<TContainer*>( pContainer );
                std::uninitialized_value_construct( std::begin( arr ), std::end( arr ) );
            }
            else
                sw_placement_new( pContainer ) TContainer{};
        }

        void destroyContainer( void* pContainer ) const override
        {
            if constexpr ( std::is_array_v<TContainer> )
            {
                TContainer& arr = *static_cast<TContainer*>( pContainer );
                std::destroy( std::begin( arr ), std::end( arr ) );
            }
            else
                static_cast<TContainer*>( pContainer )->~TContainer();
        }

        /** @brief 고정 배열은 **자랄 수 없으므로** 아무 일도 하지 않습니다. */
        void addElementDefault( void* ) const override {}

        /**
         * @brief @p elementIndex 번째 슬롯을 채웁니다. 슬롯이 없으면(파일의 원소가 배열보다 많음) 채우지 않고 false 를 반환합니다.
         * @details 고정 배열은 자라지 않으므로 기본 구현("뒤에 슬롯을 만들고 마지막 슬롯에 쓴다")을 물려받으면 모든 원소가 마지막 슬롯
         *          하나에 덮어써진다. 파일의 원소가 슬롯보다 적으면 남은 슬롯은 그대로다(`clear` 가 할 일이 없기 때문이다).
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
} // namespace sw

namespace sw
{
    template <typename TContainer>
    /// @brief map 키-값 래퍼입니다.
    struct MapWrapper : MapContainerWrapperBase<MapWrapper<TContainer>>
    {
        using KeyType   = typename TContainer::key_type;
        using ValueType = typename TContainer::mapped_type;

        /** @brief 원소 개수를 반환합니다. */
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

        /** @brief 키 타입의 바이트 크기를 반환합니다. */
        size_t getKeySize() const override { return sizeof( KeyType ); }
        /** @brief 값 타입의 바이트 크기를 반환합니다. */
        size_t getValueSize() const override { return sizeof( ValueType ); }
        /** @brief 키를 기본 생성합니다. */
        void defaultConstructKey( void* pPtr ) const override { sw_placement_new( pPtr ) KeyType{}; }
        void defaultConstructValue( void* pPtr ) const override { sw_placement_new( pPtr ) ValueType{}; }
        void destroyKey( void* pPtr ) const override { static_cast<KeyType*>( pPtr )->~KeyType(); }
        /** @brief 값을 파괴합니다. */
        void               destroyValue( void* pPtr ) const override { static_cast<ValueType*>( pPtr )->~ValueType(); }
        [[nodiscard]] bool copyValue( void* pDst, const void* pSrc ) const override
        {
            *static_cast<ValueType*>( pDst ) = *static_cast<const ValueType*>( pSrc );
            return true;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief `EnumArray<E, T>` 를 열거자 → 값 맵으로 보는 래퍼입니다. 키는 열거자마다 하나로 고정이라 늘거나 줄지 않습니다.
     * @details 텍스트는 맵과 같은 모양(`<entry key="Ban">…</entry>`)으로 열거자 이름이 키다 — 열거자 순서가 바뀌어도 값이 제 칸을 찾는다
     *          (고정 배열 꼴로 쓰면 순번이 곧 의미라 열거자를 끼워 넣으면 저장 값이 한 칸씩 밀린다). 읽기는 먼저 모든 칸을 기본값으로 되돌리고(`clear`)
     *          파일에 적힌 칸만 채운다. 언리얼은 `TEnumArray` 를 리플렉션하지 않고 C 배열 + `UENUM` 크기로 쓰는데, 그 꼴은 위의 밀림을 막지 못한다.
     */
    template <typename TContainer>
    struct EnumArrayWrapper : MapContainerWrapperBase<EnumArrayWrapper<TContainer>>
    {
        using KeyType   = typename TContainer::key_type;
        using ValueType = typename TContainer::mapped_type;

        static constexpr bool kFixedSize = true;

        size_t getSize( const void* ) const override { return TContainer::kSize; }
        /** @brief 칸을 모두 기본값으로 되돌립니다(칸 수는 그대로다). */
        void clear( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->fill( ValueType{} ); }
        void constructEmpty( void* pContainer ) const override { sw_placement_new( pContainer ) TContainer{}; }
        void destroyContainer( void* pContainer ) const override { static_cast<TContainer*>( pContainer )->~TContainer(); }

        void forEach( const void* pContainer, const MapForEachDelegate& callback ) const override
        {
            const TContainer& container = *static_cast<const TContainer*>( pContainer );
            for ( const KeyType key : EnumRange<KeyType>{} )
            {
                callback( &key, &container[key] );
            }
        }

        void forEachMutable( void* pContainer, const MapForEachMutableDelegate& callback ) const override
        {
            TContainer& container = *static_cast<TContainer*>( pContainer );
            for ( const KeyType key : EnumRange<KeyType>{} )
            {
                callback( &key, &container[key] );
            }
        }

        /** @brief 칸은 지울 수 없습니다(열거자마다 하나로 고정). */
        bool eraseAt( void*, size_t ) const override { return false; }

        /** @brief 열거자 칸에 값을 씁니다. 표식(`Count`) 이상 · 음수 같은 범위 밖 키는 버립니다(파일의 모르는 값이 남의 메모리를 쓰지 않게). */
        void insertKeyValue( void* pContainer, const void* pKey, const void* pVal ) const override
        {
            const KeyType key   = *static_cast<const KeyType*>( pKey );
            const auto    index = static_cast<std::make_unsigned_t<std::underlying_type_t<KeyType>>>( key );
            if ( static_cast<size_t>( index ) >= TContainer::kSize )
                return;
            ( *static_cast<TContainer*>( pContainer ) )[key] = *static_cast<const ValueType*>( pVal );
        }

        size_t             getKeySize() const override { return sizeof( KeyType ); }
        size_t             getValueSize() const override { return sizeof( ValueType ); }
        void               defaultConstructKey( void* pPtr ) const override { sw_placement_new( pPtr ) KeyType{}; }
        void               defaultConstructValue( void* pPtr ) const override { sw_placement_new( pPtr ) ValueType{}; }
        void               destroyKey( void* ) const override {}
        void               destroyValue( void* pPtr ) const override { static_cast<ValueType*>( pPtr )->~ValueType(); }
        [[nodiscard]] bool copyValue( void* pDst, const void* pSrc ) const override
        {
            *static_cast<ValueType*>( pDst ) = *static_cast<const ValueType*>( pSrc );
            return true;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief unordered_map 래퍼입니다. `MapWrapper` 와 **하는 일이 같아 별칭으로 둡니다.**
     * @details 정렬 여부는 컨테이너의 성질이고, 이 래퍼가 하는 일(개수, 비우기, 순회, 삽입, 키와 값 크기)은
     *          거기에 좌우되지 않는다. 구현을 복사해 두면 한쪽에 메서드를 추가할 때 다른 쪽이 조용히 뒤처지고,
     *          별칭이면 **뒤처질 수가 없다.**
     */
    template <typename TContainer>
    using UnorderedMapWrapper = MapWrapper<TContainer>;

    /**
     * @brief 래퍼 타입마다 하나인 정적 `const` 객체입니다. 생성 코드의 서술자 표(`PropertyContainerDesc::_pWrapper`)가 이 주소를 싣는다.
     * @details 래퍼는 상태가 없어 프로퍼티마다 `make_shared` 로 짓던 것을 공유한다(언리얼 `FArrayProperty` 가 내부 프로퍼티를 정적 서술자로 드는 자리).
     *          모듈(DLL)마다 하나씩 생기고 그 모듈의 수명이다 — 등록한 타입의 함수 포인터와 같다.
     */
    template <typename TWrapper>
    struct ContainerWrapperOf
    {
        static inline const TWrapper kInstance{};

        /** @brief 래퍼 형식의 종류입니다 — 기반(`ISequenceContainerWrapper` · `IMapContainerWrapper`)이 정한다. */
        static constexpr ContainerKind kKind = std::is_base_of_v<IMapContainerWrapper, TWrapper>      ? ContainerKind::Map
                                             : std::is_base_of_v<ISequenceContainerWrapper, TWrapper> ? ContainerKind::Sequence
                                                                                                      : ContainerKind::None;
        static_assert( kKind != ContainerKind::None, "a container wrapper derives from ISequenceContainerWrapper or IMapContainerWrapper" );

        /** @brief 상태 직렬화 · 상태 해시가 원소 차례대로 실을 수 있는지입니다 — 제자리에서 원소를 쓰는 시퀀스(집합 · 맵은 아니다). 표의 State 역할과 같아야 한다. */
        static constexpr bool kStateCarried = kKind == ContainerKind::Sequence && TWrapper::kInPlaceElementWrite;

        /**
         * @brief `ReflectBuiltins.xxx` CONTAINER 줄의 종류 · 역할이 이 래퍼 형식과 같은지입니다. 생성 코드가 컨테이너 칸마다 `getChecked` 로 본다.
         * @details 표가 래퍼와 다르면(종류를 Map 이라 적고 시퀀스 래퍼를 가리킴, State 를 집합에 담 …) 그 칸을 쓴 생성 코드가 컴파일되지 않는다.
         */
        static constexpr bool matchesTable( const ContainerKind kind, const bool bState, const bool bFixed )
        {
            return kind == kKind && bState == kStateCarried && bFixed == TWrapper::kFixedSize;
        }

        /** @brief 표의 종류 · 역할과 대조한 정적 래퍼의 주소입니다 — 생성 코드의 컨테이너 표가 이것으로 싣는다(어긋나면 그 칸의 생성 코드가 컴파일되지 않는다). */
        template <ContainerKind kind, bool bState, bool bFixed>
        static constexpr const IContainerWrapper* getChecked() noexcept
        {
            static_assert( matchesTable( kind, bState, bFixed ), "ReflectBuiltins.xxx: CONTAINER row (kind, State, Fixed) disagrees with the container wrapper type" );
            return &kInstance;
        }
        /** @brief 표 밖 컨테이너(`REFLECT_CONTAINER( Kind, Stem )`)의 정적 래퍼 주소입니다 — 역할 칸이 없어 종류만 대조한다. */
        template <ContainerKind kind>
        static constexpr const IContainerWrapper* getOfKind() noexcept
        {
            static_assert( kind == kKind, "REFLECT_CONTAINER kind disagrees with the container wrapper type" );
            return &kInstance;
        }
    };
} // namespace sw
