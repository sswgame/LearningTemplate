/**
 * @file JsonWebToken.h
 * @brief JWT(JWS 압축 형식) 읽기 · 서명 · JWKS 키 캐시 — OIDC ID 토큰 확인과 클라이언트 비밀 JWT(애플) · 시험 발급자가 씁니다.
 * @details 받아들이는 알고리즘은 RS256 · ES256 뿐이다(`none` · HS256 거절 — 알고리즘 바꿔치기 공격). 서명 확인은 네트워크 보안 제공자(OpenSSL).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Security/NetSecurityTypes.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class INetSecurityProvider;
    class JsonDocument;

    /**
     * @class JsonWebToken
     * @brief 읽은 JWT 하나입니다(머리 · 몸 JSON + 서명 입력 · 서명).
     */
    class SW_GF_API JsonWebToken
    {
    public:
        JsonWebToken();
        ~JsonWebToken();

        JsonWebToken( const JsonWebToken& )            = delete;
        JsonWebToken& operator=( const JsonWebToken& ) = delete;

        /** @brief `머리.몸.서명` 을 읽습니다. 형식 · base64url · JSON · 알고리즘(RS256 · ES256 외)이 틀리면 false 입니다. */
        [[nodiscard]] bool parse( string_view compact );

        NetSignatureAlgorithm getAlgorithm() const { return _algorithm; }
        const string&         getKeyId() const { return _keyId; }
        /** @brief 몸의 글 주장입니다. 없거나 글이 아니면 false 입니다. */
        [[nodiscard]] bool findText( string_view claim, string& outValue ) const;
        /** @brief 몸의 정수 주장(초 — exp · iat)입니다. 없거나 숫자가 아니면 false 입니다. */
        [[nodiscard]] bool findInteger( string_view claim, int64& outValue ) const;
        /** @brief `aud` 가 @p audience 이거나 그것을 담은 배열인가입니다. */
        bool hasAudience( string_view audience ) const;
        /** @brief @p publicKey 로 서명을 확인합니다(키 알고리즘이 머리와 달라도 false). */
        [[nodiscard]] bool verifySignature( INetSecurityProvider& provider, const NetPublicKey& publicKey ) const;

    private:
        unique_ptr<JsonDocument> _payload;
        vector<uint8>            _signatureBytes;
        string                   _signingInput;
        string                   _keyId;
        NetSignatureAlgorithm    _algorithm;
    };
} // namespace sw

namespace sw
{
    /** @brief JWT 만들기 · JWKS 읽기 도우미입니다. */
    struct SW_GF_API JsonWebTokenUtil
    {
        /** @brief @p payloadJson 을 몸으로 서명한 압축 JWT 를 만듭니다(머리 `alg` · `typ` · `kid`). */
        [[nodiscard]] static bool makeSigned( INetSecurityProvider& provider, NetSignatureAlgorithm algorithm, string_view keyId, string_view payloadJson,
                                              const string& privateKeyPem, string& outCompact );
        /** @brief 공개 키를 JWK(JSON 객체 글)로 씁니다 — 시험의 가짜 JWKS. */
        static string writeJwk( const NetPublicKey& publicKey, string_view keyId );
    };
} // namespace sw

namespace sw
{
    /**
     * @class JwksKeyCache
     * @brief 발급자 하나의 JWKS 키(kid → 공개 키) 캐시입니다. 받은 때 · 다시 받을 때를 들고 있습니다.
     */
    class SW_GF_API JwksKeyCache
    {
    public:
        struct Entry
        {
            string       _keyId{};
            NetPublicKey _publicKey{};
        };

        JwksKeyCache();

        /** @brief JWKS 글(`{"keys":[…]}`)로 키를 바꿉니다. RSA(n · e) · EC P-256(x · y) 서명 키만, 다른 것은 건너뛴다. 읽은 키가 하나도 없으면 false 이고 옛 키를 둔다. */
        [[nodiscard]] bool  replaceFromJwks( string_view jwksJson, int64 nowMs );
        const NetPublicKey* findKey( string_view keyId ) const;
        int32               getKeyCount() const { return static_cast<int32>( _listEntry.size() ); }
        int64               getFetchedAtMs() const { return _fetchedAtMs; }
        bool                hasKeys() const { return _listEntry.empty() == false; }

    private:
        vector<Entry> _listEntry;
        int64         _fetchedAtMs;
    };
} // namespace sw
