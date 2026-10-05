#include "pch.h"

#include "Engine/Network/OpenSsl/OpenSslNetSecurityProvider.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

#include <cstring>
#include <openssl/bio.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace sw
{
    SW_LOG_CALLER( "OpenSslNetSecurity" );

    namespace
    {
        struct OpenSslNetSecurityInternal
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

            static int32 toOpenSslVersion( TlsVersion version ) { return version == TlsVersion::Tls12 ? TLS1_2_VERSION : TLS1_3_VERSION; }

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
        class OpenSslAead final : public INetAead
        {
        public:
            OpenSslAead( const EVP_CIPHER* pCipher, const uint8* pKey )
                : _pEncrypt{ EVP_CIPHER_CTX_new() }
                , _pDecrypt{ EVP_CIPHER_CTX_new() }
                , _bValid{ SW_FALSE }
            {
                const bool bReady = _pEncrypt != nullptr && _pDecrypt != nullptr && EVP_EncryptInit_ex( _pEncrypt, pCipher, nullptr, pKey, nullptr ) == 1 &&
                                    EVP_DecryptInit_ex( _pDecrypt, pCipher, nullptr, pKey, nullptr ) == 1;
                _bValid = bReady ? SW_TRUE : SW_FALSE;
            }
            ~OpenSslAead() override
            {
                EVP_CIPHER_CTX_free( _pEncrypt );
                EVP_CIPHER_CTX_free( _pDecrypt );
            }

            OpenSslAead( const OpenSslAead& )            = delete;
            OpenSslAead& operator=( const OpenSslAead& ) = delete;

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
        class OpenSslTlsSession final : public ITlsSession
        {
        public:
            OpenSslTlsSession( SSL_CTX* pContext, TlsRole role, const string& serverName, const string& pinnedSha256Hex )
                : _pendingBytes{}
                , _pinnedSha256Hex{ pinnedSha256Hex }
                , _failureText{}
                , _pSsl{ SSL_new( pContext ) }
                , _pReadBio{ BIO_new( BIO_s_mem() ) }
                , _pWriteBio{ BIO_new( BIO_s_mem() ) }
                , _state{ TlsSessionState::Handshaking }
            {
                if ( _pSsl == nullptr || _pReadBio == nullptr || _pWriteBio == nullptr )
                {
                    (void)fail( "SSL_new failed" );
                    return;
                }
                SSL_set_bio( _pSsl, _pReadBio, _pWriteBio ); // BIO 는 SSL 이 소유한다
                if ( role == TlsRole::Server )
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

            ~OpenSslTlsSession() override
            {
                if ( _pSsl != nullptr )
                {
                    SSL_free( _pSsl );
                    return;
                }
                BIO_free( _pReadBio ); // SSL 에 넘기지 못한 BIO
                BIO_free( _pWriteBio );
            }

            OpenSslTlsSession( const OpenSslTlsSession& )            = delete;
            OpenSslTlsSession& operator=( const OpenSslTlsSession& ) = delete;

            TlsSessionState getState() const override { return _state; }
            const utf8*     getFailureText() const override { return _failureText.c_str(); }

            [[nodiscard]] bool feedCiphertext( const uint8* pData, int32 size ) override
            {
                if ( _state == TlsSessionState::Failed )
                    return false;
                if ( size > 0 && BIO_write( _pReadBio, pData, size ) != size )
                    return fail( "BIO_write failed" );
                if ( _state == TlsSessionState::Handshaking )
                    advanceHandshake();
                return _state != TlsSessionState::Failed;
            }

            [[nodiscard]] bool readPlaintext( vector<uint8>& outBytes ) override
            {
                if ( _state != TlsSessionState::Established )
                    return _state != TlsSessionState::Failed;
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
                        _state = TlsSessionState::Closed; // 저쪽 close_notify
                        return true;
                    }
                    return fail( OpenSslNetSecurityInternal::takeErrorText() ); // 변조된 레코드는 여기(bad record mac)
                }
            }

            [[nodiscard]] bool writePlaintext( const uint8* pData, int32 size ) override
            {
                if ( _state == TlsSessionState::Handshaking )
                {
                    _pendingBytes.insert( _pendingBytes.end(), pData, pData + size );
                    return true;
                }
                if ( _state != TlsSessionState::Established )
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
                if ( _state == TlsSessionState::Established )
                    (void)SSL_shutdown( _pSsl );
                if ( _state != TlsSessionState::Failed )
                    _state = TlsSessionState::Closed;
            }

        private:
            bool fail( const string& text )
            {
                _state       = TlsSessionState::Failed;
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
                        return fail( OpenSslNetSecurityInternal::takeErrorText() );
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
                    _state = TlsSessionState::Established;
                    if ( _pendingBytes.empty() == false )
                    {
                        (void)writeEstablished( _pendingBytes.data(), static_cast<int32>( _pendingBytes.size() ) );
                        _pendingBytes.clear();
                    }
                    return;
                }
                const int32 error = SSL_get_error( _pSsl, result );
                if ( error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE )
                    return;
                const bool bVerifyFailed = SSL_get_verify_result( _pSsl ) != X509_V_OK;
                (void)fail( bVerifyFailed ? string( X509_verify_cert_error_string( SSL_get_verify_result( _pSsl ) ) ) : OpenSslNetSecurityInternal::takeErrorText() );
                ERR_clear_error();
            }

            bool matchesPin() const
            {
                X509* pPeer = SSL_get1_peer_certificate( _pSsl );
                if ( pPeer == nullptr )
                    return false;
                string     hex;
                const bool bComputed = OpenSslNetSecurityInternal::computeSha256Hex( pPeer, hex );
                X509_free( pPeer );
                return bComputed && hex == _pinnedSha256Hex;
            }

            vector<uint8>   _pendingBytes;
            string          _pinnedSha256Hex;
            string          _failureText;
            SSL*            _pSsl;
            BIO*            _pReadBio;
            BIO*            _pWriteBio;
            TlsSessionState _state;
        };

        class OpenSslTlsContext final : public ITlsContext
        {
        public:
            OpenSslTlsContext( SSL_CTX* pContext, const TlsContextSettings& settings )
                : _serverName{ settings._serverName }
                , _pinnedSha256Hex{ settings._pinnedCertificateSha256Hex }
                , _pContext{ pContext }
                , _role{ settings._role }
            {
            }
            ~OpenSslTlsContext() override { SSL_CTX_free( _pContext ); }

            OpenSslTlsContext( const OpenSslTlsContext& )            = delete;
            OpenSslTlsContext& operator=( const OpenSslTlsContext& ) = delete;

            unique_ptr<ITlsSession> createSession() override { return sw::make_unique<OpenSslTlsSession>( _pContext, _role, _serverName, _pinnedSha256Hex ); }
            TlsRole                 getRole() const override { return _role; }

        private:
            string   _serverName;
            string   _pinnedSha256Hex;
            SSL_CTX* _pContext;
            TlsRole  _role;
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool OpenSslNetSecurityProvider::fillRandomBytes( uint8* pOut, int32 size ) { return size <= 0 || RAND_bytes( pOut, size ) == 1; }

    bool OpenSslNetSecurityProvider::makeX25519KeyPair( NetX25519KeyPair& outKeyPair )
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

    bool OpenSslNetSecurityProvider::computeX25519SharedSecret( const uint8* pPrivateKey, const uint8* pPeerPublicKey, uint8* pOutSecret )
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

    bool OpenSslNetSecurityProvider::computeHkdfSha256( const uint8* pSecret, int32 secretSize, const uint8* pSalt, int32 saltSize, const uint8* pInfo, int32 infoSize,
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
            SW_LOG_ERROR( "HKDF-SHA256 failed: %#", OpenSslNetSecurityInternal::takeErrorText().c_str() );
        EVP_KDF_CTX_free( pContext );
        return bDerived;
    }

    bool OpenSslNetSecurityProvider::computePasswordHash( const uint8* pPassword, int32 passwordSize, const uint8* pSalt, int32 saltSize, const NetPasswordHashParams& params,
                                                          uint8* pOut, int32 outSize )
    {
        // Argon2id 는 OpenSSL 3.2 부터(EVP_KDF "ARGON2ID"). 스레드는 1 — 서버가 요청마다 스레드를 늘리지 않게(병렬도는 레인 수로만, 결과는 스레드 수와 무관).
        EVP_KDF*     pKdf     = EVP_KDF_fetch( nullptr, "ARGON2ID", nullptr );
        EVP_KDF_CTX* pContext = pKdf != nullptr ? EVP_KDF_CTX_new( pKdf ) : nullptr;
        EVP_KDF_free( pKdf );
        if ( pContext == nullptr )
        {
            SW_LOG_ERROR( "Argon2id is not available in this OpenSSL build: %#", OpenSslNetSecurityInternal::takeErrorText().c_str() );
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
            SW_LOG_ERROR( "Argon2id failed: %#", OpenSslNetSecurityInternal::takeErrorText().c_str() );
        EVP_KDF_CTX_free( pContext );
        return bDerived;
    }

    unique_ptr<INetAead> OpenSslNetSecurityProvider::createAead( NetAeadAlgorithm algorithm, const uint8* pKey )
    {
        const EVP_CIPHER*       pCipher = algorithm == NetAeadAlgorithm::Aes256Gcm ? EVP_aes_256_gcm() : EVP_chacha20_poly1305();
        unique_ptr<OpenSslAead> aead    = sw::make_unique<OpenSslAead>( pCipher, pKey );
        if ( aead->isValid() == false )
        {
            SW_LOG_ERROR( "Could not create an AEAD context: %#", OpenSslNetSecurityInternal::takeErrorText().c_str() );
            return nullptr;
        }
        return aead;
    }

    unique_ptr<ITlsContext> OpenSslNetSecurityProvider::createTlsContext( const TlsContextSettings& settings, string& outError )
    {
        using Internal = OpenSslNetSecurityInternal;
        ERR_clear_error();
        const bool bServer  = settings._role == TlsRole::Server;
        SSL_CTX*   pContext = SSL_CTX_new( bServer ? TLS_server_method() : TLS_client_method() );
        bool       bReady   = pContext != nullptr && SSL_CTX_set_min_proto_version( pContext, Internal::toOpenSslVersion( settings._minVersion ) ) == 1 &&
                      SSL_CTX_set_max_proto_version( pContext, Internal::toOpenSslVersion( settings._maxVersion ) ) == 1;
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
        return sw::make_unique<OpenSslTlsContext>( pContext, settings );
    }

    bool OpenSslNetSecurityProvider::createSelfSignedCertificate( string_view commonName, int32 validDays, string& outCertificatePem, string& outPrivateKeyPem )
    {
        using Internal         = OpenSslNetSecurityInternal;
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

    bool OpenSslNetSecurityProvider::computeCertificateSha256( const string& certificatePem, string& outHex )
    {
        vector<X509*> listCertificate = OpenSslNetSecurityInternal::readCertificates( certificatePem );
        const bool    bComputed       = listCertificate.empty() == false && OpenSslNetSecurityInternal::computeSha256Hex( listCertificate[0], outHex );
        for ( X509* pCertificate : listCertificate )
        {
            X509_free( pCertificate );
        }
        return bComputed;
    }
} // namespace sw
