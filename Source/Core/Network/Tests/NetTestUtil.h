/**
 * @file NetTestUtil.h
 * @brief 네트워크 단위 테스트가 함께 쓰는 도우미 — 번호를 담은 메시지, 자리마다 다른 바이트 덩어리, 시한 있는 대기, 서버가 받은 메시지 꺼내기.
 */
#pragma once
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Time/MonotonicClock.h"

namespace sw::test
{
    /** @struct NetTestUtil @brief 네트워크 테스트 도우미입니다(전부 static). */
    struct NetTestUtil
    {
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
            NetChannelType channel      = NetChannelType::Unreliable;
            vector<uint8>  buffer;
            while ( cluster.getServer().receiveMessage( connectionID, channel, buffer ) )
            {
                outListMessage.push_back( buffer );
            }
        }

        /**
         * @brief @p predicate 가 참이 될 때까지 기다립니다(최대 @p timeoutSeconds). 시한이 지나면 false 입니다.
         * @details 다른 스레드(네트워크 스레드 · 소켓)가 상태를 바꾸기를 기다리는 자리다 — 고정 시간을 자지 않고 조건을 1 ms 마다 다시 본다.
         */
        template <typename FPredicate>
        static bool waitUntil( FPredicate&& predicate, float64 timeoutSeconds = 5.0 )
        {
            const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( timeoutSeconds * 1000.0 ) );
            while ( predicate() == false )
            {
                if ( deadline.isExpired() )
                    return false;
                ThreadUtil::sleepForMilliseconds( 1 );
            }
            return true;
        }
    };
} // namespace sw::test
