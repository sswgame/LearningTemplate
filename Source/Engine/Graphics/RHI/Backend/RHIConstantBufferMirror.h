/**
 * @file RHIConstantBufferMirror.h
 * @brief 링 상수 버퍼의 최신 값을 CPU 에 보관하고, 옛 값이 남은 프레임 슬롯을 그 슬롯의 차례에 채웁니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    /**
     * @class RHIConstantBufferMirror
     * @brief 프레임 링 상수 버퍼(`createConstantBuffer`)의 **모든 슬롯이 마지막으로 쓴 값을 갖도록** 지킵니다.
     * @details 링 상수 버퍼는 슬롯이 `constant::render::kMaxFrameCountInFlight` 개이고 `updateConstantBuffer` 는 **이번 슬롯에만** 씁니다. 매 프레임 쓰는
     *          버퍼(패스, 카메라)는 그걸로 되지만, 값이 바뀔 때만 쓰는 버퍼(머티리얼, 머티리얼 인스턴스)는 나머지 슬롯이 옛 값(처음에는 0)으로
     *          남아, DX12 와 Vulkan 에서 세 프레임 중 두 프레임을 틀린 값으로 그립니다(DX11 과 GL 은 버퍼 하나를 드라이버가 이름 바꾸기로 다뤄
     *          문제가 없습니다). 언리얼의 멀티 프레임 유니폼 버퍼는 갱신마다 새 슬롯을 받아 "읽는 쪽은 늘 마지막 값"을 보장하는데,
     *          여기서는 같은 보장을 다른 방식으로 줍니다. 마지막 값을 보관하고 있다가 링이 그 슬롯으로 돌아왔을 때(그 슬롯의 GPU 사용이 끝난 뒤) 복사해 채웁니다.
     *          모든 슬롯이 채워진 버퍼는 목록에서 빠지므로 프레임당 비용은 최근에 바뀐 버퍼 수에 비례합니다.
     *          열쇠는 (버퍼, 오프셋)이다 — 상수 버퍼 페이지(`createConstantBufferPage`)는 구간마다 따로 쓰고 채운다(감사 R-4). 버퍼 하나를 통째로 쓰면 오프셋 0 이다.
     *
     *          스레드 안전합니다. 쓰기(게임 스레드, 기록 워커)와 채우기(렌더 스레드의 프레임 시작)가 같은 잠금을 지나고, 슬롯에 쓰는 일도 그 잠금
     *          안에서 불립니다. 그래서 같은 버퍼의 두 쓰기가 겹치지 않습니다(Vulkan 은 같은 메모리를 두 스레드가 동시에 매핑할 수 없다).
     *          **잠금은 버퍼 핸들로 나눈 칸마다 하나다** — 병렬 기록은 패스마다 다른 버퍼에 쓰므로 칸이 갈려 서로 기다리지 않는다. 장치에 잠금 하나를 두면 기록 스레드 수만큼 쓰기가 줄을 선다
     *          (언리얼은 유니폼 버퍼를 리스트마다의 빠른 할당기에서 받아 공용 잠금이 없다).
     */
    class SW_API RHIConstantBufferMirror
    {
    public:
        /**
         * @brief `slot` 슬롯의 @p offset 에 값을 쓰고, 그 값을 기억해 나머지 슬롯을 채울 차례로 표시합니다.
         * @param writeSlot `void( RHIBufferHandle buffer, uint32 slot, uint32 offset, const void* pData, uint32 size )` 형태이며 슬롯 하나에 씁니다. 락 안에서 불립니다.
         */
        template <typename WriteSlotFunction>
        void write( RHIBufferHandle buffer, uint32 offset, uint32 slot, const void* pData, uint32 size, WriteSlotFunction&& writeSlot )
        {
            Shard&            shard = getShard( buffer );
            ScopedLock<mutex> lock{ shard._mutex };
            writeSlot( buffer, slot, offset, pData, size );
            rememberWrite( shard, Key{ buffer, offset }, slot, pData, size );
        }

        /**
         * @brief 링이 `slot` 슬롯으로 돌아왔을 때(그 슬롯의 GPU 사용이 끝난 뒤) 부릅니다. 그 슬롯에 옛 값이 남은 버퍼마다 마지막 값을 씁니다.
         * @param writeSlot `write` 와 같은 모양입니다. 버퍼가 이미 부서졌으면 콜백이 아무것도 하지 않으면 됩니다.
         */
        template <typename WriteSlotFunction>
        void fillSlot( uint32 slot, WriteSlotFunction&& writeSlot )
        {
            const uint32 slotBit = getSlotBitOf( slot );
            for ( Shard& shard : _arrShard )
            {
                ScopedLock<mutex> lock{ shard._mutex };
                shard._listFilledScratch.clear();
                for ( auto& [key, pending] : shard._mapPendingWrite )
                {
                    if ( ( pending._staleSlotMask & slotBit ) == 0 )
                        continue;
                    writeSlot( key._buffer, slot, key._offset, pending._bytes.data(), static_cast<uint32>( pending._bytes.size() ) );
                    pending._staleSlotMask &= ~slotBit;
                    if ( pending._staleSlotMask == 0 )
                        shard._listFilledScratch.push_back( key );
                }
                // 지우기는 순회가 끝난 뒤에 한다. 이 맵의 erase 는 마지막 원소를 빈 위치로 옮기므로 순회 도중에 지우면 원소를 건너뛴다.
                for ( const Key& key : shard._listFilledScratch )
                {
                    shard._mapPendingWrite.erase( key );
                }
            }
        }

        /** @brief 버퍼를 잊습니다(`destroyBuffer` — 페이지면 모든 구간). */
        void remove( RHIBufferHandle buffer );
        /**
         * @brief 버퍼를 잊고, 같은 잠금 안에서 @p releaseRecord 를 부릅니다 — 백엔드가 쓰기 콜백이 읽는 레코드를 여기서 거두면 진행 중인 쓰기와 겹치지 않는다.
         * @param releaseRecord `void()` 형태입니다. 이 미러를 다시 부르면 안 됩니다.
         */
        template <typename ReleaseRecordFunction>
        void remove( RHIBufferHandle buffer, ReleaseRecordFunction&& releaseRecord )
        {
            Shard&            shard = getShard( buffer );
            ScopedLock<mutex> lock{ shard._mutex };
            removeKeys( shard, buffer );
            releaseRecord();
        }
        /** @brief 모두 잊습니다(장치 종료). */
        void clear();
        /** @brief 아직 채울 슬롯이 남은 (버퍼, 오프셋) 수를 반환합니다. */
        uint32 getPendingBufferCount() const;

    private:
        /** @brief 쓰는 자리입니다 — 버퍼와 그 안의 오프셋(페이지 구간). */
        struct Key
        {
            RHIBufferHandle _buffer{ 0 };
            uint32          _offset{ 0 };

            bool   operator==( const Key& other ) const { return _buffer == other._buffer && _offset == other._offset; }
            uint64 computeHash() const { return HashUtil::combine( _buffer.getPacked(), _offset ); }
        };

        /**
         * @struct PendingWrite
         * @brief 마지막으로 쓴 값과, 아직 그 값을 받지 못한 슬롯들입니다.
         */
        struct PendingWrite
        {
            vector<uint8> _bytes;
            uint32        _staleSlotMask{ 0 }; ///< 비트 i 가 켜져 있으면 슬롯 i 에 옛 값이 남아 있다
        };

        /** @brief 슬롯 번호의 비트를 반환합니다. */
        static uint32 getSlotBitOf( uint32 slot ) { return 1u << ( slot % constant::render::kMaxFrameCountInFlight ); }
        /** @brief 버퍼 핸들로 나눈 칸 하나입니다. 잠금 · 대기 목록이 칸마다 따로다. */
        struct Shard
        {
            unordered_map<Key, PendingWrite> _mapPendingWrite SW_GUARDED_BY( _mutex );
            vector<Key> _listFilledScratch                    SW_GUARDED_BY( _mutex ); ///< 지울 자리를 모으는 임시 목록(프레임마다 다시 쓴다)
            mutable mutex                                     _mutex;
        };
        static constexpr uint32 kShardCount = 16; ///< 2 의 거듭제곱 — 기록 워커 수보다 넉넉하다

        /** @brief @p buffer 가 드는 칸입니다(핸들 번호의 아래 비트라 이웃 버퍼가 갈린다). */
        Shard& getShard( RHIBufferHandle buffer ) { return _arrShard[static_cast<size_t>( buffer.getPacked() & ( kShardCount - 1u ) )]; }
        /** @brief 쓴 값을 기억하고 나머지 슬롯을 옛 값으로 표시합니다. 칸의 잠금 안에서 부릅니다. */
        static void rememberWrite( Shard& shard, const Key& key, uint32 slot, const void* pData, uint32 size ) SW_REQUIRES_LOCK( shard._mutex );
        /** @brief 칸에서 @p buffer 의 열쇠를 모두 지웁니다. 칸의 잠금 안에서 부릅니다. */
        static void removeKeys( Shard& shard, RHIBufferHandle buffer ) SW_REQUIRES_LOCK( shard._mutex );

    private:
        Shard _arrShard[kShardCount];
    };
} // namespace sw
