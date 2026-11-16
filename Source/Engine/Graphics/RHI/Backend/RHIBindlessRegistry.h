/**
 * @file RHIBindlessRegistry.h
 * @brief bindless 번호 공간 하나의 등록표입니다 — 번호 발급 · 생존 · 해제 대기(GPU 펜스 뒤 재사용) · 이중 해제 거절을 한 곳에서 지킵니다.
 * @details 다섯 백엔드가 같은 표(값 배열 + `IndexFreeList` + 손으로 쓴 해제 대기 상태)를 따로 쓰던 것을 모은다. 백엔드는 값의 뜻(어느 자원인가)과
 *          디스크립터 쓰기 훅(Vulkan `writeBindlessTextureSlot` · DX12 `CreateShaderResourceView`)만 맡는다.
 *          잠금은 갖지 않는다 — `RHIRetireList` 처럼 쓰는 쪽이 자기 `_bindlessMutex` 아래에서 부른다(그 잠금이 상수 버퍼 크기 표 같은 이웃 상태도 함께 지킨다).
 *          언리얼 `FD3D12BindlessDescriptorAllocator`(번호 할당 + 펜스 뒤 반납)와 Godot `RID_Owner` 의 빈 칸 재사용과 같은 자리다(기억에 의존한 비교).
 *          번호 발급기(`RHIBindlessIndexAllocator`)는 따로 둔다 — DX12 처럼 한 힙 번호 공간을 SRV · UAV 표 둘이 나누면 두 등록표가 발급기 하나를 함께 쓴다.
 */
