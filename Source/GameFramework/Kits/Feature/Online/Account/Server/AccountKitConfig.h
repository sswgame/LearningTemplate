/**
 * @file AccountKitConfig.h
 * @brief 계정 키트의 서버 설정입니다(`_listKit[]._settings`). 게임 서버 조립(`gameConnect`)의 설정도 여기 있습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

namespace sw
{
    /** @brief 서버가 서자마자 가입시켜 둘 개발용 계정 하나입니다. 이미 있으면 그대로 둡니다. */
    REFLECT()
    struct AccountSeedEntry
    {
        REFLECT_BODY();
        PROPERTY( Required )
        string _loginName{};
        PROPERTY( Required )
        string _password{};
    };

    /** @brief 계정 키트의 서버 설정입니다. 파일 경로는 프로젝트 루트 기준이고, 기동 때 절대 경로로 풀며 못 풀면 기동 실패입니다. */
    REFLECT()
    struct AccountKitConfig
    {
        REFLECT_BODY();
        /** @brief 만료된 세션(밀려난 세션의 묘비 포함)의 TTL 입니다. 만료 뒤 이 기간이 지나면 TTL 쓸기가 세션 레코드를 지웁니다. */
        PROPERTY( Required )
        ServiceTTLConfig _expiredSessionTTL{};
        /** @brief 게임 접속 티켓 주 키(32 B, 16 진수 64 자)를 담은 환경 변수 이름입니다. 비우면 개발 키(DevOnline 단위), 그 단위가 없는 구성(배포)은 기동 실패입니다. */
        PROPERTY()
        string _ticketKeySecretEnvironment{};
        /** @brief 주소마다 한꺼번에 받는 로그인 시도 수입니다(부하 봇은 크게). */
        PROPERTY( Min = 1 )
        int32 _attemptBurst{ 10 };
        /** @brief 가벼운 Argon2id 를 씁니다(테스트 · 부하 봇 — DevOnline 단위만, 실제 비용을 재려면 끕니다). */
        PROPERTY()
        bool _bLightPasswordHash{ false };
        /**
         * @brief 개발 · 두 프로세스 시나리오(`Scripts/net/NetPairRun.py`)용 시드 계정입니다. **DevOnline 단위만** — 그 단위가 없는 구성(배포)은 기동 실패입니다.
         * @details 비밀번호를 설정 파일에 적어 두는 길을 운영에 남기지 않습니다. 상용 엔진의 테스트 계정 시드(언리얼 서버의 `-ini` 계정 · 유니티 UGS 의 테스트 플레이어)와 같은 자리입니다(기억에 의존).
         */
        PROPERTY()
        vector<AccountSeedEntry> _listSeedTestAccount{};
        /** @brief 외부 로그인 제공자 설정 JSON 입니다(항목 형식은 `PlatformLoginProviderConfig::readEntry`). 비우면 외부 로그인이 없습니다. */
        PROPERTY()
        string _platformProvidersFile{};
        /** @brief 제공자 HTTPS 를 검증할 CA 번들 PEM 입니다. `https://` 제공자가 있으면 필수입니다. */
        PROPERTY()
        string _platformTrustFile{};
        /** @brief 연동 비밀(애플의 refresh token)을 저장소에 쓰기 전에 암호화할 키(32 B, 16 진수 64 자)를 담은 환경 변수 이름입니다. 탈퇴 때 연동 토큰을 철회하는 제공자를 쓰면 필수입니다. */
        PROPERTY()
        string _linkSecretKeySecretEnvironment{};
    };

    /** @brief 게임 서버 조립(`gameConnect`)의 서버 설정입니다. */
    REFLECT()
    struct AccountGameConnectKitConfig
    {
        REFLECT_BODY();
        /** @brief 로그인 서버와 같은 티켓 주 키의 환경 변수입니다(`AccountKitConfig` 와 같은 규칙). */
        PROPERTY()
        string _ticketKeySecretEnvironment{};
        /** @brief 티켓이 가리켜야 하는 이 서버의 이름입니다. 클라이언트가 `AccountClient` 의 티켓 요청에 주는 이름입니다. */
        PROPERTY( Required )
        string _serverName{};
    };
} // namespace sw
