#include "pch.h"

#include "GameFramework/Kits/Online/Account/Api/LoopbackPkceLoginClient.h"

#include "Core/Network/NetTypes.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/String/Base64Util.h"
#include "Core/String/StringUtil.h"

#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "LoopbackPkceLoginClient" );

    namespace
    {
        struct LoopbackPkceLoginClientInternal
        {
            static constexpr int32 kVerifierBytes  = 32; ///< base64url 43 자 — RFC 7636 의 43..128
            static constexpr int32 kStateBytes     = 16;
            static constexpr utf8  kCallbackPath[] = "/callback";
            static constexpr utf8  kDonePage[]     = "<html><body>Sign-in finished. You can close this window and return to the game.</body></html>";

            static void setPage( HttpServerResponse& outResponse, int32 statusCode )
            {
                outResponse._statusCode = statusCode;
                outResponse._listHeader.push_back( HttpHeader{ "Content-Type", "text/html; charset=utf-8" } );
                outResponse._listHeader.push_back( HttpHeader{ "Cache-Control", "no-store" } );
                const string_view page{ kDonePage };
                outResponse._bodyBytes.assign( page.begin(), page.end() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LoopbackPkceLoginClient::LoopbackPkceLoginClient()
        : _redirectServer{}
        , _httpClient{}
        , _listProviderSettings{}
        , _listPending{}
        , _listDone{}
        , _pProvider{ nullptr }
        , _pBrowser{ nullptr }
        , _nowMs{ 0 }
        , _nextRequestId{ 1 }
    {
    }

    LoopbackPkceLoginClient::~LoopbackPkceLoginClient() { shutdown(); }

    bool LoopbackPkceLoginClient::initialize( unique_ptr<IStreamTransport> serverTransport, unique_ptr<IStreamTransport> clientTransport,
                                              const StreamTransportSettings& transportSettings, INetSecurityProvider* pProvider, IExternalBrowser* pBrowser,
                                              const vector<PkceLoginProviderSettings>& listProviderSettings )
    {
        if ( pProvider == nullptr || pBrowser == nullptr )
            return false;
        _pProvider            = pProvider;
        _pBrowser             = pBrowser;
        _listProviderSettings = listProviderSettings;
        HttpServerSettings serverSettings; // 127.0.0.1:0 — 다른 기계에서 닿지 않고 포트는 OS 가 고른다
        if ( _redirectServer.initialize( std::move( serverTransport ), transportSettings, serverSettings, this ) == false )
            return false;
        return _httpClient.initialize( std::move( clientTransport ), transportSettings, HttpClientSettings{} );
    }

    void LoopbackPkceLoginClient::shutdown()
    {
        for ( PendingLogin& pending : _listPending )
        {
            finish( pending, false, true, "client shut down", vector<uint8>{} );
        }
        _listPending.clear();
        _redirectServer.shutdown();
        _httpClient.shutdown();
    }

    uint64 LoopbackPkceLoginClient::beginLogin( string_view provider, int64 nowMs )
    {
        using Internal         = LoopbackPkceLoginClientInternal;
        const uint64 requestId = _nextRequestId++;
        PendingLogin pending;
        pending._requestId                         = requestId;
        pending._provider                          = string( provider );
        const PkceLoginProviderSettings* pSettings = findSettings( provider );
        const bool                       bSecrets  = pSettings != nullptr && makeRandomText( Internal::kVerifierBytes, pending._codeVerifier ) &&
                              makeRandomText( Internal::kStateBytes, pending._state ) && makeRandomText( Internal::kStateBytes, pending._nonce );
        uint8      arrChallenge[NetSecurityConstant::kSha256Size];
        const bool bChallenge = bSecrets && _pProvider->computeSha256( reinterpret_cast<const uint8*>( pending._codeVerifier.data() ),
                                                                       static_cast<int32>( pending._codeVerifier.size() ), arrChallenge );
        if ( bChallenge == false )
        {
            finish( pending, false, false, pSettings == nullptr ? "unknown provider" : "could not make PKCE secrets", vector<uint8>{} );
            return requestId;
        }
        vector<HttpHeader> listQuery;
        listQuery.push_back( HttpHeader{ "response_type", "code" } );
        listQuery.push_back( HttpHeader{ "client_id", pSettings->_clientId } );
        listQuery.push_back( HttpHeader{ "redirect_uri", makeRedirectUri() } );
        listQuery.push_back( HttpHeader{ "scope", pSettings->_scope } );
        listQuery.push_back( HttpHeader{ "state", pending._state } );
        listQuery.push_back( HttpHeader{ "nonce", pending._nonce } );
        listQuery.push_back( HttpHeader{ "code_challenge", Base64Util::encodeUrl( arrChallenge, sizeof( arrChallenge ) ) } );
        listQuery.push_back( HttpHeader{ "code_challenge_method", "S256" } );
        const bool   bHasQuery = pSettings->_authorizationUrl.find( '?' ) != string::npos;
        const string url       = pSettings->_authorizationUrl + ( bHasQuery ? "&" : "?" ) + HttpUtil::encodeForm( listQuery );
        if ( _pBrowser->openUrl( url ) == false )
        {
            finish( pending, false, false, "could not open the system browser", vector<uint8>{} );
            return requestId;
        }
        pending._deadlineMs = nowMs + pSettings->_timeoutMs;
        _listPending.push_back( std::move( pending ) );
        return requestId;
    }

    void LoopbackPkceLoginClient::tick( int64 nowMs )
    {
        _nowMs = nowMs;
        _redirectServer.tick();
        _httpClient.tick( nowMs );
        vector<HttpClientResponse> listResponse;
        (void)_httpClient.pollResponses( listResponse );
        for ( const HttpClientResponse& response : listResponse )
        {
            for ( size_t pendingIndex = 0; pendingIndex < _listPending.size(); ++pendingIndex )
            {
                PendingLogin& pending = _listPending[pendingIndex];
                if ( pending._tokenRequestId != response._requestId )
                    continue;
                const PkceLoginProviderSettings* pSettings = findSettings( pending._provider );
                JsonDocument                     document;
                const bool                       bParsed = response.isSuccess() && document.tryParse( response.getBodyText() ) && document.getRoot().isObject();
                const JsonValue                  token   = bParsed ? document.getRoot().get( pSettings->_bUseIdToken == SW_TRUE ? "id_token" : "access_token", false )
                                                                   : JsonValue{};
                if ( token.isString() == false || token.asString().empty() )
                {
                    finish( pending, false, false, response._bTransportFailed == SW_TRUE ? response._failureText : string( "token endpoint refused the code" ), vector<uint8>{} );
                }
                else
                {
                    string ticketText = token.asString();
                    if ( pSettings->_bUseIdToken == SW_TRUE )
                        ticketText += "|" + pending._nonce;
                    finish( pending, true, false, "", vector<uint8>( ticketText.begin(), ticketText.end() ) );
                }
                _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
                break;
            }
        }
        for ( size_t pendingIndex = 0; pendingIndex < _listPending.size(); )
        {
            PendingLogin& pending = _listPending[pendingIndex];
            if ( nowMs < pending._deadlineMs || pending._tokenRequestId != 0 )
            {
                ++pendingIndex;
                continue;
            }
            finish( pending, false, true, "sign-in timed out", vector<uint8>{} );
            _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
        }
    }

    int32 LoopbackPkceLoginClient::pollResults( vector<PlatformLoginClientResult>& outListResult )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        for ( PlatformLoginClientResult& result : _listDone )
        {
            outListResult.push_back( std::move( result ) );
        }
        _listDone.clear();
        return count;
    }

    void LoopbackPkceLoginClient::onHttpRequest( const HttpServerRequest& request, HttpServerResponse& outResponse )
    {
        using Internal = LoopbackPkceLoginClientInternal;
        if ( request._method != HttpMethod::Get || request._path != Internal::kCallbackPath )
        {
            outResponse._statusCode = HttpConstant::kStatusNotFound;
            return;
        }
        const HttpHeader* pState = request.findQuery( "state" );
        PendingLogin*     pFound = nullptr;
        for ( PendingLogin& pending : _listPending )
        {
            if ( pState != nullptr && pending._tokenRequestId == 0 && pending._state == pState->_value )
                pFound = &pending;
        }
        if ( pFound == nullptr )
        {
            // 모르는 state — 위조이거나 이미 끝난 로그인. 아무 로그인도 바꾸지 않는다.
            outResponse._statusCode = 400;
            return;
        }
        Internal::setPage( outResponse, HttpConstant::kStatusOk );
        const HttpHeader*                pError    = request.findQuery( "error" );
        const HttpHeader*                pCode     = request.findQuery( "code" );
        const PkceLoginProviderSettings* pSettings = findSettings( pFound->_provider );
        if ( pError != nullptr || pCode == nullptr || pCode->_value.empty() || pSettings == nullptr )
        {
            const bool   bDenied = pError != nullptr && pError->_value == "access_denied";
            const string reason  = pError != nullptr ? "provider returned " + pError->_value : string( "redirect carried no code" );
            for ( size_t pendingIndex = 0; pendingIndex < _listPending.size(); ++pendingIndex )
            {
                if ( &_listPending[pendingIndex] != pFound )
                    continue;
                finish( *pFound, false, bDenied, reason, vector<uint8>{} );
                _listPending.erase( _listPending.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
                break;
            }
            return;
        }
        vector<HttpHeader> listForm;
        listForm.push_back( HttpHeader{ "grant_type", "authorization_code" } );
        listForm.push_back( HttpHeader{ "code", pCode->_value } );
        listForm.push_back( HttpHeader{ "redirect_uri", makeRedirectUri() } );
        listForm.push_back( HttpHeader{ "client_id", pSettings->_clientId } );
        listForm.push_back( HttpHeader{ "code_verifier", pFound->_codeVerifier } );
        HttpClientRequest tokenRequest;
        tokenRequest._method = HttpMethod::Post;
        tokenRequest._url    = pSettings->_tokenUrl;
        const string body    = HttpUtil::encodeForm( listForm );
        tokenRequest._bodyBytes.assign( body.begin(), body.end() );
        tokenRequest._listHeader.push_back( HttpHeader{ "Content-Type", "application/x-www-form-urlencoded" } );
        tokenRequest._listHeader.push_back( HttpHeader{ "Accept", "application/json" } );
        pFound->_tokenRequestId = _httpClient.submitRequest( tokenRequest, _nowMs );
    }

    const PkceLoginProviderSettings* LoopbackPkceLoginClient::findSettings( string_view provider ) const
    {
        for ( const PkceLoginProviderSettings& settings : _listProviderSettings )
        {
            if ( settings._provider == provider )
                return &settings;
        }
        return nullptr;
    }

    bool LoopbackPkceLoginClient::makeRandomText( int32 byteCount, string& outText )
    {
        vector<uint8> bytes( static_cast<size_t>( byteCount ), 0 );
        if ( _pProvider->fillRandomBytes( bytes.data(), byteCount ) == false )
            return false;
        outText = Base64Util::encodeUrl( bytes.data(), bytes.size() );
        return true;
    }

    void LoopbackPkceLoginClient::finish( PendingLogin& pending, bool bSucceeded, bool bCancelled, string_view failureText, vector<uint8> ticketBytes )
    {
        PlatformLoginClientResult& result = _listDone.emplace_back();
        result._requestId                 = pending._requestId;
        result._provider                  = pending._provider;
        result._ticket                    = std::move( ticketBytes );
        result._failureText               = string( failureText );
        result._bSucceeded                = bSucceeded ? SW_TRUE : SW_FALSE;
        result._bCancelled                = bCancelled ? SW_TRUE : SW_FALSE;
        if ( bSucceeded == false )
            SW_LOG_WARNING( "external sign-in with '%#' did not finish: %#", pending._provider.c_str(), result._failureText.c_str() );
    }

    string LoopbackPkceLoginClient::makeRedirectUri() const
    {
        return "http://127.0.0.1:" + to_string( static_cast<int32>( _redirectServer.getListenPort() ) ) + LoopbackPkceLoginClientInternal::kCallbackPath;
    }
} // namespace sw
