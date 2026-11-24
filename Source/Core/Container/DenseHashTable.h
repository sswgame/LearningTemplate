/**
 * @file DenseHashTable.h
 * @brief 밀집 배열 해시 컨테이너(`unordered_map` · `unordered_set`)의 공통 몸통입니다 — 버킷 표 · 밀집 노드 배열 · 체인 검색 · 삽입 · 지우기 · 다시 해시.
 * @details 두 컨테이너는 노드가 값을 드는 모양만 다르다(키 · 값 쌍 / 키). 노드 형식이 `getKey()` 로 키를 내주고 나머지를 여기 한 벌로 둔다
 *          (MSVC `_Hash` · Abseil `raw_hash_set` 이 map · set 을 한 표 위에 두는 것과 같다). 예전에는 두 헤더가 체인 검색 · 지우기의 다시 잇기 · 버킷 다시 만들기 ·
 *          버킷 번호 식을 따로 들어, 한쪽에만 생긴 기능(`shrink_to_fit`)과 "저쪽과 같음" 주석으로 맞춰야 했다.
 *
 * [구조와 메모리 배치]
 * 1. 밀집 저장소: 모든 노드가 연속된 `vector<Node>` 에 들어 있어, 순회할 때 L1/L2 캐시 적중률이 높습니다.
 * 2. 버킷 인덱스 표: 해시 충돌 체인을 노드 포인터 대신 4바이트 정수 인덱스(`uint32`, 원소 수 상한 42억)로 이어, 포인터를 따라가는 비용과
 *    메모리 단편화를 없앱니다. 노드는 해시를 32비트로 접은 태그를 들고, 체인을 걸을 때 키를 비교하기 전에 태그부터 견주며
 *    `erase` 와 버킷 다시 만들기는 키를 다시 해시하지 않고 태그로 버킷을 구합니다(문자열 키에서 크다).
 * 3. 순회 순서: 넣은 순서이고, `erase` 는 마지막 원소를 빈 슬롯으로 옮깁니다(swap-and-pop — 기존 이터레이터가 무효화될 수 있다).
 * 4. 이터레이터 세대(검사 구성만): 표가 원소 자리를 옮길 때(`erase` · `clear` · 대입) 세대를 올리고, 이터레이터는 만들 때의 세대와 다르면 역참조 · 전진에서 단언한다
 *    (Abseil 해시 표의 세대 검사 · MSVC `_ITERATOR_DEBUG_LEVEL` 자리). 이름(`unordered_map`)이 말하지 않는 무효화를 실행 때 잡는다. 삽입은 세대를 올리지 않는다 —
 *    이터레이터가 번호라 밀집 배열이 커져도 같은 원소를 가리킨다(원소 주소 `&it->second` 는 바뀐다, 세대 검사도 못 잡는다). 배포 구성은 칸도 검사도 없다.
 */
#pragma once
#include "Core/Concurrency/DataRaceDetector.h"
#include "Core/Container/BitwiseRelocatable.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

namespace sw::internal
{
    /**
     * @brief `unordered_map` 의 밀집 배열 노드입니다(키-값과, 충돌 체인에서 다음 노드의 인덱스, 해시 태그).
     * @details 인덱스는 32비트입니다(원소 수 상한은 빈 슬롯 표식 미만 — 42억 개). 태그는 해시를 32비트로 접은 값입니다.
     */
    template <typename Key, typename T>
    struct DenseHashMapNode
    {
        pair<Key, T> _keyValuePair;
        uint32       _next;
        uint32       _tag;

        SW_INLINE const Key& getKey() const noexcept { return _keyValuePair.first; }
    };
} // namespace sw::internal

namespace sw::internal
{
    /** @brief `unordered_set` 의 밀집 배열 노드입니다(키와, 충돌 체인에서 다음 노드의 인덱스, 해시 태그). */
    template <typename Key>
    struct DenseHashSetNode
    {
        Key    _key;
        uint32 _next;
        uint32 _tag;

