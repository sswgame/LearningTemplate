#include "pch.h"

#include "GameFramework/Base/Online/Security/OpenSSL/OpenSSLNetSecurityProvider.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include <cstring>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace sw
{
    SW_LOG_CALLER( "OpenSSLNetSecurity" );

    namespace
    {
        struct OpenSSLNetSecurityInternal
        {
            /** @brief 자체 서명 인증서에 붙이는 확장 하나입니다. */
            struct ExtensionRow
            {
                int32       _nid;
                const utf8* _pValue;
            };

            /** @brief OpenSSL 오류 큐를 비우며 마지막 것을 글로 — 큐를 남기면 다음 호출의 오류로 섞인다. */
            static string takeErrorText()
            {
                string text;
                while ( ERR_peek_error() != 0 )
                {
                    utf8 arrBuffer[constant::kMaxBuffer256];
                    ERR_error_string_n( ERR_get_error(), arrBuffer, sizeof( arrBuffer ) );
                    text = arrBuffer;
                }
                return text.empty() ? string( "unknown OpenSSL error" ) : text;
            }

            static int32 toOpenSSLVersion( TLSVersion version ) { return version == TLSVersion::TLS12 ? TLS1_2_VERSION : TLS1_3_VERSION; }

            static string toHex( const uint8* pData, int32 size )
            {
                static constexpr utf8 kDigit[] = "0123456789abcdef";
                string                text;
                text.reserve( static_cast<size_t>( size ) * 2 );
                for ( int32 index = 0; index < size; ++index )
                {
                    text.push_back( kDigit[pData[index] >> 4] );
                    text.push_back( kDigit[pData[index] & 0x0F] );
                }
                return text;
            }

            /** @brief PEM 의 인증서를 모두 읽습니다(체인). 없으면 빈 목록. 받은 X509 는 부르는 쪽이 X509_free. */
            static vector<X509*> readCertificates( const string& pem )
            {
                vector<X509*> listCertificate;
                BIO*          pBio = BIO_new_mem_buf( pem.data(), static_cast<int32>( pem.size() ) );
                if ( pBio == nullptr )
                    return listCertificate;
                for ( X509* pCertificate = PEM_read_bio_X509( pBio, nullptr, nullptr, nullptr ); pCertificate != nullptr;
                      pCertificate       = PEM_read_bio_X509( pBio, nullptr, nullptr, nullptr ) )
                {
                    listCertificate.push_back( pCertificate );
                }
                ERR_clear_error(); // 끝까지 읽으면 "no start line" 이 남는다
                BIO_free( pBio );
                return listCertificate;
            }

            static bool computeSha256Hex( X509* pCertificate, string& outHex )
            {
                uint8  arrDigest[EVP_MAX_MD_SIZE];
                uint32 digestSize = 0;
                if ( X509_digest( pCertificate, EVP_sha256(), arrDigest, &digestSize ) != 1 )
                    return false;
                outHex = toHex( arrDigest, static_cast<int32>( digestSize ) );
                return true;
            }

            /**
             * @brief PEM 키 암호 콜백 — 설정의 암호를 그대로 준다. 암호가 없으면 0(실패)을 돌려준다: 기본 콜백은 콘솔에서 암호를 묻는데, 서버가 그 자리에서 멈추면 안 된다.
             */
            static int32 providePassphrase( utf8* pBuffer, int32 size, int32 writing, void* pUserData )
            {
                (void)writing;
                const string* pPassphrase = static_cast<const string*>( pUserData );
                if ( pPassphrase == nullptr || pPassphrase->empty() || static_cast<int32>( pPassphrase->size() ) > size )
                    return 0;
                std::memcpy( pBuffer, pPassphrase->data(), pPassphrase->size() );
                return static_cast<int32>( pPassphrase->size() );
            }
        };

        /** @brief AEAD 하나 — 키를 박은 암호 · 복호 문맥 둘을 들고, 호출마다 nonce 만 바꾼다. */
        class OpenSSLAead final : public INetAead
        {
        public:
            OpenSSLAead( const EVP_CIPHER* pCipher, const uint8* pKey )
                : _pEncrypt{ EVP_CIPHER_CTX_new() }
                , _pDecrypt{ EVP_CIPHER_CTX_new() }
                , _bValid{ SW_FALSE }
            {
                const bool bReady = _pEncrypt != nullptr && _pDecrypt != nullptr && EVP_EncryptInit_ex( _pEncrypt, pCipher, nullptr, pKey, nullptr ) == 1 &&
                                    EVP_DecryptInit_ex( _pDecrypt, pCipher, nullptr, pKey, nullptr ) == 1;
                _bValid = bReady ? SW_TRUE : SW_FALSE;
            }
            ~OpenSSLAead() override
            {
                EVP_CIPHER_CTX_free( _pEncrypt );
                EVP_CIPHER_CTX_free( _pDecrypt );
            }

            OpenSSLAead( const OpenSSLAead& )            = delete;
            OpenSSLAead& operator=( const OpenSSLAead& ) = delete;

            bool isValid() const { return _bValid == SW_TRUE; }

            [[nodiscard]] bool seal( const uint8* pNonce, const uint8* pAad, int32 aadSize, const uint8* pPlain, int32 plainSize, uint8* pOut ) override
            {
                int32      length      = 0;
                int32      finalLength = 0;
                const bool bSealed     = EVP_EncryptInit_ex( _pEncrypt, nullptr, nullptr, nullptr, pNonce ) == 1 &&
                                     ( aadSize == 0 || EVP_EncryptUpdate( _pEncrypt, nullptr, &length, pAad, aadSize ) == 1 ) &&
                                     ( plainSize == 0 || EVP_EncryptUpdate( _pEncrypt, pOut, &length, pPlain, plainSize ) == 1 ) &&
                                     EVP_EncryptFinal_ex( _pEncrypt, pOut + ( plainSize == 0 ? 0 : length ), &finalLength ) == 1 &&
                                     EVP_CIPHER_CTX_ctrl( _pEncrypt, EVP_CTRL_AEAD_GET_TAG, NetSecurityConstant::kAeadTagSize, pOut + plainSize ) == 1;
                return bSealed;
            }

            [[nodiscard]] bool open( const uint8* pNonce, const uint8* pAad, int32 aadSize, const uint8* pCipher, int32 cipherSize, uint8* pOut ) override
            {
                const int32 plainSize = cipherSize - NetSecurityConstant::kAeadTagSize;
                if ( plainSize < 0 )
                    return false;
                int32 length      = 0;
                int32 finalLength = 0;
                // 태그는 const 가 아닌 포인터를 받지만 읽기만 한다. 마지막 Final 이 태그를 대조한다 — 틀리면 0.
                const bool bOpened = EVP_DecryptInit_ex( _pDecrypt, nullptr, nullptr, nullptr, pNonce ) == 1 &&
                                     ( aadSize == 0 || EVP_DecryptUpdate( _pDecrypt, nullptr, &length, pAad, aadSize ) == 1 ) &&
                                     ( plainSize == 0 || EVP_DecryptUpdate( _pDecrypt, pOut, &length, pCipher, plainSize ) == 1 ) &&
                                     EVP_CIPHER_CTX_ctrl( _pDecrypt, EVP_CTRL_AEAD_SET_TAG, NetSecurityConstant::kAeadTagSize, const_cast<uint8*>( pCipher + plainSize ) ) == 1 &&
                                     EVP_DecryptFinal_ex( _pDecrypt, pOut + ( plainSize == 0 ? 0 : length ), &finalLength ) == 1;
                if ( bOpened == false )
                    ERR_clear_error();
                return bOpened;
            }

        private:
            EVP_CIPHER_CTX* _pEncrypt;
            EVP_CIPHER_CTX* _pDecrypt;
            uint8           _bValid;
        };

        /** @brief 메모리 BIO 둘 위의 TLS 세션 — 핸드셰이크 전에 쓴 평문은 모아 두었다가 핸드셰이크가 끝나면 보낸다. */
        class OpenSSLTLSSession final : public ITLSSession
        {
        public:
            OpenSSLTLSSession( SSL_CTX* pContext, TLSRole role, const string& serverName, const string& pinnedSha256Hex )
                : _pendingBytes{}
                , _pinnedSha256Hex{ pinnedSha256Hex }
                , _failureText{}
                , _pSsl{ SSL_new( pContext ) }
                , _pReadBio{ BIO_new( BIO_s_mem() ) }
                , _pWriteBio{ BIO_new( BIO_s_mem() ) }
                , _state{ TLSSessionState::Handshaking }
            {
                if ( _pSsl == nullptr || _pReadBio == nullptr || _pWriteBio == nullptr )
                {
                    (void)fail( "SSL_new failed" );
                    return;
                }
                SSL_set_bio( _pSsl, _pReadBio, _pWriteBio ); // BIO 는 SSL 이 소유한다
                if ( role == TLSRole::Server )
                {
                    SSL_set_accept_state( _pSsl );
                    return;
                }
                SSL_set_connect_state( _pSsl );
                if ( serverName.empty() == false )
                {
                    (void)SSL_set_tlsext_host_name( _pSsl, serverName.c_str() );
                    (void)SSL_set1_host( _pSsl, serverName.c_str() ); // 인증서 이름(SAN)이 같아야 검증이 통과한다
                }
                advanceHandshake(); // ClientHello
            }

            ~OpenSSLTLSSession() override
            {
                if ( _pSsl != nullptr )
                {
                    SSL_free( _pSsl );
                    return;
                }
                BIO_free( _pReadBio ); // SSL 에 넘기지 못한 BIO
                BIO_free( _pWriteBio );
            }

            OpenSSLTLSSession( const OpenSSLTLSSession& )            = delete;
            OpenSSLTLSSession& operator=( const OpenSSLTLSSession& ) = delete;

            TLSSessionState getState() const override { return _state; }
            const utf8*     getFailureText() const override { return _failureText.c_str(); }

            [[nodiscard]] bool feedCiphertext( const uint8* pData, int32 size ) override
            {
                if ( _state == TLSSessionState::Failed )
                    return false;
                if ( size > 0 && BIO_write( _pReadBio, pData, size ) != size )
                    return fail( "BIO_write failed" );
                if ( _state == TLSSessionState::Handshaking )
                    advanceHandshake();
                return _state != TLSSessionState::Failed;
            }

            [[nodiscard]] bool readPlaintext( vector<uint8>& outBytes ) override
            {
                if ( _state != TLSSessionState::Established )
                    return _state != TLSSessionState::Failed;
                uint8 arrBuffer[constant::kMaxBuffer8192];
                for ( ;; )
                {
                    const int32 read = SSL_read( _pSsl, arrBuffer, static_cast<int32>( sizeof( arrBuffer ) ) );
                    if ( read > 0 )
                    {
                        outBytes.insert( outBytes.end(), arrBuffer, arrBuffer + read );
                        continue;
                    }
                    const int32 error = SSL_get_error( _pSsl, read );
                    if ( error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE )
                        return true;
                    if ( error == SSL_ERROR_ZERO_RETURN )
                    {
                        _state = TLSSessionState::Closed; // 저쪽 close_notify
                        return true;
                    }
                    return fail( OpenSSLNetSecurityInternal::takeErrorText() ); // 변조된 레코드는 여기(bad record mac)
                }
            }

            [[nodiscard]] bool writePlaintext( const uint8* pData, int32 size ) override
            {
                if ( _state == TLSSessionState::Handshaking )
                {
                    _pendingBytes.insert( _pendingBytes.end(), pData, pData + size );
                    return true;
                }
                if ( _state != TLSSessionState::Established )
                    return false;
                return writeEstablished( pData, size );
            }

            void takeCiphertext( vector<uint8>& outBytes ) override
            {
                if ( _pWriteBio == nullptr )
                    return;
                const size_t pending = BIO_ctrl_pending( _pWriteBio );
                if ( pending == 0 )
                    return;
                const size_t offset = outBytes.size();
                outBytes.resize( offset + pending );
                const int32 read = BIO_read( _pWriteBio, outBytes.data() + offset, static_cast<int32>( pending ) );
                outBytes.resize( offset + static_cast<size_t>( read > 0 ? read : 0 ) );
            }

            void close() override
            {
                if ( _state == TLSSessionState::Established )
                    (void)SSL_shutdown( _pSsl );
                if ( _state != TLSSessionState::Failed )
                    _state = TLSSessionState::Closed;
            }

        private:
            bool fail( const string& text )
            {
                _state       = TLSSessionState::Failed;
                _failureText = text;
                return false;
            }

            [[nodiscard]] bool writeEstablished( const uint8* pData, int32 size )
            {
                int32 offset = 0;
                while ( offset < size )
                {
                    const int32 written = SSL_write( _pSsl, pData + offset, size - offset ); // 메모리 BIO 라 늘 다 받는다
                    if ( written <= 0 )
                        return fail( OpenSSLNetSecurityInternal::takeErrorText() );
                    offset += written;
                }
                return true;
            }

            void advanceHandshake()
            {
                const int32 result = SSL_do_handshake( _pSsl );
                if ( result == 1 )
                {
                    if ( _pinnedSha256Hex.empty() == false && matchesPin() == false )
                    {
                        (void)fail( "server certificate does not match the pinned SHA-256" );
                        return;
                    }
                    _state = TLSSessionState::Established;
                    if ( _pendingBytes.empty() == false )
                    {
                        // 실패는 fail 이 상태(Failed)와 오류 글자로 남긴다
                        (void)writeEstablished( _pendingBytes.data(), static_cast<int32>( _pendingBytes.size() ) );
                        _pendingBytes.clear();
                    }
                    return;
                }
                const int32 error = SSL_get_error( _pSsl, result );
                if ( error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE )
                    return;
                const bool bVerifyFailed = SSL_get_verify_result( _pSsl ) != X509_V_OK;
                (void)fail( bVerifyFailed ? string( X509_verify_cert_error_string( SSL_get_verify_result( _pSsl ) ) ) : OpenSSLNetSecurityInternal::takeErrorText() );
                ERR_clear_error();
            }

            bool matchesPin() const
            {
                X509* pPeer = SSL_get1_peer_certificate( _pSsl );
                if ( pPeer == nullptr )
                    return false;
                string     hex;
                const bool bComputed = OpenSSLNetSecurityInternal::computeSha256Hex( pPeer, hex );
                X509_free( pPeer );
                return bComputed && hex == _pinnedSha256Hex;
            }

            vector<uint8>   _pendingBytes;
            string          _pinnedSha256Hex;
            string          _failureText;
            SSL*            _pSsl;
            BIO*            _pReadBio;
            BIO*            _pWriteBio;
            TLSSessionState _state;
        };

        class OpenSSLTLSContext final : public ITLSContext
        {
        public:
            OpenSSLTLSContext( SSL_CTX* pContext, const TLSContextSettings& settings )
                : _serverName{ settings._serverName }
                , _pinnedSha256Hex{ settings._pinnedCertificateSha256Hex }
                , _pContext{ pContext }
                , _role{ settings._role }
            {
            }
            ~OpenSSLTLSContext() override { SSL_CTX_free( _pContext ); }

            OpenSSLTLSContext( const OpenSSLTLSContext& )            = delete;
            OpenSSLTLSContext& operator=( const OpenSSLTLSContext& ) = delete;

            unique_ptr<ITLSSession> createSession() override { return sw::make_unique<OpenSSLTLSSession>( _pContext, _role, _serverName, _pinnedSha256Hex ); }
            TLSRole                 getRole() const override { return _role; }

        private:
            string   _serverName;
            string   _pinnedSha256Hex;
            SSL_CTX* _pContext;
            TLSRole  _role;
        };

        /** @brief 서명 확인 · 서명 · 키 쌍 — EVP_PKEY 를 JWK 구성 요소에서 만들고 ES256 의 r ‖ s 와 DER 를 오간다. */
        struct OpenSSLSignatureInternal
        {
            static constexpr int32 kEcCoordinateSize = 32;
            static constexpr int32 kMinRsaBits       = 2048;

            /** @brief 공개 키 구성 요소에서 EVP_PKEY 를 만듭니다. 실패하면 nullptr. */
            static EVP_PKEY* createPublicKey( const NetPublicKey& publicKey )
            {
                OSSL_PARAM_BLD* pBuilder = OSSL_PARAM_BLD_new();
                if ( pBuilder == nullptr )
                    return nullptr;
                BIGNUM*       pModulus  = nullptr;
                BIGNUM*       pExponent = nullptr;
                vector<uint8> pointBytes;
                const utf8*   pKeyType = "RSA";
                bool          bBuilt   = false;
                if ( publicKey._algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256 )
                {
                    pModulus  = BN_bin2bn( publicKey._modulus.data(), static_cast<int32>( publicKey._modulus.size() ), nullptr );
                    pExponent = BN_bin2bn( publicKey._exponent.data(), static_cast<int32>( publicKey._exponent.size() ), nullptr );
                    bBuilt    = publicKey._modulus.empty() == false && publicKey._exponent.empty() == false && pModulus != nullptr && pExponent != nullptr &&
                             OSSL_PARAM_BLD_push_BN( pBuilder, OSSL_PKEY_PARAM_RSA_N, pModulus ) == 1 &&
                             OSSL_PARAM_BLD_push_BN( pBuilder, OSSL_PKEY_PARAM_RSA_E, pExponent ) == 1;
                }
                else
                {
                    pKeyType              = "EC";
                    const bool bSizeOk    = static_cast<int32>( publicKey._x.size() ) == kEcCoordinateSize && static_cast<int32>( publicKey._y.size() ) == kEcCoordinateSize;
                    utf8       arrGroup[] = "prime256v1";
                    if ( bSizeOk )
                    {
                        pointBytes.push_back( 0x04 ); // 압축하지 않은 점
                        pointBytes.insert( pointBytes.end(), publicKey._x.begin(), publicKey._x.end() );
                        pointBytes.insert( pointBytes.end(), publicKey._y.begin(), publicKey._y.end() );
                    }
                    bBuilt = bSizeOk && OSSL_PARAM_BLD_push_utf8_string( pBuilder, OSSL_PKEY_PARAM_GROUP_NAME, arrGroup, 0 ) == 1 &&
                             OSSL_PARAM_BLD_push_octet_string( pBuilder, OSSL_PKEY_PARAM_PUB_KEY, pointBytes.data(), pointBytes.size() ) == 1;
                }
                OSSL_PARAM*   pParams  = bBuilt ? OSSL_PARAM_BLD_to_param( pBuilder ) : nullptr;
                EVP_PKEY_CTX* pContext = pParams != nullptr ? EVP_PKEY_CTX_new_from_name( nullptr, pKeyType, nullptr ) : nullptr;
                EVP_PKEY*     pKey     = nullptr;
                const bool    bMade    = pContext != nullptr && EVP_PKEY_fromdata_init( pContext ) == 1 && EVP_PKEY_fromdata( pContext, &pKey, EVP_PKEY_PUBLIC_KEY, pParams ) == 1;
                if ( bMade == false )
                {
                    EVP_PKEY_free( pKey );
                    pKey = nullptr;
                }
                EVP_PKEY_CTX_free( pContext );
                OSSL_PARAM_free( pParams );
                OSSL_PARAM_BLD_free( pBuilder );
                BN_free( pModulus );
                BN_free( pExponent );
                return pKey;
            }

            /** @brief r ‖ s(64 B)를 DER ECDSA 서명으로 바꿉니다. */
            [[nodiscard]] static bool convertRawToDer( const uint8* pSignature, int32 signatureSize, vector<uint8>& outDer )
            {
                if ( signatureSize != kEcCoordinateSize * 2 )
                    return false;
                ECDSA_SIG* pSignatureObject = ECDSA_SIG_new();
                BIGNUM*    pR               = BN_bin2bn( pSignature, kEcCoordinateSize, nullptr );
                BIGNUM*    pS               = BN_bin2bn( pSignature + kEcCoordinateSize, kEcCoordinateSize, nullptr );
                const bool bSet             = pSignatureObject != nullptr && pR != nullptr && pS != nullptr && ECDSA_SIG_set0( pSignatureObject, pR, pS ) == 1;
                if ( bSet == false )
                {
                    BN_free( pR );
                    BN_free( pS );
                    ECDSA_SIG_free( pSignatureObject );
                    return false;
                }
                const int32 derSize = i2d_ECDSA_SIG( pSignatureObject, nullptr );
                bool        bDone   = derSize > 0;
                if ( bDone )
                {
                    outDer.resize( static_cast<size_t>( derSize ) );
                    uint8* pCursor = outDer.data();
                    bDone          = i2d_ECDSA_SIG( pSignatureObject, &pCursor ) == derSize;
                }
                ECDSA_SIG_free( pSignatureObject );
                return bDone;
            }

            /** @brief DER ECDSA 서명을 r ‖ s(64 B)로 바꿉니다. */
            [[nodiscard]] static bool convertDerToRaw( const vector<uint8>& der, vector<uint8>& outRaw )
            {
                const uint8* pCursor          = der.data();
                ECDSA_SIG*   pSignatureObject = d2i_ECDSA_SIG( nullptr, &pCursor, static_cast<int32>( der.size() ) );
                if ( pSignatureObject == nullptr )
                    return false;
                const BIGNUM* pR = nullptr;
                const BIGNUM* pS = nullptr;
                ECDSA_SIG_get0( pSignatureObject, &pR, &pS );
                outRaw.assign( static_cast<size_t>( kEcCoordinateSize * 2 ), 0 );
                const bool bDone = BN_bn2binpad( pR, outRaw.data(), kEcCoordinateSize ) == kEcCoordinateSize &&
                                   BN_bn2binpad( pS, outRaw.data() + kEcCoordinateSize, kEcCoordinateSize ) == kEcCoordinateSize;
                ECDSA_SIG_free( pSignatureObject );
                return bDone;
            }

            [[nodiscard]] static bool readBignumParam( EVP_PKEY* pKey, const utf8* pName, vector<uint8>& outBytes )
            {
                BIGNUM* pNumber = nullptr;
                if ( EVP_PKEY_get_bn_param( pKey, pName, &pNumber ) != 1 || pNumber == nullptr )
                    return false;
                outBytes.assign( static_cast<size_t>( BN_num_bytes( pNumber ) ), 0 );
                const bool bDone = BN_bn2bin( pNumber, outBytes.data() ) == static_cast<int32>( outBytes.size() );
                BN_free( pNumber );
                return bDone;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool OpenSSLNetSecurityProvider::fillRandomBytes( uint8* pOut, int32 size ) { return size <= 0 || RAND_bytes( pOut, size ) == 1; }

    bool OpenSSLNetSecurityProvider::makeX25519KeyPair( NetX25519KeyPair& outKeyPair )
    {
        EVP_PKEY* pKey = EVP_PKEY_Q_keygen( nullptr, nullptr, "X25519" );
        if ( pKey == nullptr )
            return false;
        size_t     publicSize  = NetSecurityConstant::kX25519KeySize;
        size_t     privateSize = NetSecurityConstant::kX25519KeySize;
        const bool bExtracted  = EVP_PKEY_get_raw_public_key( pKey, outKeyPair._arrPublicKey, &publicSize ) == 1 &&
                                EVP_PKEY_get_raw_private_key( pKey, outKeyPair._arrPrivateKey, &privateSize ) == 1;
        EVP_PKEY_free( pKey );
        return bExtracted;
    }

    bool OpenSSLNetSecurityProvider::computeX25519SharedSecret( const uint8* pPrivateKey, const uint8* pPeerPublicKey, uint8* pOutSecret )
    {
        EVP_PKEY*     pPrivate = EVP_PKEY_new_raw_private_key( EVP_PKEY_X25519, nullptr, pPrivateKey, NetSecurityConstant::kX25519KeySize );
        EVP_PKEY*     pPeer    = EVP_PKEY_new_raw_public_key( EVP_PKEY_X25519, nullptr, pPeerPublicKey, NetSecurityConstant::kX25519KeySize );
        EVP_PKEY_CTX* pContext = pPrivate != nullptr ? EVP_PKEY_CTX_new( pPrivate, nullptr ) : nullptr;
        size_t        size     = NetSecurityConstant::kX25519KeySize;
        bool          bDerived = pContext != nullptr && pPeer != nullptr && EVP_PKEY_derive_init( pContext ) == 1 && EVP_PKEY_derive_set_peer( pContext, pPeer ) == 1 &&
                        EVP_PKEY_derive( pContext, pOutSecret, &size ) == 1 && size == static_cast<size_t>( NetSecurityConstant::kX25519KeySize );
        // 작은 차수 점(위조한 공개 키)이면 공유 비밀이 0 이다 — OpenSSL 은 이미 실패로 돌려주지만 한 번 더 본다.
        uint8 accumulated = 0;
        for ( int32 index = 0; bDerived && index < NetSecurityConstant::kX25519KeySize; ++index )
        {
            accumulated |= pOutSecret[index];
        }
        bDerived = bDerived && accumulated != 0;
        if ( bDerived == false )
            ERR_clear_error();
        EVP_PKEY_CTX_free( pContext );
        EVP_PKEY_free( pPeer );
        EVP_PKEY_free( pPrivate );
        return bDerived;
    }

    bool OpenSSLNetSecurityProvider::computeHkdfSha256( const uint8* pSecret, int32 secretSize, const uint8* pSalt, int32 saltSize, const uint8* pInfo, int32 infoSize,
                                                        uint8* pOut, int32 outSize )
    {
        EVP_KDF*     pKdf     = EVP_KDF_fetch( nullptr, OSSL_KDF_NAME_HKDF, nullptr );
        EVP_KDF_CTX* pContext = pKdf != nullptr ? EVP_KDF_CTX_new( pKdf ) : nullptr;
        EVP_KDF_free( pKdf );
        if ( pContext == nullptr )
            return false;
        utf8       arrDigest[] = "SHA256";
        OSSL_PARAM arrParam[5];
        int32      paramCount  = 0;
        arrParam[paramCount++] = OSSL_PARAM_construct_utf8_string( OSSL_KDF_PARAM_DIGEST, arrDigest, 0 );
        arrParam[paramCount++] = OSSL_PARAM_construct_octet_string( OSSL_KDF_PARAM_KEY, const_cast<uint8*>( pSecret ), static_cast<size_t>( secretSize ) );
        if ( saltSize > 0 )
            arrParam[paramCount++] = OSSL_PARAM_construct_octet_string( OSSL_KDF_PARAM_SALT, const_cast<uint8*>( pSalt ), static_cast<size_t>( saltSize ) );
        if ( infoSize > 0 )
            arrParam[paramCount++] = OSSL_PARAM_construct_octet_string( OSSL_KDF_PARAM_INFO, const_cast<uint8*>( pInfo ), static_cast<size_t>( infoSize ) );
        arrParam[paramCount] = OSSL_PARAM_construct_end();
        const bool bDerived  = EVP_KDF_derive( pContext, pOut, static_cast<size_t>( outSize ), arrParam ) == 1;
        if ( bDerived == false )
            SW_LOG_ERROR( "HKDF-SHA256 failed: %#", OpenSSLNetSecurityInternal::takeErrorText().c_str() );
        EVP_KDF_CTX_free( pContext );
        return bDerived;
    }

    bool OpenSSLNetSecurityProvider::computePasswordHash( const uint8* pPassword, int32 passwordSize, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params,
                                                          uint8* pOut, int32 outSize )
    {
        // Argon2id 는 OpenSSL 3.2 부터(EVP_KDF "ARGON2ID"). 스레드는 1 — 서버가 요청마다 스레드를 늘리지 않게(병렬도는 레인 수로만, 결과는 스레드 수와 무관).
        EVP_KDF*     pKdf     = EVP_KDF_fetch( nullptr, "ARGON2ID", nullptr );
        EVP_KDF_CTX* pContext = pKdf != nullptr ? EVP_KDF_CTX_new( pKdf ) : nullptr;
        EVP_KDF_free( pKdf );
        if ( pContext == nullptr )
        {
            SW_LOG_ERROR( "Argon2id is not available in this OpenSSL build: %#", OpenSSLNetSecurityInternal::takeErrorText().c_str() );
            return false;
        }
        uint8      emptyPassword  = 0; // 빈 비밀번호에도 널이 아닌 포인터를 준다
        uint32     iterationCount = params._iterationCount;
        uint32     laneCount      = params._parallelism;
        uint32     memoryKiB      = params._memoryKiB;
        uint32     threadCount    = 1;
        uint8*     pPasswordBytes = passwordSize > 0 ? const_cast<uint8*>( pPassword ) : &emptyPassword;
        OSSL_PARAM arrParam[]     = { OSSL_PARAM_construct_octet_string( OSSL_KDF_PARAM_PASSWORD, pPasswordBytes, static_cast<size_t>( passwordSize > 0 ? passwordSize : 0 ) ),
                                      OSSL_PARAM_construct_octet_string( OSSL_KDF_PARAM_SALT, const_cast<uint8*>( pSalt ), static_cast<size_t>( saltSize ) ),
                                      OSSL_PARAM_construct_uint32( OSSL_KDF_PARAM_ITER, &iterationCount ),
                                      OSSL_PARAM_construct_uint32( OSSL_KDF_PARAM_THREADS, &threadCount ),
                                      OSSL_PARAM_construct_uint32( OSSL_KDF_PARAM_ARGON2_LANES, &laneCount ),
                                      OSSL_PARAM_construct_uint32( OSSL_KDF_PARAM_ARGON2_MEMCOST, &memoryKiB ),
                                      OSSL_PARAM_construct_end() };
        const bool bDerived       = EVP_KDF_derive( pContext, pOut, static_cast<size_t>( outSize ), arrParam ) == 1;
        if ( bDerived == false )
            SW_LOG_ERROR( "Argon2id failed: %#", OpenSSLNetSecurityInternal::takeErrorText().c_str() );
        EVP_KDF_CTX_free( pContext );
        return bDerived;
    }

    unique_ptr<INetAead> OpenSSLNetSecurityProvider::createAead( NetAeadAlgorithm algorithm, const uint8* pKey )
    {
        const EVP_CIPHER*       pCipher = algorithm == NetAeadAlgorithm::Aes256Gcm ? EVP_aes_256_gcm() : EVP_chacha20_poly1305();
        unique_ptr<OpenSSLAead> aead    = sw::make_unique<OpenSSLAead>( pCipher, pKey );
        if ( aead->isValid() == false )
        {
            SW_LOG_ERROR( "Could not create an AEAD context: %#", OpenSSLNetSecurityInternal::takeErrorText().c_str() );
            return nullptr;
        }
        return aead;
    }

    unique_ptr<ITLSContext> OpenSSLNetSecurityProvider::createTLSContext( const TLSContextSettings& settings, string& outError )
    {
        using Internal = OpenSSLNetSecurityInternal;
        ERR_clear_error();
        const bool bServer  = settings._role == TLSRole::Server;
        SSL_CTX*   pContext = SSL_CTX_new( bServer ? TLS_server_method() : TLS_client_method() );
        bool       bReady   = pContext != nullptr && SSL_CTX_set_min_proto_version( pContext, Internal::toOpenSSLVersion( settings._minVersion ) ) == 1 &&
                      SSL_CTX_set_max_proto_version( pContext, Internal::toOpenSSLVersion( settings._maxVersion ) ) == 1;
        if ( bReady && bServer )
        {
            vector<X509*> listCertificate = Internal::readCertificates( settings._certificatePem );
            BIO*          pKeyBio         = BIO_new_mem_buf( settings._privateKeyPem.data(), static_cast<int32>( settings._privateKeyPem.size() ) );
            EVP_PKEY*     pKey            = pKeyBio != nullptr ? PEM_read_bio_PrivateKey( pKeyBio, nullptr, &Internal::providePassphrase,
                                                                                          const_cast<string*>( &settings._privateKeyPassphrase ) )
                                                               : nullptr;
            BIO_free( pKeyBio );
            bReady = listCertificate.empty() == false && pKey != nullptr && SSL_CTX_use_certificate( pContext, listCertificate[0] ) == 1;
            for ( size_t index = 1; bReady && index < listCertificate.size(); ++index )
            {
                bReady = SSL_CTX_add1_chain_cert( pContext, listCertificate[index] ) == 1; // 중간 인증서 — add1 은 참조를 올린다
            }
            bReady = bReady && SSL_CTX_use_PrivateKey( pContext, pKey ) == 1 && SSL_CTX_check_private_key( pContext ) == 1;
            for ( X509* pCertificate : listCertificate )
            {
                X509_free( pCertificate );
            }
            EVP_PKEY_free( pKey );
        }
        else if ( bReady )
        {
            vector<X509*> listTrust = Internal::readCertificates( settings._trustPem );
            X509_STORE*   pStore    = SSL_CTX_get_cert_store( pContext );
            bReady                  = listTrust.empty() == false;
            for ( X509* pCertificate : listTrust )
            {
                bReady = bReady && X509_STORE_add_cert( pStore, pCertificate ) == 1;
                X509_free( pCertificate );
            }
            // 신뢰 목록에 든 끝 인증서(개발용 자체 서명 · 고정한 서버 인증서)를 CA 없이 그대로 믿는다.
            X509_VERIFY_PARAM_set_flags( SSL_CTX_get0_param( pContext ), X509_V_FLAG_PARTIAL_CHAIN );
            SSL_CTX_set_verify( pContext, SSL_VERIFY_PEER, nullptr );
        }
        if ( bReady == false )
        {
            outError = Internal::takeErrorText();
            SSL_CTX_free( pContext );
            return nullptr;
        }
        return sw::make_unique<OpenSSLTLSContext>( pContext, settings );
    }

    bool OpenSSLNetSecurityProvider::createSelfSignedCertificate( string_view commonName, int32 validDays, string& outCertificatePem, string& outPrivateKeyPem )
    {
        using Internal         = OpenSSLNetSecurityInternal;
        utf8      arrCurve[]   = "P-256"; // 가변 인자 keygen 이 const 가 아닌 문자열을 받는다
        EVP_PKEY* pKey         = EVP_PKEY_Q_keygen( nullptr, nullptr, "EC", arrCurve );
        X509*     pCertificate = X509_new();
        bool      bSigned      = pKey != nullptr && pCertificate != nullptr && X509_set_version( pCertificate, X509_VERSION_3 ) == 1;
        uint8     arrSerial[16];
        bSigned = bSigned && RAND_bytes( arrSerial, static_cast<int32>( sizeof( arrSerial ) ) ) == 1;
        arrSerial[0] &= 0x7F; // 양수
        BIGNUM* pSerial = bSigned ? BN_bin2bn( arrSerial, static_cast<int32>( sizeof( arrSerial ) ), nullptr ) : nullptr;
        bSigned         = bSigned && pSerial != nullptr && BN_to_ASN1_INTEGER( pSerial, X509_get_serialNumber( pCertificate ) ) != nullptr;
        BN_free( pSerial );
        bSigned = bSigned && X509_gmtime_adj( X509_getm_notBefore( pCertificate ), -60 ) != nullptr &&
                  X509_gmtime_adj( X509_getm_notAfter( pCertificate ), validDays * 24 * 60 * 60 ) != nullptr &&
                  X509_set_pubkey( pCertificate, pKey ) == 1;
        const string name( commonName );
        X509_NAME*   pName = bSigned ? X509_get_subject_name( pCertificate ) : nullptr;
        bSigned            = bSigned && X509_NAME_add_entry_by_txt( pName, "CN", MBSTRING_UTF8, reinterpret_cast<const uint8*>( name.c_str() ), -1, -1, 0 ) == 1 &&
                  X509_set_issuer_name( pCertificate, pName ) == 1;
        if ( bSigned )
        {
            X509V3_CTX extensionContext{}; // db 는 0(X509V3_set_ctx_nodb 와 같다)
            X509V3_set_ctx( &extensionContext, pCertificate, pCertificate, nullptr, nullptr, 0 );
            const string                 alternativeName = "DNS:" + name + ",DNS:localhost,IP:127.0.0.1";
            const Internal::ExtensionRow arrExtension[]  = {
                { NID_subject_alt_name,     alternativeName.c_str()},
                {NID_basic_constraints,         "critical,CA:FALSE"},
                {        NID_key_usage, "critical,digitalSignature"},
                {    NID_ext_key_usage,                "serverAuth"}
            };
            for ( const Internal::ExtensionRow& row : arrExtension )
            {
                X509_EXTENSION* pExtension = X509V3_EXT_conf_nid( nullptr, &extensionContext, row._nid, row._pValue );
                bSigned                    = bSigned && pExtension != nullptr && X509_add_ext( pCertificate, pExtension, -1 ) == 1;
                X509_EXTENSION_free( pExtension );
            }
        }
        bSigned = bSigned && X509_sign( pCertificate, pKey, EVP_sha256() ) > 0;
        if ( bSigned )
        {
            BIO* pCertificateBio = BIO_new( BIO_s_mem() );
            BIO* pKeyBio         = BIO_new( BIO_s_mem() );
            bSigned              = pCertificateBio != nullptr && pKeyBio != nullptr && PEM_write_bio_X509( pCertificateBio, pCertificate ) == 1 &&
                      PEM_write_bio_PrivateKey( pKeyBio, pKey, nullptr, nullptr, 0, nullptr, nullptr ) == 1;
            if ( bSigned )
            {
                BUF_MEM* pCertificateBuffer = nullptr;
                BUF_MEM* pKeyBuffer         = nullptr;
                BIO_get_mem_ptr( pCertificateBio, &pCertificateBuffer );
                BIO_get_mem_ptr( pKeyBio, &pKeyBuffer );
                outCertificatePem.assign( pCertificateBuffer->data, pCertificateBuffer->length );
                outPrivateKeyPem.assign( pKeyBuffer->data, pKeyBuffer->length );
            }
            BIO_free( pCertificateBio );
            BIO_free( pKeyBio );
        }
        if ( bSigned == false )
            SW_LOG_ERROR( "Could not create a self-signed certificate: %#", Internal::takeErrorText().c_str() );
        X509_free( pCertificate );
        EVP_PKEY_free( pKey );
        return bSigned;
    }

    bool OpenSSLNetSecurityProvider::computeCertificateSha256( const string& certificatePem, string& outHex )
    {
        vector<X509*> listCertificate = OpenSSLNetSecurityInternal::readCertificates( certificatePem );
        const bool    bComputed       = listCertificate.empty() == false && OpenSSLNetSecurityInternal::computeSha256Hex( listCertificate[0], outHex );
        for ( X509* pCertificate : listCertificate )
        {
            X509_free( pCertificate );
        }
        return bComputed;
    }

    bool OpenSSLNetSecurityProvider::computeSha256( const uint8* pData, int32 dataSize, uint8* pOutDigest )
    {
        if ( pOutDigest == nullptr || dataSize < 0 || ( pData == nullptr && dataSize > 0 ) )
            return false;
        uint32 digestSize = 0;
        return EVP_Digest( pData, static_cast<size_t>( dataSize ), pOutDigest, &digestSize, EVP_sha256(), nullptr ) == 1 && digestSize == 32;
    }

    bool OpenSSLNetSecurityProvider::verifySignature( const NetPublicKey& publicKey, const uint8* pData, int32 dataSize, const uint8* pSignature, int32 signatureSize )
    {
        using Internal = OpenSSLSignatureInternal;
        if ( pSignature == nullptr || signatureSize <= 0 || dataSize < 0 )
            return false;
        EVP_PKEY* pKey = Internal::createPublicKey( publicKey );
        if ( pKey == nullptr )
        {
            ERR_clear_error();
            return false;
        }
        const bool    bRsa      = publicKey._algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256;
        const bool    bStrongOk = bRsa == false || EVP_PKEY_get_bits( pKey ) >= Internal::kMinRsaBits;
        vector<uint8> derSignatureBytes;
        const bool    bShapeOk    = bRsa || Internal::convertRawToDer( pSignature, signatureSize, derSignatureBytes );
        const uint8*  pChecked    = bRsa ? pSignature : derSignatureBytes.data();
        const size_t  checkedSize = bRsa ? static_cast<size_t>( signatureSize ) : derSignatureBytes.size();
        EVP_MD_CTX*   pDigest     = EVP_MD_CTX_new();
        const bool    bVerified   = bStrongOk && bShapeOk && pDigest != nullptr && EVP_DigestVerifyInit( pDigest, nullptr, EVP_sha256(), nullptr, pKey ) == 1 &&
                               EVP_DigestVerify( pDigest, pChecked, checkedSize, pData, static_cast<size_t>( dataSize ) ) == 1;
        EVP_MD_CTX_free( pDigest );
        EVP_PKEY_free( pKey );
        ERR_clear_error(); // 틀린 서명은 정상 흐름이다 — 오류 큐를 남기지 않는다
        return bVerified;
    }

    bool OpenSSLNetSecurityProvider::createSigningKeyPair( NetSignatureAlgorithm algorithm, string& outPrivateKeyPem, NetPublicKey& outPublicKey )
    {
        using Internal          = OpenSSLSignatureInternal;
        outPublicKey            = NetPublicKey{};
        outPublicKey._algorithm = algorithm;
        utf8       arrCurve[]   = "P-256";
        const bool bRsa         = algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256;
        EVP_PKEY*  pKey         = nullptr;
        if ( bRsa )
            pKey = EVP_PKEY_Q_keygen( nullptr, nullptr, "RSA", static_cast<size_t>( Internal::kMinRsaBits ) );
        else
            pKey = EVP_PKEY_Q_keygen( nullptr, nullptr, "EC", arrCurve );
        bool bMade = pKey != nullptr;
        if ( bMade && algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256 )
        {
            bMade = Internal::readBignumParam( pKey, OSSL_PKEY_PARAM_RSA_N, outPublicKey._modulus ) &&
                    Internal::readBignumParam( pKey, OSSL_PKEY_PARAM_RSA_E, outPublicKey._exponent );
        }
        else if ( bMade )
        {
            uint8  arrPoint[1 + Internal::kEcCoordinateSize * 2];
            size_t pointSize = 0;
            bMade            = EVP_PKEY_get_octet_string_param( pKey, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, arrPoint, sizeof( arrPoint ), &pointSize ) == 1 &&
                    pointSize == sizeof( arrPoint ) && arrPoint[0] == 0x04;
            if ( bMade )
            {
                outPublicKey._x.assign( arrPoint + 1, arrPoint + 1 + Internal::kEcCoordinateSize );
                outPublicKey._y.assign( arrPoint + 1 + Internal::kEcCoordinateSize, arrPoint + sizeof( arrPoint ) );
            }
        }
        BIO* pKeyBio = bMade ? BIO_new( BIO_s_mem() ) : nullptr;
        bMade        = bMade && pKeyBio != nullptr && PEM_write_bio_PrivateKey( pKeyBio, pKey, nullptr, nullptr, 0, nullptr, nullptr ) == 1;
        if ( bMade )
        {
            BUF_MEM* pKeyBuffer = nullptr;
            BIO_get_mem_ptr( pKeyBio, &pKeyBuffer );
            outPrivateKeyPem.assign( pKeyBuffer->data, pKeyBuffer->length );
        }
        else
        {
            SW_LOG_ERROR( "Could not create a signing key pair: %#", OpenSSLNetSecurityInternal::takeErrorText().c_str() );
        }
        BIO_free( pKeyBio );
        EVP_PKEY_free( pKey );
        return bMade;
    }

    bool OpenSSLNetSecurityProvider::signData( NetSignatureAlgorithm algorithm, const string& privateKeyPem, const uint8* pData, int32 dataSize, vector<uint8>& outSignatureBytes )
    {
        using Internal = OpenSSLSignatureInternal;
        outSignatureBytes.clear();
        if ( dataSize < 0 || ( pData == nullptr && dataSize > 0 ) )
            return false;
        BIO*        pKeyBio    = BIO_new_mem_buf( privateKeyPem.data(), static_cast<int32>( privateKeyPem.size() ) );
        EVP_PKEY*   pKey       = pKeyBio != nullptr ? PEM_read_bio_PrivateKey( pKeyBio, nullptr, nullptr, nullptr ) : nullptr;
        const bool  bTypeOk    = pKey != nullptr && EVP_PKEY_is_a( pKey, algorithm == NetSignatureAlgorithm::RsaPkcs1Sha256 ? "RSA" : "EC" ) == 1;
        EVP_MD_CTX* pDigest    = EVP_MD_CTX_new();
        size_t      signedSize = 0;
        bool        bSigned    = bTypeOk && pDigest != nullptr && EVP_DigestSignInit( pDigest, nullptr, EVP_sha256(), nullptr, pKey ) == 1 &&
                       EVP_DigestSign( pDigest, nullptr, &signedSize, pData, static_cast<size_t>( dataSize ) ) == 1;
        vector<uint8> signature( signedSize, 0 );
        bSigned = bSigned && EVP_DigestSign( pDigest, signature.data(), &signedSize, pData, static_cast<size_t>( dataSize ) ) == 1;
        signature.resize( signedSize );
        if ( bSigned && algorithm == NetSignatureAlgorithm::EcdsaP256Sha256 )
            bSigned = Internal::convertDerToRaw( signature, outSignatureBytes );
        else if ( bSigned )
            outSignatureBytes = std::move( signature );
        if ( bSigned == false )
            SW_LOG_ERROR( "Could not sign: %#", OpenSSLNetSecurityInternal::takeErrorText().c_str() );
        EVP_MD_CTX_free( pDigest );
        EVP_PKEY_free( pKey );
        BIO_free( pKeyBio );
        return bSigned;
    }
} // namespace sw
