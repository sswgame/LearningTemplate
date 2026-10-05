/**
 * @file NetReplayWindow.h
 * @brief 받은 패킷 번호의 재전송 방지 창 — 가장 높은 번호에서 1024 안쪽의 처음 보는 번호만 받습니다(IPsec · DTLS · WireGuard 의 슬라이딩 창).
 * @details 순서는 `isAcceptable`(복호 전에 싸게 거른다) → 복호 · 검증 → `markReceived`. 검증 전에 표시하면 위조 패킷이 진짜 번호를 미리 태워 진짜를 거절하게 만든다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class SW_API NetReplayWindow
    {
    public:
        static constexpr uint64 kWindowSize = 1024;

        NetReplayWindow();

        /** @brief 처음 보는 번호이고 창보다 옛것이 아니면 true 입니다. */
        bool isAcceptable( uint64 packetNumber ) const;
        /** @brief 검증이 통과한 번호를 표시합니다. 더 높은 번호면 창이 앞으로 간다. */
        void markReceived( uint64 packetNumber );
        void reset();

        uint64 getHighest() const { return _highest; }
        bool   hasReceived() const { return _bHasReceived == SW_TRUE; }

    private:
        static constexpr int32 kWordCount = static_cast<int32>( kWindowSize / 64 );

        bool isMarked( uint64 packetNumber ) const;

        uint64 _arrBit[kWordCount];
        uint64 _highest;
        uint8  _bHasReceived;
    };
} // namespace sw
