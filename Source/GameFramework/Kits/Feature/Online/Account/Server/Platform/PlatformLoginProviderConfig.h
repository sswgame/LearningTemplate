/**
 * @file PlatformLoginProviderConfig.h
 * @brief 외부 로그인 제공자의 설정입니다. 제공자는 코드가 아니라 **데이터**로 정의합니다.
 * @details 설정은 이름, 방식(OIDC 또는 액세스 토큰으로 프로필 조회), 발급자와 JWKS 주소 또는 프로필 주소, client id(aud), 주체 필드 경로로 이루어진다.
 *          팩토리가 설정에서 공통 구현(`OidcLoginProvider`, `ProfileAPILoginProvider`)을 만들고, 서버 설정 JSON 의 목록을 읽는다.
 *          구글, 애플, 카카오는 OIDC ID 토큰을 쓰므로 Oidc 이고, 네이버는 OIDC 가 없어서 액세스 토큰으로 프로필 API 를 부르므로 AccessTokenProfile 이다.
 *          코드가 필요한 예외(애플의 client secret JWT 서명, 탈퇴 때 토큰 철회)만 `Provider/<제품>/` 폴더에 둔다.
 *          실제 제공자 등록(앱 등록, client id)은 쓰는 게임이 생기면 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class HTTPClient;
    class INetSecurityProvider;
    class IPlatformLoginProvider;

    /** @brief 제공자 방식입니다. */
    enum class PlatformLoginProviderKind : uint8
    {
        Oidc = 0,           ///< 티켓이 OIDC ID 토큰(JWT, 선택적으로 `|<nonce>`)이며 JWKS 로 서명을 확인한다
        AccessTokenProfile, ///< 티켓이 액세스 토큰이다. 프로필 API 를 `Authorization: Bearer` 로 호출해 주체 id 를 얻는다
        /**
         * @brief 티켓(16 진 글)과 서버 비밀 키를 확인 API 의 쿼리 인자로 보내 주체 id 를 얻는다(`TicketQueryLoginProvider`).
         * @details 세션 티켓을 웹 API 로 확인하는 제공자(예: 게임 플랫폼의 사용자 인증 티켓)를 데이터로 정의한다 — 주소 · 인자 이름 · 결과 경로가 설정이다.
         */
        TicketQuery,
        /**
         * @brief OIDC ID 토큰(`Oidc` 와 같다)에 인가 코드를 교환해 받은 refresh token 을 연동 비밀로 보관하고 탈퇴 때 철회한다(`AppleLoginProvider`, OnlineKits A4).
         * @details 티켓은 `ID 토큰|nonce|인가 코드`(nonce 가 없으면 `ID 토큰||인가 코드`)다. client secret 은 서명 키(`keySecretEnvironment` 의 PEM)로 서버가 JWT(ES256)로 만든다.
         */
        AppleSignIn
    };
} // namespace sw

namespace sw
{
    /** @brief 제공자 하나의 설정입니다. 시간은 밀리초입니다. */
    struct PlatformLoginProviderConfig
    {
        vector<string>            _listClientID{};         ///< Oidc 에서 `aud` 로 받아들이는 client id 이다. 플랫폼마다 다를 수 있다
        string                    _name{};                 ///< `[a-z0-9_]` 형식이며 16자 이하이다. 저장 키에 들어간다
        string                    _issuer{};               ///< Oidc 에서 `iss` 와 같아야 한다
        string                    _jwksURL{};              ///< Oidc
        string                    _profileURL{};           ///< AccessTokenProfile
        string                    _subjectPath{ "sub" };   ///< 프로필 응답 JSON 이나 Oidc 토큰 본문에서 주체 id 를 찾는 점 경로이다(예: "response.id")
        string                    _displayNamePath{};      ///< 표시 이름 힌트의 점 경로이다. 비우면 힌트가 없다
        string                    _ticketParam{};          ///< TicketQuery — 티켓을 싣는 쿼리 인자 이름
        string                    _keyParam{};             ///< TicketQuery — 서버 비밀 키를 싣는 쿼리 인자 이름
        string                    _keySecretEnvironment{}; ///< TicketQuery — 서버 비밀 키를 담은 환경 변수 이름(키는 설정 파일에 쓰지 않는다)
        string                    _resultPath{};           ///< TicketQuery — 확인 결과 글의 점 경로(예: "response.params.result")
        string                    _resultValue{};          ///< TicketQuery — 성공일 때 그 경로의 글(예: "OK")
        string                    _teamID{};               ///< AppleSignIn — 개발자 팀 id(client secret 의 `iss`)
        string                    _signingKeyID{};         ///< AppleSignIn — 서명 키 id(client secret 헤더의 `kid`). 서명 키 PEM 은 `_keySecretEnvironment` 의 환경 변수에서 읽는다
        string                    _tokenURL{};             ///< AppleSignIn — 인가 코드 교환 주소. 비우면 제공자의 공식 주소이고 가짜 서버(테스트)를 가리킬 때만 쓴다
        string                    _revokeURL{};            ///< AppleSignIn — 토큰 철회 주소. 비우면 제공자의 공식 주소
        int64                     _clockSkewMs{ 60000 };   ///< exp 와 iat 의 허용 오차이다
        int64                     _jwksRefreshMs{ 3600000 };
        int64                     _minRefetchMs{ 30000 }; ///< 모르는 kid 때문에 JWKS 를 다시 받는 최소 간격이다. 키 회전에 대응하면서 요청이 몰리는 것을 막는다
        int64                     _requestTimeoutMs{ 10000 };
        PlatformLoginProviderKind _kind{ PlatformLoginProviderKind::Oidc };
        uint8                     _bRequireNonce{ SW_FALSE }; ///< Oidc 에서 티켓에 `|<nonce>` 가 있고 토큰의 `nonce` 와 같아야 한다
    };
} // namespace sw

namespace sw
{
    /** @brief 설정에서 제공자를 만들고 설정 목록을 읽는 팩토리입니다. */
    struct SW_KIT_SERVER_API PlatformLoginProviderFactory
    {
        /** @brief 설정으로 공통 구현을 만듭니다. @p pHTTPClient 는 빌려 쓰며 이 제공자만 응답을 회수합니다. 응답을 다른 쪽과 나누지 않습니다. 설정이 틀리면 nullptr 을 반환합니다. */
        static unique_ptr<IPlatformLoginProvider> create( const PlatformLoginProviderConfig& settings, INetSecurityProvider* pProvider, HTTPClient* pHTTPClient );
        /**
         * @brief `{"providers":[{"name","kind":"oidc|profile|ticketQuery|appleSignIn","issuer","jwksUrl","clientIDs":[…],"requireNonce","profileUrl","subjectPath","displayNamePath",
         *        "ticketParam","keyParam","keySecretEnvironment","resultPath","resultValue","teamId","signingKeyId","tokenUrl","revokeUrl"}]}` 를 읽습니다.
         * @details 모르는 키, 빠진 필수 키, 규칙에 맞지 않는 이름은 오류(@p outError)로 처리한다. 철자가 틀린 설정이 조용히 기본값이 되지 않게 하려는 것이다.
         */
        [[nodiscard]] static bool readSettings( string_view jsonText, vector<PlatformLoginProviderConfig>& outListSettings, string& outError );
        /** @brief 설정을 쓸 수 있으면 true 입니다. 이름 규칙과 방식별 필수 항목을 검사합니다. */
        static bool isValidSettings( const PlatformLoginProviderConfig& settings );
    };
} // namespace sw
