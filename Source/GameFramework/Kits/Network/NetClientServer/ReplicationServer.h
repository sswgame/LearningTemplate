/**
 * @file ReplicationServer.h
 * @brief 권위 서버 쪽 — 매 틱 월드 상태를 모아 클라이언트마다 (관련 · 우선도 정책을 거쳐) 확인받은 기준 대비 델타로 보내고, 중복으로 온 입력을 틱 순으로 꺼내 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Network/NetMessage.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Network/NetClientServer/NetSnapshot.h"

namespace sw
{
    class NetHost;

    /**
     * @class IReplicationPolicy
     * @brief 무엇을 누구에게 얼마나 먼저 보낼지 — 게임마다 바꿉니다(배틀로얄은 거리, 비대칭 대전은 시야 · 역할, 기체 대전은 전부).
     */
    class SW_GF_API IReplicationPolicy
    {
    public:
        virtual ~IReplicationPolicy() = default;

        /** @brief 이 클라이언트에게 이 엔티티를 보내는가입니다(안 보내면 클라이언트에서 사라진다). */
        virtual bool isRelevant( int32 connectionId, const NetEntityState& entity ) const
        {
            (void)connectionId;
            (void)entity;
            return true;
        }
        /** @brief 패킷이 모자랄 때 먼저 싣는 순서(클수록 먼저)입니다. */
        virtual float32 computePriority( int32 connectionId, const NetEntityState& entity ) const
        {
            (void)connectionId;
            (void)entity;
            return 1.0f;
        }
    };

    /** @brief 서버 설정입니다. */
    struct ReplicationServerSettings
    {
        int32 _snapshotBudgetBytes{ 1000 }; ///< 클라이언트 · 틱마다 스냅샷 메시지 상한
        int32 _historySize{ 64 };           ///< 클라이언트마다 기억하는 보낸 스냅샷(기준 후보) 수
        int32 _inputBufferSize{ 64 };
    };

    /**
     * @class ReplicationServer
     * @code
     *     server.beginTick( tick );
     *     for ( each entity ) server.setEntity( id, typeId, bytes );
     *     server.endTick();
     *     server.sendSnapshots();
     *     // 받은 메시지: router.addHandler( &server ) 뒤 매 틱 router.pump( host ) — 손으로는 server.handleMessage( connectionId, bytes )
     *     // 시뮬레이션: server.popInput( connectionId, tick, inputBytes ); ... server.setLastProcessedInputTick( connectionId, tick );
     * @endcode
     */
    class SW_GF_API ReplicationServer : public INetMessageHandler
    {
    public:
        ReplicationServer();

        void initialize( NetHost* pHost, const ReplicationServerSettings& settings, const IReplicationPolicy* pPolicy = nullptr );
        void beginTick( uint32 tick );
        void setEntity( uint32 entityId, uint32 typeId, const vector<uint8>& buffer );
        void endTick();
        /** @brief 연결된 클라이언트마다 스냅샷을 보냅니다. */
        void sendSnapshots();
        /** @brief 이 키트의 메시지면 처리하고 true 입니다. */
        uint8 getMessageRangeBase() const override { return NetMessageRange::kClientServer; }
        bool  handleNetMessage( int32 connectionId, const uint8* pData, int32 size ) override;
        /** @brief 받은 메시지 하나 — 내 영역이 아니면 false(`NetMessageRouter` 를 쓰지 않는 게임의 손 배달). */
        bool handleMessage( int32 connectionId, const vector<uint8>& buffer ) { return handleNetMessage( connectionId, buffer.data(), static_cast<int32>( buffer.size() ) ); }
        void onDisconnected( int32 connectionId );

        /**
         * @brief 그 틱의 입력을 꺼냅니다. 아직 안 왔으면 가장 최근 입력을 되풀이합니다(@p outbExact false). 받은 입력이 하나도 없으면 false 입니다.
         */
        [[nodiscard]] bool popInput( int32 connectionId, uint32 tick, vector<uint8>& outInputBuffer, bool& outbExact );
        void               setLastProcessedInputTick( int32 connectionId, uint32 tick );
        /** @brief 클라이언트가 보고 있는 틱(보간 지연만큼 과거, 소수 — 랙 보정 되감기)입니다. */
        float32            getClientViewTick( int32 connectionId ) const;
        uint32             getAckedTick( int32 connectionId ) const;
        const NetSnapshot& getWorldSnapshot() const { return _world; }

    private:
        struct InputEntry
        {
            vector<uint8> _buffer{};
            uint32        _tick{ 0 };
        };

        struct ClientState
        {
            vector<NetSnapshot> _listSent{};  ///< 틱 % 크기 자리 — 보낸(재구성된) 스냅샷
            deque<InputEntry>   _listInput{}; ///< 틱 오름차순
            vector<uint8>       _lastInput{};
            float32             _viewTick{ 0.0f };
            uint32              _ackedTick{ 0 };
            uint32              _lastProcessedInputTick{ 0 };
            uint8               _bHasAck{ SW_FALSE };
            uint8               _bHasInput{ SW_FALSE };
            uint8               _bActive{ SW_FALSE };
        };

        ClientState& acquireClient( int32 connectionId );
        void         handleInput( ClientState& client, const uint8* pData, int32 size );

        vector<ClientState>       _listClient;
        NetSnapshot               _world;
        ReplicationServerSettings _settings;
        NetHost*                  _pHost;
        const IReplicationPolicy* _pPolicy;
        IReplicationPolicy        _defaultPolicy;
        NetMessageWriter          _messageWriter;   ///< 보낼 메시지 — 버퍼를 다시 쓴다
        NetSnapshot               _filteredScratch; ///< 클라이언트 하나의 관련 엔티티(틱마다 다시 쓴다)
        vector<float32>           _listPriorityScratch;
        vector<int32>             _listOrderScratch;
        vector<int32>             _listConnectionScratch;
    };
} // namespace sw
