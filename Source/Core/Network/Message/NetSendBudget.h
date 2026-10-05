/**
 * @file NetSendBudget.h
 * @brief 메시지 하나 안의 비트 예산입니다 — 종류 바이트 · 머리 · 목록 길이 · 항목 · 끝 표시까지 실제로 쓸 비트를 쓰기 전에 셉니다.
 * @details 메시지가 채널 상한(`NetConnection::getMaxMessageSize` — 순서만 · 비신뢰는 `kMaxSingleMessageSize`, 신뢰 순서는 `kMaxReliableMessageSize`)을 넘으면
 *          보내기가 오류와 함께 통째로 버린다. 그래서 예산은 그 상한(생성자의 `limitBytes`, 기본은 조각나지 않는 상한)으로 자르고, 항목 크기는 어림이 아니라
 *          `BitMath::computeVarUintBits` · `computeBlobBits` 로 정확히 센다. 목록 길이처럼 항목보다 먼저 쓰는 칸은 가장 큰 값으로 미리 잡는다.
 *          키트 틱 예산의 기본은 `computeTickBudget( 설정값, NetHost::getMaxBytesPerSecond(), 틱 간격 )` — 설정과 연결 상한의 몫 중 작은 것이다.
 * @code
 *     BitWriter&    writer = _messageWriter.begin( kKind );
 *     writer.writeVarUint( tick );
 *     NetSendBudget budget( _settings._budgetBytes );
 *     budget.reserveBits( writer.getBitCount() + 1 );              // 이미 쓴 것 + 끝 표시
 *     for ( each item ) if ( budget.tryReserveBits( 1 + computeItemBits( item ) ) ) { writer.writeBool( true ); writeItem( item ); }
 *     writer.writeBool( false );
 * @endcode
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Network/Connection/NetConnection.h"

namespace sw
{
    /** @class NetSendBudget @brief 메시지 하나의 비트 예산입니다. */
    class NetSendBudget
    {
    public:
        static constexpr float32 kReplicationShare = 0.5f; ///< 연결 상한 가운데 상태 복제 하나(스냅샷 · MMO 갱신)가 틱마다 쓰는 몫 — 나머지는 신뢰 메시지 · 재전송 · 다른 키트

        /**
         * @brief 키트의 틱 예산 — 설정값 @p configuredBytes 와 연결 상한의 몫(@p bytesPerSecond × @p tickInterval × `kReplicationShare`) 중 작은 것입니다.
         *        상한이나 간격을 모르면(0 이하) 설정값 그대로입니다. 연결 상한을 낮추면 키트 예산이 따라 준다(언리얼 `NetSpeed ÷ 틱` 의 자리).
         */
        static int32 computeTickBudget( int32 configuredBytes, int32 bytesPerSecond, float64 tickInterval )
        {
            if ( bytesPerSecond <= 0 || tickInterval <= 0.0 )
                return configuredBytes;
            const float64 shareBytes = static_cast<float64>( bytesPerSecond ) * tickInterval * static_cast<float64>( kReplicationShare );
            const int32   share      = shareBytes >= static_cast<float64>( NetConnection::kMaxReliableMessageSize ) ? NetConnection::kMaxReliableMessageSize
                                                                                                                    : static_cast<int32>( shareBytes );
            return configuredBytes < share ? configuredBytes : share;
        }

        /**
         * @brief @p maxBytes 바이트 예산입니다. @p limitBytes(보낼 채널의 상한 — 기본은 조각나지 않는 `kMaxSingleMessageSize`)를 넘으면 그것으로, 음수면 0 으로 자른다.
         *        신뢰 순서 메시지 하나에 묶는 쪽만 `NetConnection::kMaxReliableMessageSize` 를 준다.
         */
        explicit NetSendBudget( int32 maxBytes, int32 limitBytes = NetConnection::kMaxSingleMessageSize )
            : _maxBits{ clampBytes( maxBytes, limitBytes ) * 8 }
            , _usedBits{ 0 }
        {
        }

        /** @brief 반드시 쓰는 비트(이미 쓴 머리 · 끝 표시)를 셉니다. 넘쳐도 센다 — `hasExceeded` 로 본다. */
        void reserveBits( int32 bitCount ) { _usedBits += bitCount > 0 ? bitCount : 0; }
        /** @brief 남은 예산에 @p bitCount 가 들어가면 세고 true 입니다. 안 들어가면 세지 않는다(작은 다음 항목은 들어갈 수 있다). */
        [[nodiscard]] bool tryReserveBits( int32 bitCount )
        {
            if ( bitCount < 0 || bitCount > _maxBits - _usedBits )
                return false;
            _usedBits += bitCount;
            return true;
        }

        int32 getMaxBits() const { return _maxBits; }
        int32 getUsedBits() const { return _usedBits; }
        int32 getRemainingBits() const { return _maxBits > _usedBits ? _maxBits - _usedBits : 0; }
        bool  hasExceeded() const { return _usedBits > _maxBits; }

    private:
        /** @brief @p maxBytes 를 [0, min( @p limitBytes, 신뢰 상한 )] 로 자릅니다. */
        static constexpr int32 clampBytes( int32 maxBytes, int32 limitBytes )
        {
            const int32 limit = limitBytes < NetConnection::kMaxReliableMessageSize ? limitBytes : NetConnection::kMaxReliableMessageSize;
            return maxBytes < 0 ? 0 : ( maxBytes < limit ? maxBytes : limit );
        }

        int32 _maxBits;
        int32 _usedBits;
    };
} // namespace sw