#pragma once
#include "Core/Container/IndexFreeList.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /** @brief 비운 번호를 언제 다시 내는가입니다(`RHIBindlessRegistry::release`). */
    enum class RHIBindlessReuse : uint8
    {
        Immediately, ///< 바로 빈 목록으로 — 기록 때 번호를 자원으로 푸는 백엔드(DX11 · GL · WebGPU 에뮬). 기록이 끝난 번호를 GPU 가 읽지 않는다
        AfterFence   ///< `finishRelease` 까지 대기 — GPU 가 번호로 디스크립터를 읽는 백엔드(Vulkan · DX12)
    };

    /**
     * @brief 등록표 칸 하나가 어느 자원의 번호인가입니다. 버퍼 · 텍스처 핸들은 값 공간이 겹치므로(둘 다 `uint64`) 종류를 따로 든다. 0 이면 그 종류의 주인이 없다.
     */
    struct RHIBindlessOwner
    {
        RHIBufferHandle  _buffer{ 0 };
        RHITextureHandle _texture{ 0 };
    };

    /** @brief 값 형식이 스스로 주인을 말하는 등록표의 주인 풀이입니다 — `TEntry::getBindlessOwner()` 를 부른다. */
    template <typename TEntry>
    struct RHIBindlessOwnerOf
    {
        RHIBindlessOwner operator()( const TEntry& entry ) const { return entry.getBindlessOwner(); }
    };
    /** @brief 값이 버퍼 핸들 그대로인 등록표의 주인 풀이입니다(`RHIBufferHandle` 은 `uint64` 라 멤버 함수를 둘 수 없고, 텍스처 핸들과 형식이 같다). */
    struct RHIBindlessBufferOwnerOf
    {
        RHIBindlessOwner operator()( RHIBufferHandle buffer ) const { return RHIBindlessOwner{ buffer, 0 }; }
    };
    /** @brief 값이 텍스처 핸들 그대로인 등록표의 주인 풀이입니다. */
    struct RHIBindlessTextureOwnerOf
    {
        RHIBindlessOwner operator()( RHITextureHandle texture ) const { return RHIBindlessOwner{ 0, texture }; }
    };

    /**
     * @class RHIBindlessIndexAllocator
     * @brief 번호 공간 하나의 발급기입니다 — 돌려받은 번호부터, 없으면 다음 새 번호(상한까지)를 냅니다. 잠금은 등록표와 같이 쓰는 쪽 것이다.
     */
    class RHIBindlessIndexAllocator
    {
    public:
        /** @brief @p firstIndex 부터 @p capacity 미만의 번호를 내는 발급기입니다(앞 번호는 고정 디스크립터 — DX12 null 텍스처 — 의 자리일 수 있다). */
        explicit RHIBindlessIndexAllocator( uint32 capacity = kInvalidDescriptorIndex, uint32 firstIndex = 0 )
            : _freeIndex{}
            , _nextIndex{ firstIndex }
            , _capacity{ capacity }
        {
        }

        /** @brief 돌려받은 번호를 잊고 @p firstIndex 부터 다시 냅니다(장치를 세우거나 놓을 때). */
        void reset( uint32 firstIndex )
        {
            _freeIndex.clear();
            _nextIndex = firstIndex;
        }
        /** @brief 번호 상한을 정합니다. */
        void setCapacity( uint32 capacity ) { _capacity = capacity; }
        /** @brief 번호 하나를 냅니다. 상한에 닿았으면 false 입니다. */
        [[nodiscard]] bool tryAllocate( uint32& outIndex )
        {
            if ( _freeIndex.tryPop( outIndex ) )
                return true;
            if ( _nextIndex >= _capacity )
                return false;
            outIndex = _nextIndex++;
            return true;
        }
        /** @brief 번호를 돌려받습니다(산 칸이 아님은 부르는 등록표가 확인했다). */
        void free( uint32 index ) { _freeIndex.push( index ); }

    private:
        IndexFreeList _freeIndex;
        uint32        _nextIndex;
        uint32        _capacity;
    };

    /**
     * @class RHIBindlessRegistry
     * @brief 번호 → 값(`TEntry`) 표입니다. `TEntry{}` 와 같은 값이 빈 칸입니다(`==` 로 비교할 수 있어야 한다).
     * @details 칸의 상태는 셋이다: 빈 칸(빈 목록에 있다) · 산 칸(값이 있다) · 해제 대기(값은 비었지만 빈 목록에 아직 없다 — `finishRelease` 가 넣는다).
     *          해제 대기 칸은 아무에게도 다시 나가지 않으므로 GPU 가 아직 읽는 디스크립터를 새 자원이 덮지 않는다.
     *          산 칸은 주인(`TOwnerOf` 가 값에서 푼 버퍼 · 텍스처 핸들)별 번호 목록에도 오른다. 자원을 부술 때 `releaseBuffer` · `releaseTexture` 가 그 자원의
     *          번호만 놓는다 — 칸 전체를 술어로 훑던 순회를 없앤 자리다(레벨 언로드로 1 만 개를 부수면 1 만 × 칸 수 비교가 `_bindlessMutex` 아래에서 돌았다).
     *          언리얼은 뷰가 자기 bindless 핸들(`FRHIDescriptorHandle`)을 들고 해제 때 그 번호만 돌려준다. 여기서는 그 목록을 등록표가 들어, 백엔드마다 자원
     *          레코드가 따로 목록을 들다 한 표(텍스처 UAV)를 빠뜨리는 일이 생길 수 없다.
     * @code
     *     UniqueLock<shared_mutex> lock{ _bindlessMutex };
     *     const RHIDescriptorIndex index = _textureRegistry.allocate( texture );     // 꽉 차면 kInvalidDescriptorIndex
     *     if ( _textureRegistry.release( index, RHIBindlessReuse::AfterFence ) )   // 비었으면 false(이중 해제)
     *         enqueueGPURelease( [=] { lock; _textureRegistry.finishRelease( index ); }, key );
     *     _textureRegistry.releaseTexture( RHIBindlessReuse::AfterFence, texture, fnDefer ); // 텍스처를 부술 때 그 번호만
     * @endcode
     */
    template <typename TEntry, typename TOwnerOf = RHIBindlessOwnerOf<TEntry>>
    class RHIBindlessRegistry
    {
    public:
        /** @brief @p capacity 개까지 번호를 내는 표를 만듭니다(디스크립터 배열 크기 — 없으면 상한 없음). 발급기는 표가 가진다. */
        explicit RHIBindlessRegistry( uint32 capacity = kInvalidDescriptorIndex )
            : _listEntry{}
            , _mapBufferIndex{}
            , _mapTextureIndex{}
            , _ownAllocator{ capacity }
            , _pAllocator{ &_ownAllocator }
        {
        }
        /** @brief 다른 표와 번호 공간을 나누는 표입니다. @p sharedAllocator 는 이 표보다 오래 살고, 비우기(`reset`)는 그 주인이 한다. */
        explicit RHIBindlessRegistry( RHIBindlessIndexAllocator& sharedAllocator )
            : _listEntry{}
            , _mapBufferIndex{}
            , _mapTextureIndex{}
            , _ownAllocator{}
            , _pAllocator{ &sharedAllocator }
        {
        }
        RHIBindlessRegistry( const RHIBindlessRegistry& )            = delete; // 발급기 포인터가 자기 멤버를 가리킨다
        RHIBindlessRegistry& operator=( const RHIBindlessRegistry& ) = delete;

        /** @brief 모든 칸을 잊습니다. 자기 발급기면 빈 목록 · 해제 대기도 함께 잊는다(빈 목록만 남으면 다음 장치가 표 밖 번호를 낸다). 나눈 발급기는 주인이 `reset` 한다. */
        void clear()
        {
            _listEntry.clear();
            _mapBufferIndex.clear();
            _mapTextureIndex.clear();
            if ( _pAllocator == &_ownAllocator )
                _ownAllocator.reset( 0 );
        }

        /**
         * @brief @p entry 를 빈 칸에 넣고 번호를 줍니다. 빈 값이거나 표가 차면 kInvalidDescriptorIndex 입니다.
         * @param fnWrite 번호가 정해진 뒤, 값을 넣기 전에 디스크립터를 쓰는 훅입니다(`bool( RHIDescriptorIndex )`). false 면 칸을 바로 돌려받고 실패합니다.
         */
        template <typename FnWrite>
        [[nodiscard]] RHIDescriptorIndex allocate( const TEntry& entry, FnWrite&& fnWrite )
        {
            if ( entry == TEntry{} )
                return kInvalidDescriptorIndex;
            uint32 index{ 0 };
            if ( _pAllocator->tryAllocate( index ) == false )
                return kInvalidDescriptorIndex;
            if ( fnWrite( static_cast<RHIDescriptorIndex>( index ) ) == false )
            {
                _pAllocator->free( index );
                return kInvalidDescriptorIndex;
            }
            store( static_cast<RHIDescriptorIndex>( index ), entry );
            return static_cast<RHIDescriptorIndex>( index );
        }
        /**
         * @brief 발급기에서 먼저 받은 번호 @p index 에 값을 넣습니다(그 번호에 디스크립터를 먼저 만드는 쪽 — DX12 의 뷰 생성은 번호로 핸들을 계산한다).
         * @details 같은 번호가 이미 산 칸이면 덮지 않고 false 다 — 발급기를 거치지 않은 번호가 들어온 결함이다.
         */
        bool assign( RHIDescriptorIndex index, const TEntry& entry )
        {
            if ( entry == TEntry{} || isLive( index ) )
                return false;
            store( index, entry );
            return true;
        }
        /** @brief 디스크립터를 쓰지 않는 백엔드의 `allocate` 입니다. */
        [[nodiscard]] RHIDescriptorIndex allocate( const TEntry& entry )
        {
            return allocate( entry, []( RHIDescriptorIndex )
            { return true; } );
        }

        /**
         * @brief 산 칸 @p index 를 비웁니다. @p reuse 가 AfterFence 면 `finishRelease` 까지 번호를 다시 내지 않습니다.
         * @return 비웠으면 true 입니다. 범위 밖 · 이미 빈 칸(이중 해제 · 다른 공간의 번호)은 false 이고 아무것도 바꾸지 않는다 — 빈 목록에 두 번 들어가면 한 번호가 두 자원에 나간다.
         */
        bool release( RHIDescriptorIndex index, RHIBindlessReuse reuse, TEntry* pOutEntry = nullptr )
        {
            if ( isLive( index ) == false )
                return false;
            if ( pOutEntry != nullptr )
                *pOutEntry = _listEntry[index];
            const RHIBindlessOwner owner = TOwnerOf{}( _listEntry[index] );
            unlinkOwner( _mapBufferIndex, owner._buffer, index );
            unlinkOwner( _mapTextureIndex, owner._texture, index );
            _listEntry[index] = TEntry{};
            if ( reuse == RHIBindlessReuse::Immediately )
                _pAllocator->free( index );
            return true;
        }
        /** @brief AfterFence 로 비운 칸을 빈 목록에 넣습니다(펜스가 지난 뒤 — 해제 큐 콜백이 잠금 아래에서 부른다). */
        void finishRelease( RHIDescriptorIndex index ) { _pAllocator->free( index ); }

        /** @brief 버퍼 @p buffer 의 산 칸을 모두 비우고 칸마다 @p fnReleased( index ) 를 부릅니다(버퍼를 부술 때). 주인 목록만 보므로 칸 수와 무관하다. */
        template <typename FnReleased>
        void releaseBuffer( RHIBindlessReuse reuse, RHIBufferHandle buffer, FnReleased&& fnReleased )
        {
            releaseOwned( _mapBufferIndex, buffer, reuse, fnReleased );
        }
        /** @brief 텍스처 @p texture 의 산 칸을 모두 비우고 칸마다 @p fnReleased( index ) 를 부릅니다(텍스처를 부술 때). */
        template <typename FnReleased>
        void releaseTexture( RHIBindlessReuse reuse, RHITextureHandle texture, FnReleased&& fnReleased )
        {
            releaseOwned( _mapTextureIndex, texture, reuse, fnReleased );
        }

        /** @brief @p index 가 산 칸인지 봅니다. */
        bool isLive( RHIDescriptorIndex index ) const { return index < _listEntry.size() && ( _listEntry[index] == TEntry{} ) == false; }
        /** @brief 산 칸의 값입니다. 산 칸이 아니면 nullptr 입니다. 고쳐 쓰는 길은 두지 않는다 — 값을 바꾸면 주인 목록과 어긋난다. */
        const TEntry* find( RHIDescriptorIndex index ) const { return isLive( index ) ? &_listEntry[index] : nullptr; }
        /** @brief 산 칸마다 @p fn( index, const TEntry& ) 를 부릅니다. */
        template <typename Fn>
        void forEachLive( Fn&& fn ) const
        {
            for ( uint32 index = 0; index < _listEntry.size(); ++index )
            {
                if ( ( _listEntry[index] == TEntry{} ) == false )
                    fn( static_cast<RHIDescriptorIndex>( index ), _listEntry[index] );
            }
        }
        /** @brief 산 칸의 값을 돌려주고, 아니면 빈 값입니다. */
        TEntry get( RHIDescriptorIndex index ) const { return isLive( index ) ? _listEntry[index] : TEntry{}; }
        /** @brief 칸 수(산 칸 · 빈 칸 · 해제 대기 모두)입니다. 번호는 이보다 작다. */
        uint32 getSlotCount() const { return static_cast<uint32>( _listEntry.size() ); }
        /** @brief 버퍼 @p buffer 의 산 칸 수입니다. */
        uint32 getBufferIndexCount( RHIBufferHandle buffer ) const { return countOwned( _mapBufferIndex, buffer ); }
        /** @brief 텍스처 @p texture 의 산 칸 수입니다. */
        uint32 getTextureIndexCount( RHITextureHandle texture ) const { return countOwned( _mapTextureIndex, texture ); }

    private:
        using OwnerIndexMap = unordered_map<uint64, vector<RHIDescriptorIndex>>;

        void store( RHIDescriptorIndex index, const TEntry& entry )
        {
            if ( index >= _listEntry.size() )
                _listEntry.resize( index + 1 );
            _listEntry[index]            = entry;
            const RHIBindlessOwner owner = TOwnerOf{}( entry );
            if ( owner._buffer != 0 )
                _mapBufferIndex[owner._buffer].push_back( index );
            if ( owner._texture != 0 )
                _mapTextureIndex[owner._texture].push_back( index );
        }
        static void unlinkOwner( OwnerIndexMap& map, uint64 key, RHIDescriptorIndex index )
        {
            if ( key == 0 )
                return;
            const auto it = map.find( key );
            if ( it == map.end() )
                return; // `releaseOwned` 가 목록을 먼저 떼어 간 주인이다
            vector<RHIDescriptorIndex>& listIndex = it->second;
            for ( size_t at = 0; at < listIndex.size(); ++at )
            {
                if ( listIndex[at] != index )
                    continue;
                listIndex[at] = listIndex.back();
                listIndex.pop_back();
                break;
            }
            if ( listIndex.empty() )
                map.erase( key );
        }
        template <typename FnReleased>
        void releaseOwned( OwnerIndexMap& map, uint64 key, RHIBindlessReuse reuse, FnReleased& fnReleased )
        {
            if ( key == 0 )
                return;
            const auto it = map.find( key );
            if ( it == map.end() )
                return;
            // 목록을 떼어 낸 뒤 놓는다 — `release` 가 같은 지도를 고치므로(swap-and-pop) 지도 안의 목록을 돌며 놓지 않는다.
            const vector<RHIDescriptorIndex> listIndex = std::move( it->second );
            map.erase( key );
            for ( const RHIDescriptorIndex index : listIndex )
            {
                if ( release( index, reuse ) )
                    fnReleased( index );
            }
        }
        static uint32 countOwned( const OwnerIndexMap& map, uint64 key )
        {
            const auto it = map.find( key );
            return it == map.end() ? 0 : static_cast<uint32>( it->second.size() );
        }

    private:
        vector<TEntry>             _listEntry;
        OwnerIndexMap              _mapBufferIndex;  ///< 버퍼 → 그 버퍼의 산 칸 번호들
        OwnerIndexMap              _mapTextureIndex; ///< 텍스처 → 그 텍스처의 산 칸 번호들
        RHIBindlessIndexAllocator  _ownAllocator;
        RHIBindlessIndexAllocator* _pAllocator; ///< 자기 발급기 또는 다른 표와 나누는 발급기
    };
} // namespace sw
