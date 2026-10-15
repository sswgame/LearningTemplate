/**
 * @file GameNetSession.h
 * @brief 게임 인스턴스의 네트워크 역할을 정의합니다.
 * @details 서버면 UDP 포트를 열고(`NetHost::listen`), 클라이언트면 서버에 붙는다(`NetHost::connectAsync`).
 *          언리얼의 넷 드라이버(`?listen`, `open <주소>`)와 Godot 의 `SceneMultiplayer` 에 해당한다.
 *          역할은 게임 코드가 아니라 실행 인자가 정하므로, 어느 게임이든(키트가 없는 Empty 도) 서버나 클라이언트로 뜬다.
 *          장르별 복제(클라이언트-서버, 락스텝, MMO)는 키트가 `getRouter()` 에 처리기를 달아 이 호스트 위에 얹는다.
 *
 *          - 서버: `-gv_netListenPort=<포트>` 로 연다. 전용 서버(`Server`)와 리슨 호스트(`App`) 모두 같다.
 *          - 클라이언트: `-gv_netConnectAddress=<주소:포트>` 로 붙는다. 포트를 빼면 `kDefaultPort` 를 쓴다.
 *          - 둘 다 없으면 세션을 열지 않는다. 혼자 하는 게임은 이전과 같이 동작한다.
 *
 *          클라이언트는 연결이 시간 초과로 끝나면 다시 시도한다. 서버를 같이 띄우면 클라이언트가 먼저 설 수 있기 때문이다.
 *          거절이나 버전 불일치는 한 번 알리고 멈춘다.
 *
 *          네트워크 스레드(`NetHostThread`)가 받기, 확인, 재전송, 유지를 맡는다.
 *          게임 스레드는 프레임마다 `update` 로 연결 이벤트와 받은 메시지를 처리기에 나눠 준다.
 *          연결과 끊김은 로그를 한 줄씩 남기며(`Net session client N connected`, `Net session connected to …`),
 *          멀티 프로세스 재생과 테스트가 이 줄로 접속을 확인한다.
 *
 *          Dev 구성에서는 UDP 전송을 회선 흉내(`NetEmulationTransport`)로 감싼다. 시작 값은 `-gv_netEmu*` 이고, 실행 중에는 개발 명령
 *          `net.emulation` · `net.disconnect` · `net.reconnect` · `net.traffic` 이 바꾼다(에디터 Multiplayer 창 Conditions 탭이 표준 입력으로 보낸다).
 *          `net.stats` 는 연결 · 트래픽 수치 한 줄(`NetStatsReport`)을 답한다(Profiler 탭이 0.5 초마다 묻는다).
 *          조건이 꺼져 있으면 흉내는 패킷을 그대로 넘긴다. Shipping 에는 흉내가 없다.
 * @warning 게임 모듈 핫 리로드는 게임 인스턴스를 새로 만든다. 세션도 닫고 다시 열므로 클라이언트는 끊겼다가(시간 초과) 다시 붙어야 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Connection/NetHostThread.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Task/TaskFuture.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Framework/GameInstanceSubsystem.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct NetStatsReport;

    class UDPNetTransport;

    /** @brief 게임 인스턴스의 네트워크 역할입니다. */
    ENUM()
    enum class GameNetRole : uint8
    {
        None = 0, ///< 세션 없음(혼자 하는 게임)
        Server,   ///< 포트를 열고 클라이언트를 받는다(전용 서버 · 리슨 호스트)
        Client    ///< 서버에 붙는다
    };
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
        bool            _bLoopbackOnly{ true }; ///< 이 기계의 상대만 받는다 — 개발 실행의 기본. 다른 기계와 하려면 `gv_netLoopbackOnly=0`
    };
} // namespace sw

namespace sw
{
    /**
     * @class GameNetSession
     * @brief 게임 인스턴스 하나의 네트워크 호스트, 스레드, 메시지 라우터를 들고 있습니다. `GameInstanceBase` 가 소유합니다.
     * @code
     *     GameNetSessionParams params;
     *     if ( GameNetSession::makeParamsFromGlobalVariables( params ) )
     *         (void)session.initialize( params );
     *     session.getRouter().addHandler( &replicationServer ); // 키트
     *     session.update();                                     // 프레임마다(게임 스레드)
     *     session.shutdown();
     * @endcode
     */
    class SW_GF_API GameNetSession final : public GameInstanceSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "GameNetSession";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        /**
         * @brief 실행 인자(`-gv_netListenPort` · `-gv_netConnectAddress`)가 정한 역할로 세션을 엽니다. 인자가 없으면 열지 않습니다.
         * @details 포트가 사용 중이라 열지 못해도 게임은 혼자 뜬다 — 늘 성공으로 답하고 실패는 `initialize` 가 로그에 남긴다.
         */
        SubsystemInitResult initializeSubsystem() override;
        /** @brief 세션을 닫습니다. 게임 인스턴스가 `onShutdown`(키트 처리기가 내려간 뒤) 다음에 부른다. */
        void shutdownSubsystem() override { shutdown(); }

