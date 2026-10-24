/**
 * @file unordered_set.h
 * @brief 밀집 배열 해시 집합(sw::unordered_set)입니다.
 * @details 순회는 밀집 배열 순서입니다.
 *          모든 컨테이너 형식은 두 구성(`SW_ENABLE_STL_CONTAINER`)에서 같은 구현이고 바뀌는 것은 안쪽 `vector` 의 할당자(`ContainerAllocator`)뿐입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/HashUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @brief 밀집 배열 기반 해시 집합입니다. erase 하면 이터레이터가 무효화될 수 있습니다(두 구성 같음). */
    template <typename Key, typename Hash = DefaultHash<Key>, typename KeyEqual = std::equal_to<>, typename Allocator = ContainerAllocator<Key>>
    class unordered_set
    {
        SW_RACE_CTX_MEMBER

    public:
        using key_type        = Key;
        using value_type      = Key;
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
        /** @brief 키와, 버킷 체인에서 다음 노드의 인덱스, 해시 태그입니다. 자세한 것은 `unordered_map::Node` 와 같습니다(32비트 인덱스 — 원소 수 상한 42억). */
        struct Node
        {
            Key    _key;
            uint32 _next;
            uint32 _tag;
        };

        vector<uint32>                    _listBucket;
        vector<Node>                      _listDenseData;
        CompressedPair<hasher, key_equal> _traits;

        const hasher&    get_hasher() const noexcept { return _traits.first(); }
        const key_equal& get_equal() const noexcept { return _traits.second(); }

        /** @brief 빈 버킷 슬롯 표식(모든 비트 1)입니다. */
        static constexpr uint32 kEmptySlot = invalid_index::kUint32;

        /** @brief 해시를 32비트 태그로 접습니다. 위쪽 절반을 아래쪽 절반에 섞습니다(`unordered_map::makeTag` 와 같음). */
        static uint32 makeTag( size_t hash ) noexcept
        {
            const uint64 wide = static_cast<uint64>( hash );
            return static_cast<uint32>( wide ^ ( wide >> 32 ) );
        }

        /** @brief 버킷 수의 최솟값입니다. 버킷 수는 항상 2의 거듭제곱입니다(`bucketIndexOf` 가 마스크로 자르기 때문입니다). */
        static constexpr size_t kMinBucketCount = 16;

        /**
         * @brief 해시를 버킷 번호로 바꿉니다. 곱셈 한 번 · 접기 한 번 · 마스크 한 번입니다. 버킷 수는 항상 2의 거듭제곱입니다(`rehash_internal`).
         * @details `hash % 버킷 수` 를 쓰지 않습니다. 64비트 나눗셈은 이 CPU 에서 수십 사이클이라 캐시에 든 조회 하나와 맞먹습니다
         *          (`ContainerBenchTest`). 피보나치 상수를 곱하고 위쪽 절반을 아래쪽 절반에 접어 넣으므로, 아래 비트가 고르지 않은
         *          해시도 고르게 퍼집니다. libstdc++ · libc++ 의 std::hash 는 정수 · 포인터에 대해 항등 함수라서, 2의 거듭제곱 크기에
         *          `%` 만 쓰면 아래 비트만 남아 8바이트 정렬 포인터가 버킷 여덟 개 중 하나에 몰립니다.
         *          순회는 밀집 배열을 따라가므로 버킷 배치가 바뀌어도 순서는 그대로입니다.
         */
        static size_t bucketIndexOf( uint32 tag, size_t bucketCount ) noexcept
        {
            const uint64 product = static_cast<uint64>( tag ) * HashUtil::kGoldenRatio64;
            return static_cast<size_t>( product ^ ( product >> 32 ) ) & ( bucketCount - 1 );
        }

        /** @brief 원소 수가 버킷 수에 닿으면(부하율 1) 버킷을 두 배로 늘립니다. 버킷이 없으면 최소 개수로 만듭니다. */
        void check_expand()
        {
            if ( _listBucket.empty() || _listDenseData.size() >= _listBucket.size() )
                rehash_internal( _listBucket.empty() ? kMinBucketCount : _listBucket.size() * 2 );
        }

    public:
        /** @brief 밀집 배열을 순회하는 이터레이터입니다. */
        class iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = const value_type*;
            using reference         = const value_type&;

            iterator( const unordered_set* pSet, size_t index )
                : _pSet{ pSet }
                , _index{ index } {}

            iterator& operator++()
            {
                ++_index;
                return *this;
            }

            bool      operator==( const iterator& other ) const { return _index == other._index; }
            bool      operator!=( const iterator& other ) const { return _index != other._index; }
            reference operator*() const { return std::as_const( _pSet->_listDenseData ).data()[_index]._key; }
            pointer   operator->() const { return &std::as_const( _pSet->_listDenseData ).data()[_index]._key; }

            const unordered_set* _pSet;
            size_t               _index;
        };

        using const_iterator = iterator; // 집합의 이터레이터는 언제나 const 다

        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입 — 버킷 + 밀집 배열. 레이스 컨텍스트는 공유하지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 빈 집합으로 둡니다. */
        unordered_set()
            : _listBucket{}
            , _listDenseData{}
            , _traits{} {}

        /** @brief 초기화 리스트의 원소로 채웁니다. */
        unordered_set( std::initializer_list<value_type> init )
            : _listBucket{}
            , _listDenseData{}
            , _traits{}
        {
            for ( const auto& value : init )
            {
                insert( value );
            }
        }

        /** @brief 복사 생성합니다. */
        unordered_set( const unordered_set& other )
            : _listBucket{ other._listBucket }
            , _listDenseData{ other._listDenseData }
            , _traits{ other._traits }
        {
            SW_SCOPED_RACE_READ_OTHER( other );
        }

        /** @brief 이동 생성합니다. */
        unordered_set( unordered_set&& other ) noexcept
            : _listBucket{ std::move( other._listBucket ) }
            , _listDenseData{ std::move( other._listDenseData ) }
            , _traits{ std::move( other._traits ) }
        {
            SW_SCOPED_RACE_WRITE_OTHER( other );
        }

        unordered_set& operator=( const unordered_set& other )
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

        unordered_set& operator=( unordered_set&& other ) noexcept
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

        iterator begin() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, 0 );
        }

        iterator end() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return iterator( this, _listDenseData.size() );
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
        bool empty() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _listDenseData.empty();
        }

        size_type size() const noexcept
        {
            SW_SCOPED_RACE_READ();
            return _listDenseData.size();
        }

        // ------------------------------------------------------------------------------
        // 3) 변경 — insert/erase. erase 뒤 이터레이터가 무효화될 수 있다
        // ------------------------------------------------------------------------------
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            _listDenseData.clear();
            for ( uint32& bucket : _listBucket )
            {
                bucket = kEmptySlot;
            }
        }

        iterator find( const Key& key ) const
        {
            SW_SCOPED_RACE_READ();
            if ( _listBucket.empty() )
                return end();
            const uint32  tag          = makeTag( get_hasher()( key ) );
            const size_t  bucketIndex  = bucketIndexOf( tag, _listBucket.size() );
            const uint32* pBucketData  = std::as_const( _listBucket ).data();
            const Node*   pDenseData   = std::as_const( _listDenseData ).data();
            uint32        currentIndex = pBucketData[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( pDenseData[currentIndex]._tag == tag && get_equal()( pDenseData[currentIndex]._key, key ) )
                    return iterator( this, currentIndex );
                currentIndex = pDenseData[currentIndex]._next;
            }
            return end();
        }

        /**
         * @brief 이종 키(heterogeneous key, 예: string_view)로 찾습니다.
         * @details `unordered_map` 과 같은 계약입니다 — `unordered_set<string>` 을 `string_view` 로 찾아도 키를 새로 만들지 않습니다
         *          (비교자의 기본값이 `std::equal_to<>` 인 이유입니다).
         */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        iterator find( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            if ( _listBucket.empty() )
                return end();
            const uint32  tag          = makeTag( get_hasher()( key ) );
            const size_t  bucketIndex  = bucketIndexOf( tag, _listBucket.size() );
            const uint32* pBucketData  = std::as_const( _listBucket ).data();
            const Node*   pDenseData   = std::as_const( _listDenseData ).data();
            uint32        currentIndex = pBucketData[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if constexpr ( std::is_invocable_v<KeyEqual, const Key&, const K&> )
                {
                    if ( pDenseData[currentIndex]._tag == tag && get_equal()( pDenseData[currentIndex]._key, key ) )
                        return iterator( this, currentIndex );
                }
                else
                {
                    if ( pDenseData[currentIndex]._tag == tag && pDenseData[currentIndex]._key == key )
                        return iterator( this, currentIndex );
                }
                currentIndex = pDenseData[currentIndex]._next;
            }
            return end();
        }

        /** @brief 키와 같은 원소의 개수(0 또는 1)를 반환합니다. */
        size_type count( const Key& key ) const { return find( key ) != end() ? 1 : 0; }

        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        size_type count( const K& key ) const { return find( key ) != end() ? 1 : 0; }

        bool contains( const Key& key ) const { return find( key ) != end(); }

        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        bool contains( const K& key ) const { return find( key ) != end(); }

        pair<iterator, bool> insert( const value_type& value )
        {
            SW_SCOPED_RACE_WRITE();
            check_expand();
            const uint32 tag          = makeTag( get_hasher()( value ) );
            const size_t bucketIndex  = bucketIndexOf( tag, _listBucket.size() );
            uint32       currentIndex = _listBucket[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( _listDenseData[currentIndex]._tag == tag && get_equal()( _listDenseData[currentIndex]._key, value ) )
                    return { iterator( this, currentIndex ), false };
                currentIndex = _listDenseData[currentIndex]._next;
            }

            const uint32 newIndex = static_cast<uint32>( _listDenseData.size() );
            _listDenseData.push_back( { value, _listBucket[bucketIndex], tag } );
            _listBucket[bucketIndex] = newIndex;
            return { iterator( this, newIndex ), true };
        }

        template <typename... Args>
        pair<iterator, bool> emplace( Args&&... args )
        {
            SW_SCOPED_RACE_WRITE();
            check_expand();
            _listDenseData.push_back( { Key( std::forward<Args>( args )... ), kEmptySlot, 0 } );
            const Key& key = _listDenseData.back()._key;

            const uint32 tag          = makeTag( get_hasher()( key ) );
            const size_t bucketIndex  = bucketIndexOf( tag, _listBucket.size() );
            uint32       currentIndex = _listBucket[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if ( _listDenseData[currentIndex]._tag == tag && get_equal()( _listDenseData[currentIndex]._key, key ) )
                {
                    _listDenseData.pop_back();
                    return { iterator( this, currentIndex ), false };
                }
                currentIndex = _listDenseData[currentIndex]._next;
            }

            const uint32 newIndex          = static_cast<uint32>( _listDenseData.size() - 1 );
            _listDenseData[newIndex]._next = _listBucket[bucketIndex];
            _listDenseData[newIndex]._tag  = tag;
            _listBucket[bucketIndex]       = newIndex;
            return { iterator( this, newIndex ), true };
        }

        iterator erase( const_iterator pos )
        {
            if ( pos == end() )
                return end();
            erase( *pos );
            return iterator( this, pos._index );
        }

        size_type erase( const Key& key )
        {
            SW_SCOPED_RACE_WRITE();
            if ( _listBucket.empty() )
                return 0;

            const uint32 tag           = makeTag( get_hasher()( key ) );
            const size_t bucketIndex   = bucketIndexOf( tag, _listBucket.size() );
            uint32       currentIndex  = _listBucket[bucketIndex];
            uint32       previousIndex = kEmptySlot;

            while ( currentIndex != kEmptySlot )
            {
                if ( _listDenseData[currentIndex]._tag == tag && get_equal()( _listDenseData[currentIndex]._key, key ) )
                {
                    if ( previousIndex == kEmptySlot )
                        _listBucket[bucketIndex] = _listDenseData[currentIndex]._next;
                    else
                        _listDenseData[previousIndex]._next = _listDenseData[currentIndex]._next;

                    const uint32 lastIndex = static_cast<uint32>( _listDenseData.size() - 1 );
                    if ( currentIndex != lastIndex )
                    {
                        _listDenseData[currentIndex] = std::move( _listDenseData[lastIndex] );

                        // 옮겨 온 노드의 태그로 버킷을 구한다(키를 다시 해시하지 않는다).
                        const size_t lastBucketIndex   = bucketIndexOf( _listDenseData[currentIndex]._tag, _listBucket.size() );
                        uint32       lastCurrentIndex  = _listBucket[lastBucketIndex];
                        uint32       lastPreviousIndex = kEmptySlot;
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
            {
                bucketCount *= 2;
            }
            _listBucket.assign( bucketCount, kEmptySlot );
            for ( uint32 denseIndex = 0; denseIndex < static_cast<uint32>( _listDenseData.size() ); ++denseIndex )
            {
                const size_t bucketIndex         = bucketIndexOf( _listDenseData[denseIndex]._tag, _listBucket.size() ); // 태그가 해시를 들고 있다 — 키를 다시 해시하지 않는다
                _listDenseData[denseIndex]._next = _listBucket[bucketIndex];
                _listBucket[bucketIndex]         = denseIndex;
            }
        }

        /** @brief rehash( count ) 와 같습니다(버킷을 미리 잡습니다). */
        void reserve( size_type count ) { rehash( count ); }
    };
} // namespace sw
