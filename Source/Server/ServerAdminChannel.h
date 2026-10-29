/**
 * @file ServerAdminChannel.h
 * @brief 전용 서버의 관리 채널(서버 운영 O4) — 루프백 · 환경 변수 토큰의 줄 명령 채널과 허용 목록(`help` · `status` · `maintenance on|off|state` · `shutdown`), 점검 모드 상태.
 * @details `ServerApp` 이 호스트 서비스 `DebugChannel` 로 세우고 내리는 협력 객체다(언리얼 전용 서버의 RCON · 관리 명령 모듈과 같은 자리). 모든 구성(Shipping 포함)이다.
 *          서버 상태(틱 · 연결 · 드레인)는 세울 때 받은 델리게이트로 묻는다 — 채널은 서버 루프를 모른다. 점검 모드는 `/readyz` 를 503 으로 바꾼다(`ServiceHealthRegistry::setDraining`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct ServerConfig;

    class DebugChannel;
    class IStreamTransport;
    class ServiceHealthRegistry;

    /** @brief 관리 채널 `status` 가 보이는 서버 상태입니다. */
    struct ServerAdminStatus
    {
        uint64 _tickCount{ 0 };
        int32  _connectionCount{ 0 }; ///< 서버 서비스가 받고 있는 연결 수
        bool   _bDraining{ false };   ///< 종료 요청을 받아 드레인 중이다 — 점검 모드를 바꿀 수 없다
    };
    using ServerAdminStatusDelegate = Delegate<ServerAdminStatus()>;

    /**
     * @class ServerAdminChannel
     * @brief 전용 서버의 관리 채널입니다. 파일 머리말 참고.
     */
    class ServerAdminChannel
    {
    public:
        ServerAdminChannel();
        ~ServerAdminChannel();

        ServerAdminChannel( const ServerAdminChannel& )            = delete;
        ServerAdminChannel& operator=( const ServerAdminChannel& ) = delete;

        /**
         * @brief 관리 채널을 엽니다. 설정 `_adminChannelPort` 가 0 이면 아무 일도 하지 않습니다(기본 꺼짐).
         * @return 켰는데 토큰 환경 변수가 없거나 비면, 포트가 틀리면 false 입니다(기동 실패 - 토큰 없는 채널로 뜨지 않는다).
         */
        [[nodiscard]] bool start( const ServerConfig& config, ServiceHealthRegistry& healthRegistry, const ServerAdminStatusDelegate& status );
        /** @brief 채널을 닫습니다. 열지 않았어도 됩니다. */
        void stop();
        /** @brief 받은 줄을 이 스레드(게임 스레드)에서 실행합니다. 열지 않았으면 아무 일도 하지 않습니다. */
        void pump();

    private:
        /** @brief 관리 채널의 한 줄을 실행합니다(허용 목록: `help` · `status` · `maintenance on|off|state` · `shutdown`). */
        [[nodiscard]] bool executeCommand( string_view line, string& outReply );

        unique_ptr<IStreamTransport> _transport; ///< 관리 채널의 스트림 전송(채널을 종료한 뒤 지운다)
        unique_ptr<DebugChannel>     _channel;   ///< 관리 채널(`_adminChannelPort` 가 0 이 아닐 때만)
        ServiceHealthRegistry*       _pHealthRegistry;
        ServerAdminStatusDelegate    _status;
        bool                         _bMaintenance; ///< 점검 모드 - `/readyz` 가 503 이라 부하 분산기가 새 접속을 보내지 않는다(종료 드레인과 달리 서버는 계속 돈다)
    };
} // namespace sw
