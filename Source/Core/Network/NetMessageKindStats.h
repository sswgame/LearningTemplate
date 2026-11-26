/**
 * @file NetMessageKindStats.h
 * @brief 메시지 종류(첫 바이트)마다의 누적 셈과 그 표를 정의합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 메시지 종류 하나 몫 누적 셈입니다(받은 쪽은 라우터, 보낸 쪽은 엔드포인트가 센다). 개발 명령 `net.stats` 의 메시지 종류별 비중이 이 값입니다. */
    struct NetMessageKindStats
    {
        uint64 _messageCount{ 0 };
        uint64 _byteCount{ 0 }; ///< 종류 바이트를 포함한 메시지 크기의 합(패킷 헤더 · 확인 응답은 빠진다)
    };

    /**
     * @class NetMessageKindStatsTable
     * @brief 메시지 종류(첫 바이트) 256 개의 누적 셈 표입니다. 받은 쪽(`NetMessageRouter::poll`)과 보낸 쪽(`NetEndpoint::sendMessage`)이 같은 표를 써서
     *        `NetStatsReport` 가 두 방향을 같은 규칙으로 싣습니다. 스레드 보호는 들고 있는 쪽이 합니다.
     */
    class NetMessageKindStatsTable
    {
    public:
        static constexpr int32 kKindCount = 256;

        /** @brief 메시지 하나를 셉니다. 빈 메시지(종류가 없다)는 세지 않습니다. */
        void add( const uint8* pData, int32 size )
        {
            if ( size <= 0 )
                return;
            NetMessageKindStats& stats = _arrStats[pData[0]];
            ++stats._messageCount;
            stats._byteCount += static_cast<uint64>( size );
        }
        /** @brief 다른 표의 셈을 종류마다 더합니다(포트 샤드마다의 보낸 쪽 셈을 하나로 모을 때). */
        void addTable( const NetMessageKindStatsTable& other )
        {
            for ( int32 kind = 0; kind < kKindCount; ++kind )
            {
                _arrStats[kind]._messageCount += other._arrStats[kind]._messageCount;
                _arrStats[kind]._byteCount += other._arrStats[kind]._byteCount;
            }
        }
        const NetMessageKindStats& get( uint8 kind ) const { return _arrStats[kind]; }

    private:
        NetMessageKindStats _arrStats[kKindCount]{};
    };
} // namespace sw
