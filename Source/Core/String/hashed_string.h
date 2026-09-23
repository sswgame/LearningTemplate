/**
 * @file hashed_string.h
 * @brief 해시 기반 intern 문자열(basic_hashed_string)과 미리 정의된 이름의 열거형입니다.
 *
 * lock-free 청크 테이블과 문자열 아레나 위에 만든 intern 문자열입니다.
 * - c_str() · size() · getHash() 는 락 없이 O(1) 포인터 역참조로 바로 반환합니다.
 * - 문자열은 64KB 연속 아레나 블록에 모아 담아, 메모리 단편화와 문자열마다의 malloc 을 없앱니다.
 * - intern 생성 · 조회에는 32개로 나눈 샤드 뮤텍스를 써서 여러 스레드가 동시에 만들 수 있습니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

namespace sw
{

    /**
     * @enum PredefinedNameType
     * @brief PredefinedNameType.xxx 의 REGISTER_NAME 으로 만들어지는 미리 정의된 이름의 열거형입니다.
     */
    enum class PredefinedNameType : uint8
    {
#define REGISTER_NAME( index, name ) NameType_##name = ( index ),
#include "Core/Predefined/PredefinedNameType.xxx"

#undef REGISTER_NAME
        Count
    };

    /** @brief REGISTER_NAME 번호를 줄 순서대로 늘어놓은 표입니다(아래 static_assert 전용). */
    inline constexpr uint32 kArrPredefinedNameIndex[] = {
#define REGISTER_NAME( index, name ) static_cast<uint32>( PredefinedNameType::NameType_##name ),
#include "Core/Predefined/PredefinedNameType.xxx"

#undef REGISTER_NAME
    };

    /** @brief 미리 정의된 이름 번호가 0 부터 빈틈없이 이어지는지 확인합니다. */
    constexpr bool arePredefinedNameIndicesContiguous() noexcept
    {
        for ( uint32 nameIndex = 0; nameIndex < static_cast<uint32>( SW_COUNT_OF( kArrPredefinedNameIndex ) ); ++nameIndex )
        {
            if ( kArrPredefinedNameIndex[nameIndex] != nameIndex )
                return false;
        }
        return true;
    }

    // 번호가 곧 intern 인덱스다. 미리 정의된 이름은 빈 테이블에 줄 순서대로 적재되고, basic_hashed_string 은
    // PredefinedNameType 을 그대로 인덱스로 캐스팅한다. 중간에 줄 하나를 끼워 넣고 번호를 다시 매기지 않으면 그 뒤의
    // 이름이 모두 다른 문자열을 가리킨다.
    static_assert( arePredefinedNameIndicesContiguous(),
                   "PredefinedNameType.xxx: REGISTER_NAME 번호는 줄 순서(0부터)와 같아야 합니다." );
} // namespace sw

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) basic_hashed_string — intern 인덱스만 들고 있고, 비교는 4바이트 정수 비교
    // ------------------------------------------------------------------------------
    template <typename T, typename N = uint32>
    /**
     * @class basic_hashed_string
     * @brief intern 테이블 인덱스로 O(1) 정수 비교를 하는 불변 문자열입니다.
     */
    class basic_hashed_string
    {
        using value_type = T;
        using size_type  = N;
        using hash_type  = N;

    public:
        static constexpr uint32 kChunkShift     = 10;                  /**< 청크 크기의 비트 시프트(1024 = 2^10) */
        static constexpr uint32 kChunkSize      = 1u << kChunkShift;   /**< 청크 하나의 엔트리 수(1024) */
        static constexpr uint32 kChunkMask      = kChunkSize - 1u;     /**< 청크 안 오프셋 마스크 */
        static constexpr uint32 kMaxChunks      = 64;                  /**< 최대 청크 수(총 65,536개) */
        static constexpr uint32 kNumShards      = 32;                  /**< 해시로 나눈 락 샤드 수 */
        static constexpr size_t kArenaBlockSize = size_t{ 64 } * 1024; /**< 문자열 아레나 블록 크기(64KB) */

        /** @brief intern 해시를 unordered_map 키로 쓰기 위한 해시 함수 객체입니다. */
        struct HashFunc
        {
            /** @brief intern 해시 값을 size_t 로 반환합니다. */
            size_t operator()( const basic_hashed_string& key ) const noexcept { return key.getHash(); }
        };

        /** @brief NameType_None 인덱스인 빈 이름으로 둡니다. */
        basic_hashed_string() noexcept
            : _stringKeyIndex{ static_cast<uint32>( PredefinedNameType::NameType_None ) } {}

        /** @brief 미리 정의된 이름 타입으로 바로 만듭니다(O(1)). */
        explicit basic_hashed_string( PredefinedNameType type ) noexcept
            : _stringKeyIndex{ static_cast<uint32>( type ) } {}

        /** @brief 문자열을 length 만큼 intern 하고 그 인덱스를 가집니다. */
        basic_hashed_string( const value_type* pStr, const size_type length ) noexcept
            : _stringKeyIndex{ helper( pStr, length ) } {}

        /** @brief string_view 를 intern 하고 그 인덱스를 가집니다. */
        explicit basic_hashed_string( const std::basic_string_view<value_type> sv ) noexcept
            : _stringKeyIndex{ helper( sv.data(), static_cast<size_type>( sv.size() ) ) } {}

        /** @brief 배열 버퍼를 intern 하고 그 인덱스를 가집니다. */
        template <size_t U>
        explicit basic_hashed_string( const std::array<value_type, U>& scopedString ) noexcept
            : _stringKeyIndex{ helper( scopedString.data() ) } {}

        /**
         * @brief 리터럴 배열을 intern 하고 그 인덱스를 가집니다. **암시적 변환**이라 `isActionDown( "Jump" )` 처럼 쓸 수 있습니다.
         * @details 리터럴은 컴파일 타임에 정해진 유한 집합이라 intern 이 계속 늘어날 수 없습니다. 포인터 · `string_view` · `string`
         *          에서의 변환은 그대로 explicit 입니다. 동적 텍스트를 이름으로 올리는 곳은 눈에 보여야 하기 때문입니다(StringTable
         *          처럼 조회만 하려는 텍스트를 intern 하면 그것이 곧 누수입니다). 같은 이름의 함수에 `string_view` 판과
         *          `hashed_string` 판을 **둘 다** 두면 리터럴 호출이 모호해지므로, 그런 쌍은 두지 않습니다(ActionMap 의 쌍 37개를
         *          이 규칙으로 걷어 냈습니다).
         */
        template <size_type U>
        basic_hashed_string( const value_type ( &str )[U] ) noexcept
            : _stringKeyIndex{ helper( str ) } {}

        /** @brief 널 종료 문자열을 intern 하고 그 인덱스를 가집니다. */
        explicit basic_hashed_string( const T* pStr ) noexcept
            : _stringKeyIndex{ helper( pStr ) } {}

        /**
         * @brief intern 된 문자 수(널 제외)입니다. 락 없이 O(1) 로 조회합니다.
         */
        size_type size() const noexcept;

        /**
         * @brief intern 테이블에 있는 널 종료 C 문자열입니다. 락 없이 O(1) 로 조회합니다.
         */
        const value_type* c_str() const noexcept;

        /**
         * @brief intern 된 문자열의 뷰입니다. 락 없이 O(1) 로 조회합니다.
         */
        std::basic_string_view<value_type> view() const noexcept { return { c_str(), size() }; }

        /**
         * @brief intern 키의 FNV 해시입니다. 락 없이 O(1) 로 조회합니다.
         */
        hash_type getHash() const noexcept;

        /**
         * @brief 문자열을 **intern 하지 않고** 해시만 계산합니다.
         * @details `getHash()` 와 같은 값이 나옵니다. intern 여부와 상관없이 같은 해시 규칙을 씁니다. 해시로만 여는 표(`StringTable`
         *          등)를 **조회**할 때 씁니다. 조회하려고 intern 하면 그 문자열이 아레나에 영원히 남습니다. 대사 원문처럼 키가 아닌
         *          텍스트로 묻는 곳에서는 그것이 곧 계속 늘어나는 누수입니다.
         */
        static constexpr hash_type computeHash( std::basic_string_view<value_type> text ) noexcept
        {
            if ( text.empty() )
                return static_cast<hash_type>( 0 );
            if constexpr ( std::is_same_v<hash_type, uint32> )
                return StringUtil::computeHash32( text.data(), text.size() );
            else
                return StringUtil::computeHash64( text.data(), text.size() );
        }

        /** @brief 비어 있는지 반환합니다. */
        bool empty() const noexcept { return _stringKeyIndex == static_cast<uint32>( PredefinedNameType::NameType_None ) || size() == 0; }

        /** @brief intern 인덱스를 반환합니다. */
        uint32 getIndex() const noexcept { return _stringKeyIndex; }

        /** @brief 지정한 미리 정의된 이름 타입인지 확인합니다. */
        bool isPredefinedType( PredefinedNameType type ) const noexcept
        {
            return _stringKeyIndex == static_cast<uint32>( type );
        }

        /** @brief 미리 정의된 이름 타입을 반환합니다. 미리 정의된 이름이 아니면 NameType_None 입니다. */
        PredefinedNameType getPredefinedType() const noexcept
        {
            if ( _stringKeyIndex < static_cast<uint32>( PredefinedNameType::Count ) )
                return static_cast<PredefinedNameType>( _stringKeyIndex );
            return PredefinedNameType::NameType_None;
        }

    private:
        /** @brief 문자열의 intern 인덱스를 구합니다. */
        static uint32 helper( const T* pStr ) noexcept { return helper( pStr, StringUtil::strlen( pStr ) ); }

        /** @brief 전역 intern 테이블에서 인덱스를 찾거나 넣습니다. */
        static uint32 helper( const T* pStr, size_type length ) noexcept;

    public:
        /** @brief intern 맵 · 청크 테이블 · 아레나 저장소입니다. */
        struct AllocationInfo;

    private:
        /** @brief 해시 맵 조회와 엔트리 키에 쓰는 구조체입니다. */
        struct StringKey;

        /** @brief 지정한 intern 테이블에 넣고 그 인덱스를 가집니다(미리 정의된 이름용). */
        basic_hashed_string( AllocationInfo& info, const T* pStr ) noexcept
            : _stringKeyIndex{ helper_internal( info, pStr, StringUtil::strlen( pStr ) ) } {}

        /** @brief info 테이블에서 찾거나 사본을 넣어 인덱스를 반환합니다. */
        static uint32 helper_internal( AllocationInfo& info, const T* pStr, size_type length ) noexcept;

        /** @brief utf8 · utf16 전역 intern 테이블입니다. */
        static AllocationInfo& getAllocationInfo() noexcept;

        uint32 _stringKeyIndex;
    };

    /** @brief 같은지 비교합니다(인덱스 정수 비교). */
    template <typename T>
    bool operator==( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() == rhs.getIndex(); }

    /** @brief 다른지 비교합니다. */
    template <typename T>
    bool operator!=( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() != rhs.getIndex(); }

    /** @brief 사전순으로 작은지 비교합니다. */
    template <typename T>
    bool operator<( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() < rhs.getIndex(); }

    /** @brief 작거나 같은지 비교합니다. */
    template <typename T>
    bool operator<=( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() <= rhs.getIndex(); }

    /** @brief 사전순으로 큰지 비교합니다. */
    template <typename T>
    bool operator>( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() > rhs.getIndex(); }

    /** @brief 크거나 같은지 비교합니다. */
    template <typename T>
    bool operator>=( const basic_hashed_string<T>& lhs, const basic_hashed_string<T>& rhs ) noexcept { return lhs.getIndex() >= rhs.getIndex(); }

    using hashed_string  = basic_hashed_string<utf8>;
    using hashed_wstring = basic_hashed_string<utf16>;

    // ------------------------------------------------------------------------------
    // 2) intern 테이블 — Engine.dll(Core OBJECT)만 소유하고, 모든 모듈은 이 export 만 쓴다
    // ------------------------------------------------------------------------------
    /** @brief hashed_string 의 intern 테이블입니다. */
    struct SW_API HashedStringPool
    {
        /** @brief intern 테이블을 초기화합니다. */
        static void initialize() noexcept;
        /** @brief intern 테이블을 비웁니다. 그 뒤 hashed_string 을 쓰면 UB 입니다. */
        static void shutdown() noexcept;
    };

    template <>
    SW_API hashed_string::AllocationInfo& hashed_string::getAllocationInfo() noexcept;

    template <>
    SW_API hashed_wstring::AllocationInfo& hashed_wstring::getAllocationInfo() noexcept;
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) StringKey — 샤드 맵 검색용 키(비소유 포인터)
    // ------------------------------------------------------------------------------
    template <typename T, typename N>
    struct basic_hashed_string<T, N>::StringKey final
    {
        hash_type         _hash{ 0 };
        const value_type* _pStr{ nullptr };
        size_type         _stringLength{ 0 };

        /** @brief FNV 해시를 size_t 로 반환하는 해시 함수 객체입니다. */
        struct HashFunc
        {
            size_t operator()( const StringKey& key ) const noexcept
            {
                return static_cast<size_t>( key._hash );
            }
        };

        /** @brief 대소문자를 무시하고 같은지 비교합니다(해시 → 길이 → 문자열 순서로 빠르게 검사). */
        bool operator==( const StringKey& rhs ) const noexcept
        {
            if ( _hash != rhs._hash || _stringLength != rhs._stringLength )
                return false;
            return StringUtil::equals( std::basic_string_view<T>( _pStr, _stringLength ), std::basic_string_view<T>( rhs._pStr, rhs._stringLength ), true );
        }
    };

    // ------------------------------------------------------------------------------
    // 4) AllocationInfo — 청크 테이블 + 64KB 문자열 아레나 + 32개 샤드 뮤텍스
    // ------------------------------------------------------------------------------
    /**
     * @struct AllocationInfo
     * @brief 문자열 intern 풀의 저장소이자 검색 엔진입니다.
     * @details
     * - **청크 테이블(`_arrChunk`)**:
     *   1024개 단위의 Entry 청크 배열(`atomic<Entry*> _arrChunk[64]`)을 관리합니다. 인덱스를 `chunkIndex = id / 1024`,
     *   `offset = id % 1024` 로 나누기만 하면 되므로, 뮤텍스 없이 **O(1) 포인터 역참조**로 문자열 포인터 · 길이 · 해시를
     *   바로 반환합니다.
     * - **문자열 아레나(`_listArenaBlock`)**:
     *   짧은 문자열마다 `new[]` 를 따로 부르면 힙 단편화가 생기므로, 64KB 연속 메모리 블록을 미리 할당하고 차례로 채워 넣어
     *   **힙 할당 횟수를 99% 이상 줄입니다.**
     * - **32개 샤드 뮤텍스(`_arrShard`)**:
     *   여러 스레드에서 intern 요청이 몰릴 때 락 하나에 경합이 생기지 않도록, 해시 상위 비트로 32개의 독립된
     *   `shared_mutex` 와 `unordered_map` 에 나눠 처리합니다.
     */
    template <typename T, typename N>
    struct basic_hashed_string<T, N>::AllocationInfo
    {
        friend class basic_hashed_string<T, N>;

        /** @brief 청크 엔트리입니다. 락 없이 읽을 수 있도록 바뀌지 않는 데이터 슬롯입니다. */
        struct Entry
        {
            const value_type* _pStr{ nullptr };   ///< 아레나에 적재된 불변 C 문자열 포인터
            size_type         _stringLength{ 0 }; ///< 문자열 길이(널 제외)
            hash_type         _hash{ 0 };         ///< 미리 계산한 FNV 해시 값
        };

        /** @brief 32개로 나눈 맵 샤드입니다(각자 shared_mutex 로 보호합니다). */
        struct Shard
        {
            mutable std::shared_mutex                                      _mutex;         ///< 이 샤드 전용 읽기/쓰기 락
            unordered_map<StringKey, uint32, typename StringKey::HashFunc> _mapKeyToIndex; ///< 문자열 키 → 청크 인덱스 해시맵
        };

        atomic<Entry*> _arrChunk[kMaxChunks]{}; /**< 1024 단위 엔트리 청크의 원자 포인터 배열(락 없는 O(1) 조회) */
        atomic<uint32> _entryCount{ 0 };        /**< 지금까지 등록된 문자열 수(단조 증가하는 인덱스) */
        mutex          _globalAppendMutex;      /**< 새 청크를 만들거나 64KB 아레나 블록을 더할 때 쓰는 뮤텍스 */

        vector<value_type*> _listArenaBlock;                /**< 64KB 단위 연속 문자열 아레나 블록 목록 */
        value_type*         _pCurrentArenaBlock{ nullptr }; ///< 지금 문자열을 채우고 있는 활성 아레나 블록
        size_t              _arenaOffset{ 0 };              ///< 활성 아레나 블록 안에서 다음에 쓸 위치
        vector<value_type*> _listLargeAllocation;           /**< 64KB 를 넘는 큰 문자열 전용 개별 힙 블록 목록 */

        Shard _arrShard[kNumShards]; /**< 32개 샤드 해시맵 배열 */

        /** @brief intern 테이블 생성자입니다(0번 청크와 미리 정의된 이름을 미리 적재합니다). */
        AllocationInfo()
        {
            initializeStorage();
        }

        /**
         * @brief 0번 청크를 잡고 미리 정의된 이름을 적재합니다. **빈 테이블에서만 부릅니다.**
         * @details 생성자와 `HashedStringPool::initialize` 가 함께 씁니다. 후자가 필요한 이유는 그 인스턴스가 **함수 지역 static**
         *          이라서 두 번째 initialize 에서는 생성자가 돌지 않기 때문입니다. `clear()` 가 0번 청크까지 돌려준 뒤라 그대로 두면
         *          빈 테이블을 가리키게 됩니다.
         */
        void initializeStorage()
        {
            // 0번 청크를 미리 할당해 미리 정의된 이름을 적재한다
            constexpr size_t chunkSize   = sizeof( Entry ) * kChunkSize;
            Entry*           pFirstChunk = static_cast<Entry*>( Memory::allocate( chunkSize ) );
            Memory::set( pFirstChunk, 0, chunkSize );
            _arrChunk[0].store( pFirstChunk, std::memory_order_release );

            createPredefinedNameTypes();

            // 두 이름이 대소문자만 다르면 intern 테이블이 둘을 하나로 합치고(비교와 해시가 대소문자를 무시한다), 그 뒤 이름의
            // 인덱스가 한 칸씩 밀려 열거값과 어긋난다. 번호가 줄 순서와 맞는지는 static_assert 가 확인하지만, 합쳐진 것은
            // 여기서만 보인다.
            SW_ASSERT( _entryCount.load( std::memory_order_relaxed ) == static_cast<uint32>( PredefinedNameType::Count ) );
        }

        /** @brief 모든 메모리 블록 · 청크 · 맵을 한꺼번에 해제합니다. */
        void clear() noexcept
        {
            std::unique_lock<std::shared_mutex> arrShardLock[kNumShards];
            for ( uint32 shardIndex = 0; shardIndex < kNumShards; ++shardIndex )
            {
                arrShardLock[shardIndex] = std::unique_lock<std::shared_mutex>( _arrShard[shardIndex]._mutex );
                _arrShard[shardIndex]._mapKeyToIndex.clear();
            }

            std::scoped_lock<mutex> globalLock{ _globalAppendMutex };

            for ( uint32 chunkIndex = 0; chunkIndex < kMaxChunks; ++chunkIndex )
            {
                Entry* pChunk = _arrChunk[chunkIndex].exchange( nullptr, std::memory_order_acq_rel );
                if ( pChunk != nullptr )
                    Memory::free( pChunk );
            }

            for ( value_type* pBlock : _listArenaBlock )
            {
                Memory::free( pBlock );
            }
            _listArenaBlock.clear();
            _pCurrentArenaBlock = nullptr;
            _arenaOffset        = 0;

            for ( value_type* largeBlock : _listLargeAllocation )
            {
                Memory::free( largeBlock );
            }
            _listLargeAllocation.clear();

            _entryCount.store( 0, std::memory_order_release );
        }

        /** @brief 문자열 아레나에서 연속 공간을 받아 문자열을 복사합니다. */
        const value_type* allocateString( const value_type* pStr, const size_type length )
        {
            const size_t requiredChars = static_cast<size_t>( length ) + 1;
            const size_t requiredBytes = requiredChars * sizeof( value_type );

            // 64KB 를 넘는 큰 문자열은 따로 할당한다
            if ( requiredBytes > kArenaBlockSize )
            {
                value_type* pLargeBuf = static_cast<value_type*>( Memory::allocate( requiredBytes ) );
                std::char_traits<value_type>::copy( pLargeBuf, pStr, length );
                pLargeBuf[length] = static_cast<value_type>( 0 );
                _listLargeAllocation.push_back( pLargeBuf );
                return pLargeBuf;
            }

            // 현재 블록의 공간이 모자라면 새 64KB 블록을 할당한다
            if ( _pCurrentArenaBlock == nullptr || ( _arenaOffset + requiredChars ) * sizeof( value_type ) > kArenaBlockSize )
            {
                _pCurrentArenaBlock = static_cast<value_type*>( Memory::allocate( kArenaBlockSize ) );
                _listArenaBlock.push_back( _pCurrentArenaBlock );
                _arenaOffset = 0;
            }

            value_type* pDest = _pCurrentArenaBlock + _arenaOffset;
            std::char_traits<value_type>::copy( pDest, pStr, length );
            pDest[length] = static_cast<value_type>( 0 );
            _arenaOffset += requiredChars;
            return pDest;
        }

    private:
        /** @brief PredefinedNameType.xxx 의 이름들을 intern 합니다. */
        void createPredefinedNameTypes()
        {
            if constexpr ( std::is_same_v<T, utf16> )
            {
#define REGISTER_NAME( index, name ) basic_hashed_string<T, N> predefined_##name{ *this, SW_CONCAT( L, #name ) };
#include "Core/Predefined/PredefinedNameType.xxx"
#undef REGISTER_NAME
            }
            else
            {
#define REGISTER_NAME( index, name ) basic_hashed_string<T, N> predefined_##name{ *this, #name };
#include "Core/Predefined/PredefinedNameType.xxx"
#undef REGISTER_NAME
            }
        }
    };

    /** @brief intern 된 문자열의 길이를 락 없이 O(1) 로 반환합니다. */
    template <typename T, typename N>
    typename basic_hashed_string<T, N>::size_type basic_hashed_string<T, N>::size() const noexcept
    {
        auto&        info       = getAllocationInfo();
        const uint32 chunkIndex = _stringKeyIndex >> kChunkShift;
        const uint32 offset     = _stringKeyIndex & kChunkMask;

        if ( chunkIndex < kMaxChunks )
        {
            const auto* chunk = info._arrChunk[chunkIndex].load( std::memory_order_acquire );
            if ( chunk != nullptr )
                return chunk[offset]._stringLength;
        }
        return 0;
    }

    /** @brief 널 종료 C 문자열 포인터를 락 없이 O(1) 로 반환합니다. */
    template <typename T, typename N>
    const typename basic_hashed_string<T, N>::value_type* basic_hashed_string<T, N>::c_str() const noexcept
    {
        auto&        info       = getAllocationInfo();
        const uint32 chunkIndex = _stringKeyIndex >> kChunkShift;
        const uint32 offset     = _stringKeyIndex & kChunkMask;

        if ( chunkIndex < kMaxChunks )
        {
            const auto* chunk = info._arrChunk[chunkIndex].load( std::memory_order_acquire );
            if ( chunk != nullptr )
                return chunk[offset]._pStr;
        }
        return nullptr;
    }

    /** @brief intern 키의 FNV 해시 값을 락 없이 O(1) 로 반환합니다. */
    template <typename T, typename N>
    typename basic_hashed_string<T, N>::hash_type basic_hashed_string<T, N>::getHash() const noexcept
    {
        auto&        info       = getAllocationInfo();
        const uint32 chunkIndex = _stringKeyIndex >> kChunkShift;
        const uint32 offset     = _stringKeyIndex & kChunkMask;

        if ( chunkIndex < kMaxChunks )
        {
            const auto* chunk = info._arrChunk[chunkIndex].load( std::memory_order_acquire );
            if ( chunk != nullptr )
                return chunk[offset]._hash;
        }
        return 0;
    }

    /** @brief 전역 intern 테이블에서 인덱스를 찾거나 넣습니다. */
    template <typename T, typename N>
    uint32 basic_hashed_string<T, N>::helper( const T* str, const size_type length ) noexcept
    {
        auto& info = getAllocationInfo();
        return helper_internal( info, str, length );
    }

    /**
     * @brief 32개 샤드의 공유 락으로 빠르게 조회하고, 새 문자열만 아레나에 적재해 청크에 등록합니다.
     */
    template <typename T, typename N>
    uint32 basic_hashed_string<T, N>::helper_internal( AllocationInfo& info, const T* str, size_type length ) noexcept
    {
        if ( str == nullptr || length == 0 )
            return static_cast<uint32>( PredefinedNameType::NameType_None );

        hash_type hash{};
        if constexpr ( std::is_same_v<hash_type, uint32> )
            hash = StringUtil::computeHash32( str, length );
        else if constexpr ( std::is_same_v<hash_type, uint64> )
            hash = StringUtil::computeHash64( str, length );

        const uint32 shardIndex = ( hash ^ ( hash >> 16 ) ) % kNumShards;
        auto&        shard      = info._arrShard[shardIndex];
        StringKey    lookupKey{ hash, str, length };

        // 1단계: 샤드 공유 락으로 빠르게 조회한다(이미 등록된 문자열은 대부분 여기서 끝난다)
        {
            std::shared_lock<std::shared_mutex> readLock{ shard._mutex };
            const auto                          iter = shard._mapKeyToIndex.find( lookupKey );
            if ( iter != shard._mapKeyToIndex.end() )
                return iter->second;
        }

        // 2단계: 새 문자열 등록. 샤드 배타 락 + 전역 할당 락
        std::unique_lock<std::shared_mutex> writeLock{ shard._mutex };

        // 다시 확인한다(double-check)
        const auto iter = shard._mapKeyToIndex.find( lookupKey );
        if ( iter != shard._mapKeyToIndex.end() )
            return iter->second;

        std::scoped_lock<mutex> allocLock{ info._globalAppendMutex };

        const uint32 newIndex = info._entryCount.load( std::memory_order_relaxed );
        if ( newIndex >= kMaxChunks * kChunkSize )
        {
            SW_LOG_ASSERT( false, "hashed_string 풀 용량(65,536개)을 초과했습니다!" );
            return static_cast<uint32>( PredefinedNameType::NameType_None );
        }

        const uint32 chunkIndex = newIndex >> kChunkShift;
        const uint32 offset     = newIndex & kChunkMask;

        auto* chunk = info._arrChunk[chunkIndex].load( std::memory_order_relaxed );
        if ( chunk == nullptr )
        {
            constexpr size_t chunkSize = sizeof( typename AllocationInfo::Entry ) * kChunkSize;
            chunk                      = static_cast<typename AllocationInfo::Entry*>( Memory::allocate( chunkSize ) );
            Memory::set( chunk, 0, chunkSize );
            info._arrChunk[chunkIndex].store( chunk, std::memory_order_release );
        }

        const value_type* internedStr = info.allocateString( str, length );

        chunk[offset]._pStr         = internedStr;
        chunk[offset]._stringLength = length;
        chunk[offset]._hash         = hash;

        info._entryCount.store( newIndex + 1, std::memory_order_release );

        StringKey permanentKey{ hash, internedStr, length };
        shard._mapKeyToIndex.emplace( permanentKey, newIndex );

        return newIndex;
    }
} // namespace sw

namespace std
{
    template <typename T, typename N>
    /** @brief intern 해시를 std::unordered_map 키로 쓸 수 있게 하는 특수화입니다. */
    struct hash<sw::basic_hashed_string<T, N>>
    {
        /** @brief intern 해시 값을 size_t 로 반환합니다. */
        size_t operator()( const sw::basic_hashed_string<T, N>& key ) const noexcept { return key.getHash(); }
    };

    template <typename T, typename N>
    /** @brief intern 인덱스가 같으면 같은 이름으로 보는 std::equal_to 특수화입니다. */
    struct equal_to<sw::basic_hashed_string<T, N>>
    {
        /** @brief 인덱스가 같으면 true 입니다. */
        bool operator()( const sw::basic_hashed_string<T, N>& lhs, const sw::basic_hashed_string<T, N>& rhs ) const noexcept { return lhs.getIndex() == rhs.getIndex(); }
    };
} // namespace std
