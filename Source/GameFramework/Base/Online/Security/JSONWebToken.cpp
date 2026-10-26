#include "pch.h"

#include "GameFramework/Base/Online/Security/JSONWebToken.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/String/Base64Util.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct JSONWebTokenInternal
        {
            static constexpr int32 kMaxCompactSize = 16 * 1024;

            static const utf8* getAlgorithmName( NetSignatureAlgorithm algorithm ) { return algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256 ? "RS256" : "ES256"; }

            [[nodiscard]] static bool decodeJSON( string_view encoded, JSONDocument& outDocument )
            {
                vector<uint8> bytes;
                if ( Base64Util::decodeURL( encoded, bytes ) == false )
                    return false;
                const string_view text( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
                return outDocument.tryParse( text ) && outDocument.getRoot().isObject();
            }

            static string encodeText( string_view text ) { return Base64Util::encodeURL( reinterpret_cast<const uint8*>( text.data() ), text.size() ); }

            [[nodiscard]] static bool decodeField( const JSONValue& key, const utf8* pName, vector<uint8>& outBytes )
            {
                const JSONValue field = key.get( pName );
                return field.isString() && Base64Util::decodeURL( field.asString(), outBytes ) && outBytes.empty() == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    JSONWebToken::JSONWebToken()
        : _payload{ make_unique<JSONDocument>() }
        , _listCertificateDer{}
        , _signatureBytes{}
        , _signingInput{}
        , _keyID{}
        , _algorithm{ NetSignatureAlgorithm::RsaPkcs1Sha256 }
    {
    }

    JSONWebToken::~JSONWebToken() = default;

    bool JSONWebToken::parse( string_view compact )
    {
        using Internal = JSONWebTokenInternal;
        if ( compact.empty() || compact.size() > static_cast<size_t>( Internal::kMaxCompactSize ) )
            return false;
        const size_t firstDot  = compact.find( '.' );
        const size_t secondDot = firstDot == string_view::npos ? string_view::npos : compact.find( '.', firstDot + 1 );
        if ( secondDot == string_view::npos || compact.find( '.', secondDot + 1 ) != string_view::npos )
            return false;
        JSONDocument header;
        if ( Internal::decodeJSON( compact.substr( 0, firstDot ), header ) == false )
            return false;
        if ( Internal::decodeJSON( compact.substr( firstDot + 1, secondDot - firstDot - 1 ), *_payload ) == false )
            return false;
        if ( Base64Util::decodeURL( compact.substr( secondDot + 1 ), _signatureBytes ) == false || _signatureBytes.empty() )
            return false;
        const JSONValue algorithm = header.getRoot().get( "alg" );
        if ( algorithm.isString() == false )
            return false;
        const string algorithmName = algorithm.asString();
        if ( algorithmName == "RS256" )
            _algorithm = NetSignatureAlgorithm::RsaPkcs1Sha256;
        else if ( algorithmName == "ES256" )
            _algorithm = NetSignatureAlgorithm::EcdsaP256Sha256;
        else
            return false; // none, HS256, 그 밖의 알고리즘은 받지 않는다
        const JSONValue keyID = header.getRoot().get( "kid" );
        _keyID                = keyID.isString() ? keyID.asString() : string{};
        _listCertificateDer.clear();
        const JSONValue chain = header.getRoot().get( "x5c" );
        if ( chain.isValid() )
        {
            if ( chain.isArray() == false )
                return false;
            for ( size_t index = 0; index < chain.size(); ++index )
            {
                const JSONValue element = chain.at( index );
                vector<uint8>   listDer;
                const bool      bDecoded = element.isString() && Base64Util::decode( element.asString(), listDer );
                if ( bDecoded == false || listDer.empty() )
                    return false; // 사슬이 깨졌다 — 일부만 믿지 않는다
                _listCertificateDer.push_back( std::move( listDer ) );
            }
        }
        _signingInput = string( compact.substr( 0, secondDot ) );
        return true;
    }

    bool JSONWebToken::findText( string_view claim, string& outValue ) const
    {
        const JSONValue value = _payload->getRoot().get( claim );
        if ( value.isString() == false )
            return false;
        outValue = value.asString();
        return true;
    }

    bool JSONWebToken::findTextPath( string_view path, string& outValue ) const
    {
        JSONValue current = _payload->getRoot();
        while ( path.empty() == false )
        {
            const size_t      dot  = path.find( '.' );
            const string_view part = path.substr( 0, dot );
            path                   = dot == string_view::npos ? string_view{} : path.substr( dot + 1 );
            if ( current.isObject() == false )
                return false;
            current = current.get( part );
            if ( current.isValid() == false )
                return false;
        }
        if ( current.isString() == false )
            return false;
        outValue = current.asString();
        return true;
    }

    bool JSONWebToken::findInteger( string_view claim, int64& outValue ) const
    {
        // 시각 클레임(NumericDate)은 소수 초도 허용된다(RFC 7519 2절) — 실수로 읽어 초 단위로 버린다. 값은 2^53 보다 작다.
        const JSONValue              value  = _payload->getRoot().get( claim );
        const std::optional<float64> number = value.isNumber() ? value.read<float64>() : std::nullopt;
        if ( number.has_value() == false )
            return false;
        outValue = static_cast<int64>( *number );
        return true;
    }

    bool JSONWebToken::hasAudience( string_view audience ) const
    {
        const JSONValue value = _payload->getRoot().get( "aud" );
        if ( value.isString() )
            return value.asString() == audience;
        if ( value.isArray() == false )
            return false;
        for ( size_t index = 0; index < value.size(); ++index )
        {
            const JSONValue element = value.at( index );
            if ( element.isString() && element.asString() == audience )
                return true;
        }
        return false;
    }

    bool JSONWebToken::verifySignature( INetSecurityProvider& provider, const NetPublicKey& publicKey ) const
    {
        if ( publicKey._algorithm != _algorithm )
            return false;
        return provider.verifySignature( publicKey, reinterpret_cast<const uint8*>( _signingInput.data() ), static_cast<int32>( _signingInput.size() ), _signatureBytes.data(),
                                         static_cast<int32>( _signatureBytes.size() ) );
    }

    bool JSONWebToken::makeSigned( INetSecurityProvider& provider, NetSignatureAlgorithm algorithm, string_view keyID, string_view payloadJSON,
                                   const string& privateKeyPem, string& outCompact )
    {
        using Internal            = JSONWebTokenInternal;
        const string header       = string( "{\"alg\":\"" ) + Internal::getAlgorithmName( algorithm ) + "\",\"typ\":\"JWT\",\"kid\":\"" + JSONDocument::escapeString( keyID ) + "\"}";
        string       signingInput = Internal::encodeText( header );
        signingInput.push_back( '.' );
        signingInput += Internal::encodeText( payloadJSON );
        vector<uint8> signature;
        if ( provider.signData( algorithm, privateKeyPem, reinterpret_cast<const uint8*>( signingInput.data() ), static_cast<int32>( signingInput.size() ), signature ) ==
             false )
            return false;
        outCompact = signingInput + "." + Base64Util::encodeURL( signature.data(), signature.size() );
        return true;
    }

    string JwksKeyCache::writeJwk( const NetPublicKey& publicKey, string_view keyID )
    {
        string jwk = "{\"kid\":\"" + JSONDocument::escapeString( keyID ) + "\",\"use\":\"sig\",";
        if ( publicKey._algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256 )
        {
            jwk += "\"kty\":\"RSA\",\"alg\":\"RS256\",\"n\":\"" + Base64Util::encodeURL( publicKey._modulus.data(), publicKey._modulus.size() ) + "\",\"e\":\"" +
                   Base64Util::encodeURL( publicKey._exponent.data(), publicKey._exponent.size() ) + "\"}";
            return jwk;
        }
        jwk += "\"kty\":\"EC\",\"alg\":\"ES256\",\"crv\":\"P-256\",\"x\":\"" + Base64Util::encodeURL( publicKey._x.data(), publicKey._x.size() ) + "\",\"y\":\"" +
               Base64Util::encodeURL( publicKey._y.data(), publicKey._y.size() ) + "\"}";
        return jwk;
    }

    JwksKeyCache::JwksKeyCache()
        : _listEntry{}
        , _fetchedAtMs{ 0 }
    {
    }

    bool JwksKeyCache::replaceFromJwks( string_view jwksJSON, int64 nowMs )
    {
        using Internal = JSONWebTokenInternal;
        JSONDocument document;
        if ( document.tryParse( jwksJSON ) == false )
            return false;
        const JSONValue keys = document.getRoot().get( "keys" );
        if ( keys.isArray() == false )
            return false;
        vector<Entry> listEntry;
        for ( size_t index = 0; index < keys.size(); ++index )
        {
            const JSONValue key      = keys.at( index );
            const JSONValue use      = key.get( "use" );
            const JSONValue kid      = key.get( "kid" );
            const JSONValue type     = key.get( "kty" );
            const bool      bSign    = use.isValid() == false || use.isNull() || use.asString() == "sig";
            const bool      bSkipKey = key.isObject() == false || bSign == false || kid.isString() == false || type.isString() == false;
            if ( bSkipKey )
                continue;
            Entry entry;
            entry._keyID          = kid.asString();
            const string typeName = type.asString();
            bool         bRead    = false;
            if ( typeName == "RSA" )
            {
                entry._publicKey._algorithm = NetSignatureAlgorithm::RsaPkcs1Sha256;
                bRead                       = Internal::decodeField( key, "n", entry._publicKey._modulus ) && Internal::decodeField( key, "e", entry._publicKey._exponent );
            }
            else if ( typeName == "EC" && key.get( "crv" ).asString() == "P-256" )
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

    const NetPublicKey* JwksKeyCache::findKey( string_view keyID ) const
    {
        const Entry* pEntry = VectorUtil::findBy<&Entry::_keyID>( _listEntry, keyID );
        return pEntry != nullptr ? &pEntry->_publicKey : nullptr;
    }
} // namespace sw
