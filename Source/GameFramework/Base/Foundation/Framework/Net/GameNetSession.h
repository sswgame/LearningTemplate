/**
 * @file GameNetSession.h
 * @brief 게임 인스턴스의 네트워크 역할입니다 — 서버면 UDP 포트를 열고(`NetHost::listen`), 클라이언트면 서버에 붙습니다(`NetHost::connectAsync`).
 * @details 언리얼의 넷 드라이버(`?listen` · `open <주소>`)와 Godot 의 `SceneMultiplayer` 자리입니다. 역할은 게임 코드가 아니라 실행 인자가 정하므로
 *          어느 게임이든(키트가 없는 Empty 도) 서버 · 클라이언트로 뜹니다. 장르별 복제(클라이언트-서버 · 락스텝 · MMO)는 키트가 `getRouter()` 에
 *          처리기를 달아 이 호스트 위에 얹습니다.
 *
 *          - 서버: `-gv_netListenPort=<포트>` — 전용 서버(`Server`)와 리슨 호스트(`App`) 모두.
 *          - 클라이언트: `-gv_netConnectAddress=<주소:포트>`(포트를 빼면 `kDefaultPort`).
 *          - 둘 다 없으면 세션을 열지 않습니다(혼자 하는 게임 — 지금까지와 같다).
 *          클라이언트는 연결이 시간 초과로 끝나면 다시 시도합니다(서버를 같이 띄우면 클라이언트가 먼저 설 수 있다). 거절 · 판 불일치는 한 번 알리고 멈춥니다.
 *
 *          네트워크 스레드(`NetHostThread`)가 받기 · 확인 · 재전송 · 유지를 맡고, 게임 스레드는 프레임마다 `update` 로 연결 사건과 받은 메시지를
 *          처리기에 나눠 줍니다. 연결 · 끊김은 로그 한 줄씩 남깁니다(`Net session client N connected` · `Net session connected to …`) — 멀티 프로세스
 *          재생과 시험이 이 줄로 접속을 확인합니다.
 * @warning 게임 모듈 핫 리로드는 게임 인스턴스를 새로 만듭니다 — 세션도 닫고 다시 열므로 클라이언트는 끊겼다가(시간 초과) 다시 붙어야 합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Connection/NetHostThread.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/NetTypes.h"
#include "Core/Task/TaskFuture.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class UDPNetTransport;

    /** @brief 게임 인스턴스의 네트워크 역할입니다. */
    enum class GameNetRole : uint8
    {
        None = 0, ///< 세션 없음(혼자 하는 게임)
        Server,   ///< 포트를 열고 클라이언트를 받는다(전용 서버 · 리슨 호스트)
        Client    ///< 서버에 붙는다
    };

    SW_GF_API const utf8* toString( GameNetRole role );
} // namespace sw

namespace sw
{
    /** @brief 세션을 여는 인자입니다. */
    struct GameNetSessionParams
    {
        NetHostSettings _hostSettings{};
        NetAddress      _serverAddress{}; ///< 클라이언트 — 붙을 서버
        uint16          _listenPort{ 0 }; ///< 서버 — 열 UDP 포트
        GameNetRole     _role{ GameNetRole::None };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameNetSession
     * @brief 게임 인스턴스 하나의 네트워크 호스트 · 스레드 · 메시지 라우터를 들고 있습니다(`GameInstanceBase` 가 소유한다).
     * @code
     *     GameNetSessionParams params;
     *     if ( GameNetSession::makeParamsFromGlobalVariables( params ) )
     *         (void)session.initialize( params );
     *     session.getRouter().addHandler( &replicationServer ); // 키트
     *     session.update();                                     // 프레임마다(게임 스레드)
     *     session.shutdown();
     * @endcode
     */
    class SW_GF_API GameNetSession
    {
    public:
        /** @brief 주소에 포트가 없을 때의 기본 게임 포트입니다(언리얼 기본 포트와 같다 — `ServerConfig::_gamePort` 기본값도 같다). */
        static constexpr uint16 kDefaultPort = 7777;

        GameNetSession();
        ~GameNetSession();

        GameNetSession( const GameNetSession& )            = delete;
        GameNetSession& operator=( const GameNetSession& ) = delete;

        /**
         * @brief 실행 인자(`-gv_netListenPort` · `-gv_netConnectAddress`)에서 인자를 만듭니다. 둘 다 없으면 false(세션을 열지 않는다)입니다.
         * @details 둘 다 있으면 서버를 고르고 경고합니다. 주소를 읽지 못하면 경고하고 false 입니다.
         */
        [[nodiscard]] static bool makeParamsFromGlobalVariables( GameNetSessionParams& outParams );

        /** @brief 소켓을 열고 역할을 시작합니다(서버 listen · 클라이언트 connectAsync) — 실패는 로그에 남기고 false 입니다. */
        [[nodiscard]] bool initialize( const GameNetSessionParams& params );
        /** @brief 모든 연결을 끊고 네트워크 스레드 · 소켓을 닫습니다. 두 번 불러도 됩니다. */
        void shutdown();
        /** @brief 쌓인 연결 사건과 받은 메시지를 처리기에 나눠 주고, 연결 · 끊김을 로그에 남깁니다(게임 스레드, 프레임마다). */
        void update();

        bool        isActive() const { return _role != GameNetRole::None; }
        GameNetRole getRole() const { return _role; }
        /** @brief 서버 — 연 UDP 포트(0 을 줬으면 운영체제가 고른 포트), 클라이언트 — 0 입니다. */
        uint16 getListenPort() const { return _listenPort; }
        /** @brief 서버 — 붙은 클라이언트 수, 클라이언트 — 서버에 붙었으면 1 입니다. */
        int32 getConnectedCount() const;
        /** @brief 처리기 · 송수신이 쓰는 호스트입니다. 세션을 열지 않았으면 쓰지 않습니다(`isActive`). */
        NetHost& getHost() { return _host; }
        /** @brief 키트 · 게임이 메시지 처리기를 다는 라우터입니다(`update` 가 pump 한다). */
        NetMessageRouter& getRouter() { return _router; }

    private:
        /** @brief 클라이언트 연결이 끝났으면 로그에 남깁니다. 시간 초과면 다시 시도합니다(같이 띄운 서버가 아직 기동 중일 수 있다). */
        void reportConnectResult();
        /** @brief `_listEventScratch` 의 연결 · 끊김을 로그 한 줄씩 남깁니다(서버는 둘 다, 클라이언트는 끊김만). */
        void logEvents() const;

    private:
        NetHost                      _host;
        NetHostThread                _thread;
        NetMessageRouter             _router;
        unique_ptr<UDPNetTransport>  _transport;
        TaskFuture<NetConnectResult> _connectFuture;    ///< 클라이언트 — 기다리는 연결(결과를 알리면 비운다)
        vector<NetHostEvent>         _listEventScratch; ///< `update` 가 다시 쓴다
        NetAddress                   _serverAddress;
        uint16                       _listenPort;
        GameNetRole                  _role;
        uint8                        _bConnectFailureReported; ///< 클라이언트 — 연결 실패를 한 번 알렸다(다시 시도하는 시간 초과는 처음만 알린다)
    };
} // namespace sw
