/**
 * @file JSONWebToken.h
 * @brief JWT(JWS 압축 형식)를 읽고 서명하며 JWKS 키를 캐시합니다. OIDC ID 토큰 확인, 클라이언트 비밀 JWT(애플), 테스트 발급자가 씁니다.
 * @details 받아들이는 알고리즘은 RS256 과 ES256 뿐이다. 알고리즘 바꿔치기 공격을 막으려고 `none` 과 HS256 은 거절한다. 서명 확인은 네트워크 보안 제공자(OpenSSL)가 한다.
 *          **클레임은 서명을 확인한 형식(`JWTVerified`)에서만 읽는다.** 읽기만 한 토큰(`JWTUnverified`)은 머리(알고리즘 · kid · x5c)만 보이고,
 *          `verify` 가 서명과 시각 · 발급자 · 대상 클레임(`JWTClaimRule`)을 한 번에 확인한 뒤에야 본문을 넘긴다. 호출부마다 exp · iss · aud 검사를 빠뜨릴 틈을 없앤다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/NetSecurityTypes.h"

#include "GameFramework/GameFrameworkExports.h"

#include <optional>

namespace sw
{
    class INetSecurityProvider;
    class JSONDocument;
    class JSONText;

    /** @brief JWT 를 서명해 만드는 쪽입니다(클라이언트 비밀 · 서비스 계정 단언 · 테스트 발급자). */
    struct SW_GF_API JWTUtil
    {
        /** @brief @p payload(`JSONWriter::finish` 의 결과만 받는다 — 클레임 값을 글로 이어 붙여 다른 클레임을 끼워 넣는 모양을 형식으로 막는다)를 본문으로 서명한 압축 JWT 를 만듭니다. 헤더에는 `alg`, `typ`, `kid` 를 넣습니다. */
        [[nodiscard]] static bool makeSigned( INetSecurityProvider& provider, NetSignatureAlgorithm algorithm, string_view keyID, const JSONText& payload,
                                              const string& privateKeyPem, string& outCompact );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct JWTClaimRule
     * @brief `JWTUnverified::verify` 가 서명과 함께 확인할 클레임입니다. 만드는 길은 아래 둘뿐이라 "무엇을 확인하는가" 가 호출부에 이름으로 남는다.
     */
    struct JWTClaimRule
    {
        /**
         * @brief ID 토큰 규칙 — `iss` 가 @p issuer 와 같고, `aud` 가 @p listAudience 중 하나를 담고, `exp` 가 있어 아직 지나지 않았고, `iat` 가 있으면 미래가 아니다.
         * @details 시각은 밀리초, @p clockSkewMs 는 exp · iat 양쪽의 허용 오차다. @p issuer 와 @p listAudience 는 `verify` 가 끝날 때까지 살아 있어야 한다.
         */
        static JWTClaimRule makeIDToken( string_view issuer, const vector<string>& listAudience, int64 nowMs, int64 clockSkewMs )
        {
            return JWTClaimRule{ issuer, &listAudience, nowMs, clockSkewMs, SW_TRUE };
        }
        /** @brief 서명만 확인합니다. 신뢰가 서명 키 자체에서 오고 시각 · 발급자 클레임이 없는 JWS(애플 x5c 서명 거래, 테스트의 가짜 상대가 받는 단언)에만 쓴다. */
        static JWTClaimRule makeSignatureOnly() { return JWTClaimRule{ string_view{}, nullptr, 0, 0, SW_FALSE }; }

        string_view           _issuer;
        const vector<string>* _pListAudience;
        int64                 _nowMs;
        int64                 _clockSkewMs;
        uint8                 _bCheckClaims;
    };
} // namespace sw

namespace sw
{
    class JWTUnverified;

    /**
     * @class JWTVerified
     * @brief 서명과 클레임 규칙을 통과한 JWT 의 본문입니다. `JWTUnverified::verify` 만 만든다.
     */
    class SW_GF_API JWTVerified
    {
    public:
        ~JWTVerified();
        JWTVerified( JWTVerified&& other ) noexcept;
        JWTVerified& operator=( JWTVerified&& other ) noexcept;
        JWTVerified( const JWTVerified& )            = delete;
        JWTVerified& operator=( const JWTVerified& ) = delete;

