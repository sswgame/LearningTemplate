/**
 * @file ServerConfig.h
 * @brief 전용 서버의 운영 설정(`Config/Server/<SW_ACTIVE_GAME>.json`, `-server-config=<경로>` 로 바꿈)입니다.
 * @details 실행 파일에 굽지 않습니다 — Shipping 서버도 디스크에서 읽고, 없으면 기동하지 않습니다(Dev 는 기본값 + 경고). 운영자가 포트 ·
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
        string _name{};
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
        string _name{};
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
        /** @brief 게임(UDP) 포트입니다. */
        PROPERTY( Min = 1, Max = 65535 )
        int32 _gamePort{ 7777 };
        /**
         * @brief 게임 UDP 샤드 수입니다 — 서버는 `_gamePort` 부터 `_gamePort + _gamePortShardCount - 1` 까지 포트마다 소켓 하나(샤드마다 NetHost)를 엽니다.
         * @details 두 플랫폼 공통 받기 분산 계약(포트 샤딩): 로그인 토큰이 클라이언트에게 샤드 포트를 알려 줍니다. 리눅스에서 `_bGamePortReuse` 를 켜면
         *          같은 포트 하나에 `SO_REUSEPORT` 소켓 N 개(커널이 4-튜플로 나눔)로 바뀝니다. 1 이면 소켓 하나.
         */
        PROPERTY( Min = 1, Max = 64 )
        int32 _gamePortShardCount{ 1 };
        /** @brief 리눅스 전용 — 샤드를 포트 여러 개 대신 `SO_REUSEPORT` 한 포트로 엽니다. Windows 는 무시하고 포트 샤딩으로 엽니다. */
        PROPERTY()
        bool _bGamePortReuse{ false };
        /** @brief 서비스(TCP — 로그인 · 채팅 · 거래) 포트입니다. 0 이면 서비스 서버를 열지 않습니다. */
        PROPERTY( Min = 0, Max = 65535 )
        int32 _servicePort{ 7780 };
        /**
         * @brief 운영 HTTP 끝점(`/metrics` · `/healthz` · `/readyz`, 평문 · 인증 없음) 포트입니다. 0 이면 열지 않습니다.
         * @details 기본은 끔 — 한 기계에 서버 여럿(시험 · 개발)이 뜨면 고정 포트가 부딪친다. 운영 설정 파일이 켠다(Prometheus 관례 9100 대).
         */
        PROPERTY( Min = 0, Max = 65535 )
        int32 _opsPort{ 0 };
        /** @brief 운영 끝점이 받을 주소입니다 — 기본 `127.0.0.1`(이 기계만). 스크레이퍼가 다른 기계면 사설 주소를 준다(공용 인터페이스에 열지 말 것). */
        PROPERTY()
        string _opsListenAddress{ "127.0.0.1" };
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
