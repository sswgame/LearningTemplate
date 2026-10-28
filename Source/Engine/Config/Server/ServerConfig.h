/**
 * @file ServerConfig.h
 * @brief 전용 서버의 운영 설정(`Config/Server/<SW_ACTIVE_GAME>.json`, `-server-config=<경로>` 로 바꿈)입니다.
 * @details 실행 파일에 베이크하지 않습니다 — Shipping 서버도 디스크에서 읽고, 없으면 기동하지 않습니다(Dev 는 기본값 + 경고). 운영자가 포트 ·
 *          접속 문자열을 고치는 파일이라서입니다. **비밀번호 · 토큰은 이 파일에 쓰지 않습니다** — 항목의 `_secretEnvironment` 가 가리키는 환경 변수에서
 *          드라이버가 읽습니다(`ServerSecret::read`). 저장소 · 캐시 항목의 드라이버 이름(`postgres` · `sqlite` · `memory` · `resp`)을 해석하는 것은
 *          서버 전용 모듈입니다 — 엔진은 항목을 운반만 합니다.
 *          클라이언트 빌드에도 이 타입은 있습니다(리플렉션 등록 한 줄) — 서버 로직이 아니라 설정의 모양이라서입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 영속 저장소(SQL) 하나입니다 — 키트가 `_name` 으로 찾습니다(`accounts` · `trades`). */
    REFLECT()
    struct SW_API ServerStoreEntry
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{}; ///< 키트가 찾는 저장소 이름(`accounts` · `trades`)
        /** @brief 드라이버 이름(`postgres` · `sqlite` · `memory`)입니다. 해석은 서버 전용 모듈이 합니다. */
        PROPERTY()
        string _driver{ "memory" };
        /** @brief 비밀이 빠진 접속 문자열(`host=127.0.0.1 port=5432 dbname=game user=game` · SQLite 파일 경로)입니다. */
        PROPERTY()
        string _connection{};
        /** @brief 비밀번호를 담은 환경 변수 이름(`SW_DB_ACCOUNTS_PASSWORD`)입니다. 비면 비밀 없음. */
        PROPERTY()
        string _secretEnvironment{};
    };
} // namespace sw

namespace sw
{
    /** @brief 메모리 캐시(RESP — Valkey · Garnet, 또는 프로세스 안) 하나입니다. */
    REFLECT()
    struct SW_API ServerCacheEntry
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{}; ///< 키트가 찾는 캐시 이름(`presence` · `sessions`)
        /** @brief 드라이버 이름(`resp` · `memory`)입니다. */
        PROPERTY()
        string _driver{ "memory" };
        /** @brief `host:port`(RESP)입니다. `memory` 는 비웁니다. */
        PROPERTY()
        string _endpoint{};
        /** @brief AUTH 비밀번호를 담은 환경 변수 이름입니다. 비면 AUTH 없음. */
        PROPERTY()
        string _secretEnvironment{};
    };
} // namespace sw

