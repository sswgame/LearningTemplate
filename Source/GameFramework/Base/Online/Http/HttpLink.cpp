#include "pch.h"

#include "GameFramework/Base/Online/Http/HttpLink.h"

#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Network/Transport/IStreamTransport.h"

namespace sw
{
    HttpLink::HttpLink()
        : _cipherBytes{}
        , _tlsSession{}
        , _handle{}
    {
    }

    HttpLink::~HttpLink() = default;

    bool HttpLink::initialize( ITlsContext* pTlsContext )
    {
        _tlsSession.reset();
        if ( pTlsContext == nullptr )
            return true;
        _tlsSession = pTlsContext->createSession();
        return _tlsSession != nullptr;
    }

    bool HttpLink::writePlain( IStreamTransport& transport, const uint8* pData, size_t size )
    {
        if ( _tlsSession == nullptr )
        {
            const StreamSendResult result = transport.send( _handle, pData, static_cast<int32>( size ) );
            return result == StreamSendResult::Queued || result == StreamSendResult::QueuedAboveHighWatermark;
        }
        if ( _tlsSession->writePlaintext( pData, static_cast<int32>( size ) ) == false )
            return false;
        return flushCiphertext( transport );
    }

    bool HttpLink::readReceived( IStreamTransport& transport, const uint8* pData, int32 size, vector<uint8>& outPlainBytes )
    {
        if ( _tlsSession == nullptr )
        {
            outPlainBytes.insert( outPlainBytes.end(), pData, pData + size );
            return true;
        }
        const bool bFed = _tlsSession->feedCiphertext( pData, size );
        (void)flushCiphertext( transport ); // 핸드셰이크 답 · 세션 표
        const bool bRead = bFed && _tlsSession->getState() != TlsSessionState::Failed && _tlsSession->readPlaintext( outPlainBytes );
        (void)flushCiphertext( transport ); // 핸드셰이크가 끝나며 내보낸 모아 둔 평문
        return bRead;
    }

    bool HttpLink::flushHandshake( IStreamTransport& transport ) { return _tlsSession == nullptr || flushCiphertext( transport ); }

    const utf8* HttpLink::getFailureText() const { return _tlsSession != nullptr ? _tlsSession->getFailureText() : "stream failure"; }

    bool HttpLink::flushCiphertext( IStreamTransport& transport )
    {
        _cipherBytes.clear();
        _tlsSession->takeCiphertext( _cipherBytes );
        if ( _cipherBytes.empty() )
            return true;
        const StreamSendResult result = transport.send( _handle, _cipherBytes.data(), static_cast<int32>( _cipherBytes.size() ) );
        return result == StreamSendResult::Queued || result == StreamSendResult::QueuedAboveHighWatermark;
    }
} // namespace sw
