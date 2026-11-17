/**
 * @file TextureStreamingPool.h
 * @brief 텍스처 밉 스트리밍의 풀 예산(`TextureStreamingPool`)과 승급 하나가 쥐는 예약(`TextureStreamingReservation`)입니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /**
     * @class TextureStreamingReservation
     * @brief 위쪽 밉까지 올라간 텍스처 하나가 풀에서 쥔 바이트입니다. 사라지면(텍스처 항목을 지우거나 비울 때) 풀에 돌려준다 — 지우는 자리마다 손으로 빼지 않는다.
     */
    class SW_API TextureStreamingReservation
    {
    public:
        TextureStreamingReservation() = default;
        TextureStreamingReservation( atomic<uint64>& usedBytes, uint64 bytes );
        ~TextureStreamingReservation();
        TextureStreamingReservation( TextureStreamingReservation&& other ) noexcept;
        TextureStreamingReservation& operator=( TextureStreamingReservation&& other ) noexcept;
        TextureStreamingReservation( const TextureStreamingReservation& )            = delete;
        TextureStreamingReservation& operator=( const TextureStreamingReservation& ) = delete;

        /** @brief 쥔 바이트입니다(비었으면 0). */
        uint64 getBytes() const { return _bytes; }

    private:
        /** @brief 쥔 바이트를 풀에 돌려주고 비웁니다. */
        void reset();

    private:
        atomic<uint64>* _pUsedBytes{ nullptr };
        uint64          _bytes{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class TextureStreamingPool
     * @brief 밉 스트리밍이 위쪽 밉으로 올린 텍스처의 바이트 합과 예산입니다(언리얼 `r.Streaming.PoolSize` · 유니티 Mipmap Streaming 의 메모리 예산 자리).
     * @details 예산(`TextureStreamingConfig::_poolMegabytes`)을 넘는 승급은 예약을 받지 못한다 — 그 텍스처는 아래쪽 밉 그대로다. 예약은 텍스처 항목이 쥐고
     *          항목이 사라질 때 돌려준다. 첫 로드의 아래쪽 밉 · 렌더 텍스처 · CPU 텍스처는 세지 않는다(풀 밖이다).
     */
    class SW_API TextureStreamingPool
    {
    public:
        TextureStreamingPool();
        ~TextureStreamingPool();
        TextureStreamingPool( const TextureStreamingPool& )            = delete;
        TextureStreamingPool& operator=( const TextureStreamingPool& ) = delete;

        /** @brief @p bytes 를 더해도 @p budgetBytes 이하면 예약을 돌려줍니다. 넘으면 없습니다(그 승급은 하지 않는다). */
        [[nodiscard]] optional<TextureStreamingReservation> tryReserve( uint64 bytes, uint64 budgetBytes );
        /** @brief 지금 예약된 바이트 합입니다. */
        uint64 getUsedBytes() const { return _usedBytes.load( std::memory_order_acquire ); }

    private:
        atomic<uint64> _usedBytes; ///< 예약이 사라질 때 직접 뺀다 — 예약보다 오래 산다(텍스처 항목보다 먼저 만들고 뒤에 없앤다)
    };
} // namespace sw
