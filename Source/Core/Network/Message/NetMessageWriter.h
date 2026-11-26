/**
 * @file NetMessageWriter.h
 * @brief 종류 바이트와 비트 본문으로 메시지를 쓰고 보내는 작성기를 정의합니다.
 * @details 메시지마다 `BitWriter` 를 새로 만들지 말고 `NetMessageWriter` 를 다시 쓴다(버퍼를 다시 쓰므로 메시지마다 할당하지 않는다).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/NetChannelKind.h"
#include "Core/Serialization/BitStream.h"

namespace sw
{
    /** @brief 메시지 서술자의 채널을 누가 정하는가입니다. 표 메시지는 모두 둘 중 하나를 적는다(`NetMessageDef` · `NetMessageSenderChannelDef`). */
    enum class NetChannelPolicy : uint8
    {
        Descriptor = 0, ///< 서술자의 채널(`kChannel`)로만 나간다
        SenderChooses   ///< 보내는 쪽이 상태에 따라 고른다(원격 호출의 신뢰 여부, 멈춘 자세) — `kChannel` 은 평소 채널이다
    };

    /** @brief 줄에 세웠다가 나중에 보내는 메시지 하나입니다 — 서술자에 묶인 채널을 바이트와 함께 든다(꺼낼 때 채널을 다시 고르지 않는다). */
    struct NetQueuedMessage
    {
        vector<uint8>  _bytes{};
        NetChannelKind _channel{ NetChannelKind::ReliableOrdered };
    };

    /**
     * @class NetMessageWriter
     * @brief 종류 바이트를 쓰고 본문은 돌려준 `BitWriter` 에 이어 쓴 뒤 보냅니다. 버퍼를 다시 쓰므로 메시지마다 할당하지 않습니다.
     * @details 표 메시지의 채널은 서술자 한 곳에서만 나온다.
     *          - 본문 형식이 있는 메시지(`writeMessage`)와 손으로 쓰는 표 메시지(`beginMessage`)는 서술자의 채널로만 나간다 — `sendMessage` · `trySendMessage` ·
     *            `broadcastMessage` · `sendMessageToPeers` · `sendNotifiedMessage` 는 채널을 받지 않는다. 보내는 쪽이 고르는 메시지(`NetChannelPolicy::SenderChooses`)는
     *            `beginMessage( channel )` 에서 한 번 고른다 — 서술자가 허락하지 않으면 컴파일되지 않는다.
     *          - 채널을 받는 `send` · `broadcast` · `sendToPeers` 는 표 밖 종류(`begin( kind )` — 라우터 판 알림, 채널 등록 메시지)만 보낸다. 표 메시지를 그쪽으로 보내려 하면
     *            오류 로그와 함께 보내지 않는다(서술자와 다른 채널로 나가던 스냅샷 확인 · 복제 입력 자리).
     * @code
     *     if ( _messageWriter.writeMessage<NetLockstepMessage::Table, NetLockstepMessage::Input>( body ) ) // 본문 형식이 있는 메시지(`NetMessageBodyDef`)
     *         (void)_messageWriter.sendMessage( *_pHost, connectionID );                              // 서술자의 채널
     *     BitWriter& writer = _messageWriter.beginMessage<NetLockstepMessage::Table, NetLockstepMessage::RollbackInput>(); // 손으로 쓰는 표 메시지
     *     writer.writeVarUint( frame );
     *     (void)_messageWriter.sendMessageToPeers( *_pHost );
     * @endcode
     */
    class SW_API NetMessageWriter
    {
    public:
        NetMessageWriter();

        /** @brief 비우고 표 밖 종류 바이트를 씁니다(라우터 판 알림 · 채널 등록 메시지 · 테스트). 채널을 받는 `send` 계열로만 보냅니다. 표 메시지는 `beginMessage` 입니다. */
        BitWriter& begin( uint8 kind );
        /** @brief 비우고 손으로 쓰는 표 메시지 @p TMessage 의 종류 바이트를 씁니다. 서술자의 채널로만 보냅니다(`sendMessage` 계열). */
        template <typename TTable, typename TMessage>
        BitWriter& beginMessage()
        {
            static_assert( TMessage::kChannelPolicy == NetChannelPolicy::Descriptor, "the sender chooses this message's channel - pass it to beginMessage( channel )" );
            return beginDescribed( TTable::template getKindOf<TMessage>(), TMessage::kChannel, TTable::kWorldScoped );
        }
        /** @brief 보내는 쪽이 채널을 고르는 표 메시지(`NetChannelPolicy::SenderChooses`)를 @p channel 로 시작합니다. 서술자가 허락하지 않으면 컴파일되지 않는다. */
        template <typename TTable, typename TMessage>
        BitWriter& beginMessage( NetChannelKind channel )
        {
            static_assert( TMessage::kChannelPolicy == NetChannelPolicy::SenderChooses, "this message travels on its descriptor channel - use beginMessage()" );
            return beginDescribed( TTable::template getKindOf<TMessage>(), channel, TTable::kWorldScoped );
        }
        /**
         * @brief 비우고 표 @p TTable 의 메시지 @p TMessage 의 종류 바이트와 본문(고정 배치형, `NetMessageBodyDef`)을 씁니다.
         * @return 범위 밖 값이 있으면 오류를 남기고 false 입니다 — 쓰다 만 바이트이므로 보내지 않는다.
         */
        template <typename TTable, typename TMessage>
        [[nodiscard]] bool writeMessage( const typename TMessage::Body& body )
        {
            const bool bWritten = TMessage::writeBody( body, beginDescribed( TTable::template getKindOf<TMessage>(), TMessage::kChannel, TTable::kWorldScoped ) );
            _state              = bWritten ? State::Typed : State::Failed;
            return bWritten;
        }
        /** @brief 손으로 쓴 본문(`begin`)을 @p channel 로 보냅니다. `writeMessage` 로 쓴 메시지는 받지 않습니다(`sendMessage`). */
        [[nodiscard]] bool send( INetPeers& peers, int32 connectionID, NetChannelKind channel ) const;
        /** @brief 손으로 쓴 본문을 연결된 모두에게 보냅니다. @p exceptID 는 제외합니다. 반환값은 보낸 수입니다. */
        int32 broadcast( INetPeers& peers, NetChannelKind channel, int32 exceptID = constant::invalid_index::kInt32 ) const;
        /** @brief 손으로 쓴 본문을 상대 모두에게 보냅니다. 서버는 연결된 모두에게, 클라이언트는 서버(연결 0)에게 보냅니다. 반환값은 보낸 수입니다. */
        int32 sendToPeers( INetPeers& peers, NetChannelKind channel ) const;
        /** @brief `writeMessage` 로 쓴 메시지를 서술자의 채널로 보냅니다. 쓰지 못했거나 손으로 쓴 본문이면 오류 로그와 false 입니다. */
        [[nodiscard]] bool sendMessage( INetPeers& peers, int32 connectionID );
        /** @brief `sendMessage` 와 같되 신뢰 창이 차면 기다리지 않고 false 입니다(`INetPeers::trySendMessage`). */
        [[nodiscard]] bool trySendMessage( INetPeers& peers, int32 connectionID );
        /** @brief `writeMessage` 로 쓴 메시지를 서술자의 채널로 연결된 모두에게(월드 범위면 같은 월드의 연결 모두에게) 보냅니다. 반환값은 보낸 수입니다. */
        int32 broadcastMessage( INetPeers& peers, int32 exceptID = constant::invalid_index::kInt32 );
        /** @brief `writeMessage` 로 쓴 메시지를 서술자의 채널로 상대 모두에게 보냅니다. 반환값은 보낸 수입니다. */
        int32 sendMessageToPeers( INetPeers& peers );
        /** @brief 전달 통지를 달아 보냅니다(`INetPeers::sendNotifiedMessage` — 비신뢰). 서술자 채널이 `Unreliable` 이 아니면 오류 로그와 false 입니다. */
        [[nodiscard]] bool sendNotifiedMessage( INetPeers& peers, int32 connectionID, uint32 tag );
        /** @brief `writeMessage` 로 쓴 메시지의 채널입니다(줄에 세웠다가 나중에 보내는 쪽이 바이트와 함께 든다). 손으로 쓴 본문이면 nullopt 입니다. */
        optional<NetChannelKind> getMessageChannel() const;
        /** @brief 서술자에 묶인 메시지를 줄에 세울 사본으로 만듭니다. 표 밖 종류(`begin`)이거나 쓰지 못한 메시지면 오류 로그와 nullopt 입니다. */
        [[nodiscard]] optional<NetQueuedMessage> makeQueuedMessage() const; ///< 월드 범위 표의 메시지는 세울 수 없다(보낼 때의 월드가 정해지지 않았다 — 오류 로그와 nullopt)

        /** @brief 검사 · 테스트용 바이트입니다. 보내는 길은 위의 send 계열이다(바이트를 꺼내 채널을 따로 고르지 않는다). */
        const vector<uint8>& getBytes() const { return _writer.getBytes(); }
        int32                getByteCount() const { return _writer.getByteCount(); }

    private:
        /** @brief 지금 버퍼의 메시지가 어느 길로 쓰였는지입니다 — 보내는 함수가 채널을 고를 수 있는지를 정한다. */
        enum class State : uint8
        {
            Raw = 0, ///< `begin` — 표 밖 종류. 채널을 받는 함수로만 보낸다
            Typed,   ///< `writeMessage` 성공 · `beginMessage` — 서술자에 묶인 채널로만 보낸다
            Failed   ///< `writeMessage` 실패 — 쓰다 만 바이트라 보내지 않는다
        };

        [[nodiscard]] bool allowsCallerChannel() const;
        [[nodiscard]] bool allowsDescriptorChannel() const;
        BitWriter&         beginDescribed( uint8 kind, NetChannelKind channel, bool bWorldScoped );
        /**
         * @brief 월드 범위 메시지에 @p peers 의 월드 세대를 적습니다. 서술자에 묶인 메시지가 아니면 오류를 남기고 false 입니다.
         * @details 월드 범위 메시지는 그 연결이 같은 월드에 있을 때만(`INetPeers::isInWorld`) 나간다 — 아니면 조용히 false 다(들어오면 처리기가 상태를 처음부터 보낸다).
         */
        [[nodiscard]] bool prepareDescribed( const INetPeers& peers );
        [[nodiscard]] bool prepareDescribed( const INetPeers& peers, int32 connectionID );
        /** @brief 쓴 메시지를 서버가 갈 수 있는 연결 모두에(월드 범위면 같은 월드의 연결만) 보냅니다. */
        int32 broadcastDescribed( INetPeers& peers, int32 exceptID );

    private:
        BitWriter      _writer;
        NetChannelKind _messageChannel;
        State          _state;
        uint8          _bWorldScoped; ///< 종류 뒤의 한 바이트가 월드 세대다(보낼 때 적는다)
    };
} // namespace sw
