/**
 * @file RHIBindlessRegistry.h
 * @brief bindless 번호 공간 하나의 등록표입니다 — 번호 발급 · 생존 · 해제 대기(GPU 펜스 뒤 재사용) · 이중 해제 거절을 한 곳에서 지킵니다.
 * @details 다섯 백엔드가 같은 표(값 배열 + `IndexFreeList` + 손으로 쓴 해제 대기 상태)를 따로 쓰던 것을 모은다. 백엔드는 값의 뜻(어느 자원인가)과
 *          디스크립터 쓰기 훅(Vulkan `writeBindlessTextureSlot` · DX12 `CreateShaderResourceView`)만 맡는다.
 *          잠금은 갖지 않는다 — `RHIRetireList` 처럼 쓰는 쪽이 자기 `_bindlessMutex` 아래에서 부른다(그 잠금이 상수 버퍼 크기 표 같은 이웃 상태도 함께 지킨다).
 *          언리얼 `FD3D12BindlessDescriptorAllocator`(번호 할당 + 펜스 뒤 반납)와 Godot `RID_Owner` 의 빈 칸 재사용과 같은 자리다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Container/IndexFreeList.h"
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
     * @class RHIBindlessRegistry
     * @brief 번호 → 값(`TEntry`) 표입니다. `TEntry{}` 와 같은 값이 빈 칸입니다(`==` 로 비교할 수 있어야 한다).
     * @details 칸의 상태는 셋이다: 빈 칸(빈 목록에 있다) · 산 칸(값이 있다) · 해제 대기(값은 비었지만 빈 목록에 아직 없다 — `finishRelease` 가 넣는다).
     *          해제 대기 칸은 아무에게도 다시 나가지 않으므로 GPU 가 아직 읽는 디스크립터를 새 자원이 덮지 않는다.
     * @code
     *     UniqueLock<shared_mutex> lock{ _bindlessMutex };
     *     const RHIDescriptorIndex index = _textureRegistry.allocate( texture );     // 꽉 차면 kInvalidDescriptorIndex
     *     if ( _textureRegistry.release( index, RHIBindlessReuse::AfterFence ) )   // 비었으면 false(이중 해제)
     *         enqueueGPURelease( [=] { lock; _textureRegistry.finishRelease( index ); }, key );
     * @endcode
     */
    template <typename TEntry>
    class RHIBindlessRegistry
    {
    public:
        /** @brief @p capacity 개까지 번호를 내는 표를 만듭니다(디스크립터 배열 크기 — 없으면 상한 없음). */
        explicit RHIBindlessRegistry( uint32 capacity = kInvalidDescriptorIndex )
            : _listEntry{}
            , _freeIndex{}
            , _capacity{ capacity }
        {
        }

        /** @brief 번호 상한을 정합니다(디스크립터 배열을 만든 뒤). */
        void setCapacity( uint32 capacity ) { _capacity = capacity; }
        /** @brief 모든 칸 · 빈 목록 · 해제 대기를 잊습니다(장치를 놓거나 다시 세울 때 — 빈 목록만 남으면 다음 장치가 표 밖 번호를 낸다). */
        void clear()
        {
            _listEntry.clear();
            _freeIndex.clear();
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
            if ( _freeIndex.tryPop( index ) == false )
            {
                if ( _listEntry.size() >= _capacity )
                    return kInvalidDescriptorIndex;
                index = static_cast<uint32>( _listEntry.size() );
                _listEntry.push_back( TEntry{} );
            }
            if ( fnWrite( static_cast<RHIDescriptorIndex>( index ) ) == false )
            {
                _freeIndex.push( index );
                return kInvalidDescriptorIndex;
            }
            _listEntry[index] = entry;
            return static_cast<RHIDescriptorIndex>( index );
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
            _listEntry[index] = TEntry{};
            if ( reuse == RHIBindlessReuse::Immediately )
                _freeIndex.push( index );
            return true;
        }
        /** @brief AfterFence 로 비운 칸을 빈 목록에 넣습니다(펜스가 지난 뒤 — 해제 큐 콜백이 잠금 아래에서 부른다). */
        void finishRelease( RHIDescriptorIndex index ) { _freeIndex.push( index ); }

        /** @brief @p pred( const TEntry& ) 가 참인 산 칸을 모두 비우고 @p fnReleased( index ) 를 부릅니다(자원을 부술 때 그 자원의 번호를 모두 놓는다). */
        template <typename Pred, typename FnReleased>
        void releaseIf( RHIBindlessReuse reuse, Pred&& pred, FnReleased&& fnReleased )
        {
            for ( uint32 index = 0; index < _listEntry.size(); ++index )
            {
                if ( _listEntry[index] == TEntry{} || pred( _listEntry[index] ) == false )
                    continue;
                (void)release( index, reuse );
                fnReleased( static_cast<RHIDescriptorIndex>( index ) );
            }
        }

        /** @brief @p index 가 산 칸인지 봅니다. */
        bool isLive( RHIDescriptorIndex index ) const { return index < _listEntry.size() && ( _listEntry[index] == TEntry{} ) == false; }
        /** @brief 산 칸의 값입니다. 산 칸이 아니면 nullptr 입니다. */
        const TEntry* find( RHIDescriptorIndex index ) const { return isLive( index ) ? &_listEntry[index] : nullptr; }
        /** @brief 산 칸의 값을 돌려주고, 아니면 빈 값입니다. */
        TEntry get( RHIDescriptorIndex index ) const { return isLive( index ) ? _listEntry[index] : TEntry{}; }
        /** @brief 칸 수(산 칸 · 빈 칸 · 해제 대기 모두)입니다. 번호는 이보다 작다. */
        uint32 getSlotCount() const { return static_cast<uint32>( _listEntry.size() ); }

    private:
        vector<TEntry> _listEntry;
        IndexFreeList  _freeIndex;
        uint32         _capacity;
    };
} // namespace sw