namespace sw
{
    /** @brief 전용 서버의 운영 설정 전체입니다(파일 하나). */
    REFLECT()
    struct SW_API ServerConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief 받는 주소(`0.0.0.0` 모두 · `127.0.0.1` 이 기계만)입니다. */
        PROPERTY()
        string _listenAddress{ "0.0.0.0" };
        /**
         * @brief 게임(UDP) 포트입니다. 전용 서버의 게임 인스턴스가 늘 이 포트를 엽니다(`GameNetSession::makeServerParams`, 받는 주소는 `_listenAddress`).
         * @details 0 이면 운영체제가 고른 포트입니다 — 한 기계에서 서버를 여럿 띄우는 테스트용이고, 연 포트는 로그 `Net session listening on UDP port N` 에 남는다.
         *          개발 빌드에서는 `-gv_netListenPort` 가 이 값보다 우선한다.
         */
        PROPERTY( Min = 0, Max = 65535 )
        int32 _gamePort{ 7777 };
        /**
         * @brief 게임 UDP 샤드 수입니다 — 서버는 `_gamePort` 부터 `_gamePort + _gamePortShardCount - 1` 까지 포트마다 소켓 하나(샤드마다 NetHost)를 엽니다.
         * @details 두 플랫폼 공통 받기 분산 계약(포트 샤딩): 서버 배정(`ServerDescriptor::pickPort` — 계정마다 정해진 샤드)이 클라이언트에게 샤드 포트를 알려 줍니다.
         *          리눅스에서 `_bGamePortReuse` 를 켜면 같은 포트 하나에 `SO_REUSEPORT` 소켓 N 개(커널이 4-튜플로 나눔)로 바뀝니다. 1 이면 소켓 하나.
         *          2 이상이면 `_gamePort` 가 0 이 아니어야 합니다(연이은 포트가 배정의 계약이다). 키트는 샤드를 모른다(`GameNetSession::getPeers`).
         */
        PROPERTY( Min = 1, Max = 64 )
        int32 _gamePortShardCount{ 1 };
        /** @brief 리눅스 전용 — 샤드(2 개 이상)를 포트 여러 개 대신 `SO_REUSEPORT` 한 포트로 엽니다. 다른 플랫폼에서 켜면 게임 포트를 열지 않고 오류입니다. */
        PROPERTY()
        bool _bGamePortReuse{ false };
        /**
         * @brief 게임 포트(UDP)가 계정 키트의 접속 티켓을 요구합니다 — 포트를 암호화하고 연결을 티켓의 세션 비밀에 묶습니다(`GameConnectSecurity`, OnlineKits F5).
         * @details 켜면 온라인 설정에 티켓을 확인하는 조립(계정 키트의 `gameConnect`)이 있어야 합니다. 서버 서비스가 선 뒤 인증기가 묶이지 않았으면 서버는 기동을 멈춥니다.
         *          매칭으로 배정받는 서버는 매칭 키트의 `gameServer` 옵션을 켜 배정받은 계정만 받습니다. Shipping 의 암호화 게임 포트는 이것이 켜져야 엽니다.
         */
        PROPERTY()
        bool _bGameConnectTicket{ false };
        /** @brief 서비스(TCP — 로그인 · 채팅 · 거래) 포트입니다. 0 이면 서비스 서버를 열지 않습니다. */
        PROPERTY( Min = 0, Max = 65535 )
        int32 _servicePort{ 7780 };
        /**
         * @brief GM 도구 전용 서비스(TCP) 포트입니다. 0 이면 GM 리슨이 없고, GM 키트를 켠 서버는 기동하지 못합니다(서비스 포트로 받는 폴백 없음).
         * @details GM 서비스(GM 키트)는 이 포트의 연결만, 플레이어 서비스는 `_servicePort` 의 연결만 받습니다. 계정 서비스는 둘 다 받습니다(GM 도 로그인한다).
         *          TLS 는 서비스 포트와 같은 인증서입니다. 받는 주소는 `_adminListenAddress` 입니다(OnlineKits I6).
         */
        PROPERTY( Min = 0, Max = 65535 )
        int32 _adminServicePort{ 0 };
        /** @brief GM 포트가 받을 주소입니다. 기본 `127.0.0.1` 은 이 기계만 받습니다. GM 도구가 다른 기계면 사설 주소를 준다(공용 인터페이스에 열지 말 것). */
        PROPERTY()
        string _adminListenAddress{ "127.0.0.1" };
        /**
         * @brief 운영 HTTP 엔드포인트(`/metrics`, `/healthz`, `/readyz`. 평문이고 인증 없음) 포트입니다. 0 이면 열지 않습니다.
         * @details 기본은 끔 — 한 기계에 서버 여럿(테스트 · 개발)이 뜨면 고정 포트가 부딪친다. 운영 설정 파일이 켠다(Prometheus 관례 9100 대).
         */
        PROPERTY( Min = 0, Max = 65535 )
        int32 _opsPort{ 0 };
        /** @brief 운영 엔드포인트가 받을 주소입니다. 기본 `127.0.0.1` 은 이 기계만 받습니다. 스크레이퍼가 다른 기계면 사설 주소를 준다(공용 인터페이스에 열지 말 것). */
        PROPERTY()
        string _opsListenAddress{ "127.0.0.1" };
        /**
         * @brief 관리 채널(서버 운영 O4 — 점검 모드 · 상태 · 정상 종료를 재시작 없이 부르는 텍스트 TCP 채널) 포트입니다. 0 이면 끔(기본 — Shipping 도 같다).
         * @details 켜려면 `_adminChannelTokenEnvironment` 의 환경 변수에 비밀 토큰이 있어야 한다 — 없거나 비면 서버가 기동하지 못한다(토큰 없는 관리 채널로 뜨지 않는다).
         *          루프백에만 묶고(쿠버네티스 `port-forward` · 사이드카 · SSH 터널로 닿는다), 요청 줄마다 토큰을 요구하며 틀리면 끊는다. Dev 의 디버그 채널과 같은 채널 코드(`DebugChannel`)이지만
         *          개발 명령 전체가 아니라 허용 목록(`help`)만 받는다. 언리얼 `Rcon` · 유니티 Multiplay 관리 API 처럼 비밀은 파일이 아니라 환경에서만 읽는다.
         */
        PROPERTY( Min = 0, Max = 65535 )
        int32 _adminChannelPort{ 0 };
        /** @brief 관리 채널 토큰을 담은 환경 변수 이름입니다(토큰 자체는 설정 파일에 쓰지 않는다). */
        PROPERTY()
        string _adminChannelTokenEnvironment{ "SW_ADMIN_CHANNEL_TOKEN" };
        /** @brief 서버 틱 수(초당)입니다. 한 틱이 게임 고정 스텝 하나입니다. */
        PROPERTY( Min = 1, Max = 240 )
        int32 _tickRateHz{ 30 };
        /** @brief 밀렸을 때 잠자지 않고 몰아 도는 틱 상한입니다. 넘으면 밀린 시간을 버리고 경고합니다. */
        PROPERTY( Min = 1, Max = 60 )
        int32 _maxCatchUpTicks{ 5 };
        /** @brief 표준 입력 명령(quit · status …)을 받을지입니다. 서비스 · 컨테이너는 꺼도 된다(EOF 로는 종료하지 않는다). */
        PROPERTY()
        bool _bConsoleInput{ true };
        /** @brief 종료 요청 뒤 연결 정리 · 저장에 주는 시간(초)입니다(키트가 읽는다). */
        PROPERTY( Min = 0, Max = 120 )
        int32 _shutdownGraceSeconds{ 10 };
        /**
         * @brief 종료 신호(SIGTERM 등)를 받은 뒤 접속자가 모두 나가기를 기다리는 시간(초)입니다(서버 운영 O3 — 드레인).
         * @details 신호가 오면 먼저 `/readyz` 가 503 이 되고(새 접속을 부하 분산기가 끊는다), 서버는 접속(서비스 연결 수)이 0 이 되거나 이 시간이 지날 때까지 틱을 돌린 뒤
         *          서비스를 멈춘다. 0 이면 기다리지 않고 바로 내린다. 콘솔 `quit` · 틱 상한 종료는 기다리지 않는다(사람이 내린 것이다 — 테스트가 느려지지 않게).
         *          쿠버네티스는 `terminationGracePeriodSeconds` 를 이 값 + `_shutdownGraceSeconds` 보다 크게 잡는다(`deploy/kubernetes/`).
         */
        PROPERTY( Min = 0, Max = 3600 )
        int32 _drainWaitSeconds{ 0 };
        /** @brief 서버 로그를 줄마다 JSON 한 줄(JSON lines)로 표준 출력에 씁니다(서버 운영 O2 — 컨테이너 로그 수집기용). 켜면 콘솔 장치는 꺼지고 파일 로그는 그대로입니다. */
        PROPERTY()
        bool _bJSONLogs{ false };
        /**
         * @brief 틱 본문이 이 시간(밀리초)을 넘으면 프레임 이력(`ServerFrameHistory`)을 `Saved/ServerReports/` 에 덤프합니다(서버 운영 O1). 0 이면 끔.
         * @details 덤프는 틱 사이에 최소 10 초 간격을 두며 최근 `_frameHistoryTicks` 틱의 시간 · 접속 수 · 메시지 바이트가 든다. Dev 의 `-gv_traceSlowTickMs`(프로파일러 캡처)와는 따로다.
         */
        PROPERTY( Min = 0, Max = 60000 )
        int32 _slowTickDumpMs{ 0 };
        /** @brief 프레임 이력 링 버퍼가 기억하는 최근 틱 수입니다(서버 운영 O1). 0 이면 이력을 남기지 않습니다. */
        PROPERTY( Min = 0, Max = 36000 )
        int32 _frameHistoryTicks{ 600 };
        /**
         * @brief 크래시 보고 묶음(`Saved/CrashReports`)을 보낼지입니다(서버 운영 O1): `local` 기계 안에만(기본), `send` 묶자마자 보낼 줄에 세웁니다.
         * @details 서버에는 "보낼까요?" 를 물을 사람이 없어 `ask` 는 받지 않는다(기동 오류). 모르는 값도 오류다 — 보내지 않는 쪽으로 조용히 바꾸지 않는다.
         *          언리얼 서버의 `-NoCrashReport` · 유니티 Cloud Diagnostics 의 서버 켜기 칸과 같은 자리이며 기본은 끔이다(운영자가 동의한 서버만 보낸다).
         */
        PROPERTY()
        string _crashReportConsent{ "local" };
        /**
         * @brief 틱 사용률(틱 본문 시간 ÷ 틱 주기)이 이 값(퍼센트)을 넘으면 경고합니다(서버 운영 O5). 0 이면 끔.
         * @details 초마다 구간 평균 사용률을 지표 `server_tick_utilization_ratio` 에 적고, 넘으면 로그 경고를 남긴다(경고는 1 분에 한 번). 예 70.
         */
        PROPERTY( Min = 0, Max = 100 )
        int32 _tickLoadWarnPercent{ 0 };
        /**
         * @brief 서비스 TLS 1.3 인증서(PEM, 체인 포함) 경로입니다 — 네트워크 보안 제공자의 서버 TLS 문맥에 그대로 넘깁니다.
         * @details 둘 다 비면 **Dev 에서만** `Saved/Certificates/` 의 개발용 자체 서명 인증서, Shipping 은 서비스 포트를 열 때 오류입니다.
         *          상대 경로는 이 설정 파일이 아니라 프로젝트 루트 기준(ConfigManager 와 같다)입니다.
         */
        PROPERTY()
        string _tlsCertificateFile{};
        /**
         * @brief TLS 개인키(PEM) 경로입니다. 키는 파일 권한(리눅스 0600 · Windows ACL)으로 지키고, 이 설정 파일에는 경로만 둡니다 — 키 암호가 있으면
         *        `_tlsPrivateKeySecretEnvironment` 의 환경 변수에서 읽습니다(비밀은 파일에 쓰지 않는다).
         */
        PROPERTY()
        string _tlsPrivateKeyFile{};
        /** @brief 개인키 암호를 담은 환경 변수 이름입니다. 비면 암호 없는 키. */
        PROPERTY()
        string _tlsPrivateKeySecretEnvironment{};
        /** @brief 영속 저장소 항목입니다. */
        PROPERTY()
        vector<ServerStoreEntry> _listStore{};
        /** @brief 메모리 캐시 항목입니다. */
        PROPERTY()
        vector<ServerCacheEntry> _listCache{};
    };
} // namespace sw
