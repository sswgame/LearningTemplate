#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatSpamGuard.h"

#include "Core/String/StringUtil.h"

namespace sw
{
    ChatSpamGuard::ChatSpamGuard()
        : _bucketMap{}
        , _mapAccountToHistory{}
        , _settings{}
    {
    }

    void ChatSpamGuard::initialize( const ChatSpamConfig& settings, const ChatTextNormalizer& normalizer )
    {
        _settings   = settings;
        _normalizer = normalizer;
        _bucketMap.initialize( settings._burstCount, settings._refillIntervalMs, static_cast<size_t>( settings._maxTrackedAccount ) );
        _mapAccountToHistory.clear();
    }

    ChatSpamResult ChatSpamGuard::check( AccountID accountID, string_view text, int64 nowMs, int64& outRetryAfterMs )
    {
        const uint64 hash        = _normalizer.computeTextHash( text );
        History&     history     = _mapAccountToHistory[accountID];
        int32        repeatCount = 0;
        for ( const SentText& sent : history )
        {
            const bool bRecent = nowMs - sent._timeMs < _settings._repeatWindowMs;
            repeatCount += ( bRecent && sent._hash == hash ) ? 1 : 0;
        }
        if ( repeatCount >= _settings._maxRepeatCount )
        {
            outRetryAfterMs = _settings._repeatWindowMs;
            return ChatSpamResult::Repeated;
        }
        if ( _bucketMap.tryConsume( accountID, nowMs, outRetryAfterMs ) == false )
            return ChatSpamResult::RateLimited;
        SentText sent;
        sent._hash   = hash;
        sent._timeMs = nowMs;
        history.push( sent );
        outRetryAfterMs = 0;
        return ChatSpamResult::Allowed;
    }
} // namespace sw
