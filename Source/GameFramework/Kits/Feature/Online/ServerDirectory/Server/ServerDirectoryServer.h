/**
 * @file ServerDirectoryServer.h
 * @brief 서버 디렉터리의 스트림 바인딩입니다. `IOnlineHostService`(영역 `kServerDirectory`)를 구현합니다.
 * @details 요청을 로직에 넘겨 바로 응답하고, 보이는 상태가 바뀌면 이 프로세스의 모든 클라이언트에 알리며, 버스 `sd.changed` 를 로직에 전달한다.
 *          - 상태와 목록은 로그인 전에도 쓸 수 있다. 서버 고르기 화면과 점검 안내 화면이 쓴다. 배정은 로그인한 뒤에 쓴다. 점검 허용 목록이 계정을 보기 때문이다.
 *          - 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerHostService( &server )`, 호스트 `initialize` 전에). 버스 구독은 첫 서비스 틱에서 하며 이때는 호스트가 버스를 받은 뒤이다.
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
     * @brief 서버 디렉터리 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_API ServerDirectoryServer final : public IOnlineHostService
    {
    public:
        ServerDirectoryServer();

        /** @brief @p pService 는 빌려 씁니다. 첫 서비스 틱 전에 지정합니다. */
        void initialize( ServerDirectoryService* pService );
        /** @brief 버스 구독을 해제합니다. 로직은 호출하는 쪽이 종료합니다. */
        void shutdown();

        // IOnlineHostService
        uint16 getMethodRange() const override { return OnlineMethodRange::kServerDirectory; }
        uint32 getProtocolVersion() const override;
        bool   isAnonymousMethod( uint16 method ) const override;
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onHostShutdown( OnlineServiceHost& host ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );

        ServerDirectoryService* _pService;
        OnlineServiceHost*      _pHost; ///< 버스를 구독한 호스트이다. 구독을 해제할 때 쓴다
    };
} // namespace sw
