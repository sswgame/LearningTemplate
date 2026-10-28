#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatSpamGuard.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatWordFilter.h"

namespace sw
{
    ChatSpamGuard::ChatSpamGuard()
        : _bucketMap{}
        , _mapAccountToHistory{}
        , _settings{}
    {
    }

    void ChatSpamGuard::initialize( const ChatSpamConfig& settings )
    {
        _settings = settings;
        _bucketMap.initialize( settings._burstCount, settings._refillIntervalMs, static_cast<size_t>( settings._maxTrackedAccount ) );
        _mapAccountToHistory.clear();
    }

    uint64 ChatSpamGuard::computeTextHash( string_view text )
    {
        // 코드 포인트 단위 FNV-1a 이다. 글자 하나가 정규화로 사라지거나(끼움 글자) 바뀌어도(대소문자, 전각) 같은 값이 된다.
        uint64 hash   = HashUtil::kFNVOffset64;
        size_t offset = 0;
        while ( offset < text.size() )
        {
            const uint32 codepoint = ChatWordFilter::normalizeCodepoint( StringUtil::decodeUTF8( text, offset ) );
            if ( codepoint == 0 )
                continue;
            hash = HashUtil::fnvStep64( hash, codepoint );
        }
        return hash;
    }

    ChatSpamVerdict ChatSpamGuard::check( AccountID accountID, string_view text, int64 nowMs, int64& outRetryAfterMs )
    {
        const uint64 hash        = computeTextHash( text );
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
            return ChatSpamVerdict::Repeated;
        }
        if ( _bucketMap.tryConsume( accountID, nowMs, outRetryAfterMs ) == false )
            return ChatSpamVerdict::RateLimited;
        SentText sent;
        sent._hash   = hash;
        sent._timeMs = nowMs;
        history.push( sent );
        outRetryAfterMs = 0;
        return ChatSpamVerdict::Allowed;
    }
} // namespace sw
