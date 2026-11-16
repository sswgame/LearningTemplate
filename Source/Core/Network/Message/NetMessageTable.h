/**
 * @file NetMessageTable.h
 * @brief 게임 넷 메시지(종류 바이트 + 비트 본문)의 서술자 표입니다. 메시지마다 (영역 안 번호, 방향, 기본 채널, 본문 판)을 형식 하나에 적고,
 *        받는 종류 마스크 · 종류별 디스패치 · 와이어 버전이 모두 표에서 나옵니다.
 * @details 예전에는 키트마다 종류 상수(`영역 + n`), 손으로 쓴 마스크(`1u << ( kX - kBase )` 16 곳), 손 `switch` · `if` 디스패치, 손으로 올리는 키트 와이어 버전
 *          (`NetKitWireVersion`)이 따로 놀았다. 마스크에서 종류 하나를 빠뜨리면 라우터가 그 메시지를 버렸고, 메시지를 더해도 버전을 올리지 않으면 다른 빌드와 갈리지 않았다.
 *          - 처리기는 `NetTableHandler<TDerived, TTable, TMessage...>` 로 서서 받는 메시지를 형식으로 적는다. 마스크와 디스패치는 그 목록에서 나오고,
 *            목록의 메시지마다 `receive( TMessage, const NetMessageContext&, BitReader& )` 오버로드가 없으면 컴파일되지 않는다.
 *          - 와이어 버전(`computeWireVersion`)은 영역과 메시지마다 (번호, 방향, 채널, 본문 판)을 섞은 값이다. 메시지를 더하거나 빼거나 방향 · 채널을 바꾸면 저절로 바뀐다.
 *            본문은 손으로 쓰는 비트 형식이라 배치는 해시에 들지 않는다 — 본문을 바꾸면 그 메시지의 판(`BodyRevision`)만 올린다(예전의 키트 하나짜리 수보다 좁다).
 *          gRPC 서비스 서술자, 언리얼 `NetSerialize` 를 가진 RPC 표의 자리입니다(기억에 의존).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/NetTypes.h"

#include <type_traits>

namespace sw
{
    /** @brief 메시지가 가는 방향입니다. 와이어 버전에 섞인다. */
    enum class NetMessageDirection : uint8
    {
        ToServer = 0, ///< 클라이언트 → 서버
        ToClient,     ///< 서버 → 클라이언트
        Both          ///< 양쪽(락스텝 입력처럼 서버가 받아 다시 나눈다)
    };

    /**
     * @brief 메시지 서술자입니다. 키트는 `using Input = NetMessageDef<0, NetMessageDirection::Both, NetChannelType::ReliableOrdered>;` 로 씁니다.
     * @tparam Offset 영역 안 번호(0 ~ 15)
     * @tparam Channel 기본 채널. 보내는 쪽이 상태에 따라 다른 채널을 고르는 메시지(멈추면 신뢰로 보내는 자세)는 평소 채널을 적는다
     * @tparam BodyRevision 본문 판. 본문의 형식이나 뜻을 바꾸면 올린다
     */
    template <uint8 Offset, NetMessageDirection Direction, NetChannelType Channel, uint32 BodyRevision = 1>
    struct NetMessageDef
    {
        static constexpr uint8               kOffset       = Offset;
        static constexpr NetMessageDirection kDirection    = Direction;
        static constexpr NetChannelType      kChannel      = Channel;
        static constexpr uint32              kBodyRevision = BodyRevision;
        static_assert( Offset < NetMessageRange::kSize, "a message must fit inside its range" );
    };

    /**
     * @brief 영역 하나의 메시지 표입니다. 열거 순서는 상관없지만 번호는 겹치면 안 됩니다.
     * @tparam RangeBase 영역의 첫 값(`NetKitMessageRange::kLockstep` 등)
     */
    template <uint8 RangeBase, typename... TMessage>
    struct NetMessageTable
    {
        static constexpr uint8 kRangeBase = RangeBase;

        /** @brief 메시지 @p TOne 이 이 표에 있는지 봅니다. */
        template <typename TOne>
        static constexpr bool contains()
        {
            return ( std::is_same_v<TOne, TMessage> || ... );
        }

        /** @brief 메시지 @p TOne 의 종류 바이트입니다. */
        template <typename TOne>
        static constexpr uint8 getKindOf()
        {
            static_assert( contains<TOne>(), "the message is not in this table" );
            static_assert( hasUniqueOffsets(), "two messages of a table share a kind" );
            return static_cast<uint8>( RangeBase + TOne::kOffset );
        }

        /** @brief 메시지 @p TSubset 들의 종류 마스크입니다(비트 n 이 영역 + n). 모두 이 표의 메시지여야 합니다. */
        template <typename... TSubset>
        static constexpr uint16 makeMask()
        {
            static_assert( ( contains<TSubset>() && ... ), "every message of the mask must be in this table" ); // 두 팩을 한 접기에 넣으면 함께 펼쳐진다 — 안쪽을 함수로 뗀다
            return static_cast<uint16>( ( 0u | ... | ( 1u << TSubset::kOffset ) ) );
        }

        /** @brief 번호가 @p kind 인 메시지의 `visitor.visit<TMessage>()` 를 부르고 그 값을 돌려줍니다. 표에 없으면 @p notFound 입니다. */
        template <typename TResult, typename TVisitor>
        static TResult dispatch( uint8 kind, TVisitor& visitor, TResult notFound )
        {
            TResult    result = notFound;
            const bool bHit   = ( ( kind == getKindOf<TMessage>() ? ( result = visitor.template visit<TMessage>(), true ) : false ) || ... );
            (void)bHit;
            return result;
        }

        /** @brief 영역과 메시지마다 (번호, 방향, 채널, 본문 판)을 FNV-1a 로 섞은 와이어 버전입니다. 게임은 쓰는 표의 값과 자기 값을 `NetWireVersion::combine` 으로 묶는다. */
        static constexpr uint32 computeWireVersion()
        {
            static_assert( hasUniqueOffsets(), "two messages of a table share a kind" );
            uint32 hash = foldWord( HashUtil::kFNVOffset32, RangeBase );
            ( ( hash = foldWord( foldWord( foldWord( foldWord( hash, TMessage::kOffset ), static_cast<uint32>( TMessage::kDirection ) ), static_cast<uint32>( TMessage::kChannel ) ),
                                 TMessage::kBodyRevision ) ),
              ... );
            return hash;
        }

    private:
        static constexpr uint32 foldWord( uint32 hash, uint32 value )
        {
            for ( int32 shift = 0; shift < 32; shift += 8 )
            {
                hash = HashUtil::fnvStep32( hash, ( value >> shift ) & 0xFFu );
            }
            return hash;
        }

        static constexpr bool hasUniqueOffsets()
        {
            constexpr uint8 arrOffset[] = { TMessage::kOffset... };
            for ( size_t first = 0; first < sizeof...( TMessage ); ++first )
            {
                for ( size_t second = first + 1; second < sizeof...( TMessage ); ++second )
                {
                    if ( arrOffset[first] == arrOffset[second] )
                        return false;
                }
            }
            return true;
        }

        static_assert( sizeof...( TMessage ) > 0, "a message table needs at least one message" );
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetTableHandler
     * @brief 표 @p TTable 의 메시지 @p TReceived 들을 받는 처리기의 기반입니다. 영역 · 마스크 · 디스패치를 표가 맡습니다.
     * @details 파생은 받는 메시지마다 `NetHandleResult receive( TMessage, const NetMessageContext&, BitReader& )` 를 둡니다(public). 빠지면 컴파일되지 않는다.
     *          한 영역을 둘이 나눠 받으면(락스텝 · 롤백) 각자 자기 메시지만 적는다 — 라우터가 겹침을 거절한다.
     */
    template <typename TDerived, typename TTable, typename... TReceived>
    class NetTableHandler : public INetMessageHandler
    {
    public:
        static constexpr uint16 kReceivedMask = TTable::template makeMask<TReceived...>();

        uint8           getMessageRangeBase() const final { return TTable::kRangeBase; }
        uint16          getMessageKindMask() const final { return kReceivedMask; }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) final
        {
            ReceiveVisitor visitor{ static_cast<TDerived&>( *this ), context, body };
            return TTable::template dispatch<NetHandleResult>( context._kind, visitor, NetHandleResult::Handled ); // 마스크가 거른 뒤라 표 밖 종류는 오지 않는다
        }

    private:
        struct ReceiveVisitor
        {
            TDerived&                _handler;
            const NetMessageContext& _context;
            BitReader&               _body;

            template <typename TMessage>
            NetHandleResult visit()
            {
                if constexpr ( ( std::is_same_v<TMessage, TReceived> || ... ) )
                    return _handler.receive( TMessage{}, _context, _body );
                else
                    return NetHandleResult::Handled; // 받지 않는 메시지이다(마스크 밖이라 오지 않는다)
            }
        };
    };
} // namespace sw
