#include "pch.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"

namespace sw
{
    namespace
    {
        struct LedgerTypesInternal
        {
            static constexpr int32 kMaxEscrowDomainSize = 16;
            static constexpr int32 kMaxEscrowTokenSize  = 64;
            static constexpr int32 kMaxScopeSize        = 32;
            static constexpr int32 kMaxStoredTextSize   = 512;
            static constexpr utf8  kHexDigit[]          = "0123456789abcdef";

            static bool isLowerOrDigit( utf8 ch ) { return ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ); }

            /** @brief 글자가 모두 [0-9a-z] 또는 @p pExtra 에 든 것이고 길이가 1..@p maxSize 인가입니다. */
            static bool isTokenText( string_view text, int32 maxSize, const utf8* pExtra )
            {
                if ( text.empty() || text.size() > static_cast<size_t>( maxSize ) )
                    return false;
                for ( const utf8 ch : text )
                {
                    if ( isLowerOrDigit( ch ) )
                        continue;
                    bool bExtra = false;
                    for ( const utf8* pCursor = pExtra; *pCursor != '\0'; ++pCursor )
                    {
                        if ( *pCursor == ch )
                        {
                            bExtra = true;
                            break;
                        }
                    }
                    if ( bExtra == false )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LedgerHolder LedgerHolder::makeAccount( uint64 accountId )
    {
        LedgerHolder holder;
        holder._kind      = LedgerHolderKind::Account;
        holder._accountId = accountId;
        return holder;
    }

    LedgerHolder LedgerHolder::makeEscrow( string_view domain, string_view token )
    {
        LedgerHolder holder;
        holder._kind         = LedgerHolderKind::Escrow;
        holder._escrowDomain = string( domain );
        holder._escrowToken  = string( token );
        return holder;
    }

    LedgerHolder LedgerHolder::makeMint()
    {
        LedgerHolder holder;
        holder._kind = LedgerHolderKind::Mint;
        return holder;
    }

    LedgerHolder LedgerHolder::makeSink()
    {
        LedgerHolder holder;
        holder._kind = LedgerHolderKind::Sink;
        return holder;
    }

    bool LedgerHolder::isValid() const
    {
        switch ( _kind )
        {
            case LedgerHolderKind::Account:
                return _accountId != 0;
            case LedgerHolderKind::Escrow:
                return LedgerUtil::isValidEscrowDomain( _escrowDomain ) && LedgerUtil::isValidEscrowToken( _escrowToken );
            case LedgerHolderKind::Mint:
            case LedgerHolderKind::Sink:
                return true;
            case LedgerHolderKind::Count:
                return false;
        }
        return false;
    }

    string LedgerHolder::makeKey() const
    {
        string key;
        switch ( _kind )
        {
            case LedgerHolderKind::Account:
            {
                key = "acct/";
                ServiceKeyUtil::appendHex64( key, _accountId );
                break;
            }
            case LedgerHolderKind::Escrow:
            {
                key = "esc/";
                key += _escrowDomain;
                key.push_back( '/' );
                key += _escrowToken;
                break;
            }
            case LedgerHolderKind::Mint:
            {
                key = "sys/mint";
                break;
            }
            case LedgerHolderKind::Sink:
            {
                key = "sys/sink";
                break;
            }
            case LedgerHolderKind::Count:
            {
                break;
            }
        }
        return key;
    }

    bool LedgerHolder::operator==( const LedgerHolder& other ) const
    {
        if ( _kind != other._kind )
            return false;
        switch ( _kind )
        {
            case LedgerHolderKind::Account:
                return _accountId == other._accountId;
            case LedgerHolderKind::Escrow:
                return _escrowDomain == other._escrowDomain && _escrowToken == other._escrowToken;
            case LedgerHolderKind::Mint:
            case LedgerHolderKind::Sink:
            case LedgerHolderKind::Count:
                return true;
        }
        return true;
    }

    const utf8* toString( LedgerResult result )
    {
        switch ( result )
        {
            case LedgerResult::Ok:
                return "Ok";
            case LedgerResult::InsufficientFunds:
                return "InsufficientFunds";
            case LedgerResult::CapExceeded:
                return "CapExceeded";
            case LedgerResult::JournalKeyReused:
                return "JournalKeyReused";
            case LedgerResult::Conflict:
                return "Conflict";
            case LedgerResult::Invalid:
                return "Invalid";
            case LedgerResult::Unavailable:
                return "Unavailable";
        }
        return "Unknown";
    }

    string LedgerJournalKey::makeFromIdempotency( string_view scope, uint64 keyHigh, uint64 keyLow )
    {
        string key{ scope };
        key.push_back( '/' );
        ServiceKeyUtil::appendHex64( key, keyHigh );
        ServiceKeyUtil::appendHex64( key, keyLow );
        return key;
    }

    bool LedgerJournalKey::makeFromToken( string_view scope, string_view token, string& outKey )
    {
        outKey.clear();
        const bool bScopeOk = LedgerTypesInternal::isTokenText( scope, LedgerTypesInternal::kMaxScopeSize, "_." );
        const bool bTokenOk = token.empty() == false && token.size() <= static_cast<size_t>( LedgerConstant::kMaxTokenBytes );
        if ( bScopeOk == false || bTokenOk == false )
            return false;
        outKey.reserve( scope.size() + 1 + token.size() * 2 );
        outKey += scope;
        outKey.push_back( '/' );
        for ( const utf8 ch : token )
        {
            const uint8 byteValue = static_cast<uint8>( ch );
            outKey.push_back( LedgerTypesInternal::kHexDigit[byteValue >> 4] );
            outKey.push_back( LedgerTypesInternal::kHexDigit[byteValue & 0xFu] );
        }
        return true;
    }

    string LedgerJournalKey::makeAccountScope( uint64 accountId )
    {
        string scope{ "acct." };
        ServiceKeyUtil::appendHex64( scope, accountId );
        return scope;
    }

    string LedgerJournalKey::makeAdminScope( uint64 adminAccountId )
    {
        string scope{ "gm." };
        ServiceKeyUtil::appendHex64( scope, adminAccountId );
        return scope;
    }

    bool LedgerUtil::isValidAssetId( string_view assetId ) { return LedgerTypesInternal::isTokenText( assetId, LedgerConstant::kMaxAssetIdSize, "_.-" ); }

    bool LedgerUtil::isValidReasonCode( string_view reason ) { return LedgerTypesInternal::isTokenText( reason, LedgerConstant::kMaxReasonSize, "_." ); }

    bool LedgerUtil::isValidEscrowDomain( string_view domain ) { return LedgerTypesInternal::isTokenText( domain, LedgerTypesInternal::kMaxEscrowDomainSize, "_" ); }

    bool LedgerUtil::isValidEscrowToken( string_view token ) { return LedgerTypesInternal::isTokenText( token, LedgerTypesInternal::kMaxEscrowTokenSize, "_.-" ); }

    bool LedgerUtil::isValidJournalKey( string_view journalKey )
    {
        if ( journalKey.empty() )
            return false;
        const bool bHasScope = journalKey.find( '/' ) != string_view::npos && journalKey.front() != '/' && journalKey.back() != '/';
        return bHasScope && LedgerTypesInternal::isTokenText( journalKey, LedgerConstant::kMaxJournalKeySize, "_./-" );
    }

    void LedgerUtil::writeHolder( BitWriter& outWriter, const LedgerHolder& holder )
    {
        outWriter.writeVarUint( static_cast<uint64>( holder._kind ) );
        if ( holder._kind == LedgerHolderKind::Account )
        {
            outWriter.writeVarUint( holder._accountId );
        }
        else if ( holder._kind == LedgerHolderKind::Escrow )
        {
            ServiceKeyUtil::writeString( outWriter, holder._escrowDomain );
            ServiceKeyUtil::writeString( outWriter, holder._escrowToken );
        }
    }

    bool LedgerUtil::readHolder( BitReader& reader, LedgerHolder& outHolder )
    {
        const uint64 kind = reader.readVarUint();
        if ( kind >= static_cast<uint64>( LedgerHolderKind::Count ) )
            return false;
        outHolder       = LedgerHolder{};
        outHolder._kind = static_cast<LedgerHolderKind>( kind );
        if ( outHolder._kind == LedgerHolderKind::Account )
        {
            outHolder._accountId = reader.readVarUint();
        }
        else if ( outHolder._kind == LedgerHolderKind::Escrow )
        {
            if ( ServiceKeyUtil::readString( reader, LedgerTypesInternal::kMaxStoredTextSize, outHolder._escrowDomain ) == false )
                return false;
            if ( ServiceKeyUtil::readString( reader, LedgerTypesInternal::kMaxStoredTextSize, outHolder._escrowToken ) == false )
                return false;
        }
        return reader.hasOverflowed() == false && outHolder.isValid();
    }

    void LedgerUtil::writePosting( BitWriter& outWriter, const LedgerPosting& posting )
    {
        writeHolder( outWriter, posting._from );
        writeHolder( outWriter, posting._to );
        ServiceKeyUtil::writeString( outWriter, posting._assetId );
        outWriter.writeVarInt( posting._amount );
    }

    bool LedgerUtil::readPosting( BitReader& reader, LedgerPosting& outPosting )
    {
        if ( readHolder( reader, outPosting._from ) == false || readHolder( reader, outPosting._to ) == false )
            return false;
        if ( ServiceKeyUtil::readString( reader, LedgerConstant::kMaxAssetIdSize, outPosting._assetId ) == false )
            return false;
        outPosting._amount = reader.readVarInt();
        return reader.hasOverflowed() == false;
    }
} // namespace sw
