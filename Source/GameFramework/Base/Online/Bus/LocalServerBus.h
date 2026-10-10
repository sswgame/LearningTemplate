/**
 * @file LocalServerBus.h
 * @brief 프로세스 안의 서버 간 버스 — 같은 허브(`LocalServerBusHub`)를 쓰는 버스끼리 주고받습니다(서버 한 대 · 시험의 "서버 둘").
 * @details 허브는 버스마다 받은편지함 · 구독 주제를 갖고 잠금 하나로 줄 세운다(버스들이 다른 스레드여도 된다). 허브는 버스보다 오래 산다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class LocalServerBusHub
     * @brief 프로세스 안 버스들의 배달부입니다.
     */
    class SW_GF_API LocalServerBusHub
    {
    public:
        LocalServerBusHub();

        LocalServerBusHub( const LocalServerBusHub& )            = delete;
        LocalServerBusHub& operator=( const LocalServerBusHub& ) = delete;

        uint64 registerInbox();
        void   unregisterInbox( uint64 inboxID );
        void   subscribe( uint64 inboxID, string_view topic );
        void   unsubscribe( uint64 inboxID, string_view topic );
        /** @brief @p message 를 그 주제를 구독한 받은편지함마다 넣습니다. 받은 수입니다. */
        int32 deliver( const ServerBusMessage& message );
        int32 takeMessages( uint64 inboxID, vector<ServerBusMessage>& outListMessage );

    private:
        struct Inbox
        {
            vector<ServerBusMessage> _listMessage{};
            vector<string>           _listTopic{};
        };

        mutable mutex                _mutex;
        unordered_map<uint64, Inbox> _mapInbox;
        uint64                       _nextInboxID;
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalServerBus
     * @brief 허브 위의 버스 하나(서버 하나)입니다.
     */
    class SW_GF_API LocalServerBus final : public IServerBus
    {
    public:
        /** @brief @p pHub 는 빌려 쓴다(버스보다 오래 산다). */
        LocalServerBus( LocalServerBusHub* pHub, uint64 serverID );
        ~LocalServerBus() override;

        void   publish( string_view topic, const uint8* pData, int32 size ) override;
        void   subscribe( string_view topic ) override;
        void   unsubscribe( string_view topic ) override;
        int32  pollMessages( vector<ServerBusMessage>& outListMessage ) override;
        uint64 getServerID() const override { return _serverID; }

    private:
        LocalServerBusHub* _pHub;
        uint64             _serverID;
        uint64             _inboxID;
        uint64             _nextSequence;
    };
} // namespace sw
