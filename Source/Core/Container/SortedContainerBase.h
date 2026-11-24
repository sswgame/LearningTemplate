/**
 * @file SortedContainerBase.h
 * @brief 키 순서로 도는 컨테이너(`flat_map` · `flat_set`)의 공통 몸통입니다 — 공개 API · 레이스 검사 · 이터레이터 세대 검사.
 * @details 저장은 **여기 한 자리** 에서 구성(`SW_ENABLE_STL_CONTAINER`)으로 고른다: 기본은 정렬 벡터(`Native/NativeSortedStorage.h`), 켜면 std::map · std::set 을 멤버로 든 `Std/StdSortedStorage.h`.
 *
 *          공통 계약은 두 저장 가운데 **약한 쪽** 이다: 원소 주소 · 이터레이터는 삽입 · 삭제 · 비우기 · 대입 · `reserve` 뒤 무효이고, 삽입 · 삭제는 O(n) 으로 본다(조회는 O(log n)).
 *          이터레이터는 양방향이다(정렬 벡터의 임의 접근 · 포인터 산술을 쓰면 std 저장에서 컴파일되지 않으므로 아예 주지 않는다).
 *          무효화는 검사 구성에서 세대로 잡는다(`DenseHashTable` 과 같은 장치): 변경마다 세대를 올리고 이터레이터는 만들 때의 세대와 다르면 역참조 · 전진 · 비교에서 단언한다.
 *          배포 구성은 칸도 검사도 없어 저장의 반복자 그대로다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

#if defined( SW_ENABLE_STL_CONTAINER )
    #include "Core/Container/Std/StdSortedStorage.h"
#else
    #include "Core/Container/Native/NativeSortedStorage.h"
#endif

namespace sw::internal
{
    /**
     * @brief 키가 겹치지 않는 정렬 컨테이너입니다.
     * @tparam T       값 형식. void 면 키만 든 집합입니다(원소를 바꿀 수 없다).
     * @tparam Compare 키끼리 견주는 엄격 약순서.
     */
    template <typename Key, typename T, typename Compare, typename Allocator>
    class SortedContainerBase
    {
    protected:
        using Storage = SortedStorage<Key, T, Compare, Allocator>;

    public:
        /** @brief 키 순서로 도는 양방향 이터레이터입니다. */
        template <bool kConst>
        class Iterator
        {
            using StorageIterator = std::conditional_t<kConst, typename Storage::const_iterator, typename Storage::iterator>;

        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type        = typename Storage::value_type;
            using difference_type   = std::ptrdiff_t;
            using reference         = std::conditional_t<kConst || std::is_void_v<T>, const value_type&, value_type&>;
            using pointer           = std::remove_reference_t<reference>*;

            Iterator() = default;
            /** @brief iterator → const_iterator 변환입니다. */
            template <bool kOtherConst, typename = std::enable_if_t<kConst && kOtherConst == false>>
            Iterator( const Iterator<kOtherConst>& other )
                : _it{ other._it }
#if defined( SW_DO_CHECKS )
                , _pOwner{ other._pOwner }
                , _generation{ other._generation }
#endif
            {
            }

            reference operator*() const
            {
                assertGeneration();
                return *_it;
            }
            pointer operator->() const
            {
                assertGeneration();
                return std::addressof( *_it );
            }
            Iterator& operator++()
            {
                assertGeneration();
                ++_it;
                return *this;
            }
            Iterator operator++( int )
            {
                Iterator previous = *this;
                ++*this;
                return previous;
            }
            Iterator& operator--()
            {
                assertGeneration();
                --_it;
                return *this;
            }
            Iterator operator--( int )
            {
                Iterator previous = *this;
                --*this;
                return previous;
            }

            template <bool kOtherConst>
            bool operator==( const Iterator<kOtherConst>& other ) const
            {
                assertGeneration(); // 낡은 `end()` 와 견주면 정렬 벡터에서는 놓아 준 버퍼의 주소를 견준다
                other.assertGeneration();
                return _it == other._it;
            }
            template <bool kOtherConst>
            bool operator!=( const Iterator<kOtherConst>& other ) const
            {
                return ( *this == other ) == false;
            }

        private:
            Iterator( [[maybe_unused]] const SortedContainerBase* pOwner, StorageIterator it )
                : _it{ it }
#if defined( SW_DO_CHECKS )
                , _pOwner{ pOwner }
                , _generation{ pOwner->_iteratorGeneration }
#endif
            {
            }

            void assertGeneration() const noexcept
            {
#if defined( SW_DO_CHECKS )
                SW_LOW_LEVEL_ASSERT( _pOwner == nullptr || _pOwner->_iteratorGeneration == _generation,
                                     "sorted container iterator used after insert/erase/clear/assign moved elements - take the iterator again (erase(it) returns the next one)" );
#endif
            }

        private:
            template <bool>
            friend class Iterator;
            friend class SortedContainerBase;

            StorageIterator _it{};
#if defined( SW_DO_CHECKS )
            const SortedContainerBase* _pOwner     = nullptr;
            uint32                     _generation = 0; ///< 만들 때의 컨테이너 세대
#endif
        };

        using value_type             = typename Storage::value_type;
        using insert_type            = typename Storage::insert_type; ///< 넣을 때 받는 형식(std 저장의 원소는 키가 const 인 std::pair 다)
        using size_type              = size_t;
        using difference_type        = std::ptrdiff_t;
        using key_compare            = Compare;
        using allocator_type         = Allocator;
        using reference              = value_type&;
        using const_reference        = const value_type&;
        using pointer                = value_type*;
        using const_pointer          = const value_type*;
        using iterator               = Iterator<false>;
        using const_iterator         = Iterator<true>;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    public:
        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입 — 레이스 컨텍스트 · 세대는 공유하지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 빈 컨테이너로 둡니다. */
        SortedContainerBase()
            : _storage( Compare(), Allocator() ) {}

        /** @brief 비교자와 할당자를 지정합니다. */
        explicit SortedContainerBase( const Compare& comp, const Allocator& alloc = Allocator() )
            : _storage( comp, alloc ) {}

        /** @brief 지정한 할당자로 빈 컨테이너를 만듭니다. */
        explicit SortedContainerBase( const Allocator& alloc )
            : _storage( Compare(), alloc ) {}

        /** @brief [first, last) 를 넣고 정렬합니다. */
        template <class InputIt, typename = std::enable_if_t<kIsInputIterator<InputIt>>>
        SortedContainerBase( InputIt first, InputIt last, const Compare& comp = Compare(), const Allocator& alloc = Allocator() )
            : _storage( comp, alloc ) { _storage.insertRange( first, last ); }

        /** @brief 초기화 리스트를 넣고 정렬합니다. */
        SortedContainerBase( std::initializer_list<insert_type> init, const Compare& comp = Compare(), const Allocator& alloc = Allocator() )
            : _storage( comp, alloc ) { _storage.insertRange( init.begin(), init.end() ); }

        /** @brief 복사 생성합니다. */
        SortedContainerBase( const SortedContainerBase& other )
            : _storage( other._storage ) {}

        /** @brief 이동 생성합니다. */
        SortedContainerBase( SortedContainerBase&& other ) noexcept
            : _storage( std::move( other._storage ) )
        {
            other.bumpIteratorGeneration();
        }

        SortedContainerBase& operator=( const SortedContainerBase& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                bumpIteratorGeneration();
                _storage = other._storage;
            }
            return *this;
        }

        SortedContainerBase& operator=( SortedContainerBase&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                bumpIteratorGeneration();
                other.bumpIteratorGeneration();
                _storage = std::move( other._storage );
            }
            return *this;
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — O(log n) 검색 · 이터레이터 · 크기
        // ------------------------------------------------------------------------------
        allocator_type get_allocator() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.get_allocator();
        }

        iterator begin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, _storage.begin() );
        }

        const_iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, _storage.begin() );
        }

        const_iterator cbegin() const noexcept { return begin(); }

        iterator end() noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, _storage.end() );
        }

        const_iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, _storage.end() );
        }

        const_iterator         cend() const noexcept { return end(); }
        reverse_iterator       rbegin() noexcept { return reverse_iterator( end() ); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator( end() ); }
        const_reverse_iterator crbegin() const noexcept { return rbegin(); }
        reverse_iterator       rend() noexcept { return reverse_iterator( begin() ); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator( begin() ); }
        const_reverse_iterator crend() const noexcept { return rend(); }

        bool empty() const noexcept { return size() == 0; }

        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.size();
        }

        size_type max_size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.max_size();
        }

        /** @brief 정렬 벡터의 용량을 미리 잡습니다(std 저장은 노드라 아무것도 하지 않는다). */
        void reserve( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            bumpIteratorGeneration();
            _storage.reserve( count );
        }

        size_type capacity() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _storage.capacity();
        }

        /** @brief 키와 같은 원소의 개수(0 또는 1)를 반환합니다. */
        template <typename K>
        size_type count( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return _storage.find( key ) != _storage.end() ? 1 : 0;
        }

        template <typename K>
        iterator find( const K& key )
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, _storage.find( key ) );
        }

        template <typename K>
        const_iterator find( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, _storage.find( key ) );
        }

        template <typename K>
        bool contains( const K& key ) const { return count( key ) > 0; }

        /** @brief 키와 같은 원소의 범위를 반환합니다(equal_range). */
        template <typename K>
        pair<iterator, iterator> equal_range( const K& key )
        {
            SW_SCOPED_RACE_READ();
            return { iterator( this, _storage.lower_bound( key ) ), iterator( this, _storage.upper_bound( key ) ) };
        }

        /** @brief 키와 같은 원소의 범위를 반환합니다(equal_range). */
        template <typename K>
        pair<const_iterator, const_iterator> equal_range( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return { const_iterator( this, _storage.lower_bound( key ) ), const_iterator( this, _storage.upper_bound( key ) ) };
        }

        /** @brief 키보다 작지 않은 첫 원소를 가리키는 이터레이터를 반환합니다(lower_bound). */
        template <typename K>
        iterator lower_bound( const K& key )
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, _storage.lower_bound( key ) );
        }

        /** @brief 키보다 작지 않은 첫 원소를 가리키는 이터레이터를 반환합니다(lower_bound). */
        template <typename K>
        const_iterator lower_bound( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, _storage.lower_bound( key ) );
        }

        /** @brief 키보다 큰 첫 원소를 가리키는 이터레이터를 반환합니다(upper_bound). */
        template <typename K>
        iterator upper_bound( const K& key )
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, _storage.upper_bound( key ) );
        }

        /** @brief 키보다 큰 첫 원소를 가리키는 이터레이터를 반환합니다(upper_bound). */
        template <typename K>
        const_iterator upper_bound( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, _storage.upper_bound( key ) );
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — 정렬을 유지하며 insert/erase. 뒤에는 이터레이터 · 원소 주소가 무효다
        // ------------------------------------------------------------------------------
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            bumpIteratorGeneration();
            _storage.clear();
        }

        pair<iterator, bool> insert( const insert_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            return wrapInserted( _storage.insert( value ) );
        }

        pair<iterator, bool> insert( insert_type&& value )
        {
            SW_SCOPED_RACE_WRITE();
            return wrapInserted( _storage.insert( std::move( value ) ) );
        }

        iterator insert( [[maybe_unused]] const_iterator hint, const insert_type& value ) { return insert( value ).first; }

        iterator insert( [[maybe_unused]] const_iterator hint, insert_type&& value ) { return insert( std::move( value ) ).first; }

        /** @brief [first, last) 를 넣습니다. 키가 이미 있거나 범위 안에서 겹치면 먼저 온 것(있던 원소, 그다음 입력 순서) 하나만 남습니다. */
        template <class InputIt, typename = std::enable_if_t<kIsInputIterator<InputIt>>>
        void insert( InputIt first, InputIt last )
        {
            SW_SCOPED_RACE_WRITE();
            bumpIteratorGeneration();
            _storage.insertRange( first, last );
        }

        void insert( std::initializer_list<insert_type> ilist ) { insert( ilist.begin(), ilist.end() ); }

        template <class... Args>
        pair<iterator, bool> emplace( Args&&... args )
        {
            insert_type value( std::forward<Args>( args )... );
            return insert( std::move( value ) );
        }

        /** @brief 원소를 만들어 삽입합니다. 힌트는 쓰지 않습니다. */
        template <class... Args>
        iterator emplace_hint( [[maybe_unused]] const_iterator hint, Args&&... args ) { return emplace( std::forward<Args>( args )... ).first; }

        iterator erase( iterator pos ) { return erase( const_iterator( pos ) ); }

        iterator erase( const_iterator pos )
        {
            SW_SCOPED_RACE_WRITE();
            pos.assertGeneration();
            bumpIteratorGeneration();
            return iterator( this, _storage.erase( pos._it ) );
        }

        iterator erase( const_iterator first, const_iterator last )
        {
            SW_SCOPED_RACE_WRITE();
            first.assertGeneration();
            last.assertGeneration();
            bumpIteratorGeneration();
            return iterator( this, _storage.erase( first._it, last._it ) );
        }

        /** @brief 키와 같은 원소를 제거하고, 제거한 개수(0 또는 1)를 반환합니다. */
        template <typename K, typename = std::enable_if_t<std::is_convertible_v<const K&, const_iterator> == false>>
        size_type erase( const K& key )
        {
            SW_SCOPED_RACE_WRITE();
            const typename Storage::iterator it = _storage.find( key );
            if ( it == _storage.end() )
                return 0;
            bumpIteratorGeneration();
            _storage.erase( it );
            return 1;
        }

        void swap( SortedContainerBase& other ) noexcept
        {
            SW_SCOPED_RACE_WRITE();
            SW_SCOPED_RACE_WRITE_OTHER( other );
            bumpIteratorGeneration();
            other.bumpIteratorGeneration();
            _storage.swap( other._storage );
        }

    protected:
        /** @brief 내용을 @p ilist 로 바꿉니다 — 파생 형식의 `operator=( initializer_list )` 가 자기 형식을 돌려주며 부릅니다. */
        void assign( std::initializer_list<insert_type> ilist )
        {
            SW_SCOPED_RACE_WRITE();
            clear();
            insert( ilist.begin(), ilist.end() );
        }

        /** @brief 저장의 삽입 결과를 이터레이터로 감쌉니다. 새로 넣었으면 세대를 올립니다. */
        pair<iterator, bool> wrapInserted( const pair<typename Storage::iterator, bool>& inserted )
        {
            if ( inserted.second )
                bumpIteratorGeneration();
            return { iterator( this, inserted.first ), inserted.second };
        }

#if defined( SW_DO_CHECKS )
        void bumpIteratorGeneration() noexcept { ++_iteratorGeneration; }
#else
        static void bumpIteratorGeneration() noexcept {}
#endif

    protected:
        Storage _storage;
#if defined( SW_DO_CHECKS )
        uint32 _iteratorGeneration{ 0 }; ///< 원소 자리를 옮기는 변경마다 오르는 세대(검사 구성만 — 배포 sizeof 는 그대로)
#endif
        SW_RACE_CTX_MEMBER
    };
} // namespace sw::internal
