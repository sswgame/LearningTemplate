/**
 * @file NetMessageBody.h
 * @brief 본문이 고정 배치형(B, `REFLECT` 구조체)인 게임 넷 메시지의 서술자입니다. 본문 판이 그 형식의 스키마 해시라 손으로 올리지 않습니다.
 * @details 손으로 쓰는 비트 본문(`NetMessageDef` + `BitReader` 읽기)은 키트마다 넘침 검사 · 열거자 범위 검사를 따로 적었고, 배치를 바꾸고 판(`BodyRevision`)을 올리지 않으면
 *          다른 빌드와 갈리지 않았다. 이 서술자는 본문 형식 하나에서 셋을 낸다(GameServer PacketGenerator 의 스키마 → 코드 · 판 자리, 언리얼 `USTRUCT` 넷 직렬화).
 *          - 쓰기: `NetMessageWriter::writeMessage<TTable, TMessage>( body )` — 범위 밖 값은 오류 로그와 false(보내지 않는다).
 *          - 읽기: 표 처리기(`NetTableHandler`)가 본문을 다 읽고 `receive( TMessage, const NetMessageContext&, const Body& )` 를 부른다. 잘림 · 남는 비트 · 범위 밖 값은 Malformed.
 *          - 판: `getBodyRevision` 이 스키마 해시를 접은 값이고 `NetMessageTable::computeWireVersion` 에 섞인다. 라우터가 영역마다 그 판을 상대와 맞추고 다르면 끊는다 —
 *            그래서 본문은 스키마 해시 머리 없이 싣는다(`NetMessageChannel` 이 핸드셰이크로 확인한 채널 안에서 머리를 빼는 것과 같은 이유).
 * @code
 *     REFLECT() struct TurnJoinBody { REFLECT_BODY(); PROPERTY() uint32 _roomID{ 0 }; };
 *     using Join = NetMessageBodyDef<0, NetMessageDirection::ToServer, NetChannelKind::ReliableOrdered, TurnJoinBody>;
 *     if ( _messageWriter.writeMessage<Table, Join>( body ) ) (void)_messageWriter.send( peers, 0, Join::kChannel );
 *     NetHandleResult receive( Join, const NetMessageContext& context, const TurnJoinBody& body );
 * @endcode
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetMessageTable.h"
#include "Core/Network/NetTypes.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

namespace sw
{
    /**
     * @struct NetMessageBodyCodec
     * @brief 넷 메시지 본문을 스키마 해시 머리 없이 쓰고 읽는 창구입니다(`FixedLayout` 의 friend). `NetMessageBodyDef` 만 부른다.
     */
    struct NetMessageBodyCodec
    {
        template <typename TBody>
        [[nodiscard]] static bool write( const TBody& body, BitWriter& writer )
        {
            if ( FixedLayout::writeBody( body, writer ) )
                return true;
            SW_LOG_ERROR_ID( "Net.MessageBodyOutOfRange", "NetMessageBody: a field of %# is out of its declared range - the message is not sent", typeFqn<TBody>().c_str() );
            return false;
        }

        template <typename TBody>
        [[nodiscard]] static bool read( BitReader& reader, TBody& outBody )
        {
            return FixedLayout::readBody( reader, outBody ) == FixedReadResult::Ok;
        }

        /** @brief 64 비트 스키마 해시를 와이어 버전 칸(32 비트)으로 접습니다. */
        template <typename TBody>
        [[nodiscard]] static uint32 computeRevision()
        {
            const uint64 schemaHash = FixedLayout::getSchemaHash<TBody>();
            return static_cast<uint32>( schemaHash ) ^ static_cast<uint32>( schemaHash >> 32 );
        }
    };

    /**
     * @brief 본문이 고정 배치형 @p TBody 인 메시지 서술자입니다. 번호 · 방향 · 채널은 `NetMessageDef` 와 같다.
     * @tparam TBody `REFLECT` 구조체. 고정 배치형 코덱이 없으면 첫 사용(처리기 등록 때 판 계산)에서 이유를 남기고 멈춘다(`FixedLayout::findCodec`)
     */
    template <uint8 Offset, NetMessageDirection Direction, NetChannelKind Channel, typename TBody>
    struct NetMessageBodyDef
    {
        using Body = TBody;

        static constexpr uint8               kOffset    = Offset;
        static constexpr NetMessageDirection kDirection = Direction;
        static constexpr NetChannelKind      kChannel   = Channel;
        static_assert( Offset < NetMessageRange::kSize, "a message must fit inside its range" );

        /** @brief 본문 판 — 본문 형식의 스키마 해시입니다. 필드 · 범위 · 순서를 바꾸면 저절로 바뀐다. */
        static uint32             getBodyRevision() { return NetMessageBodyCodec::computeRevision<TBody>(); }
        [[nodiscard]] static bool writeBody( const TBody& body, BitWriter& writer ) { return NetMessageBodyCodec::write( body, writer ); }
        /** @brief 남은 비트 전체를 본문 하나로 읽습니다. 잘림 · 남는 비트 · 범위 밖 값이면 false 이고 @p outBody 는 그대로다. */
        [[nodiscard]] static bool readBody( BitReader& reader, TBody& outBody ) { return NetMessageBodyCodec::read( reader, outBody ); }
    };
} // namespace sw