        /** @brief 주소에 포트가 없을 때의 기본 게임 포트입니다. 언리얼 기본 포트와 같으며 `ServerConfig::_gamePort` 기본값도 같습니다. */
        static constexpr uint16 kDefaultPort = 7777;

        GameNetSession();
        ~GameNetSession() override;

        GameNetSession( const GameNetSession& )            = delete;
        GameNetSession& operator=( const GameNetSession& ) = delete;

        /**
         * @brief 실행 인자(`-gv_netListenPort`, `-gv_netConnectAddress`)에서 세션 인자를 만듭니다. 둘 다 없으면 false 이며 세션을 열지 않습니다.
         * @details 둘 다 있으면 서버를 고르고 경고한다. 주소를 읽지 못하면 경고하고 false 를 반환한다.
         */
        [[nodiscard]] static bool makeParamsFromGlobalVariables( GameNetSessionParams& outParams );

        /** @brief 소켓을 열고 역할을 시작합니다(서버는 listen, 클라이언트는 connectAsync). 실패하면 로그를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool initialize( const GameNetSessionParams& params );
        /** @brief 모든 연결을 끊고 네트워크 스레드와 소켓을 닫습니다. 두 번 불러도 됩니다. */
        void shutdown();
        /** @brief 쌓인 연결 이벤트와 받은 메시지를 처리기에 나눠 주고, 연결과 끊김을 로그에 남깁니다. 게임 스레드에서 프레임마다 부릅니다. */
        void update();

        bool        isActive() const { return _role != GameNetRole::None; }
        GameNetRole getRole() const { return _role; }
        /** @brief 서버면 연 UDP 포트를 반환합니다. 포트로 0 을 줬으면 운영체제가 고른 포트입니다. 클라이언트면 0 입니다. */
        uint16 getListenPort() const { return _listenPort; }
        /** @brief 서버면 붙은 클라이언트 수를 반환하고, 클라이언트면 서버에 붙었을 때 1 을 반환합니다. */
        int32 getConnectedCount() const;
        /** @brief 처리기와 송수신이 쓰는 호스트입니다. 세션을 열지 않았으면 쓰면 안 됩니다(`isActive`). */
        NetHost& getHost() { return _host; }
        /** @brief 키트와 게임이 메시지 처리기를 다는 라우터입니다. `update` 가 pump 합니다. */
        NetMessageRouter& getRouter() { return _router; }
        /** @brief 세션 전송을 감싼 회선 흉내입니다. 세션을 열지 않았거나 Shipping 이면 nullptr 입니다. 스레드 안전합니다(네트워크 스레드가 같이 쓴다). */
        NetEmulationTransport* findEmulation() { return _emulation.get(); }

        /** @brief 이쪽에서 모든 연결을 끊습니다(회선 조절의 "끊기"). 클라이언트는 저절로 다시 붙지 않습니다 — `reconnect` 를 부릅니다. */
        void disconnectPeers();
        /** @brief 끊긴 클라이언트가 서버에 다시 붙기 시작합니다. 서버이거나 이미 붙어 있거나 붙는 중이면 false 입니다. */
        [[nodiscard]] bool reconnect();
        /** @brief 회선 흉내와 연결들의 수치를 한 보고로 모읍니다(`net.emulation stats` 의 답). 흉내가 없으면 흉내 칸은 0 입니다. */
        NetEmulationReport makeEmulationReport() const;
        /**
         * @brief 연결 통계와 라우터의 종류별 셈을 한 보고로 모읍니다(`net.stats` 의 답 — 에디터 Profiler 탭이 묻는다).
         * @details 이미 세고 있는 값을 읽기만 한다(연결마다 `getConnectionStats` 사본 하나). 세션을 열지 않았으면 빈 보고입니다.
         */
        NetStatsReport makeStatsReport() const;

    private:
        /** @brief 클라이언트 연결이 끝났으면 로그에 남깁니다. 시간 초과면 다시 시도하는데, 같이 띄운 서버가 아직 기동 중일 수 있기 때문입니다. */
        void reportConnectResult();
        /** @brief `_listEventScratch` 의 연결과 끊김을 로그 한 줄씩 남깁니다. 서버는 둘 다 남기고, 클라이언트는 끊김만 남깁니다. */
        void logEvents() const;

    private:
        NetHost                           _host;
        NetHostThread                     _thread;
        NetMessageRouter                  _router;
        unique_ptr<UDPNetTransport>       _transport;
        unique_ptr<NetEmulationTransport> _emulation;        ///< `_transport` 를 감싼 회선 흉내(Dev). 호스트는 이것을 전송으로 쓴다
        TaskFuture<NetConnectResult>      _connectFuture;    ///< 클라이언트 — 기다리는 연결(결과를 알리면 비운다)
        vector<NetHostEvent>              _listEventScratch; ///< `update` 가 다시 쓴다
        NetAddress                        _serverAddress;
        uint16                            _listenPort;
        GameNetRole                       _role;
        uint8                             _bConnectFailureReported; ///< 클라이언트 — 연결 실패를 한 번 알렸다(다시 시도하는 시간 초과는 처음만 알린다)
    };
} // namespace sw
