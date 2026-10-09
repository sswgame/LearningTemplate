#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/JsonWebToken.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/Base64Util.h"

#include "Engine/Serialization/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        struct JsonWebTokenInternal
        {
            static constexpr int32 kMaxCompactSize = 16 * 1024;

            static const utf8* getAlgorithmName( NetSignatureAlgorithm algorithm ) { return algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256 ? "RS256" : "ES256"; }

            [[nodiscard]] static bool decodeJson( string_view encoded, JsonDocument& outDocument )
            {
                vector<uint8> bytes;
                if ( Base64Util::decodeUrl( encoded, bytes ) == false )
                    return false;
                const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
                return outDocument.tryParse( text ) && outDocument.getRoot().isObject();
            }

            static string encodeText( string_view text ) { return Base64Util::encodeUrl( reinterpret_cast<const uint8*>( text.data() ), text.size() ); }

            [[nodiscard]] static bool decodeField( const JsonValue& key, const utf8* pName, vector<uint8>& outBytes )
            {
                const JsonValue field = key.get( pName, false );
                return field.isString() && Base64Util::decodeUrl( field.asString(), outBytes ) && outBytes.empty() == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    JsonWebToken::JsonWebToken()
        : _payload{ make_unique<JsonDocument>() }
        , _signatureBytes{}
        , _signingInput{}
        , _keyId{}
        , _algorithm{ NetSignatureAlgorithm::RsaPkcs1Sha256 }
    {
    }

    JsonWebToken::~JsonWebToken() = default;

    bool JsonWebToken::parse( string_view compact )
    {
        using Internal = JsonWebTokenInternal;
        if ( compact.empty() || compact.size() > static_cast<size_t>( Internal::kMaxCompactSize ) )
            return false;
        const size_t firstDot  = compact.find( '.' );
        const size_t secondDot = firstDot == string_view::npos ? string_view::npos : compact.find( '.', firstDot + 1 );
        if ( secondDot == string_view::npos || compact.find( '.', secondDot + 1 ) != string_view::npos )
            return false;
        JsonDocument header;
        if ( Internal::decodeJson( compact.substr( 0, firstDot ), header ) == false )
            return false;
        if ( Internal::decodeJson( compact.substr( firstDot + 1, secondDot - firstDot - 1 ), *_payload ) == false )
            return false;
        if ( Base64Util::decodeUrl( compact.substr( secondDot + 1 ), _signatureBytes ) == false || _signatureBytes.empty() )
            return false;
        const JsonValue algorithm = header.getRoot().get( "alg", false );
        if ( algorithm.isString() == false )
            return false;
        const string algorithmName = algorithm.asString();
        if ( algorithmName == "RS256" )
            _algorithm = NetSignatureAlgorithm::RsaPkcs1Sha256;
        else if ( algorithmName == "ES256" )
            _algorithm = NetSignatureAlgorithm::EcdsaP256Sha256;
        else
            return false; // none · HS256 · 그 밖 — 받지 않는다
        const JsonValue keyId = header.getRoot().get( "kid", false );
        _keyId                = keyId.isString() ? keyId.asString() : string{};
        _signingInput         = string( compact.substr( 0, secondDot ) );
        return true;
    }

    bool JsonWebToken::findText( string_view claim, string& outValue ) const
    {
        const JsonValue value = _payload->getRoot().get( claim, false );
        if ( value.isString() == false )
            return false;
        outValue = value.asString();
        return true;
    }

    bool JsonWebToken::findInteger( string_view claim, int64& outValue ) const
    {
        const JsonValue value = _payload->getRoot().get( claim, false );
        if ( value.isNumber() == false )
            return false;
        outValue = value.asInt();
        return true;
    }

    bool JsonWebToken::hasAudience( string_view audience ) const
    {
        const JsonValue value = _payload->getRoot().get( "aud", false );
        if ( value.isString() )
            return value.asString() == audience;
        if ( value.isArray() == false )
            return false;
        for ( size_t index = 0; index < value.size(); ++index )
        {
            const JsonValue element = value.at( index );
            if ( element.isString() && element.asString() == audience )
                return true;
        }
        return false;
    }

    bool JsonWebToken::verifySignature( INetSecurityProvider& provider, const NetPublicKey& publicKey ) const
    {
        if ( publicKey._algorithm != _algorithm )
            return false;
        return provider.verifySignature( publicKey, reinterpret_cast<const uint8*>( _signingInput.data() ), static_cast<int32>( _signingInput.size() ), _signatureBytes.data(),
                                         static_cast<int32>( _signatureBytes.size() ) );
    }

    bool JsonWebTokenUtil::makeSigned( INetSecurityProvider& provider, NetSignatureAlgorithm algorithm, string_view keyId, string_view payloadJson,
                                       const string& privateKeyPem, string& outCompact )
    {
        using Internal            = JsonWebTokenInternal;
        const string header       = string( "{\"alg\":\"" ) + Internal::getAlgorithmName( algorithm ) + "\",\"typ\":\"JWT\",\"kid\":\"" + JsonDocument::escapeString( keyId ) + "\"}";
        string       signingInput = Internal::encodeText( header );
        signingInput.push_back( '.' );
        signingInput += Internal::encodeText( payloadJson );
        vector<uint8> signature;
        if ( provider.signData( algorithm, privateKeyPem, reinterpret_cast<const uint8*>( signingInput.data() ), static_cast<int32>( signingInput.size() ), signature ) ==
             false )
            return false;
        outCompact = signingInput + "." + Base64Util::encodeUrl( signature.data(), signature.size() );
        return true;
    }

    string JsonWebTokenUtil::writeJwk( const NetPublicKey& publicKey, string_view keyId )
    {
        string jwk = "{\"kid\":\"" + JsonDocument::escapeString( keyId ) + "\",\"use\":\"sig\",";
        if ( publicKey._algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256 )
        {
            jwk += "\"kty\":\"RSA\",\"alg\":\"RS256\",\"n\":\"" + Base64Util::encodeUrl( publicKey._modulus.data(), publicKey._modulus.size() ) + "\",\"e\":\"" +
                   Base64Util::encodeUrl( publicKey._exponent.data(), publicKey._exponent.size() ) + "\"}";
            return jwk;
        }
        jwk += "\"kty\":\"EC\",\"alg\":\"ES256\",\"crv\":\"P-256\",\"x\":\"" + Base64Util::encodeUrl( publicKey._x.data(), publicKey._x.size() ) + "\",\"y\":\"" +
               Base64Util::encodeUrl( publicKey._y.data(), publicKey._y.size() ) + "\"}";
        return jwk;
    }

    JwksKeyCache::JwksKeyCache()
        : _listEntry{}
        , _fetchedAtMs{ 0 }
    {
    }

    bool JwksKeyCache::replaceFromJwks( string_view jwksJson, int64 nowMs )
    {
        using Internal = JsonWebTokenInternal;
        JsonDocument document;
        if ( document.tryParse( jwksJson ) == false )
            return false;
        const JsonValue keys = document.getRoot().get( "keys", false );
        if ( keys.isArray() == false )
            return false;
        vector<Entry> listEntry;
        for ( size_t index = 0; index < keys.size(); ++index )
        {
            const JsonValue key   = keys.at( index );
            const JsonValue use   = key.get( "use", false );
            const JsonValue kid   = key.get( "kid", false );
            const JsonValue type  = key.get( "kty", false );
            const bool      bSign = use.isValid() == false || use.isNull() || use.asString() == "sig";
            if ( key.isObject() == false || bSign == false || kid.isString() == false || type.isString() == false )
                continue;
            Entry entry;
            entry._keyId          = kid.asString();
            const string typeName = type.asString();
            bool         bRead    = false;
            if ( typeName == "RSA" )
            {
                entry._publicKey._algorithm = NetSignatureAlgorithm::RsaPkcs1Sha256;
                bRead                       = Internal::decodeField( key, "n", entry._publicKey._modulus ) && Internal::decodeField( key, "e", entry._publicKey._exponent );
            }
            else if ( typeName == "EC" && key.get( "crv", false ).asString() == "P-256" )
            {
                entry._publicKey._algorithm = NetSignatureAlgorithm::EcdsaP256Sha256;
                bRead                       = Internal::decodeField( key, "x", entry._publicKey._x ) && Internal::decodeField( key, "y", entry._publicKey._y );
            }
            if ( bRead )
                listEntry.push_back( std::move( entry ) );
        }
        if ( listEntry.empty() )
            return false;
        _listEntry   = std::move( listEntry );
        _fetchedAtMs = nowMs;
        return true;
    }

    const NetPublicKey* JwksKeyCache::findKey( string_view keyId ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._keyId == keyId )
                return &entry._publicKey;
        }
        return nullptr;
    }
} // namespace sw
