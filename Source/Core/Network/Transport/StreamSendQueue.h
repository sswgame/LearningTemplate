/**
 * @file StreamSendQueue.h
 * @brief 스트림 연결 하나의 보낼 줄 — 덩어리(64 KB) 목록에 복사해 쌓고, 앞에서부터 연속 구간을 꺼내 OS 쓰기(WSASend · sendmsg)에 그대로 건다.
 * @details 높은 물금 · 낮은 물금 · 상한을 한 곳에서 판정한다 — 세 전송(IOCP · epoll · 루프백)이 같은 배압 규칙을 쓴다. 잠금은 소유자(연결)가 잡는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Network/Transport/StreamTypes.h"

namespace sw
{
    /** @brief 보낼 줄의 연속 구간 하나 — 다음 `consume` · `append` 까지 유효합니다. */
    struct StreamSendSpan
    {
        const uint8* _pData{ nullptr };
        int32        _size{ 0 };
    };
} // namespace sw

namespace sw
{
    class SW_API StreamSendQueue
    {
    public:
        static constexpr int32 kChunkBytes = 64 * 1024;

        StreamSendQueue();

        void configure( int32 highWatermarkBytes, int32 lowWatermarkBytes, int32 maxQueuedBytes );

        /** @brief 복사해 쌓습니다. 상한을 넘으면 쌓지 않고 `QueueFull` 입니다. */
        StreamSendResult append( const uint8* pData, int32 size );
        /** @brief 앞에서부터 구간을 @p pOutSpan 에 최대 @p maxSpanCount 개(합 @p maxBytes 이하) 채웁니다. 채운 수입니다. */
        int32 collectSpans( StreamSendSpan* pOutSpan, int32 maxSpanCount, int32 maxBytes ) const;
        /** @brief 앞에서 @p byteCount 를 지웁니다. 높은 물금을 넘었던 줄이 낮은 물금 아래로 내려가면 true(쓰기 가능을 알린다). */
        [[nodiscard]] bool consume( int32 byteCount );
        void               clear();

        int32 getQueuedBytes() const { return _queuedBytes; }
        bool  isEmpty() const { return _queuedBytes == 0; }
        bool  isAboveHighWatermark() const { return _bAboveHighWatermark == SW_TRUE; }

    private:
        struct Chunk
        {
            vector<uint8> _bytes{};
            int32         _readOffset{ 0 };
        };

        deque<Chunk>          _listChunk;
        vector<vector<uint8>> _listFreeBuffer; ///< 다 보낸 덩어리의 버퍼(최대 4 개) — 연결마다 할당이 끓지 않게
        int32                 _queuedBytes;
        int32                 _highWatermarkBytes;
        int32                 _lowWatermarkBytes;
        int32                 _maxQueuedBytes;
        uint8                 _bAboveHighWatermark;
    };
} // namespace sw
