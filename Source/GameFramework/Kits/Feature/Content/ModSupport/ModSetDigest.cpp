#include "pch.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModSetDigest.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Net/GameNetSession.h"

namespace sw
{
    namespace
    {
        struct ModSetDigestInternal
        {
            static constexpr size_t kHashTextLength = 16; ///< 해시 글(16 진수 16 자리)
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
        GameNetWireContent wireContent;
        wireContent._contentConfigHash = _listEntry.empty() ? uint64{ 0 } : _configHash;
        return GameNetSession::combineWireContent( wireVersion, wireContent );
    }

    string ModSetDigest::describeDifference( const ModSetDigest& other ) const
    {
        for ( const ModDigestEntry& theirs : other._listEntry )
        {
            const ModDigestEntry* pMine = findEntry( theirs._id );
            if ( pMine == nullptr )
            {
                StringBuilder<constant::buffer::kMax256> resultBuilder3;
                resultBuilder3.appendFormat( "mod '%#' %# is missing here", theirs._id, theirs._version.toString() );
                return string( resultBuilder3.view() );
            }
            if ( pMine->_version != theirs._version )
            {
                StringBuilder<constant::buffer::kMax256> resultBuilder2;
                resultBuilder2.appendFormat( "mod '%#' is %# here but %# there", theirs._id, pMine->_version.toString(), theirs._version.toString() );
                return string( resultBuilder2.view() );
            }
            if ( pMine->_contentHash != theirs._contentHash )
            {
                StringBuilder<constant::buffer::kMax256> resultBuilder;
                resultBuilder.appendFormat( "mod '%#' %# has different content here", theirs._id, theirs._version.toString() );
                return string( resultBuilder.view() );
            }
        }
        for ( const ModDigestEntry& mine : _listEntry )
        {
            if ( other.findEntry( mine._id ) == nullptr )
            {
                StringBuilder<constant::buffer::kMax256> resultBuilder;
                resultBuilder.appendFormat( "mod '%#' %# is only here", mine._id, mine._version.toString() );
                return string( resultBuilder.view() );
            }
        }
        if ( _configHash != other._configHash )
            return string( "the same mods are mounted in a different order" );
        return {};
    }

    bool ModSetDigest::validateSaveCompatible( const ModSetDigest& saved ) const
    {
        for ( const ModDigestEntry& savedEntry : saved._listEntry )
        {
            const ModDigestEntry* pActive = findEntry( savedEntry._id );
            if ( pActive == nullptr )
            {
                // 빠진 모드는 알리고 불러온다 — 콘텐츠 전용 모드라 그 리소스를 가리키던 것은 게임이 없는 것으로 다룬다(벨모라 확장 지역 → 일어설 자리).
                // 상용(Bethesda Creation · Paradox · 마인크래프트)도 경고 뒤 불러온다. 같은 id 의 다른 판은 내용이 달라 아래에서 거절한다.
                SW_LOG_WARNING_ID( "ModSetDigest.SaveModMissing", "The save was made with mod '%#' %#, which is not enabled - loading without it (its content is gone from this save)",
                                   savedEntry._id, savedEntry._version.toString() );
                continue;
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
        StringBuilder<constant::buffer::kMax1024> text;
        for ( const ModDigestEntry& entry : _listEntry )
        {
            // 해시는 16 진수 16 자리(소문자)다.
            text.appendFormat( "%#%#@%##%016llx", text.size() == 0 ? "" : ";", entry._id, entry._version.toString(), static_cast<unsigned long long>( entry._contentHash ) );
        }
        return string( text.view() );
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
        return VectorUtil::findBy<&ModDigestEntry::_id>( _listEntry, id );
    }
} // namespace sw
