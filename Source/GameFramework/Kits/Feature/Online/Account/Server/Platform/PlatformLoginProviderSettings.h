/**
 * @file PlatformLoginProviderSettings.h
 * @brief 외부 로그인 제공자는 **데이터**다 — 이름 · 방식(OIDC | 액세스 토큰 프로필 조회) · 발급자/JWKS 주소 또는 프로필 주소 · client id(aud) · 주체 필드 경로.
 *        공장이 설정에서 공통 구현(`OidcLoginProvider` · `ProfileApiLoginProvider`)을 만들고, 서버 설정 JSON 의 목록을 읽습니다.
 * @details 구글 · 애플 · 카카오(OIDC ID 토큰)는 Oidc, 네이버(OIDC 없음 — 액세스 토큰으로 프로필 API)는 AccessTokenProfile. 코드가 필요한 예외(애플 client secret
 *          JWT 서명 · 탈퇴 때 토큰 철회)만 `Provider/<제품>/` 폴더에 둔다. 실제 제공자 등록(앱 등록 · client id)은 쓰는 게임이 생기면(백로그).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class HttpClient;
    class INetSecurityProvider;
    class IPlatformLoginProvider;

    /** @brief 제공자 방식입니다. */
    enum class PlatformLoginProviderKind : uint8
    {
        Oidc = 0,          ///< 표 = OIDC ID 토큰(JWT, 선택으로 `|<nonce>`) — JWKS 로 서명 확인
        AccessTokenProfile ///< 표 = 액세스 토큰 — 프로필 API 를 `Authorization: Bearer` 로 불러 주체 id 를 얻는다
    };
} // namespace sw

namespace sw
{
    /** @brief 제공자 하나의 설정입니다. 시간은 밀리초입니다. */
    struct PlatformLoginProviderSettings
    {
        vector<string>            _listClientID{};       ///< Oidc — `aud` 로 받아들이는 client id(플랫폼마다 다를 수 있다)
        string                    _name{};               ///< `[a-z0-9_]` 16 자 이하 — 저장 키에 든다
        string                    _issuer{};             ///< Oidc — `iss` 와 같아야 한다
        string                    _jwksURL{};            ///< Oidc
        string                    _profileURL{};         ///< AccessTokenProfile
        string                    _subjectPath{ "sub" }; ///< 응답 JSON(프로필) · 토큰 몸(Oidc)에서 주체 id 의 점 경로("response.id")
        string                    _displayNamePath{};    ///< 표시 이름 힌트의 점 경로(비우면 없음)
        int64                     _clockSkewMs{ 60000 }; ///< exp · iat 허용 오차
        int64                     _jwksRefreshMs{ 3600000 };
        int64                     _minRefetchMs{ 30000 }; ///< 모르는 kid 로 JWKS 를 다시 받는 최소 간격(키 회전 — 도배 막기)
        int64                     _requestTimeoutMs{ 10000 };
        PlatformLoginProviderKind _kind{ PlatformLoginProviderKind::Oidc };
        uint8                     _bRequireNonce{ SW_FALSE }; ///< Oidc — 표에 `|<nonce>` 가 있고 토큰의 `nonce` 와 같아야 한다
    };
} // namespace sw

namespace sw
{
    /** @brief 설정 → 제공자 공장 · 설정 목록 읽기입니다. */
    struct SW_GF_API PlatformLoginProviderFactory
    {
        /** @brief 설정으로 공통 구현을 만듭니다. @p pHttpClient 는 빌려 쓰고 이 제공자 혼자 거둔다(응답을 다른 쪽과 나누지 않는다). 설정이 틀리면 nullptr. */
        static unique_ptr<IPlatformLoginProvider> create( const PlatformLoginProviderSettings& settings, INetSecurityProvider* pProvider, HttpClient* pHttpClient );
        /**
         * @brief `{"providers":[{"name","kind":"oidc|profile","issuer","jwksUrl","clientIDs":[…],"requireNonce","profileUrl","subjectPath","displayNamePath"}]}` 를 읽습니다.
         * @details 모르는 키 · 빠진 필수 키 · 규칙 밖 이름은 오류(@p outError) — 철자가 틀린 설정이 조용히 기본값이 되지 않게.
         */
        [[nodiscard]] static bool readSettings( string_view jsonText, vector<PlatformLoginProviderSettings>& outListSettings, string& outError );
        /** @brief 설정이 쓸 수 있는가입니다(이름 규칙 · 방식별 필수 칸). */
        static bool isValidSettings( const PlatformLoginProviderSettings& settings );
    };
} // namespace sw
