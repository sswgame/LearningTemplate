/**
 * @file DenseHashTable.h
 * @brief 밀집 배열 해시 컨테이너(`unordered_map` · `unordered_set`)의 공통 몸통입니다 — 밀집 노드 배열 · 찾기 · 삽입 · 지우기. 태그 → 번호 색인은 저장(`internal::DenseHashIndex`)이 든다.
 * @details 두 컨테이너는 노드가 값을 드는 모양만 다르다(키 · 값 쌍 / 키). 노드 형식이 `getKey()` 로 키를 내주고 나머지를 여기 한 벌로 둔다
 *          (MSVC `_Hash` · Abseil `raw_hash_set` 이 map · set 을 한 표 위에 두는 것과 같다). 예전에는 두 헤더가 체인 검색 · 지우기의 다시 잇기 · 버킷 다시 만들기 ·
 *          버킷 번호 식을 따로 들어, 한쪽에만 생긴 기능(`shrink_to_fit`)과 "저쪽과 같음" 주석으로 맞춰야 했다.
 *          색인만 여기 한 자리 에서 구성(`SW_ENABLE_STL_CONTAINER`)으로 고른다: 기본은 버킷 표 + 노드 안 체인(`Native/NativeHashIndex.h`), 켜면 std::unordered_multimap 을 멤버로 든
 *          `Std/StdHashIndex.h`. std::unordered_map 을 통째로 들지 않는 까닭: C++17 에는 이종 키 조회가 없어 `string_view` 로 찾을 때마다 키를 만들고, 순회 순서(넣은 순서)와
 *          무효화 규칙이 구성마다 달라진다 — 밀집 배열을 두고 색인만 바꾸면 계약이 하나다.
 *
 * [구조와 메모리 배치]
 * 1. 밀집 저장소: 모든 노드가 연속된 `vector<Node>` 에 들어 있어, 순회할 때 L1/L2 캐시 적중률이 높습니다.
 * 2. 버킷 인덱스 표(기본 구성): 해시 충돌 체인을 노드 포인터 대신 4바이트 정수 인덱스(`uint32`, 원소 수 상한 42억)로 이어, 포인터를 따라가는 비용과
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

#if defined( SW_ENABLE_STL_CONTAINER )
    #include "Core/Container/Std/StdHashIndex.h"
#else
    #include "Core/Container/Native/NativeHashIndex.h"
#endif

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
     * @brief 색인과 밀집 노드 배열로 된 해시 표입니다. 이터레이터와 값을 만드는 삽입 함수는 파생 컨테이너가 둡니다.
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
        // 1) 생성 · 대입 — 색인 + 밀집 배열. 레이스 컨텍스트는 공유하지 않는다
        // ------------------------------------------------------------------------------
        /** @brief 빈 표로 둡니다. */
        DenseHashTable()
            : _index{}
            , _listDenseData{}
            , _traits{} {}

        /** @brief 복사 생성합니다. */
        DenseHashTable( const DenseHashTable& other )
            : _index{ other._index }
            , _listDenseData{ other._listDenseData }
            , _traits{ other._traits }
        {
            SW_SCOPED_RACE_READ_OTHER( other );
        }

        /** @brief 이동 생성합니다. */
        DenseHashTable( DenseHashTable&& other ) noexcept
            : _index{ std::move( other._index ) }
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
                _index         = other._index;
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
                _index         = std::move( other._index );
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
            _index.clear();
        }

        /** @brief 키와 같은 원소를 제거하고, 제거한 개수(0 또는 1)를 반환합니다. 마지막 원소가 빈 슬롯으로 옮겨집니다. */
        size_type erase( const Key& key )
        {
            SW_SCOPED_RACE_WRITE();
            const uint32 tag        = makeTag( get_hasher()( key ) );
            const uint32 foundIndex = _index.unlink( _listDenseData, tag, [this, &key]( const Node& node, uint32 )
            { return get_equal()( node.getKey(), key ); } );
            if ( foundIndex == kEmptySlot )
                return 0;
            removeNode( foundIndex );
            return 1;
        }

        /** @brief 버킷 수를 count 이상인 2의 거듭제곱으로 늘리고 재해시합니다. 지금보다 작으면 아무것도 하지 않습니다. */
        void rehash( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            _index.rehash( _listDenseData, count );
        }

        /**
         * @brief 원소 count 개를 더 할당하지 않고 넣을 만큼 버킷과 밀집 배열을 함께 잡습니다(언리얼 `TMap::Reserve`).
         * @details 버킷만 잡으면 밀집 배열이 삽입마다 키우기를 거듭한다(감사 C-9). 부하율 1 이라 버킷 수 ≥ count 면 넣는 동안 재해시도 없다.
         */
        void reserve( size_type count )
        {
            SW_SCOPED_RACE_WRITE();
            _listDenseData.reserve( count );
            _index.rehash( _listDenseData, count );
        }

        /**
         * @brief 남는 용량을 놓습니다. 비었으면 버킷 테이블과 밀집 배열의 버퍼를 모두 놓아 처음 상태로 돌아갑니다.
         * @details `clear()` 는 용량을 남긴다(다시 채울 때 할당하지 않도록). 버퍼까지 놓는 비우기는 `clear()` + `shrink_to_fit()` 이다.
         *          원소가 있으면 버킷 수를 원소 수 이상인 가장 작은 2의 거듭제곱으로 줄인다(기본 구성의 버킷 표).
         */
        void shrink_to_fit()
        {
            SW_SCOPED_RACE_WRITE();
            _listDenseData.shrink_to_fit();
            _index.shrink( _listDenseData );
        }

    protected:
        /** @brief 빈 자리를 나타내는 센티넬 값(모든 비트 1)입니다. */
        static constexpr uint32 kEmptySlot = constant::invalid_index::kUint32;

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
            if ( _listDenseData.empty() )
                return 0;
            const uint32 index = findByTag( key, makeTag( get_hasher()( key ) ) );
            return index != kEmptySlot ? index : _listDenseData.size();
        }

        /**
         * @brief 키가 있으면 그 인덱스와 false, 없으면 @p makeNode 가 만든 노드를 끝에 넣어 체인에 걸고 그 인덱스와 true 를 돌려줍니다. 키는 한 번만 해시합니다.
         * @param makeNode `Node( uint32 next, uint32 tag )` — 키가 없을 때만 불립니다(키가 있으면 넣으려던 값을 건드리지 않는다).
         * @details 키가 있으면 색인을 늘리지 않습니다. 없을 때만 색인을 키운 뒤 넣습니다.
         *          찾기 · 넣기 길의 도우미는 `SW_INLINE` 이다 — 컨테이너 함수마다 본문이 통째로 있던 예전과 같은 코드가 나오고, Debug 에서도 호출 층이 늘지 않는다.
         */
        template <typename TMakeNode>
        SW_INLINE pair<uint32, bool> getOrCreateNode( const Key& key, TMakeNode&& makeNode )
        {
            const uint32 tag        = makeTag( get_hasher()( key ) );
            const uint32 foundIndex = findByTag( key, tag );
            if ( foundIndex != kEmptySlot )
                return { foundIndex, false };

            _index.reserveForInsert( _listDenseData );
            const uint32 newIndex = static_cast<uint32>( _listDenseData.size() );
            _listDenseData.push_back( makeNode( kEmptySlot, tag ) );
            _index.link( _listDenseData, newIndex );
            return { newIndex, true };
        }

        /**
         * @brief 방금 밀집 배열 끝에 붙인 노드(체인에 걸리지 않은 것)를 체인에 겁니다. 같은 키가 이미 있으면 붙인 노드를 떼고 있던 인덱스와 false 를 돌려줍니다.
         * @details `emplace` 의 길입니다 — 키를 알려면 일단 만들어야 한다(표준 emplace 도 제자리에서 생성한다). 붙이기 전에 `check_expand()` 를 불러 둡니다.
         */
        pair<uint32, bool> linkAppendedNode()
        {
            const uint32 newIndex   = static_cast<uint32>( _listDenseData.size() - 1 );
            const Key&   key        = _listDenseData[newIndex].getKey();
            const uint32 tag        = makeTag( get_hasher()( key ) );
            const uint32 foundIndex = findByTag( key, tag ); // 붙인 노드는 아직 색인에 없다
            if ( foundIndex != kEmptySlot )
            {
                _listDenseData.pop_back();
                return { foundIndex, false };
            }

            _listDenseData[newIndex]._tag = tag;
            _index.link( _listDenseData, newIndex );
            return { newIndex, true };
        }

        /**
         * @brief 밀집 배열 번호로 노드를 지웁니다 — 이터레이터로 지우는 길이다. 키를 다시 해시하거나 견주지 않고 노드가 든 태그로 버킷을 찾아 체인에서 번호만 찾는다
         *        (표준의 `erase( const_iterator )` 가 평균 O(1) 이고 키를 비교하지 않는 것과 같다).
         */
        void eraseAt( size_t index )
        {
            SW_CHECK_INDEX( index, _listDenseData.size() );
            const uint32 targetIndex = static_cast<uint32>( index );
            const uint32 tag         = _listDenseData[targetIndex]._tag;
            _index.unlink( _listDenseData, tag, [targetIndex]( const Node&, uint32 candidateIndex )
            { return candidateIndex == targetIndex; } );
            removeNode( targetIndex );
        }

        /** @brief 노드 하나를 더 넣을 만큼 색인을 키웁니다(`emplace` 가 노드를 붙이기 전에 부른다). */
        void check_expand() { _index.reserveForInsert( _listDenseData ); }

    private:
        /** @brief 색인에서 뗀 노드 @p currentIndex 를 지우고 밀집 배열의 마지막 노드를 그 자리로 옮깁니다. */
        void removeNode( uint32 currentIndex )
        {
            bumpIteratorGeneration(); // 지운 자리로 마지막 원소가 옮겨 온다 — 옛 이터레이터가 다른 원소를 가리키게 된다
            const uint32 lastIndex = static_cast<uint32>( _listDenseData.size() - 1 );
            if ( currentIndex != lastIndex )
            {
                _listDenseData[currentIndex] = std::move( _listDenseData[lastIndex] );
                _index.relocate( _listDenseData, _listDenseData[currentIndex]._tag, lastIndex, currentIndex );
            }
            _listDenseData.pop_back();
        }

        /** @brief 해시를 32비트 태그로 접습니다. 위쪽 절반을 아래쪽 절반에 섞으므로 64비트 해시의 위쪽 비트도 버킷과 태그 비교에 반영됩니다. */
        static uint32 makeTag( size_t hash ) noexcept
        {
            const uint64 wide = static_cast<uint64>( hash );
            return static_cast<uint32>( wide ^ ( wide >> 32 ) );
        }

        /**
         * @brief 태그가 같고 키가 같은 노드 인덱스입니다. 없으면 `kEmptySlot` 입니다.
         * @details 키를 비교하기 전에 태그부터 견줍니다. 비교자가 이종 키를 받지 못하면(`std::equal_to<Key>`) 키의 `==` 로 견줍니다.
         */
        template <typename K>
        SW_INLINE uint32 findByTag( const K& key, uint32 tag ) const
        {
            return _index.find( _listDenseData, tag, [this, &key]( const Node& node, uint32 )
            {
                if constexpr ( std::is_invocable_v<KeyEqual, const Key&, const K&> )
                    return get_equal()( node.getKey(), key );
                else
                    return node.getKey() == key;
            } );
        }

    protected:
        DenseHashIndex<Node>              _index;         ///< 해시 태그 → 밀집 배열 번호
        vector<Node>                      _listDenseData; ///< 연속 메모리에 놓인 밀집 데이터 배열
        CompressedPair<hasher, key_equal> _traits;        ///< 해시 · 키 비교 함수 객체(EBO 로 압축 보관)
#if defined( SW_DO_CHECKS )
        uint32 _iteratorGeneration{ 0 }; ///< 원소 자리를 옮기는 변경마다 오르는 세대(검사 구성만 — 배포 sizeof 는 그대로)
#endif
        SW_RACE_CTX_MEMBER
    };
} // namespace sw::internal