        SW_INLINE const Key& getKey() const noexcept { return _key; }
    };
} // namespace sw::internal

namespace sw
{
    template <typename Key, typename T>
    struct is_bitwise_relocatable<internal::DenseHashMapNode<Key, T>> : std::bool_constant<is_bitwise_relocatable_v<Key> && is_bitwise_relocatable_v<T>>
    {
    };

    template <typename Key>
    struct is_bitwise_relocatable<internal::DenseHashSetNode<Key>> : is_bitwise_relocatable<Key>
    {
    };
} // namespace sw

namespace sw::internal
{
    /**
     * @brief 버킷 표와 밀집 노드 배열로 된 해시 표입니다. 이터레이터와 값을 만드는 삽입 함수는 파생 컨테이너가 둡니다.
     * @tparam Node `_next` · `_tag` 칸과 `getKey()` 를 가진 노드(`DenseHashMapNode` · `DenseHashSetNode`).
     */
    template <typename Key, typename Node, typename Hash, typename KeyEqual>
    class DenseHashTable
    {
    public:
        using size_type       = size_t;
        using difference_type = ptrdiff_t;
        using hasher          = Hash;
        using key_equal       = KeyEqual;

    public:
        // ------------------------------------------------------------------------------
        // 1) 생성 · 대입 — 버킷 + 밀집 배열. 레이스 컨텍스트는 공유하지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 빈 표로 둡니다. */
        DenseHashTable()
            : _listBucket{}
            , _listDenseData{}
            , _traits{} {}

        /** @brief 복사 생성합니다. */
        DenseHashTable( const DenseHashTable& other )
            : _listBucket{ other._listBucket }
            , _listDenseData{ other._listDenseData }
            , _traits{ other._traits }
        {
            SW_SCOPED_RACE_READ_OTHER( other );
        }

        /** @brief 이동 생성합니다. */
        DenseHashTable( DenseHashTable&& other ) noexcept
            : _listBucket{ std::move( other._listBucket ) }
            , _listDenseData{ std::move( other._listDenseData ) }
            , _traits{ std::move( other._traits ) }
        {
            SW_SCOPED_RACE_WRITE_OTHER( other );
        }

