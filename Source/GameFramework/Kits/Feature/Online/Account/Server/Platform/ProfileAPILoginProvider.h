/**
 * @file ProfileAPILoginProvider.h
 * @brief 확인 API 에 HTTP 로 물어 응답 JSON 의 경로에서 주체 id 를 읽는 제공자입니다. OIDC 가 없는 곳을 제품 코드 없이 데이터(주소 · 인자 이름 · 응답 경로)로 정의합니다.
 * @details 묻는 방식은 설정의 `_kind` 가 고른다. 응답을 읽는 규칙(401 · 403 은 거절, 연결 실패 · 시한 · 5xx · 깨진 JSON 은 쓸 수 없음, 주체 경로의 값이 주체)은 같다.
 *          - `AccessTokenProfile`(네이버 등): 티켓 문자열은 액세스 토큰이다. `GET <프로필 주소>` 에 `Authorization: Bearer <토큰>` 을 실어 보낸다. 200 이면 경로의 값(문자열이나 숫자)이 주체다.
 *          - `TicketQuery`(OnlineKits A5 — 게임 플랫폼의 사용자 인증 티켓 확인 웹 API): 티켓(16 진 글)과 서버 비밀 키를 쿼리 인자로 보내고, 결과 경로의 글이 성공 값이면 주체 경로의 값이 주체다. 예:
 *            `{"name":"steam","kind":"ticketQuery","profileUrl":"https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/?appid=<앱>",
 *              "ticketParam":"ticket","keyParam":"key","keySecretEnvironment":"<변수>","identityParam":"identity","identity":"<서비스 이름>",
 *              "resultPath":"response.params.result","resultValue":"OK","subjectPath":"response.params.steamid"}`
 *            · 키는 설정 파일에 쓰지 않고 환경 변수(서버 비밀)에서 읽는다. 읽지 못하면 제공자를 만들지 않는다(`create` 가 nullptr — 서버 기동 실패).
 *            · 받을 서비스 이름(`identity`)은 필수다. 스팀은 그 이름으로 만든 티켓(`GetAuthTicketForWebApi( pchIdentity )`)만 확인하므로, 클라이언트는 같은 글을
 *              `IPlatformServices::requestAuthTicket` 에 넘긴다(partner.steamgames.com ISteamUserAuth · ISteamUser 문서, 2026-11-07 확인).
 *            · 티켓은 16 진 글이어야 한다(쿼리에 그대로 싣는다). 아니면 확인하지 않고 거절한다.
 *            · 200 인데 결과 글이 성공 값과 다르거나 주체가 없으면 거절이다.
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"

namespace sw
{
    class HTTPClient;
    struct HTTPClientResponse;

    /**
     * @class ProfileAPILoginProvider
     * @brief 프로필 · 티켓 확인 API 제공자입니다.
     */
    class SW_KIT_SERVER_API ProfileAPILoginProvider final : public IPlatformLoginProvider
    {
    public:
        /** @brief 설정의 방식대로 만듭니다. `TicketQuery` 는 비밀 키를 환경 변수에서 읽으며, 읽지 못하거나 키가 쿼리에 실을 수 없는 글이면 오류를 남기고 nullptr 입니다. */
        static unique_ptr<IPlatformLoginProvider> create( const PlatformLoginProviderConfig& settings, HTTPClient* pHTTPClient );
        /** @brief @p pHTTPClient 는 빌려 쓰며 그 응답은 이 객체만 회수합니다. @p key 는 `TicketQuery` 의 서버 비밀 키입니다(키를 이미 가진 테스트가 넘긴다) — `AccessTokenProfile` 은 비웁니다. */
        ProfileAPILoginProvider( const PlatformLoginProviderConfig& settings, HTTPClient* pHTTPClient, string key = {} );
        ~ProfileAPILoginProvider() override;

        const utf8* getName() const override { return _settings._name.c_str(); }
        uint64      submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds nowMs ) override;
        int32       pollVerifications( vector<PlatformLoginVerification>& outListVerification ) override;
        void        update( UnixMilliseconds nowMs ) override;

        /** @brief 쿼리에 그대로 실을 수 있는 글(영숫자 · `-` · `_` · `.`)이면 true 입니다. `TicketQuery` 의 키 · 티켓 · 서비스 이름을 검사합니다. */
        static bool isQuerySafe( string_view text );
        /** @brief `TicketQuery` 의 확인 요청 주소입니다(설정 주소에 키 · 티켓 · 서비스 이름 인자를 덧붙인다). 테스트가 봅니다. */
        string makeRequestURL( string_view ticket ) const;

    private:
        /** @brief 응답 하나를 확인 결과로 바꿉니다. `TicketQuery` 는 결과 경로의 글이 성공 값이어야 주체를 읽습니다. */
        void readVerification( const HTTPClientResponse& response, PlatformLoginVerification& outVerification ) const;

    private:
        PlatformLoginProviderConfig            _settings;
        string                                 _key; ///< `TicketQuery` 의 서버 비밀 키. 소멸자가 지운다
        unordered_map<uint64, uint64>          _mapRequestToVerification;
        EventBuffer<PlatformLoginVerification> _verificationBuffer;
        HTTPClient*                            _pHTTPClient;
        IDAllocator<uint64>                    _verificationIDAllocator;
    };
} // namespace sw
