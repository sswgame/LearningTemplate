// SessionPacket 퍼저 — 게임 세션이 UDP 로 받는 바이트 두 갈래에 임의 바이트를 넣는다. ① 연결이 선 뒤의 패킷 본문(`NetConnection::readPacket` — 시퀀스 · 확인 · 메시지 · 조각 모으기):
// 입력을 `[길이 2 바이트][본문]` 의 줄로 읽어 한 연결에 차례로 넣는다. ② 연결 전의 데이터그램(`NetEndpoint` — 핸드셰이크 · 제어 패킷): 체크섬은 맞춰 주고 본문만 임의다
// (체크섬이 다르면 첫 검사에서 버려 뒤를 보지 못한다). 거절은 정상이고, 크래시 · 새니타이저 보고만 결함이다.
#include "pch.h"

#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Serialization/ByteStream.h"

#include <algorithm>

namespace
{
    struct FuzzSessionPacketInternal
    {
        static constexpr float64 kStepSeconds  = 1.0 / 60.0;
        static constexpr uint16  kServerPort   = 4000;
        static constexpr uint16  kAttackerPort = 5000;

        /** @brief 선 연결 하나에 패킷 줄을 넣고, 답 · 꺼내기도 함께 돌립니다(받은 큐 · 확인 · 재전송 갈래까지). */
        static void feedConnection( const uint8* pData, size_t size )
        {
            sw::NetConnection connection;
            sw::ByteReader    reader( pData, size );
            float64           time = 0.0;
            while ( reader.getRemainingByteCount() > sizeof( uint16 ) )
            {
                const size_t       packetSize = std::min<size_t>( reader.readUint16(), reader.getRemainingByteCount() );
                const uint8* const pPacket    = pData + ( size - reader.getRemainingByteCount() );
                sw::BitReader      packet( pPacket, static_cast<int32>( packetSize ) );
                time += kStepSeconds;
                (void)connection.readPacket( time, packet );
                if ( reader.skipBytes( packetSize ) == false )
                    return;
                sw::vector<uint8> message;
                for ( uint8 channel = 0; channel < static_cast<uint8>( sw::NetChannelKind::Count ); ++channel )
                {
                    while ( connection.receiveMessage( static_cast<sw::NetChannelKind>( channel ), message ) )
                    {
                    }
                }
                sw::BitWriter reply;
                connection.writePacket( time, reply, sw::NetConnection::kMaxSingleMessageSize );
                if ( connection.hasReceiveOverflowed() )
                    return; // 엔드포인트는 여기서 연결을 끊는다
            }
        }

        /** @brief 듣는 서버에 연결 없는 주소의 데이터그램 하나를 보냅니다. @p headerID 는 버전 헤더거나 고정 핸드셰이크 헤더입니다. */
        static void feedServer( const uint8* pData, size_t size, bool bHandshakeHeader )
        {
            sw::LoopbackNetwork    network;
            sw::LoopbackTransport* pServerTransport = network.createEndpoint( kServerPort );
            sw::LoopbackTransport* pAttacker        = network.createEndpoint( kAttackerPort );
            sw::NetEndpointConfig  settings;
            settings._saltSeed = 1; // 같은 입력은 같은 길을 지난다
            sw::NetEndpoint server;
            const bool      bReady = pServerTransport != nullptr && pAttacker != nullptr && server.initialize( pServerTransport, settings ) && server.listen();
            if ( bReady == false )
                std::abort(); // 서버가 서지 못하면 퍼징이 아무것도 보지 않는다
            const uint32      headerID = bHandshakeHeader ? sw::NetProtocol::kHandshakeID : server.getProtocolID();
            sw::vector<uint8> datagram;
            sw::ByteWriter    writer( datagram );
            writer.writeUint32( headerID );
            writer.writeUint32( sw::NetEndpoint::computePacketChecksum( headerID, pData, static_cast<int32>( size ) ) );
            writer.writeBytes( pData, size );
            float64 time = 0.0;
            for ( int32 round = 0; round < 2; ++round ) // 같은 패킷이 두 번 와도(중복) 선다
            {
                (void)pAttacker->send( pServerTransport->getLocalAddress(), datagram.data(), static_cast<int32>( datagram.size() ) );
                time += kStepSeconds;
                server.update( time );
            }
        }
    };
} // namespace

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* pData, size_t size )
{
    constexpr size_t kMaxDatagramSize = 65507; // UDP 본문의 상한 — 받는 쪽도 이보다 큰 데이터그램을 받지 못한다
    if ( size == 0 || size > kMaxDatagramSize - 8 )
        return 0;
    FuzzSessionPacketInternal::feedConnection( pData, size );
    FuzzSessionPacketInternal::feedServer( pData, size, false );
    FuzzSessionPacketInternal::feedServer( pData, size, true );
    return 0;
}
