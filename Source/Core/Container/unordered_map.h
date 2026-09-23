/**
 * @file unordered_map.h
 * @brief DoD(Data-Oriented Design) 기반의 밀집 배열 해시맵(sw::unordered_map)입니다.
 *
 * [구조와 메모리 배치]
 * 1. 밀집 저장소: 모든 키-값 쌍(Node)이 연속된 `vector<Node>` 에 들어 있어, 순회할 때 L1/L2 캐시 적중률이 높습니다.
 * 2. 버킷 인덱스 표: 해시 충돌 체인을 노드 포인터 대신 8바이트 정수 인덱스(`size_t`)로 이어, 포인터를 따라가는 비용과
 *    메모리 단편화를 없앱니다.
 * 3. 레이스 탐지: 디버그 빌드에서 ScopedRaceRead / ScopedRaceWrite 로 동시 쓰기와 읽기/쓰기 충돌을 바로 잡아냅니다.
 * 4. 이종 키 조회: `string_view` 같은 이종 키로 찾을 때 키를 새로 만들지 않습니다(할당 없음).
 *    성립 조건은 **해시가 transparent** 라는 것 하나입니다. `std::hash<sw::string>` 에 `string_view` 오버로드가 있어서
 *    같은 바이트에서 같은 해시가 나옵니다. 비교자의 기본값을 `std::equal_to<>` 로 둔 이유도 여기에 있습니다.
 *    `std::equal_to<Key>` 였다면 `SW_ENABLE_STL_CONTAINER` 로 std 컨테이너를 쓸 때만 이종 검색이 컴파일되지 않아,
 *    같은 코드가 빌드 옵션에 따라 달라졌을 것입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
#if defined( SW_ENABLE_STL_CONTAINER )
    template <typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<>, typename Allocator = std::allocator<pair<const Key, T>>>
    using unordered_map = std::unordered_map<Key, T, Hash, KeyEqual, Allocator>;
#else
    /**
     * @class unordered_map
     * @brief 데이터 지향(DoD) 밀집 배열 기반의 해시맵입니다.
     *
     * 주의: `erase` 는 마지막 원소를 빈 자리로 옮기는 방식(swap-and-pop)이라, 기존 이터레이터가 무효화될 수 있습니다.
     */
    template <typename Key, typename T, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<>, typename Allocator = Allocator<pair<const Key, T>>>
    class unordered_map
    {
        SW_RACE_CTX_MEMBER

    public:
        using key_type        = Key;
        using mapped_type     = T;
        using value_type      = pair<const Key, T>;
        using size_type       = size_t;
        using difference_type = ptrdiff_t;
        using hasher          = Hash;
        using key_equal       = KeyEqual;
        using allocator_type  = Allocator;
        using reference       = value_type&;
        using const_reference = const value_type&;
        using pointer         = value_type*;
        using const_pointer   = const value_type*;

    private:
        /**
         * @brief 밀집 배열에 저장되는 노드 하나입니다(키-값과, 충돌 체인에서 다음 노드의 인덱스).
         */
        struct Node
        {
            pair<Key, T> _keyValuePair;
            size_t       _next;
        };

        vector<size_t>          _listBucket;    ///< 버킷마다 체인 첫 노드의 인덱스
        vector<Node>            _listDenseData; ///< 연속 메모리에 놓인 밀집 데이터 배열
        pair<hasher, key_equal> _traits;        ///< 해시 · 키 비교 함수 객체(EBO 로 압축 보관)

        const hasher&    get_hasher() const noexcept { return _traits.first(); }
        const key_equal& get_equal() const noexcept { return _traits.second(); }

        /** @brief 빈 버킷 슬롯을 나타내는 센티넬 값(-1, 모든 비트 1)입니다. */
        static constexpr size_t kEmptySlot = invalid_index::kUint64;

        /** @brief 버킷 수의 최솟값입니다. 버킷 수는 항상 2의 거듭제곱입니다(`bucketIndexOf` 가 마스크로 자르기 때문입니다). */
        static constexpr size_t kMinBucketCount = 16;

        /**
         * @brief 해시를 버킷 번호로 바꿉니다. 곱셈 한 번 · 접기 한 번 · 마스크 한 번입니다. 버킷 수는 항상 2의 거듭제곱입니다(`rehash_internal`).
         * @details 예전에는 `hash % 버킷 수` 였습니다. 64비트 나눗셈은 이 CPU 에서 수십 사이클이라 캐시에 든 조회 하나와 맞먹습니다
         *          (`ContainerBenchTest`). 피보나치 상수를 곱하고 위쪽 절반을 아래쪽 절반에 접어 넣으므로, 아래 비트가 고르지 않은
         *          해시도 고르게 퍼집니다. libstdc++ · libc++ 의 std::hash 는 정수 · 포인터에 대해 항등 함수라서, 2의 거듭제곱 크기에
         *          `%` 만 쓰면 아래 비트만 남아 8바이트 정렬 포인터가 버킷 여덟 개 중 하나에 몰렸습니다(기본 증가 경로가 그 크기였습니다).
         *          순회는 밀집 배열을 따라가므로 버킷 배치가 바뀌어도 순서는 그대로입니다.
         */
        static size_t bucketIndexOf( size_t hash, size_t bucketCount ) noexcept
        {
            const uint64 product = static_cast<uint64>( hash ) * 0x9E3779B97F4A7C15ull;
            return static_cast<size_t>( product ^ ( product >> 32 ) ) & ( bucketCount - 1 );
        }

        /**
         * @brief 부하율(load factor)이 1 에 닿으면 버킷 수를 두 배로 늘리고 재해시합니다.
         */
        void check_expand()
        {
            if ( _listBucket.empty() || _listDenseData.size() >= _listBucket.size() )
                rehash_internal( _listBucket.empty() ? kMinBucketCount : _listBucket.size() * 2 );
        }

    public:
        /**
         * @brief 밀집 데이터 배열(_listDenseData)을 차례로 순회하는 이터레이터입니다.
         */
        class iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = value_type*;
            using reference         = value_type&;

            /** @brief 맵과 밀집 배열 인덱스로 만듭니다. */
            iterator( unordered_map* pMap, size_t index )
                : _pMap{ pMap }
                , _index{ index } {}

            /** @brief 다음 원소로 넘어갑니다. */
            iterator& operator++()
            {
                ++_index;
                return *this;
            }

            /** @brief 같은지 비교합니다. */
            bool operator==( const iterator& other ) const { return _index == other._index; }
            /** @brief 다른지 비교합니다. */
            bool operator!=( const iterator& other ) const { return _index != other._index; }
            /** @brief 역참조합니다. */
            reference operator*() const { return *reinterpret_cast<pointer>( const_cast<pair<Key, T>*>( &std::as_const( _pMap->_listDenseData ).data()[_index]._keyValuePair ) ); }
            /** @brief 멤버에 접근합니다. */
            pointer operator->() const { return reinterpret_cast<pointer>( const_cast<pair<Key, T>*>( &std::as_const( _pMap->_listDenseData ).data()[_index]._keyValuePair ) ); }

            unordered_map* _pMap;
            size_t         _index;
        };

        /** @brief 밀집 배열을 순회하는 상수 이터레이터입니다. */
        class const_iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = const value_type*;
            using reference         = const value_type&;

            /** @brief 맵과 밀집 배열 인덱스로 만듭니다. */
            const_iterator( const unordered_map* pMap, size_t index )
                : _pMap{ pMap }
                , _index{ index } {}

            const_iterator( const iterator& other )
                : _pMap{ other._pMap }
                , _index{ other._index } {}

            const_iterator& operator++()
            {
                ++_index;
                return *this;
            }

            /** @brief 같은지 비교합니다. */
            bool operator==( const const_iterator& other ) const { return _index == other._index; }
            /** @brief 다른지 비교합니다. */
            bool operator!=( const const_iterator& other ) const { return _index != other._index; }
            /** @brief 역참조합니다. */
            reference operator*() const { return *reinterpret_cast<pointer>( &std::as_const( _pMap->_listDenseData ).data()[_index]._keyValuePair ); }
            /** @brief 멤버에 접근합니다. */
            pointer operator->() const { return reinterpret_cast<pointer>( &std::as_const( _pMap->_listDenseData ).data()[_index]._keyValuePair ); }

            const unordered_map* _pMap;
            size_t               _index;
        };

        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입 — 버킷 + 밀집 배열. 레이스 컨텍스트는 공유하지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 빈 맵으로 둡니다. */
        unordered_map()
            : _listBucket{}
            , _listDenseData{}
            , _traits{} {}

        /** @brief 초기화 리스트의 원소로 채웁니다. */
        unordered_map( std::initializer_list<value_type> init )
            : _listBucket{}
            , _listDenseData{}
            , _traits{}
        {
            for ( const auto& keyValuePair : init )
            {
                insert( keyValuePair );
            }
        }

        /** @brief 복사 생성합니다. */
        unordered_map( const unordered_map& other )
            : _listBucket{ other._listBucket }
            , _listDenseData{ other._listDenseData }
            , _traits{ other._traits }
        {
            SW_SCOPED_RACE_READ_OTHER( other );
        }

        /** @brief 이동 생성합니다. */
        unordered_map( unordered_map&& other ) noexcept
            : _listBucket{ std::move( other._listBucket ) }
            , _listDenseData{ std::move( other._listDenseData ) }
            , _traits{ std::move( other._traits ) }
        {
            SW_SCOPED_RACE_WRITE_OTHER( other );
        }

        /** @brief 복사 대입합니다. */
        unordered_map& operator=( const unordered_map& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                _listBucket    = other._listBucket;
                _listDenseData = other._listDenseData;
                _traits        = other._traits;
            }
            return *this;
        }

        /** @brief 이동 대입합니다. */
        unordered_map& operator=( unordered_map&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                _listBucket    = std::move( other._listBucket );
                _listDenseData = std::move( other._listDenseData );
                _traits        = std::move( other._traits );
            }
            return *this;
        }

        /** @brief 시작 이터레이터를 반환합니다. */
        iterator begin() noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, 0 );
        }

        /** @brief 끝 이터레이터를 반환합니다. */
        iterator end() noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, _listDenseData.size() );
        }

        const_iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, 0 );
        }

        const_iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, _listDenseData.size() );
        }

        const_iterator cbegin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, 0 );
        }

        const_iterator cend() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return const_iterator( this, _listDenseData.size() );
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 밀집 순회 · 버킷 검색
        // ------------------------------------------------------------------------------
        /** @brief 비어 있는지 반환합니다. */
        bool empty() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _listDenseData.empty();
        }

        /** @brief 원소 개수를 반환합니다. */
        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _listDenseData.size();
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — insert/erase. erase 뒤 이터레이터가 무효화될 수 있다
        // ------------------------------------------------------------------------------
        /** @brief 모든 원소를 제거합니다. */
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            _listDenseData.clear();
            for ( size_t& bucket : _listBucket )
            {
                bucket = kEmptySlot;
            }
        }

        /** @brief 키를 찾습니다. */
        iterator find( const Key& key )
        {
            SW_SCOPED_RACE_READ();
            if ( _listBucket.empty() )
                return end();
            size_t        hash         = get_hasher()( key );
            const size_t  bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            const size_t* pBucketData  = std::as_const( _listBucket ).data();
            const Node*   pDenseData   = std::as_const( _listDenseData ).data();
            size_t        currentIndex = pBucketData[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( pDenseData[currentIndex]._keyValuePair.first, key ) )
                    return iterator( this, currentIndex );
                currentIndex = pDenseData[currentIndex]._next;
            }
            return end();
        }

        /** @brief 키를 찾습니다. */
        const_iterator find( const Key& key ) const
        {
            SW_SCOPED_RACE_READ();
            if ( _listBucket.empty() )
                return end();
            size_t        hash         = get_hasher()( key );
            const size_t  bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            const size_t* pBucketData  = std::as_const( _listBucket ).data();
            const Node*   pDenseData   = std::as_const( _listDenseData ).data();
            size_t        currentIndex = pBucketData[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( pDenseData[currentIndex]._keyValuePair.first, key ) )
                    return const_iterator( this, currentIndex );
                currentIndex = pDenseData[currentIndex]._next;
            }
            return end();
        }

        /** @brief 이종 키(heterogeneous key, 예: string_view)로 찾습니다. */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        iterator find( const K& key )
        {
            SW_SCOPED_RACE_READ();
            if ( _listBucket.empty() )
                return end();
            size_t        hash         = get_hasher()( key );
            const size_t  bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            const size_t* pBucketData  = std::as_const( _listBucket ).data();
            const Node*   pDenseData   = std::as_const( _listDenseData ).data();
            size_t        currentIndex = pBucketData[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if constexpr ( std::is_invocable_v<KeyEqual, const Key&, const K&> )
                {
                    if ( get_equal()( pDenseData[currentIndex]._keyValuePair.first, key ) )
                        return iterator( this, currentIndex );
                }
                else
                {
                    if ( pDenseData[currentIndex]._keyValuePair.first == key )
                        return iterator( this, currentIndex );
                }
                currentIndex = pDenseData[currentIndex]._next;
            }
            return end();
        }

        /** @brief 이종 키(heterogeneous key, 예: string_view)로 찾습니다. */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        const_iterator find( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            if ( _listBucket.empty() )
                return end();
            size_t        hash         = get_hasher()( key );
            const size_t  bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            const size_t* pBucketData  = std::as_const( _listBucket ).data();
            const Node*   pDenseData   = std::as_const( _listDenseData ).data();
            size_t        currentIndex = pBucketData[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if constexpr ( std::is_invocable_v<KeyEqual, const Key&, const K&> )
                {
                    if ( get_equal()( pDenseData[currentIndex]._keyValuePair.first, key ) )
                        return const_iterator( this, currentIndex );
                }
                else
                {
                    if ( pDenseData[currentIndex]._keyValuePair.first == key )
                        return const_iterator( this, currentIndex );
                }
                currentIndex = pDenseData[currentIndex]._next;
            }
            return end();
        }

        /** @brief 키와 같은 원소의 개수(0 또는 1)를 반환합니다. */
        size_type count( const Key& key ) const { return find( key ) != end() ? 1 : 0; }

        /** @brief 이종 키와 같은 원소의 개수(0 또는 1)를 반환합니다. */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        size_type count( const K& key ) const { return find( key ) != end() ? 1 : 0; }

        /** @brief 키가 있는지 반환합니다. */
        bool contains( const Key& key ) const { return find( key ) != end(); }

        /** @brief 이종 키가 있는지 반환합니다. */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        bool contains( const K& key ) const { return find( key ) != end(); }

        /** @brief 키에 해당하는 값을 반환합니다. 없으면 기본값으로 만들어 넣습니다. */
        T& operator[]( const Key& key )
        {
            SW_SCOPED_RACE_WRITE();
            check_expand();
            size_t       hash         = get_hasher()( key );
            const size_t bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            size_t       currentIndex = _listBucket[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( _listDenseData[currentIndex]._keyValuePair.first, key ) )
                    return _listDenseData[currentIndex]._keyValuePair.second;
                currentIndex = _listDenseData[currentIndex]._next;
            }

            const size_t newIndex = _listDenseData.size();
            _listDenseData.push_back( { pair<Key, T>( key, T{} ), _listBucket[bucketIndex] } );
            _listBucket[bucketIndex] = newIndex;
            return _listDenseData[newIndex]._keyValuePair.second;
        }

        /** @brief 키에 해당하는 값을 반환합니다. 없으면 기본값으로 만들어 넣습니다. */
        T& operator[]( Key&& key )
        {
            SW_SCOPED_RACE_WRITE();
            check_expand();
            size_t       hash         = get_hasher()( key );
            const size_t bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            size_t       currentIndex = _listBucket[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( _listDenseData[currentIndex]._keyValuePair.first, key ) )
                    return _listDenseData[currentIndex]._keyValuePair.second;
                currentIndex = _listDenseData[currentIndex]._next;
            }

            const size_t newIndex = _listDenseData.size();
            _listDenseData.push_back( { pair<Key, T>( std::move( key ), T{} ), _listBucket[bucketIndex] } );
            _listBucket[bucketIndex] = newIndex;
            return _listDenseData[newIndex]._keyValuePair.second;
        }

        /** @brief 원소를 삽입합니다. */
        pair<iterator, bool> insert( const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            check_expand();
            size_t       hash         = get_hasher()( value.first );
            const size_t bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            size_t       currentIndex = _listBucket[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( _listDenseData[currentIndex]._keyValuePair.first, value.first ) )
                    return { iterator( this, currentIndex ), false };
                currentIndex = _listDenseData[currentIndex]._next;
            }

            const size_t newIndex = _listDenseData.size();
            _listDenseData.push_back( { pair<Key, T>( value.first, value.second ), _listBucket[bucketIndex] } );
            _listBucket[bucketIndex] = newIndex;
            return { iterator( this, newIndex ), true };
        }

        /** @brief 키가 없을 때만 값을 제자리에서 생성해 넣습니다. */
        template <typename... Args>
        pair<iterator, bool> try_emplace( const Key& k, Args&&... args ) { return emplace( k, T{ std::forward<Args>( args )... } ); }

        /** @brief 키가 없을 때만 값을 제자리에서 생성해 넣습니다. */
        template <typename... Args>
        pair<iterator, bool> try_emplace( Key&& k, Args&&... args ) { return emplace( std::move( k ), T{ std::forward<Args>( args )... } ); }

        /** @brief 원소를 제자리에서 생성해 넣습니다. */
        template <typename... Args>
        pair<iterator, bool> emplace( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            check_expand();
            // 키를 알려면 일단 만들어야 한다(표준 emplace 도 제자리에서 생성한다).
            // 밀집 배열 끝에 만든 뒤, 키가 이미 있으면 pop 한다.
            _listDenseData.push_back( { pair<Key, T>( std::forward<Args>( args )... ), kEmptySlot } );
            const Key& key = _listDenseData.back()._keyValuePair.first;

            size_t       hash         = get_hasher()( key );
            const size_t bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            size_t       currentIndex = _listBucket[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( _listDenseData[currentIndex]._keyValuePair.first, key ) )
                {
                    _listDenseData.pop_back();
                    return { iterator( this, currentIndex ), false };
                }
                currentIndex = _listDenseData[currentIndex]._next;
            }

            const size_t newIndex          = _listDenseData.size() - 1;
            _listDenseData[newIndex]._next = _listBucket[bucketIndex];
            _listBucket[bucketIndex]       = newIndex;
            return { iterator( this, newIndex ), true };
        }

        /** @brief pos 가 가리키는 원소를 제거합니다. */
        iterator erase( iterator pos )
        {
            if ( pos == end() )
                return end();
            erase( pos->first );
            return iterator( this, pos._index );
        }

        /** @brief pos 가 가리키는 원소를 제거합니다. */
        iterator erase( const_iterator pos )
        {
            if ( pos == end() )
                return end();
            erase( pos->first );
            return iterator( this, pos._index );
        }

        /** @brief 키가 없으면 삽입하고, 있으면 값을 대입합니다. */
        template <typename M>
        pair<iterator, bool> insert_or_assign( const Key& key, M&& mappedValue )
        {
            SW_SCOPED_RACE_WRITE();
            check_expand();
            size_t       hash         = get_hasher()( key );
            const size_t bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            size_t       currentIndex = _listBucket[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( _listDenseData[currentIndex]._keyValuePair.first, key ) )
                {
                    _listDenseData[currentIndex]._keyValuePair.second = std::forward<M>( mappedValue );
                    return { iterator( this, currentIndex ), false };
                }
                currentIndex = _listDenseData[currentIndex]._next;
            }

            const size_t newIndex = _listDenseData.size();
            _listDenseData.push_back( { pair<Key, T>( key, std::forward<M>( mappedValue ) ), _listBucket[bucketIndex] } );
            _listBucket[bucketIndex] = newIndex;
            return { iterator( this, newIndex ), true };
        }

        /** @brief 키가 없으면 삽입하고, 있으면 값을 대입합니다. */
        template <typename M>
        pair<iterator, bool> insert_or_assign( Key&& key, M&& mappedValue )
        {
            SW_SCOPED_RACE_WRITE();
            check_expand();
            size_t       hash         = get_hasher()( key );
            const size_t bucketIndex  = bucketIndexOf( hash, _listBucket.size() );
            size_t       currentIndex = _listBucket[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( _listDenseData[currentIndex]._keyValuePair.first, key ) )
                {
                    _listDenseData[currentIndex]._keyValuePair.second = std::forward<M>( mappedValue );
                    return { iterator( this, currentIndex ), false };
                }
                currentIndex = _listDenseData[currentIndex]._next;
            }

            const size_t newIndex = _listDenseData.size();
            _listDenseData.push_back( { pair<Key, T>( std::move( key ), std::forward<M>( mappedValue ) ), _listBucket[bucketIndex] } );
            _listBucket[bucketIndex] = newIndex;
            return { iterator( this, newIndex ), true };
        }

        /** @brief 키와 같은 원소를 제거하고, 제거한 개수를 반환합니다. */
        size_type erase( const Key& key )
        {
            SW_SCOPED_RACE_WRITE();
            if ( _listBucket.empty() )
                return 0;

            size_t       hash          = get_hasher()( key );
            const size_t bucketIndex   = bucketIndexOf( hash, _listBucket.size() );
            size_t       currentIndex  = _listBucket[bucketIndex];
            size_t       previousIndex = kEmptySlot;

            while ( currentIndex != kEmptySlot )
            {
                if ( get_equal()( _listDenseData[currentIndex]._keyValuePair.first, key ) )
                {
                    // 버킷 체인에서 뗀다
                    if ( previousIndex == kEmptySlot )
                        _listBucket[bucketIndex] = _listDenseData[currentIndex]._next;
                    else
                        _listDenseData[previousIndex]._next = _listDenseData[currentIndex]._next;

                    const size_t lastIndex = _listDenseData.size() - 1;
                    if ( currentIndex != lastIndex )
                    {
                        // 마지막 원소를 currentIndex 자리로 옮긴다.
                        _listDenseData[currentIndex] = std::move( _listDenseData[lastIndex] );

                        // lastIndex 를 가리키던 버킷 체인이 currentIndex 를 가리키게 바꾼다.
                        size_t       lastHash          = get_hasher()( _listDenseData[currentIndex]._keyValuePair.first );
                        const size_t lastBucketIndex   = bucketIndexOf( lastHash, _listBucket.size() );
                        size_t       lastCurrentIndex  = _listBucket[lastBucketIndex];
                        size_t       lastPreviousIndex = kEmptySlot;
                        while ( lastCurrentIndex != kEmptySlot )
                        {
                            if ( lastCurrentIndex == lastIndex )
                            {
                                if ( lastPreviousIndex == kEmptySlot )
                                    _listBucket[lastBucketIndex] = currentIndex;
                                else
                                    _listDenseData[lastPreviousIndex]._next = currentIndex;
                                break;
                            }
                            lastPreviousIndex = lastCurrentIndex;
                            lastCurrentIndex  = _listDenseData[lastCurrentIndex]._next;
                        }
                    }

                    _listDenseData.pop_back();
                    return 1;
                }
                previousIndex = currentIndex;
                currentIndex  = _listDenseData[currentIndex]._next;
            }
            return 0;
        }

        /** @brief 버킷 수를 count 이상인 2의 거듭제곱으로 늘리고 재해시합니다. 지금보다 작으면 아무것도 하지 않습니다. */
        void rehash( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            rehash_internal( count );
        }

        /** @brief rehash 와 같지만 레이스 가드를 잡지 않습니다(내부용). */
        void rehash_internal( size_type count )
        {
            if ( count <= _listBucket.size() )
                return;
            // 2의 거듭제곱으로 올린다(`reserve( 3000 )` 이면 4096). `bucketIndexOf` 가 마스크로 자르는 전제다.
            size_t bucketCount = _listBucket.empty() ? kMinBucketCount : _listBucket.size();
            while ( bucketCount < count )
                bucketCount *= 2;
            _listBucket.assign( bucketCount, kEmptySlot );
            for ( size_t denseIndex = 0; denseIndex < _listDenseData.size(); ++denseIndex )
            {
                size_t       hash                = get_hasher()( _listDenseData[denseIndex]._keyValuePair.first );
                const size_t bucketIndex         = bucketIndexOf( hash, _listBucket.size() );
                _listDenseData[denseIndex]._next = _listBucket[bucketIndex];
                _listBucket[bucketIndex]         = denseIndex;
            }
        }

        /** @brief rehash( count ) 와 같습니다(버킷을 미리 잡습니다). */
        void reserve( size_type count ) { rehash( count ); }
    };
#endif
} // namespace sw
