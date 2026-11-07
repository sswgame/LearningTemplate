#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModSetDigest.h"

#include "Core/Container/StringUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Net/GameNetSession.h"

namespace sw
{
    namespace
    {
        struct ModSetDigestInternal
        {
            static constexpr size_t kHashTextLength = 16; ///< 해시 글(16 진수 16 자리)

            /** @brief @p value 의 16 진수 16 자리(소문자)입니다. */
            static string formatHash( uint64 value )
            {
                static constexpr const utf8* kDigits = "0123456789abcdef";
                string                       text( kHashTextLength, '0' );
                for ( size_t index = 0; index < kHashTextLength; ++index )
                {
                    text[kHashTextLength - 1 - index] = kDigits[( value >> ( index * 4 ) ) & 0xFu];
                }
                return text;
            }

            static string formatEntry( const ModDigestEntry& entry )
            {
                return entry._id + "@" + entry._version.toString() + "#" + formatHash( entry._contentHash );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ModSetDigest ModSetDigest::make( vector<ModDigestEntry> listEntry )
    {
        ModSetDigest digest;
        digest._listEntry = std::move( listEntry );
        if ( digest._listEntry.empty() )
            return digest;
        uint64 hash = HashUtil::kFNVOffset64;
        for ( const ModDigestEntry& entry : digest._listEntry )
        {
            hash = HashUtil::combine( hash, HashUtil::computeStringHash64( entry._id, false ) );
            hash = HashUtil::combine( hash, entry._version.toOrderKey() );
            hash = HashUtil::combine( hash, entry._contentHash );
        }
        digest._configHash = hash != 0 ? hash : 1; // 0 은 빈 구성의 값이다
        return digest;
    }

    uint32 ModSetDigest::foldWireVersion( uint32 wireVersion ) const
    {
        // 섞는 규칙은 넷 세션 한 곳이다 — 게임 인스턴스가 세션을 열 때 이 구성 해시(`IContentConfiguration`)를 같은 함수로 섞는다.
        return GameNetSession::combineContentConfigHash( wireVersion, _listEntry.empty() ? uint64{ 0 } : _configHash );
    }

    string ModSetDigest::describeDifference( const ModSetDigest& other ) const
    {
        for ( const ModDigestEntry& theirs : other._listEntry )
        {
            const ModDigestEntry* pMine = findEntry( theirs._id );
            if ( pMine == nullptr )
                return formatToString( "mod '%#' %# is missing here", theirs._id, theirs._version.toString() );
            if ( pMine->_version != theirs._version )
                return formatToString( "mod '%#' is %# here but %# there", theirs._id, pMine->_version.toString(), theirs._version.toString() );
            if ( pMine->_contentHash != theirs._contentHash )
                return formatToString( "mod '%#' %# has different content here", theirs._id, theirs._version.toString() );
        }
        for ( const ModDigestEntry& mine : _listEntry )
        {
            if ( other.findEntry( mine._id ) == nullptr )
                return formatToString( "mod '%#' %# is only here", mine._id, mine._version.toString() );
        }
        if ( _configHash != other._configHash )
            return string( "the same mods are mounted in a different order" );
        return {};
    }

    bool ModSetDigest::checkSaveCompatible( const ModSetDigest& saved ) const
    {
        for ( const ModDigestEntry& savedEntry : saved._listEntry )
        {
            const ModDigestEntry* pActive = findEntry( savedEntry._id );
            if ( pActive == nullptr )
            {
                SW_LOG_ERROR_ID( "ModSetDigest.SaveNeedsMod", "The save was made with mod '%#' %#, which is not enabled - enable it to load this save", savedEntry._id,
                                 savedEntry._version.toString() );
                return false;
            }
            if ( pActive->_version != savedEntry._version )
            {
                SW_LOG_ERROR_ID( "ModSetDigest.SaveModVersion", "The save was made with mod '%#' %#, but %# is enabled", savedEntry._id, savedEntry._version.toString(),
                                 pActive->_version.toString() );
                return false;
            }
        }
        return true;
    }

    string ModSetDigest::toText() const
    {
        string text;
        for ( const ModDigestEntry& entry : _listEntry )
        {
            if ( text.empty() == false )
                text += ';';
            text += ModSetDigestInternal::formatEntry( entry );
        }
        return text;
    }

    bool ModSetDigest::parseText( string_view text, ModSetDigest& outDigest )
    {
        vector<ModDigestEntry> listEntry;
        for ( const string_view part : StringUtil::split( text, ";", StringSplitOptions{ false, true } ) )
        {
            const size_t   at   = part.find( '@' );
            const size_t   hash = part.find( '#' );
            ModDigestEntry entry;
            const bool     bShaped = at != string_view::npos && hash != string_view::npos && at < hash && part.size() - hash - 1 == ModSetDigestInternal::kHashTextLength;
            const bool     bRead   = bShaped && ModManifest::isValidID( part.substr( 0, at ) ) && ModVersion::parse( part.substr( at + 1, hash - at - 1 ), entry._version ) &&
                               StringUtil::parseUint64( part.substr( hash + 1 ), entry._contentHash, 16 );
            if ( bRead == false )
            {
                SW_LOG_ERROR_ID( "ModSetDigest.BadText", "Mod set entry '%#' is not id@major.minor.patch#hash", part );
                return false;
            }
            entry._id = string{ part.substr( 0, at ) };
            listEntry.push_back( std::move( entry ) );
        }
        outDigest = make( std::move( listEntry ) );
        return true;
    }

    const ModDigestEntry* ModSetDigest::findEntry( string_view id ) const
    {
        for ( const ModDigestEntry& entry : _listEntry )
        {
            if ( entry._id == id )
                return &entry;
        }
        return nullptr;
    }
} // namespace sw
