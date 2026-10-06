/**
 * @file ServerDirectoryServer.h
 * @brief 서버 디렉터리의 스트림 바인딩 — `IOnlineService`(영역 `kServerDirectory`). 요청을 로직에 넘겨 바로 답하고, 보이는 상태가 바뀌면 이 프로세스의 모두에게 알리고,
 *        버스 `sd.changed` 를 로직에 전합니다.
 * @details - 상태 · 목록은 익명(로그인 전 서버 고르기 · 점검 안내 화면), 배정은 로그인 뒤(점검 허용 목록이 계정을 본다).
 *          - 호스트에 올리는 것은 부르는 쪽이(`host.registerService( &server )` — 호스트 `initialize` 전에). 버스 구독은 첫 서비스 틱에 한다(호스트가 버스를 받은 뒤).
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ServerDirectoryService;

    /**
     * @class ServerDirectoryServer
     * @brief 서버 디렉터리 바인딩입니다(호스트 `tick` 스레드).
     */
    class SW_GF_API ServerDirectoryServer final : public IOnlineService
    {
    public:
        ServerDirectoryServer();

        /** @brief @p pService 는 빌려 쓴다(첫 서비스 틱 전에). */
        void initialize( ServerDirectoryService* pService );
        /** @brief 버스 구독을 풉니다. 로직은 부르는 쪽이 내린다. */
        void shutdown();

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kServerDirectory; }
        uint32 getProtocolVersion() const override;
        bool   isAnonymousMethod( uint16 method ) const override;
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );

        ServerDirectoryService* _pService;
        OnlineServiceHost*      _pHost; ///< 버스를 구독한 호스트(풀 때)
    };
} // namespace sw
