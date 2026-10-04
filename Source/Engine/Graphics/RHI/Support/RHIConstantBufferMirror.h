/**
 * @file RHIConstantBufferMirror.h
 * @brief 링 상수버퍼의 최신 값을 CPU 에 들고, 옛 값이 남은 프레임 칸을 그 칸의 차례에 채웁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class RHIConstantBufferMirror
     * @brief 프레임 링 상수버퍼(`createConstantBuffer`)의 **모든 칸이 마지막으로 쓴 값을 갖도록** 지킵니다.
     * @details 링 상수버퍼는 칸이 `constant::kMaxFrameCountInFlight` 개이고 `updateConstantBuffer` 는 **이번 칸에만** 씁니다. 매 프레임 쓰는
     *          버퍼(패스 · 카메라)는 그걸로 되지만, 값이 바뀔 때만 쓰는 버퍼(머티리얼 · 머티리얼 인스턴스)는 나머지 칸이 옛 값(처음에는 0)으로
     *          남아, DX12 · Vulkan 에서 세 프레임 중 두 프레임을 틀린 값으로 그립니다(DX11 · GL 은 버퍼 하나를 드라이버가 이름 바꾸기로 다뤄
     *          문제가 없습니다). 언리얼의 멀티 프레임 유니폼 버퍼가 갱신마다 새 자리를 받아 "읽는 쪽은 늘 마지막 값" 을 보장하는 것과 같은
     *          보장을, 여기서는 마지막 값을 들고 있다가 링이 그 칸으로 돌아왔을 때(그 칸의 GPU 사용이 끝난 뒤) 복사해 채우는 방식으로 줍니다.
     *          모든 칸이 채워진 버퍼는 목록에서 빠지므로 프레임당 비용은 최근에 바뀐 버퍼 수에 비례합니다.
     *
     *          스레드 안전합니다. 쓰기(게임 스레드 · 렌더 스레드)와 채우기(렌더 스레드의 프레임 시작)가 같은 락을 지나고, 칸에 쓰는 콜백도 그 락
     *          안에서 불립니다. 그래서 같은 버퍼의 두 쓰기가 겹치지 않습니다(Vulkan 은 같은 메모리를 두 스레드가 동시에 매핑할 수 없습니다).
     */
    class SW_API RHIConstantBufferMirror
    {
    public:
        /**
         * @brief `slot` 칸에 값을 쓰고, 그 값을 기억해 나머지 칸을 채울 차례로 표시합니다.
         * @param writeSlot `void( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size )` — 칸 하나에 씁니다. 락 안에서 불립니다.
         */
        template <typename WriteSlotFunction>
        void write( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size, WriteSlotFunction&& writeSlot )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            writeSlot( buffer, slot, pData, size );
            rememberWrite( buffer, slot, pData, size );
        }

        /**
         * @brief 링이 `slot` 칸으로 돌아왔을 때(그 칸의 GPU 사용이 끝난 뒤) 부릅니다. 그 칸에 옛 값이 남은 버퍼마다 마지막 값을 씁니다.
         * @param writeSlot `write` 와 같은 모양입니다. 버퍼가 이미 부서졌으면 콜백이 아무것도 하지 않으면 됩니다.
         */
        template <typename WriteSlotFunction>
        void fillSlot( uint32 slot, WriteSlotFunction&& writeSlot )
        {
            std::scoped_lock<mutex> lock{ _mutex };
            if ( _mapPendingWrite.empty() )
                return;
            const uint32 slotBit = slotBitOf( slot );
            _listFilledScratch.clear();
            for ( auto& [buffer, pending] : _mapPendingWrite )
            {
                if ( ( pending._staleSlotMask & slotBit ) == 0 )
                    continue;
                writeSlot( buffer, slot, pending._bytes.data(), static_cast<uint32>( pending._bytes.size() ) );
                pending._staleSlotMask &= ~slotBit;
                if ( pending._staleSlotMask == 0 )
                    _listFilledScratch.push_back( buffer );
            }
            // 지우기는 훑기가 끝난 뒤에 한다. 이 표의 erase 는 마지막 원소를 빈 자리로 옮기므로 훑는 도중에 지우면 원소를 건너뛴다.
            for ( const RHIBufferHandle buffer : _listFilledScratch )
                _mapPendingWrite.erase( buffer );
        }

        /** @brief 버퍼를 잊습니다(`destroyBuffer`). */
        void forget( RHIBufferHandle buffer );
        /** @brief 모두 잊습니다(장치 종료). */
        void clear();
        /** @brief 아직 채울 칸이 남은 버퍼 수를 반환합니다. */
        uint32 getPendingBufferCount() const;

    private:
        /**
         * @struct PendingWrite
         * @brief 마지막으로 쓴 값과, 아직 그 값을 받지 못한 칸들입니다.
         */
        struct PendingWrite
        {
            vector<uint8> _bytes;
            uint32        _staleSlotMask{ 0 }; ///< 비트 i 가 켜져 있으면 칸 i 에 옛 값이 남아 있다
        };

        /** @brief 칸 번호의 비트를 반환합니다. */
        static uint32 slotBitOf( uint32 slot ) { return 1u << ( slot % constant::kMaxFrameCountInFlight ); }
        /** @brief 쓴 값을 기억하고 나머지 칸을 옛 값으로 표시합니다. 락 안에서 부릅니다. */
        void rememberWrite( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size );

        unordered_map<RHIBufferHandle, PendingWrite> _mapPendingWrite;
        vector<RHIBufferHandle>                      _listFilledScratch; ///< fillSlot 이 다 채운 버퍼를 모으는 자리(프레임마다 다시 쓴다)
        mutable mutex                                _mutex;
    };
} // namespace sw
