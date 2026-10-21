/**
 * @file NetHostShardSet.h
 * @brief 포트 샤드마다 하나씩 있는 `NetHost` 여럿을 키트에게 연결 하나의 목록(`INetPeers`)으로 보이게 묶습니다.
 * @details 전용 서버는 받기 스레드 하나가 차면 포트를 나눈다(포트 샤딩 — 샤드마다 소켓 · `NetHost` · `NetHostThread` 하나).
 *          키트 · 라우터는 호스트가 몇 개인지 모르고 연결 ID 만 다룬다. 이 묶음이 연결 ID 에 샤드 번호를 접어 넣는다:
 *          `연결 ID = 샤드 번호 × 간격 + 샤드 안의 연결 ID`. 간격은 샤드 하나의 연결 상한(`NetHostConfig::_maxConnections`)이라
 *          두 샤드의 ID 가 겹치지 않는다. 샤드 하나(클라이언트 · 샤딩하지 않는 서버)면 ID 가 호스트의 것과 같다.
 *
 *          스레드: 샤드 목록은 `initialize` 뒤 바뀌지 않으므로, 보내기 · 묻기는 어느 스레드에서나 부를 수 있다(각 호스트가 잠금으로 지킨다).
 *          `drainInbound` 는 스크래치 버퍼를 다시 쓰므로 한 스레드(라우터를 pump 하는 게임 스레드)만 부른다.
 *          언리얼 `UNetDriver` 가 연결 목록 하나를 키트(리플리케이션 그래프)에 주는 자리와 같다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Network/Connection/NetHost.h"

namespace sw
{
    /**
     * @class NetHostShardSet
     * @brief 빌려 쓰는 `NetHost` 여럿을 `INetPeers` 하나로 보입니다. 호스트는 이 묶음보다 오래 살아야 합니다.
     * @code
     *     vector<NetHost*> listShard = { &first, &second };
     *     if ( shardSet.initialize( listShard, settings._maxConnections ) == false ) return false;
     *     router.pump( shardSet );                          // 두 샤드의 사건 · 메시지 — 연결 ID 는 접어 넣은 값
     *     (void)shardSet.sendMessage( connectionID, ... ); // ID 가 샤드를 고른다
     * @endcode
     */
    class SW_API NetHostShardSet final : public INetPeers
    {
    public:
        NetHostShardSet();
        ~NetHostShardSet() override;

        /**
         * @brief 샤드를 정합니다. @p connectionStride 는 샤드 하나의 연결 상한이며 모든 샤드가 같은 호스트 설정을 쓴다는 약속입니다.
         * @details 샤드가 없거나, nullptr 가 있거나, 간격이 1 보다 작거나, 접은 ID 가 int32 를 넘으면 오류 로그와 false 다(묶음은 빈 채로 남는다).
         */
        [[nodiscard]] bool initialize( const vector<NetHost*>& listShard, int32 connectionStride );
        void               shutdown();

        int32 getShardCount() const { return static_cast<int32>( _listShard.size() ); }
        /** @brief 샤드 하나의 호스트입니다. @p shardIndex 는 0 ~ `getShardCount() - 1` 입니다. */
        NetHost& getShard( int32 shardIndex ) const { return *_listShard[static_cast<size_t>( shardIndex )]; }
        /** @brief 샤드 번호와 샤드 안의 연결 ID 를 묶음의 연결 ID 로 접습니다. */
        int32 makeConnectionID( int32 shardIndex, int32 localConnectionID ) const { return shardIndex * _connectionStride + localConnectionID; }

        [[nodiscard]] bool sendMessage( int32 connectionID, NetChannelType channel, const uint8* pData, int32 size ) override;
        int32              broadcast( NetChannelType channel, const uint8* pData, int32 size, int32 exceptID = -1 ) override;
        void               disconnect( int32 connectionID ) override;
        /** @brief 샤드마다 꺼내 하나로 모읍니다. 모든 샤드의 사건이 모든 메시지보다 앞에 오고, 연결 ID 는 접어 넣은 값입니다. */
        void               drainInbound( NetInbound& outInbound ) override;
        bool               isServer() const override;
        NetConnectionState getConnectionState( int32 connectionID ) const override;
        [[nodiscard]] bool getConnectionStats( int32 connectionID, NetConnectionStats& outStats ) const override;
        NetAddress         getConnectionAddress( int32 connectionID ) const override;
        int32              getConnectedCount() const override;
        void               collectConnected( vector<int32>& outListConnection ) const override;
        int32              getMaxBytesPerSecond() const override;
        bool               isConnectionSaturated( int32 connectionID ) const override;
        void               addSentKindTally( NetMessageKindTallyTable& outTable ) const override;

    private:
        /** @brief 묶음의 연결 ID 를 샤드와 샤드 안의 ID 로 풉니다. 범위 밖이면 nullptr 입니다. */
        NetHost* findShard( int32 connectionID, int32& outLocalConnectionID ) const;

        vector<NetHost*> _listShard;
        NetInbound       _shardInbound; ///< `drainInbound` 가 샤드마다 다시 쓴다(게임 스레드 전용)
        int32            _connectionStride;
    };
} // namespace sw