        DenseHashTable& operator=( const DenseHashTable& other )
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_READ_OTHER( other );
                bumpIteratorGeneration();
                _listBucket    = other._listBucket;
                _listDenseData = other._listDenseData;
                _traits        = other._traits;
            }
            return *this;
        }

        DenseHashTable& operator=( DenseHashTable&& other ) noexcept
        {
            if ( this != &other )
            {
                SW_SCOPED_RACE_WRITE();
                SW_SCOPED_RACE_WRITE_OTHER( other );
                bumpIteratorGeneration();
                other.bumpIteratorGeneration();
                _listBucket    = std::move( other._listBucket );
                _listDenseData = std::move( other._listDenseData );
                _traits        = std::move( other._traits );
            }
            return *this;
        }

        // ------------------------------------------------------------------------------
        // 2) 조회 — 크기 · 버킷 검색
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

        /** @brief 키와 같은 원소의 개수(0 또는 1)를 반환합니다. */
        size_type count( const Key& key ) const
        {
            SW_SCOPED_RACE_READ();
            return findDenseIndex( key ) != _listDenseData.size() ? 1 : 0;
        }

        /** @brief 이종 키(heterogeneous key, 예: string_view)와 같은 원소의 개수(0 또는 1)를 반환합니다. */
        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        size_type count( const K& key ) const
        {
            SW_SCOPED_RACE_READ();
            return findDenseIndex( key ) != _listDenseData.size() ? 1 : 0;
        }

        bool contains( const Key& key ) const { return count( key ) != 0; }

        template <typename K, typename = std::enable_if_t<!std::is_same_v<std::decay_t<K>, Key>>>
        bool contains( const K& key ) const { return count( key ) != 0; }

        // ------------------------------------------------------------------------------
        // 3) 변경 — 비우기 · 지우기 · 용량. erase 뒤 이터레이터가 무효화될 수 있다
        // ------------------------------------------------------------------------------
        /** @brief 모든 원소를 제거합니다. 용량은 남깁니다(놓으려면 이어서 `shrink_to_fit()`). */
        void clear() noexcept
        {
            SW_SCOPED_RACE_WRITE();
            bumpIteratorGeneration();
            _listDenseData.clear();
            for ( uint32& bucket : _listBucket )
            {
                bucket = kEmptySlot;
            }
        }

        /** @brief 키와 같은 원소를 제거하고, 제거한 개수(0 또는 1)를 반환합니다. 마지막 원소가 빈 슬롯으로 옮겨집니다. */
        size_type erase( const Key& key )
        {
            SW_SCOPED_RACE_WRITE();
            if ( _listBucket.empty() )
                return 0;

            const uint32 tag           = makeTag( get_hasher()( key ) );
            const size_t bucketIndex   = computeBucketIndex( tag, _listBucket.size() );
            uint32       currentIndex  = _listBucket[bucketIndex];
            uint32       previousIndex = kEmptySlot;

            while ( currentIndex != kEmptySlot )
            {
                if ( _listDenseData[currentIndex]._tag == tag && get_equal()( _listDenseData[currentIndex].getKey(), key ) )
                {
                    bumpIteratorGeneration(); // 지운 자리로 마지막 원소가 옮겨 온다 — 옛 이터레이터가 다른 원소를 가리키게 된다
                    // 버킷 체인에서 분리한다
                    if ( previousIndex == kEmptySlot )
                        _listBucket[bucketIndex] = _listDenseData[currentIndex]._next;
                    else
                        _listDenseData[previousIndex]._next = _listDenseData[currentIndex]._next;

                    const uint32 lastIndex = static_cast<uint32>( _listDenseData.size() - 1 );
                    if ( currentIndex != lastIndex )
                    {
                        // 마지막 원소를 currentIndex 위치로 옮긴다.
                        _listDenseData[currentIndex] = std::move( _listDenseData[lastIndex] );

                        // lastIndex 를 가리키던 버킷 체인이 currentIndex 를 가리키게 바꾼다. 옮겨 온 노드의 태그로 버킷을 구한다(키를 다시 해시하지 않는다).
                        const size_t lastBucketIndex   = computeBucketIndex( _listDenseData[currentIndex]._tag, _listBucket.size() );
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

        /**
         * @brief 원소 count 개를 더 할당하지 않고 넣을 만큼 버킷과 밀집 배열을 함께 잡습니다(언리얼 `TMap::Reserve`).
         * @details 버킷만 잡으면 밀집 배열이 삽입마다 키우기를 거듭한다(감사 C-9). 부하율 1 이라 버킷 수 ≥ count 면 넣는 동안 재해시도 없다.
         */
        void reserve( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            _listDenseData.reserve( count );
            rehash_internal( count );
        }

        /**
         * @brief 남는 용량을 놓습니다. 비었으면 버킷 테이블과 밀집 배열의 버퍼를 모두 놓아 처음 상태로 돌아갑니다.
         * @details `clear()` 는 용량을 남긴다(다시 채울 때 할당하지 않도록). 버퍼까지 놓는 비우기는 `clear()` + `shrink_to_fit()` 이다.
         *          원소가 있으면 버킷 수를 원소 수 이상인 가장 작은 2의 거듭제곱(최소 `kMinBucketCount`)으로 줄인다.
         */
        void shrink_to_fit()
        {
            SW_SCOPED_RACE_WRITE();
            _listDenseData.shrink_to_fit();
            if ( _listDenseData.empty() )
            {
                _listBucket.clear();
                _listBucket.shrink_to_fit();
                return;
            }
            size_t bucketCount = kMinBucketCount;
            while ( bucketCount < _listDenseData.size() )
            {
                bucketCount *= 2;
            }
            if ( bucketCount < _listBucket.size() )
            {
                rebuild_buckets( bucketCount );
                _listBucket.shrink_to_fit();
            }
        }

    protected:
        /** @brief 빈 버킷 슬롯을 나타내는 센티넬 값(모든 비트 1)입니다. */
        static constexpr uint32 kEmptySlot = constant::invalid_index::kUint32;

        /** @brief 버킷 수의 최솟값입니다. 버킷 수는 항상 2의 거듭제곱입니다(`computeBucketIndex` 가 마스크로 자르기 때문입니다). */
        static constexpr size_t kMinBucketCount = 16;

        const hasher&    get_hasher() const noexcept { return _traits.first(); }
        const key_equal& get_equal() const noexcept { return _traits.second(); }

#if defined( SW_DO_CHECKS )
        /** @brief 지금 이터레이터 세대입니다(검사 구성만). 이터레이터가 만들 때 읽어 둔다. */
        uint32 getIteratorGeneration() const noexcept { return _iteratorGeneration; }
        /** @brief 이터레이터가 만들 때의 세대 @p generation 이 지금과 다르면 단언합니다 — 그 사이 `erase` · `clear` · 대입이 원소 자리를 옮겼다. */
        void assertIteratorGeneration( uint32 generation ) const noexcept
        {
            SW_LOW_LEVEL_ASSERT( generation == _iteratorGeneration,
                                 "hash container iterator used after erase/clear/assign moved elements (swap-and-pop) - take the iterator again (erase(it) returns the next one)" );
        }
        void bumpIteratorGeneration() noexcept { ++_iteratorGeneration; }
#else
        static void bumpIteratorGeneration() noexcept {}
#endif

        /**
         * @brief 키가 든 밀집 배열 인덱스입니다. 없으면 원소 수(`end()` 의 인덱스)입니다. 레이스 가드는 부르는 쪽이 잡습니다.
         * @details 이종 키(예: `string_view`)로 찾아도 키를 새로 만들지 않습니다 — 해시가 transparent 여야 합니다(`DefaultHash<sw::string>` 의 `string_view` 오버로드).
         */
        template <typename K>
        SW_INLINE size_t findDenseIndex( const K& key ) const
        {
            if ( _listBucket.empty() )
                return _listDenseData.size();
            const uint32 tag   = makeTag( get_hasher()( key ) );
            const uint32 index = findInChain( key, tag, computeBucketIndex( tag, _listBucket.size() ) );
            return index != kEmptySlot ? index : _listDenseData.size();
        }

        /**
         * @brief 키가 있으면 그 인덱스와 false, 없으면 @p makeNode 가 만든 노드를 끝에 넣어 체인에 걸고 그 인덱스와 true 를 돌려줍니다. 키는 한 번만 해시합니다.
         * @param makeNode `Node( uint32 next, uint32 tag )` — 키가 없을 때만 불립니다(키가 있으면 넣으려던 값을 건드리지 않는다).
         * @details 키가 있으면 버킷을 늘리지 않습니다. 없을 때만 부하율(1)을 보고 버킷을 두 배로 늘린 뒤 넣습니다.
         *          찾기 · 넣기 길의 도우미는 `SW_INLINE` 이다 — 컨테이너 함수마다 본문이 통째로 있던 예전과 같은 코드가 나오고, Debug 에서도 호출 층이 늘지 않는다.
         */
        template <typename TMakeNode>
        SW_INLINE pair<uint32, bool> getOrCreateNode( const Key& key, TMakeNode&& makeNode )
        {
            const uint32 tag         = makeTag( get_hasher()( key ) );
            size_t       bucketIndex = 0;
            if ( _listBucket.empty() == false )
            {
                bucketIndex             = computeBucketIndex( tag, _listBucket.size() );
                const uint32 foundIndex = findInChain( key, tag, bucketIndex );
                if ( foundIndex != kEmptySlot )
                    return { foundIndex, false };
            }
            if ( _listBucket.empty() || _listDenseData.size() >= _listBucket.size() )
            {
                rehash_internal( _listBucket.empty() ? kMinBucketCount : _listBucket.size() * 2 );
                bucketIndex = computeBucketIndex( tag, _listBucket.size() );
            }

            const uint32 newIndex = static_cast<uint32>( _listDenseData.size() );
            _listDenseData.push_back( makeNode( _listBucket[bucketIndex], tag ) );
            _listBucket[bucketIndex] = newIndex;
            return { newIndex, true };
        }

        /**
         * @brief 방금 밀집 배열 끝에 붙인 노드(체인에 걸리지 않은 것)를 체인에 겁니다. 같은 키가 이미 있으면 붙인 노드를 떼고 있던 인덱스와 false 를 돌려줍니다.
         * @details `emplace` 의 길입니다 — 키를 알려면 일단 만들어야 한다(표준 emplace 도 제자리에서 생성한다). 붙이기 전에 `check_expand()` 를 불러 둡니다.
         */
        pair<uint32, bool> linkAppendedNode()
        {
            const uint32 newIndex    = static_cast<uint32>( _listDenseData.size() - 1 );
            const Key&   key         = _listDenseData[newIndex].getKey();
            const uint32 tag         = makeTag( get_hasher()( key ) );
            const size_t bucketIndex = computeBucketIndex( tag, _listBucket.size() );
            const uint32 foundIndex  = findInChain( key, tag, bucketIndex );
            if ( foundIndex != kEmptySlot )
            {
                _listDenseData.pop_back();
                return { foundIndex, false };
            }

            _listDenseData[newIndex]._next = _listBucket[bucketIndex];
            _listDenseData[newIndex]._tag  = tag;
            _listBucket[bucketIndex]       = newIndex;
            return { newIndex, true };
        }

        /** @brief 원소 수가 버킷 수에 닿으면(부하율 1) 버킷을 두 배로 늘립니다. 버킷이 없으면 최소 개수로 만듭니다. */
        void check_expand()
        {
            if ( _listBucket.empty() || _listDenseData.size() >= _listBucket.size() )
                rehash_internal( _listBucket.empty() ? kMinBucketCount : _listBucket.size() * 2 );
        }

    private:
        /** @brief 해시를 32비트 태그로 접습니다. 위쪽 절반을 아래쪽 절반에 섞으므로 64비트 해시의 위쪽 비트도 버킷과 태그 비교에 반영됩니다. */
        static uint32 makeTag( size_t hash ) noexcept
        {
            const uint64 wide = static_cast<uint64>( hash );
            return static_cast<uint32>( wide ^ ( wide >> 32 ) );
        }

        /**
         * @brief 해시 태그(`makeTag`)를 버킷 번호로 바꿉니다. 곱셈 한 번 · 접기 한 번 · 마스크 한 번입니다. 버킷 수는 항상 2의 거듭제곱입니다(`rehash_internal`).
         * @details `hash % 버킷 수` 를 쓰지 않습니다. 64비트 나눗셈은 이 CPU 에서 수십 사이클이라 캐시에 든 조회 하나와 맞먹습니다.
         *          피보나치 상수를 곱하고 위쪽 절반을 아래쪽 절반에 접어 넣으므로, 아래 비트가 고르지 않은
         *          해시도 고르게 퍼집니다. libstdc++ · libc++ 의 std::hash 는 정수 · 포인터에 대해 항등 함수라서, 2의 거듭제곱 크기에
         *          `%` 만 쓰면 아래 비트만 남아 8바이트 정렬 포인터가 버킷 여덟 개 중 하나에 몰립니다.
         *          순회는 밀집 배열을 따라가므로 버킷 배치가 바뀌어도 순서는 그대로입니다.
         */
        static size_t computeBucketIndex( uint32 tag, size_t bucketCount ) noexcept
        {
            const uint64 product = static_cast<uint64>( tag ) * HashUtil::kGoldenRatio64;
            return static_cast<size_t>( product ^ ( product >> 32 ) ) & ( bucketCount - 1 );
        }

        /**
         * @brief 버킷 @p bucketIndex 의 체인에서 키가 든 노드 인덱스를 찾습니다. 없으면 `kEmptySlot` 입니다. 버킷 표가 비어 있으면 부르지 않습니다.
         * @details 키를 비교하기 전에 태그부터 견줍니다. 비교자가 이종 키를 받지 못하면(`std::equal_to<Key>`) 키의 `==` 로 견줍니다.
         */
        template <typename K>
        SW_INLINE uint32 findInChain( const K& key, uint32 tag, size_t bucketIndex ) const
        {
            // 무검사 접근: 버킷 번호는 표 크기로 접은 값이고 체인 번호는 이 표가 쓴 값이다 — 조회 핫 루프에 범위 검사를 두지 않는다
            const uint32* pBucketData  = std::as_const( _listBucket ).data();
            const Node*   pDenseData   = std::as_const( _listDenseData ).data();
            uint32        currentIndex = pBucketData[bucketIndex];
            while ( currentIndex != kEmptySlot )
            {
                if constexpr ( std::is_invocable_v<KeyEqual, const Key&, const K&> )
                {
                    if ( pDenseData[currentIndex]._tag == tag && get_equal()( pDenseData[currentIndex].getKey(), key ) )
                        return currentIndex;
                }
                else
                {
                    if ( pDenseData[currentIndex]._tag == tag && pDenseData[currentIndex].getKey() == key )
                        return currentIndex;
                }
                currentIndex = pDenseData[currentIndex]._next;
            }
            return kEmptySlot;
        }

        /** @brief rehash 와 같지만 레이스 가드를 잡지 않습니다. */
        void rehash_internal( size_type count )
        {
            if ( count <= _listBucket.size() )
                return;
            // 2의 거듭제곱으로 올린다(`reserve( 3000 )` 이면 4096). `computeBucketIndex` 가 마스크로 자르는 전제다.
            size_t bucketCount = _listBucket.empty() ? kMinBucketCount : _listBucket.size();
            while ( bucketCount < count )
            {
                bucketCount *= 2;
            }
            rebuild_buckets( bucketCount );
        }

        /** @brief 버킷 테이블을 bucketCount(2의 거듭제곱) 개로 다시 만들고 밀집 배열의 체인을 다시 연결합니다. */
        void rebuild_buckets( size_t bucketCount )
        {
            _listBucket.assign( bucketCount, kEmptySlot );
            for ( uint32 denseIndex = 0; denseIndex < static_cast<uint32>( _listDenseData.size() ); ++denseIndex )
            {
                const size_t bucketIndex         = computeBucketIndex( _listDenseData[denseIndex]._tag, _listBucket.size() ); // 태그가 해시를 들고 있다 — 키를 다시 해시하지 않는다
                _listDenseData[denseIndex]._next = _listBucket[bucketIndex];
                _listBucket[bucketIndex]         = denseIndex;
            }
        }

    protected:
        vector<uint32>                    _listBucket;    ///< 버킷마다 체인 첫 노드의 인덱스
        vector<Node>                      _listDenseData; ///< 연속 메모리에 놓인 밀집 데이터 배열
        CompressedPair<hasher, key_equal> _traits;        ///< 해시 · 키 비교 함수 객체(EBO 로 압축 보관)
#if defined( SW_DO_CHECKS )
        uint32 _iteratorGeneration{ 0 }; ///< 원소 자리를 옮기는 변경마다 오르는 세대(검사 구성만 — 배포 sizeof 는 그대로)
#endif
        SW_RACE_CTX_MEMBER
    };
} // namespace sw::internal
