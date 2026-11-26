/**
 * @file NetTestUtil.h
 * @brief 네트워크 단위 테스트가 함께 쓰는 도우미 — 주소마다 오간 바이트를 세는 전송, 손으로 만든 패킷, 번호를 담은 메시지, 자리마다 다른 바이트 덩어리, 서버가 받은 메시지 꺼내기. 조건 대기는 `test::waitUntil`(Core/TestFramework/TestWait.h).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/Serialization/LittleEndianUtil.h"

#include <cstring>

namespace sw::test
{
    /** @brief 전송에 씌워 보낸 데이터그램을 모두 남기고 상대 주소마다 오간 바이트를 셉니다(증폭 확인 · 가로챈 패킷 다시 보내기). */
    class RecordingTransport final : public INetTransport
    {
    public:
        struct SentDatagram
        {
            NetAddress    _to{};
            vector<uint8> _bytes{};
        };

        struct Traffic
        {
            NetAddress _peer{};
            int32      _receivedBytes{ 0 };
            int32      _sentBytes{ 0 };
            int32      _sentCount{ 0 };
        };

        INetTransport* _pRelay{ nullptr }; ///< 있으면 보내는 것만 이 전송의 주소에서 나간다

        explicit RecordingTransport( INetTransport* pInner )
            : _pInner{ pInner }
        {
        }

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override
        {
            Traffic& traffic = getTraffic( to );
            traffic._sentBytes += size;
            ++traffic._sentCount;
            _listSent.push_back( SentDatagram{ to, vector<uint8>( pData, pData + size ) } );
            return ( _pRelay != nullptr ? _pRelay : _pInner )->send( to, pData, size );
        }
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override
        {
            if ( _pInner->receive( outFrom, outBuffer ) == false )
                return false;
            getTraffic( outFrom )._receivedBytes += static_cast<int32>( outBuffer.size() );
            return true;
        }
        NetAddress getLocalAddress() const override { return _pInner->getLocalAddress(); }
        void       update( float64 time ) override { _pInner->update( time ); }

        Traffic& getTraffic( const NetAddress& peer )
        {
            for ( Traffic& traffic : _listTraffic )
            {
                if ( traffic._peer == peer )
                    return traffic;
            }
            _listTraffic.push_back( Traffic{ peer } );
            return _listTraffic.back();
        }
        void clear()
        {
            _listSent.clear();
            _listTraffic.clear();
        }
        /** @brief 가장 최근에 보낸 @p kind 패킷입니다. 없으면 빈 바이트입니다. */
        vector<uint8> findLastSent( NetEndpoint::PacketKind kind ) const
        {
            vector<uint8> found;
            for ( const SentDatagram& sent : _listSent )
            {
                if ( NetEndpoint::peekPacketKind( sent._bytes.data(), static_cast<int32>( sent._bytes.size() ) ) == kind )
                    found = sent._bytes;
            }
            return found;
        }
        /** @brief @p kind 로 보낸 패킷 수와 바이트 합입니다. */
        Traffic sumSent( NetEndpoint::PacketKind kind ) const
        {
            Traffic sum;
            for ( const SentDatagram& sent : _listSent )
            {
                if ( NetEndpoint::peekPacketKind( sent._bytes.data(), static_cast<int32>( sent._bytes.size() ) ) != kind )
                    continue;
                sum._sentBytes += static_cast<int32>( sent._bytes.size() );
                ++sum._sentCount;
            }
            return sum;
        }

    private:
        vector<SentDatagram> _listSent{};
        vector<Traffic>      _listTraffic{};
        INetTransport*       _pInner;
    };

    /** @struct NetTestUtil @brief 네트워크 테스트 도우미입니다(전부 static). */
    struct NetTestUtil
    {
        static constexpr int32 kPacketHeaderSize = 8; ///< 헤더 값 4 바이트와 체크섬 4 바이트

        /** @brief 제어 패킷(종류 3 비트 + 64 비트 값 둘)을 손으로 만듭니다 — 위조 패킷 테스트용. */
        static vector<uint8> makeControlPacket( uint32 headerID, NetEndpoint::PacketKind kind, uint64 valueA, uint64 valueB )
        {
            BitWriter writer;
            writer.writeBits( static_cast<uint32>( kind ), 3 );
            writer.writeBits( static_cast<uint32>( valueA ), 32 );
            writer.writeBits( static_cast<uint32>( valueA >> 32 ), 32 );
            writer.writeBits( static_cast<uint32>( valueB ), 32 );
            writer.writeBits( static_cast<uint32>( valueB >> 32 ), 32 );
            vector<uint8> packet( static_cast<size_t>( kPacketHeaderSize + writer.getByteCount() ), 0 );
            std::memcpy( packet.data() + kPacketHeaderSize, writer.getBytes().data(), static_cast<size_t>( writer.getByteCount() ) );
            LittleEndianUtil::writeUint32( packet.data(), headerID );
            return resizePacket( packet, static_cast<int32>( packet.size() ) );
        }

        /** @brief 패킷을 @p size 바이트로 자르거나 0 으로 채우고 체크섬을 다시 맞춥니다. */
        static vector<uint8> resizePacket( const vector<uint8>& source, int32 size )
        {
            vector<uint8> packet = source;
            packet.resize( static_cast<size_t>( size ), 0 );
            const uint32 headerID = ByteReader( packet.data(), packet.size() ).readUint32();
            LittleEndianUtil::writeUint32( packet.data() + 4, NetEndpoint::computePacketChecksum( headerID, packet.data() + kPacketHeaderSize, size - kPacketHeaderSize ) );
            return packet;
        }

        /** @brief @p packet 을 @p from 의 주소에서 포트 4000 의 서버로 @p count 번 보냅니다. */
        static void sendRaw( INetTransport& from, const vector<uint8>& packet, int32 count )
        {
            for ( int32 index = 0; index < count; ++index )
            {
                (void)from.send( NetAddress::makeLoopback( 4000 ), packet.data(), static_cast<int32>( packet.size() ) ); // 루프백이라 언제나 나간다
            }
        }

        /** @brief 앞 4 바이트에 @p value 를(낮은 바이트부터) 담은 @p size 바이트 메시지입니다. */
        static vector<uint8> makeMessage( int32 value, int32 size = 4 )
        {
            vector<uint8> buffer( static_cast<size_t>( size ), 0 );
            for ( int32 index = 0; index < 4 && index < size; ++index )
            {
                buffer[static_cast<size_t>( index )] = static_cast<uint8>( value >> ( index * 8 ) );
            }
            return buffer;
        }

        /** @brief `makeMessage` 가 담은 값을 읽습니다. */
        static int32 readMessageValue( const vector<uint8>& buffer )
        {
            int32 value = 0;
            for ( int32 index = 0; index < 4 && index < static_cast<int32>( buffer.size() ); ++index )
            {
                value |= static_cast<int32>( buffer[static_cast<size_t>( index )] ) << ( index * 8 );
            }
            return value;
        }

        /** @brief @p seed 로 정해지는 바이트 @p size 개입니다(조각 경계를 넘어 섞여도 알아보게 자리마다 다르다). */
        static vector<uint8> makePatternMessage( uint32 seed, int32 size )
        {
            vector<uint8> buffer( static_cast<size_t>( size ), 0 );
            uint32        state = seed * 2654435761u + 1u;
            for ( uint8& byte : buffer )
            {
                state = state * 1664525u + 1013904223u;
                byte  = static_cast<uint8>( state >> 24 );
            }
            return buffer;
        }

        /** @brief 서버(포트 4000 — 엔드포인트 0) · 클라이언트(포트 5000 — 엔드포인트 1)를 더합니다. listen · connect 는 케이스가 한다. */
        static void addServerAndClient( LoopbackCluster& cluster, const NetEndpointConfig& settings )
        {
            (void)cluster.addEndpoint( 4000, settings ); // 빈 클러스터라 포트가 겹치지 않는다
            (void)cluster.addEndpoint( 5000, settings );
        }

        /** @brief 서버(엔드포인트 0)가 받은 메시지를 모두 꺼내 뒤에 붙입니다. */
        static void pollServerMessages( LoopbackCluster& cluster, vector<vector<uint8>>& outListMessage )
        {
            int32          connectionID = -1;
            NetChannelKind channel      = NetChannelKind::Unreliable;
            vector<uint8>  buffer;
            while ( cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
            {
                outListMessage.push_back( buffer );
            }
        }
    };
} // namespace sw::test
