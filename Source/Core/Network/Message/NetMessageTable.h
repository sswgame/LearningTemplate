/**
 * @file NetMessageTable.h
 * @brief 게임 넷 메시지(종류 바이트 + 비트 본문)의 서술자 표입니다. 메시지마다 (영역 안 번호, 방향, 기본 채널, 본문 판)을 형식 하나에 적고,
 *        받는 종류 마스크 · 종류별 디스패치 · 와이어 버전이 모두 표에서 나옵니다.
 * @details 예전에는 키트마다 종류 상수(`영역 + n`), 손으로 쓴 마스크(`1u << ( kX - kBase )` 16 곳), 손 `switch` · `if` 디스패치, 손으로 올리는 키트 와이어 버전
 *          (`NetKitWireVersion`)이 따로 놀았다. 마스크에서 종류 하나를 빠뜨리면 라우터가 그 메시지를 버렸고, 메시지를 더해도 버전을 올리지 않으면 다른 빌드와 갈리지 않았다.
 *          - 처리기는 `NetTableHandler<TDerived, TTable, TMessage...>` 로 서서 받는 메시지를 형식으로 적는다. 마스크와 디스패치는 그 목록에서 나오고,
 *            목록의 메시지마다 `receive( TMessage, const NetMessageContext&, BitReader& )` 오버로드가 없으면 컴파일되지 않는다.
 *          - **본문 형식이 있는 메시지**(`Body` 를 가진 서술자 — Engine 의 `NetMessageBodyDef`, 본문은 `REFLECT` 고정 배치형)는 처리기가 본문을 읽지 않는다.
 *            표가 본문을 다 읽고(잘림 · 남는 비트 · 범위 밖 값 · 등록 밖 열거자는 Malformed) `receive( TMessage, const NetMessageContext&, const Body& )` 를 부른다.
 *            보내는 쪽은 `NetMessageWriter::writeMessage<TTable, TMessage>( body )` 하나다.
 *          - 와이어 버전(`computeWireVersion`)은 영역과 메시지마다 (번호, 방향, 채널, 본문 판)을 섞은 값이다. 메시지를 더하거나 빼거나 방향 · 채널을 바꾸면 저절로 바뀐다.
 *            본문 형식이 있는 메시지의 판은 그 형식의 스키마 해시다(`getBodyRevision`) — 필드 · 범위 · 순서를 바꾸면 저절로 바뀐다(GameServer PacketGenerator 의 자리).
 *            손으로 쓰는 비트 본문(`NetMessageDef`)은 배치가 해시에 들지 않는다 — 본문을 바꾸면 그 메시지의 판(`BodyRevision`)만 손으로 올린다.
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
    /**
     * @brief 메시지 서술자입니다. 키트는 `using Input = NetMessageDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered>;` 로 씁니다.
     * @tparam Offset 영역 안 번호(0 ~ 15)
     * @tparam Channel 기본 채널. 보내는 쪽이 상태에 따라 다른 채널을 고르는 메시지(멈추면 신뢰로 보내는 자세)는 평소 채널을 적는다
     * @tparam BodyRevision 본문 판. 본문의 형식이나 뜻을 바꾸면 올린다
     * @details 손으로 쓰는 비트 본문의 서술자다. 본문이 고정 배치형으로 적히는 메시지는 Engine 의 `NetMessageBodyDef` 를 쓴다(판이 스키마 해시라 손으로 올리지 않는다).
     */
    template <uint8 Offset, NetMessageDirection Direction, NetChannelKind Channel, uint32 BodyRevision = 1>
    struct NetMessageDef
    {
        static constexpr uint8               kOffset        = Offset;
        static constexpr NetMessageDirection kDirection     = Direction;
        static constexpr NetChannelKind      kChannel       = Channel;
        static constexpr uint32              kBodyRevision  = BodyRevision;
        static constexpr NetChannelPolicy    kChannelPolicy = NetChannelPolicy::Descriptor;
        static_assert( Offset < NetMessageRange::kSize, "a message must fit inside its range" );

        /** @brief 와이어 버전에 섞는 본문 판입니다(`NetMessageTable::computeWireVersion`). */
        static constexpr uint32 getBodyRevision() { return BodyRevision; }
    };

    /**
     * @brief 보내는 쪽이 상태에 따라 채널을 고르는 손 본문 메시지의 서술자입니다(원격 호출의 신뢰 여부, 멈춘 덩어리 자세). @p DefaultChannel 은 평소 채널이다.
     * @details 고르는 자리는 `NetMessageWriter::beginMessage( channel )` 하나다 — 이 서술자가 아니면 채널을 고르는 시작이 컴파일되지 않는다.
     */
    template <uint8 Offset, NetMessageDirection Direction, NetChannelKind DefaultChannel, uint32 BodyRevision = 1>
    struct NetMessageSenderChannelDef : NetMessageDef<Offset, Direction, DefaultChannel, BodyRevision>
    {
        static constexpr NetChannelPolicy kChannelPolicy = NetChannelPolicy::SenderChooses;
    };

    /** @brief 메시지 서술자 @p TMessage 가 본문 형식(`Body`)을 가지면 true 입니다 — 표가 본문을 읽어 처리기에 넘긴다. */
    template <typename TMessage, typename = void>
    struct NetMessageHasBody : std::false_type
    {
    };
    template <typename TMessage>
    struct NetMessageHasBody<TMessage, std::void_t<typename TMessage::Body>> : std::true_type
    {
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

        /** @brief 메시지 @p TSubset 들의 방향 마스크입니다(`INetMessageHandler::getMessageDirectionMask`). 라우터가 받는 쪽 역할과 견준다. */
        template <typename... TSubset>
        static constexpr NetMessageDirectionMask makeDirectionMask()
        {
            static_assert( ( contains<TSubset>() && ... ), "every message of the mask must be in this table" );
            NetMessageDirectionMask mask{};
            mask._toServerMask = static_cast<uint16>( ( 0u | ... | ( TSubset::kDirection != NetMessageDirection::ToClient ? 1u << TSubset::kOffset : 0u ) ) );
            mask._toClientMask = static_cast<uint16>( ( 0u | ... | ( TSubset::kDirection != NetMessageDirection::ToServer ? 1u << TSubset::kOffset : 0u ) ) );
            return mask;
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

        /**
         * @brief 영역과 메시지마다 (번호, 방향, 채널, 본문 판)을 FNV-1a 로 섞은 와이어 버전입니다. 게임은 쓰는 표의 값과 자기 값을 `NetWireVersion::combine` 으로 묶는다.
         * @details 본문 형식이 있는 메시지의 판은 실행 중 코덱의 스키마 해시라(`NetMessageBodyDef::getBodyRevision`) 상수식이 아니다. 라우터가 등록 · 판 알림 때 부른다.
         */
        static uint32 computeWireVersion()
        {
            static_assert( hasUniqueOffsets(), "two messages of a table share a kind" );
            uint32 hash = foldWord( HashUtil::kFNVOffset32, RangeBase );
            ( ( hash = foldWord( foldWord( foldWord( foldWord( hash, TMessage::kOffset ), static_cast<uint32>( TMessage::kDirection ) ), static_cast<uint32>( TMessage::kChannel ) ),
                                 TMessage::getBodyRevision() ) ),
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
        // 라우터의 판 알림 종류(엔진 영역 0x0F)를 표 메시지가 쓰면 받는 라우터가 그 메시지를 판 알림으로 읽는다(상대 판을 빈 목록으로 덮어 판 맞춤이 꺼진다).
        static_assert( ( ( RangeBase + TMessage::kOffset != NetMessageRange::kRouterWireVersionKind ) && ... ), "the router owns the table version notice kind" );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NetMessageDelivery
     * @brief 표 메시지 하나를 처리기의 `receive` 오버로드로 넘깁니다. 표 처리기(`NetTableHandler`)와 역할마다 마스크를 고르는 처리기(엔진 세션)가 함께 쓴다.
     * @details 본문 형식이 있는 메시지(`NetMessageHasBody`)는 본문을 다 읽은 뒤에만 넘긴다 — 처리기가 넘침 · 범위 검사를 빠뜨릴 자리가 없다.
     *          잘림 · 남는 비트 · 범위 밖 값 · 등록 밖 열거자는 처리기를 부르지 않고 Malformed 다.
     *          손 본문은 처리기가 Handled 를 돌려도 읽기가 넘쳤으면 Malformed 로 바꾼다 — `BitReader` 는 잘림 · 범위 밖 값(`readInt` · `readEnum` · `readCount`) ·
     *          유한하지 않은 실수 · 남는 비트(`readEnd`)를 넘침으로 표시하므로, 처리기가 마지막 넘침 검사를 빠뜨려도 깨진 메시지가 처리된 것으로 세지지 않는다.
     */
    struct NetMessageDelivery
    {
        template <typename TMessage, typename THandler>
        static NetHandleResult deliver( THandler& handler, const NetMessageContext& context, BitReader& body )
        {
            if constexpr ( NetMessageHasBody<TMessage>::value )
            {
                typename TMessage::Body value{};
                if ( TMessage::readBody( body, value ) == false )
                    return NetHandleResult::Malformed;
                return handler.receive( TMessage{}, context, static_cast<const typename TMessage::Body&>( value ) );
            }
            else
            {
                const NetHandleResult result = handler.receive( TMessage{}, context, body );
                return result == NetHandleResult::Handled && body.hasOverflowed() ? NetHandleResult::Malformed : result;
            }
        }
    };

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

        uint8                   getMessageRangeBase() const final { return TTable::kRangeBase; }
        uint16                  getMessageKindMask() const final { return kReceivedMask; }
        NetMessageDirectionMask getMessageDirectionMask() const final { return TTable::template makeDirectionMask<TReceived...>(); }
        uint32                  getWireVersion() const final { return TTable::computeWireVersion(); }
        NetHandleResult         handleNetMessage( const NetMessageContext& context, BitReader& body ) final
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
                    return NetMessageDelivery::deliver<TMessage>( _handler, _context, _body );
                else
                    return NetHandleResult::Handled; // 받지 않는 메시지이다(마스크 밖이라 오지 않는다)
            }
        };
    };
} // namespace sw