        const string& getKeyID() const { return _keyID; }
        /** @brief 본문의 문자열 클레임을 읽습니다. 없거나 문자열이 아니면 false 를 반환합니다. */
        [[nodiscard]] bool findText( string_view claim, string& outValue ) const;
        /** @brief 본문의 중첩된 문자열 클레임을 점 경로(`data.signedTransactionInfo`)로 읽습니다. 없거나 문자열이 아니면 false 를 반환합니다. */
        [[nodiscard]] bool findTextPath( string_view path, string& outValue ) const;
        /** @brief 본문의 정수 클레임을 읽습니다(소수는 버린다). 없거나 숫자가 아니면 false 를 반환합니다. */
        [[nodiscard]] bool findInteger( string_view claim, int64& outValue ) const;
        /** @brief `aud` 가 @p audience 와 같거나 그것을 담은 배열이면 true 입니다. */
        bool hasAudience( string_view audience ) const;

    private:
        friend class JWTUnverified;
        JWTVerified( unique_ptr<JSONDocument> payload, string keyID );

        unique_ptr<JSONDocument> _payload;
        string                   _keyID;
    };
} // namespace sw

namespace sw
{
    /**
     * @class JWTUnverified
     * @brief 읽기만 한 JWT 하나입니다. 머리(알고리즘 · kid · x5c)만 보이고, 본문 클레임은 `verify` 가 돌려준 `JWTVerified` 에서 읽는다.
     */
    class SW_GF_API JWTUnverified
    {
    public:
        JWTUnverified();
        ~JWTUnverified();

        JWTUnverified( const JWTUnverified& )            = delete;
        JWTUnverified& operator=( const JWTUnverified& ) = delete;

        /** @brief `헤더.본문.서명` 을 읽습니다. 형식, base64url, JSON, 알고리즘(RS256, ES256 외)이 틀리면 false 를 반환합니다. */
        [[nodiscard]] bool parse( string_view compact );

        NetSignatureAlgorithm getAlgorithm() const { return _algorithm; }
        const string&         getKeyID() const { return _keyID; }
        /** @brief 머리의 `x5c`(표준 base64 DER 인증서 배열 — 리프가 먼저)를 풀어 둔 것입니다. 머리에 없으면 비어 있습니다. 사슬을 믿을지는 부르는 쪽이 정한다(`INetSecurityProvider::verifyCertificateChain`). */
        const vector<vector<uint8>>& getCertificateChain() const { return _listCertificateDer; }
        /**
         * @brief @p publicKey 로 서명을 확인하고 @p rule 의 클레임을 확인합니다. 둘 다 맞으면 본문을 넘긴 `JWTVerified` 이고, 아니면 비어 있습니다.
         * @details 키 알고리즘이 헤더와 달라도 비어 있습니다. 본문을 넘기므로 rvalue 에서만 부른다(`std::move( token ).verify( … )`).
         */
        [[nodiscard]] std::optional<JWTVerified> verify( INetSecurityProvider& provider, const NetPublicKey& publicKey, const JWTClaimRule& rule ) &&;

    private:
        unique_ptr<JSONDocument> _payload;
        vector<vector<uint8>>    _listCertificateDer;
        vector<uint8>            _signatureBytes;
        string                   _signingInput;
        string                   _keyID;
        NetSignatureAlgorithm    _algorithm;
    };
} // namespace sw

namespace sw
{
    /**
     * @class JwksKeyCache
     * @brief 발급자 하나의 JWKS 키(kid에서 공개 키로 가는 맵) 캐시입니다. 받은 시각과 다시 받을 시각을 보관합니다.
     */
    class SW_GF_API JwksKeyCache
    {
    public:
        /** @brief 공개 키를 JWK(JSON 객체 문자열)로 씁니다. 테스트의 가짜 JWKS 에 씁니다. */
        static string writeJwk( const NetPublicKey& publicKey, string_view keyID );

        struct Entry
        {
            string       _keyID{};
            NetPublicKey _publicKey{};
        };

        JwksKeyCache();

        /** @brief JWKS 문자열(`{"keys":[…]}`)로 키를 교체합니다. RSA(n, e)와 EC P-256(x, y) 서명 키만 읽고 다른 키는 건너뜁니다. 읽은 키가 하나도 없으면 false 를 반환하고 이전 키를 그대로 둡니다. */
        [[nodiscard]] bool  replaceFromJwks( string_view jwksJSON, int64 nowMs );
        const NetPublicKey* findKey( string_view keyID ) const;
        int32               getKeyCount() const { return static_cast<int32>( _listEntry.size() ); }
        int64               getFetchedAtMs() const { return _fetchedAtMs; }
        bool                hasKeys() const { return _listEntry.empty() == false; }

    private:
        vector<Entry> _listEntry;
        int64         _fetchedAtMs;
    };
} // namespace sw
