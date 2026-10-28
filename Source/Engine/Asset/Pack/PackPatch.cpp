#include "pch.h"

#include "Engine/Asset/Pack/PackPatch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Hash/SecureUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    SW_LOG_CALLER( "PackPatch" );

    namespace
    {
        struct PackPatchInternal
        {
            static constexpr const utf8* kRootName      = "PackPatch";
            static constexpr const utf8* kRemovedName   = "Removed";
            static constexpr const utf8* kRecordRoot    = "PackBuild";
            static constexpr const utf8* kEntryName     = "Entry";
            static constexpr const utf8* kPatchingChain = "chain";
            static constexpr const utf8* kPatchingNone  = "none";
            static constexpr int32       kHashDigits    = 16; ///< 내용 해시의 16 진수 자리 수(64 비트)

            /** @brief 16 자리 16 진수 글을 64 비트 값으로 읽습니다. 자리 수나 글자가 틀리면 false 입니다. */
            [[nodiscard]] static bool parseHash( string_view text, uint64& outHash )
            {
                return text.size() == static_cast<size_t>( kHashDigits ) && StringUtil::parseUint64( text, outHash, 16 );
            }

            static string makeHashText( uint64 hash )
            {
                utf8 arrDigit[kHashDigits];
                const auto [pEnd, error] = std::to_chars( arrDigit, arrDigit + kHashDigits, hash, 16 );
                (void)error; // 16 자리 버퍼에 64 비트가 들어간다
                const size_t digitCount = static_cast<size_t>( pEnd - arrDigit );
                string       text( static_cast<size_t>( kHashDigits ) - digitCount, '0' );
                text.append( arrDigit, digitCount );
                return text;
            }

            static constexpr const utf8* kPayloadMagic       = "SWPackPatch1";
            static constexpr size_t      kSHA256HexDigits    = static_cast<size_t>( NetSecurityConstant::kSHA256Size ) * 2;
            static constexpr size_t      kSignatureSize      = 64; ///< ES256 은 r ‖ s(각 32 바이트)
            static constexpr size_t      kSignatureHexDigits = kSignatureSize * 2;
            static constexpr const utf8* kHexDigits          = "0123456789abcdef";

            static string toHex( const uint8* pBytes, size_t size )
            {
                string text;
                text.reserve( size * 2 );
                for ( size_t index = 0; index < size; ++index )
                {
                    text.push_back( kHexDigits[pBytes[index] >> 4] );
                    text.push_back( kHexDigits[pBytes[index] & 0x0F] );
                }
                return text;
            }

            /** @brief 소문자 16 진 글을 바이트로 읽습니다. 홀수 길이나 소문자 16 진이 아닌 글자가 있으면 false 입니다. */
            [[nodiscard]] static bool fromHex( string_view text, vector<uint8>& outBytes )
            {
                outBytes.clear();
                if ( text.size() % 2 != 0 )
                    return false;
                const auto digitValue = []( utf8 letter ) -> int32
                {
                    if ( letter >= '0' && letter <= '9' )
                        return letter - '0';
                    if ( letter >= 'a' && letter <= 'f' )
                        return letter - 'a' + 10;
                    return -1;
                };
                for ( size_t index = 0; index < text.size(); index += 2 )
                {
                    const int32 high = digitValue( text[index] );
                    const int32 low  = digitValue( text[index + 1] );
                    if ( high < 0 || low < 0 )
                        return false;
                    outBytes.push_back( static_cast<uint8>( high * 16 + low ) );
                }
                return true;
            }

            /** @brief 목록의 글 필드에 줄바꿈이 있으면 true 입니다(서명 글의 필드 경계가 흐려진다). */
            static bool hasLineBreakField( const PackPatchManifest& manifest )
            {
                const auto hasBreak = []( const string& text )
                { return text.find_first_of( "\r\n" ) != string::npos; };
                const bool bBuildBreak = hasBreak( manifest._fromBuild ) || hasBreak( manifest._toBuild );
                const bool bPackBreak  = hasBreak( manifest._packFileName ) || hasBreak( manifest._packSHA256Hex );
                if ( bBuildBreak || bPackBreak )
                    return true;
                for ( const string& removedPath : manifest._listRemovedPath )
                {
                    if ( hasBreak( removedPath ) )
                        return true;
                }
                return false;
            }

            /** @brief 팩 바이트의 SHA-256 을 소문자 16 진으로 셉니다. 제공자의 해시가 32 비트 크기를 받아 2 GiB 이상은 거절합니다. */
            [[nodiscard]] static bool computePackSHA256Hex( INetSecurityProvider& provider, const vector<uint8>& packBytes, string& outHex )
            {
                if ( packBytes.size() > static_cast<size_t>( MathUtil::kMaxInt32 ) )
                {
                    SW_LOG_ERROR( "Pack patch pack is %# bytes - signing and checking take packs under 2 GiB", packBytes.size() );
                    return false;
                }
                uint8 arrDigest[NetSecurityConstant::kSHA256Size]{};
                if ( provider.computeSHA256( packBytes.data(), static_cast<int32>( packBytes.size() ), arrDigest ) == false )
                {
                    SW_LOG_ERROR( "Pack patch pack hash could not be computed" );
                    return false;
                }
                outHex = toHex( arrDigest, sizeof( arrDigest ) );
                return true;
            }

            /** @brief 경로 → 내용 해시 표를 만듭니다. 같은 경로가 두 번이면 오류를 남기고 false 입니다. */
            static bool makePathTable( const vector<PackContentEntry>& listEntry, const utf8* pWhich, unordered_map<string, uint64>& outMapPathToHash )
            {
                for ( const PackContentEntry& entry : listEntry )
                {
                    if ( outMapPathToHash.emplace( entry._path, entry._contentHash ).second == false )
                    {
                        SW_LOG_ERROR( "Pack delta: the %# build lists '%#' twice - cannot tell which content is right", pWhich, entry._path );
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace

    bool PackPatchUtil::computeDelta( const vector<PackContentEntry>& listBase, const vector<PackContentEntry>& listTarget, PackDelta& outDelta )
    {
        outDelta = PackDelta{};
        unordered_map<string, uint64> mapBase;
        unordered_map<string, uint64> mapTarget;
        if ( PackPatchInternal::makePathTable( listBase, "base", mapBase ) == false || PackPatchInternal::makePathTable( listTarget, "target", mapTarget ) == false )
            return false;
        for ( const PackContentEntry& entry : listTarget )
        {
            const auto baseIt = mapBase.find( entry._path );
            if ( baseIt == mapBase.end() || baseIt->second != entry._contentHash )
                outDelta._listChangedPath.push_back( entry._path );
        }
        for ( const PackContentEntry& entry : listBase )
        {
            if ( mapTarget.find( entry._path ) == mapTarget.end() )
                outDelta._listRemovedPath.push_back( entry._path );
        }
        std::sort( outDelta._listChangedPath.begin(), outDelta._listChangedPath.end() );
        std::sort( outDelta._listRemovedPath.begin(), outDelta._listRemovedPath.end() );
        return true;
    }

    bool PackPatchUtil::parseManifest( string_view xmlText, string_view sourceName, PackPatchManifest& outManifest )
    {
        outManifest = PackPatchManifest{};
        XMLDocument document;
        if ( document.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "Pack patch manifest is not valid XML: %#", document.getLastError() );
            return false;
        }
        const XMLNode root = document.getRoot( PackPatchInternal::kRootName );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Pack patch manifest %# has no <%#> root", sourceName, PackPatchInternal::kRootName );
            return false;
        }
        const std::optional<string> fromBuild     = root.readRequiredAttribute<string>( "from" );
        const std::optional<string> toBuild       = root.readRequiredAttribute<string>( "to" );
        const std::optional<string> packFile      = root.readRequiredAttribute<string>( "pack" );
        const bool                  bMissingField = fromBuild.has_value() == false || toBuild.has_value() == false || packFile.has_value() == false;
        if ( bMissingField )
            return false; // 읽기가 원소 · 속성 이름으로 오류를 남겼다
        if ( *fromBuild == *toBuild )
        {
            SW_LOG_ERROR( "Pack patch manifest %# goes from build '%#' to itself", sourceName, *fromBuild );
            return false;
        }
        // 패치 팩은 목록 파일과 같은 폴더의 파일이다 — 폴더를 벗어나는 이름을 받지 않는다.
        const bool bUnsafePackName = packFile->empty() || packFile->find_first_of( "/\\" ) != string::npos || *packFile == "..";
        if ( bUnsafePackName )
        {
            SW_LOG_ERROR( "Pack patch manifest %# names pack '%#' - it must be a plain file name next to the manifest", sourceName, *packFile );
            return false;
        }
        // 팩 해시와 서명은 함께 있거나 함께 없다 — 한쪽만 있으면 서명이 무엇을 묶는지 알 수 없다.
        std::optional<string> packSHA256;
        std::optional<string> signature;
        if ( root.readOptionalAttribute<string>( "packSHA256", packSHA256 ) == false || root.readOptionalAttribute<string>( "signature", signature ) == false )
            return false;
        if ( packSHA256.has_value() != signature.has_value() )
        {
            SW_LOG_ERROR( "Pack patch manifest %# has only one of packSHA256 and signature - a signed patch needs both", sourceName );
            return false;
        }
        if ( packSHA256.has_value() )
        {
            vector<uint8> listDecoded;
            if ( packSHA256->size() != PackPatchInternal::kSHA256HexDigits || PackPatchInternal::fromHex( *packSHA256, listDecoded ) == false )
            {
                SW_LOG_ERROR( "Pack patch manifest %#: packSHA256 must be %# lowercase hex digits", sourceName, PackPatchInternal::kSHA256HexDigits );
                return false;
            }
            if ( signature->size() != PackPatchInternal::kSignatureHexDigits || PackPatchInternal::fromHex( *signature, listDecoded ) == false )
            {
                SW_LOG_ERROR( "Pack patch manifest %#: signature must be %# lowercase hex digits", sourceName, PackPatchInternal::kSignatureHexDigits );
                return false;
            }
            outManifest._packSHA256Hex = *packSHA256;
            outManifest._signatureHex  = *signature;
        }
        outManifest._fromBuild    = *fromBuild;
        outManifest._toBuild      = *toBuild;
        outManifest._packFileName = *packFile;
        for ( XMLNode removed = root.findChild( PackPatchInternal::kRemovedName ); removed.isValid(); removed = removed.findNextSibling( PackPatchInternal::kRemovedName ) )
        {
            const std::optional<string> path = removed.readRequiredAttribute<string>( "path" );
            if ( path.has_value() == false )
                return false;
            outManifest._listRemovedPath.push_back( *path );
        }
        return true;
    }

    string PackPatchUtil::makeManifestText( const PackPatchManifest& manifest )
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( PackPatchInternal::kRootName );
        root.appendAttribute( "from", string_view{ manifest._fromBuild } );
        root.appendAttribute( "to", string_view{ manifest._toBuild } );
        root.appendAttribute( "pack", string_view{ manifest._packFileName } );
        if ( manifest._signatureHex.empty() == false )
        {
            root.appendAttribute( "packSHA256", string_view{ manifest._packSHA256Hex } );
            root.appendAttribute( "signature", string_view{ manifest._signatureHex } );
        }
        for ( const string& removedPath : manifest._listRemovedPath )
        {
            root.appendChild( PackPatchInternal::kRemovedName ).appendAttribute( "path", string_view{ removedPath } );
        }
        return document.saveToString();
    }

    uint64 PackPatchUtil::computeEntryHash( const vector<uint8>& bytes )
    {
        return HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
    }

    bool PackPatchUtil::parseBuildRecord( string_view xmlText, string_view sourceName, PackBuildRecord& outRecord )
    {
        outRecord = PackBuildRecord{};
        XMLDocument document;
        if ( document.parse( xmlText, sourceName ) == false )
        {
            SW_LOG_ERROR( "Pack build record is not valid XML: %#", document.getLastError() );
            return false;
        }
        const XMLNode root = document.getRoot( PackPatchInternal::kRecordRoot );
        if ( root.isValid() == false )
        {
            SW_LOG_ERROR( "Pack build record %# has no <%#> root", sourceName, PackPatchInternal::kRecordRoot );
            return false;
        }
        const std::optional<string> patching = root.readRequiredAttribute<string>( "patching" );
        if ( patching.has_value() == false )
            return false; // 옛 기록(patching 이 없는 것)은 다시 굽는다 — 없는 값을 chain 으로 보지 않는다
        if ( *patching == PackPatchInternal::kPatchingChain )
            outRecord._patching = PackPatching::Chain;
        else if ( *patching == PackPatchInternal::kPatchingNone )
            outRecord._patching = PackPatching::None;
        else
        {
            SW_LOG_ERROR( "Pack build record %#: patching '%#' is neither '%#' nor '%#'", sourceName, *patching, PackPatchInternal::kPatchingChain, PackPatchInternal::kPatchingNone );
            return false;
        }
        if ( outRecord._patching == PackPatching::None )
        {
            if ( root.findChild( PackPatchInternal::kEntryName ).isValid() || root.findAttribute( "build" ) != nullptr )
            {
                SW_LOG_ERROR( "Pack build record %#: patching '%#' carries no build number or entries", sourceName, PackPatchInternal::kPatchingNone );
                return false;
            }
            return true;
        }
        const std::optional<string> build = root.readRequiredAttribute<string>( "build" );
        if ( build.has_value() == false )
            return false;
        outRecord._build = *build;
        unordered_map<string, uint64> mapPathToHash;
        for ( XMLNode entry = root.findChild( PackPatchInternal::kEntryName ); entry.isValid(); entry = entry.findNextSibling( PackPatchInternal::kEntryName ) )
        {
            const std::optional<string> path     = entry.readRequiredAttribute<string>( "path" );
            const std::optional<string> hashText = entry.readRequiredAttribute<string>( "hash" );
            if ( path.has_value() == false || hashText.has_value() == false )
                return false;
            PackContentEntry content;
            content._path = *path;
            if ( PackPatchInternal::parseHash( *hashText, content._contentHash ) == false )
            {
                SW_LOG_ERROR( "Pack build record %#: '%#' has hash '%#' - need %# hex digits", sourceName, *path, *hashText, PackPatchInternal::kHashDigits );
                return false;
            }
            if ( mapPathToHash.emplace( content._path, content._contentHash ).second == false )
            {
                SW_LOG_ERROR( "Pack build record %# lists '%#' twice - cannot tell which content is right", sourceName, *path );
                return false;
            }
            outRecord._listEntry.push_back( std::move( content ) );
        }
        return true;
    }

    string PackPatchUtil::makeBuildRecordText( const PackBuildRecord& record )
    {
        vector<const PackContentEntry*> listSorted;
        listSorted.reserve( record._listEntry.size() );
        for ( const PackContentEntry& entry : record._listEntry )
        {
            listSorted.push_back( &entry );
        }
        std::sort( listSorted.begin(), listSorted.end(), []( const PackContentEntry* pLeft, const PackContentEntry* pRight )
        { return pLeft->_path < pRight->_path; } );
        XMLDocument   document;
        const XMLNode root = document.appendRoot( PackPatchInternal::kRecordRoot );
        root.appendAttribute( "patching", string_view{ record._patching == PackPatching::None ? PackPatchInternal::kPatchingNone : PackPatchInternal::kPatchingChain } );
        if ( record._patching == PackPatching::None )
            return document.saveToString(); // 패치를 쓰지 않는 빌드는 번호도 내용 목록도 갖지 않는다
        root.appendAttribute( "build", string_view{ record._build } );
        for ( const PackContentEntry* pEntry : listSorted )
        {
            const XMLNode entryNode = root.appendChild( PackPatchInternal::kEntryName );
            entryNode.appendAttribute( "path", string_view{ pEntry->_path } );
            entryNode.appendAttribute( "hash", string_view{ PackPatchInternal::makeHashText( pEntry->_contentHash ) } );
        }
        return document.saveToString();
    }

    bool PackPatchUtil::orderPatchChainToLatest( string_view installedBuild, const vector<PackPatchManifest>& listManifest, vector<size_t>& outListOrder, string& outLatestBuild )
    {
        outListOrder.clear();
        outLatestBuild = string( installedBuild );
        while ( true )
        {
            size_t nextIndex  = listManifest.size();
            int32  startCount = 0;
            for ( size_t manifestIndex = 0; manifestIndex < listManifest.size(); ++manifestIndex )
            {
                if ( listManifest[manifestIndex]._fromBuild != outLatestBuild )
                    continue;
                nextIndex = manifestIndex;
                ++startCount;
            }
            if ( startCount == 0 )
                break;
            if ( startCount > 1 )
            {
                SW_LOG_ERROR( "Pack patch chain forks: %# patches start at build '%#'", startCount, outLatestBuild );
                return false;
            }
            if ( outListOrder.size() >= listManifest.size() )
            {
                SW_LOG_ERROR( "Pack patch chain from build '%#' loops back to '%#'", installedBuild, outLatestBuild );
                return false;
            }
            outListOrder.push_back( nextIndex );
            outLatestBuild = listManifest[nextIndex]._toBuild;
        }
        for ( size_t manifestIndex = 0; manifestIndex < listManifest.size(); ++manifestIndex )
        {
            if ( std::find( outListOrder.begin(), outListOrder.end(), manifestIndex ) != outListOrder.end() )
                continue;
            SW_LOG_ERROR( "Pack patch from build '%#' to '%#' (%#) cannot be reached from the installed build '%#' - a patch in between is missing", listManifest[manifestIndex]._fromBuild,
                          listManifest[manifestIndex]._toBuild, listManifest[manifestIndex]._packFileName, installedBuild );
            return false;
        }
        return true;
    }

    string PackPatchUtil::makeSigningPayload( const PackPatchManifest& manifest )
    {
        // 줄 하나 = `이름=값\n`. `operator+` 대신 append 로 잇는다(sw::string 은 std::string 과 섞어 더하지 않는다).
        string     payload;
        const auto appendLine = [&payload]( string_view name, string_view value )
        {
            payload.append( name );
            payload.append( "=" );
            payload.append( value );
            payload.append( "\n" );
        };
        payload.append( PackPatchInternal::kPayloadMagic );
        payload.append( "\n" );
        appendLine( "from", string_view{ manifest._fromBuild } );
        appendLine( "to", string_view{ manifest._toBuild } );
        appendLine( "pack", string_view{ manifest._packFileName } );
        appendLine( "packSHA256", string_view{ manifest._packSHA256Hex } );
        utf8 arrCount[24];
        const auto [pCountEnd, countError] = std::to_chars( arrCount, arrCount + sizeof( arrCount ), manifest._listRemovedPath.size() );
        (void)countError; // 24 칸에 64 비트 십진수가 들어간다
        appendLine( "removedCount", string_view{ arrCount, static_cast<size_t>( pCountEnd - arrCount ) } );
        for ( const string& removedPath : manifest._listRemovedPath )
        {
            appendLine( "removed", string_view{ removedPath } );
        }
        return payload;
    }

    bool PackPatchUtil::signManifest( INetSecurityProvider& provider, const string& privateKeyPem, const vector<uint8>& packBytes, PackPatchManifest& inOutManifest )
    {
        string packSHA256Hex;
        if ( PackPatchInternal::computePackSHA256Hex( provider, packBytes, packSHA256Hex ) == false )
            return false;
        inOutManifest._packSHA256Hex = packSHA256Hex;
        inOutManifest._signatureHex.clear();
        if ( PackPatchInternal::hasLineBreakField( inOutManifest ) )
        {
            SW_LOG_ERROR( "Pack patch %# -> %# has a line break in a field - it cannot be signed", inOutManifest._fromBuild, inOutManifest._toBuild );
            return false;
        }
        const string  payload = makeSigningPayload( inOutManifest );
        vector<uint8> listSignature;
        if ( provider.signData( NetSignatureAlgorithm::EcdsaP256Sha256, privateKeyPem, reinterpret_cast<const uint8*>( payload.data() ), static_cast<int32>( payload.size() ), listSignature ) == false )
        {
            SW_LOG_ERROR( "Pack patch %# -> %# could not be signed - check the private key is an ES256 (P-256) PKCS#8 PEM", inOutManifest._fromBuild, inOutManifest._toBuild );
            return false;
        }
        if ( listSignature.size() != PackPatchInternal::kSignatureSize )
        {
            SW_LOG_ERROR( "Pack patch signature is %# bytes - ES256 gives %#", listSignature.size(), PackPatchInternal::kSignatureSize );
            return false;
        }
        inOutManifest._signatureHex = PackPatchInternal::toHex( listSignature.data(), listSignature.size() );
        return true;
    }

    bool PackPatchUtil::verifyManifest( INetSecurityProvider& provider, const NetPublicKey& publicKey, const vector<uint8>& packBytes, const PackPatchManifest& manifest,
                                        string_view sourceName )
    {
        if ( manifest._signatureHex.empty() || manifest._packSHA256Hex.empty() )
        {
            SW_LOG_ERROR( "Pack patch manifest %# is not signed but a signature is required", sourceName );
            return false;
        }
        if ( publicKey._algorithm != NetSignatureAlgorithm::EcdsaP256Sha256 )
        {
            SW_LOG_ERROR( "Pack patch signing key for %# is not an ES256 (P-256) key", sourceName );
            return false;
        }
        if ( PackPatchInternal::hasLineBreakField( manifest ) )
        {
            SW_LOG_ERROR( "Pack patch manifest %# has a line break in a field", sourceName );
            return false;
        }
        string actualSHA256Hex;
        if ( PackPatchInternal::computePackSHA256Hex( provider, packBytes, actualSHA256Hex ) == false )
            return false;
        if ( SecureUtil::equalsConstantTime( actualSHA256Hex, manifest._packSHA256Hex ) == false )
        {
            SW_LOG_ERROR( "Pack patch pack %# does not match the SHA-256 in %# - the pack was changed after signing", manifest._packFileName, sourceName );
            return false;
        }
        vector<uint8> listSignature;
        if ( PackPatchInternal::fromHex( manifest._signatureHex, listSignature ) == false || listSignature.size() != PackPatchInternal::kSignatureSize )
        {
            SW_LOG_ERROR( "Pack patch manifest %# has a malformed signature", sourceName );
            return false;
        }
        const string payload = makeSigningPayload( manifest );
        if ( provider.verifySignature( publicKey, reinterpret_cast<const uint8*>( payload.data() ), static_cast<int32>( payload.size() ), listSignature.data(), static_cast<int32>( listSignature.size() ) ) == false )
        {
            SW_LOG_ERROR( "Pack patch manifest %# signature does not verify with the trusted key", sourceName );
            return false;
        }
        return true;
    }

    bool PackPatchUtil::orderPatchChain( string_view installedBuild, string_view targetBuild, const vector<PackPatchManifest>& listManifest, vector<size_t>& outListOrder )
    {
        outListOrder.clear();
        string currentBuild{ installedBuild };
        while ( currentBuild != targetBuild )
        {
            if ( outListOrder.size() >= listManifest.size() )
            {
                SW_LOG_ERROR( "Pack patch chain from build '%#' loops before it reaches '%#'", installedBuild, targetBuild );
                return false;
            }
            size_t nextIndex  = listManifest.size();
            int32  startCount = 0;
            for ( size_t manifestIndex = 0; manifestIndex < listManifest.size(); ++manifestIndex )
            {
                if ( listManifest[manifestIndex]._fromBuild != currentBuild )
                    continue;
                nextIndex = manifestIndex;
                ++startCount;
            }
            if ( startCount != 1 )
            {
                SW_LOG_ERROR( "Pack patch chain: %# patches start at build '%#' (need exactly one to reach '%#')", startCount, currentBuild, targetBuild );
                return false;
            }
            outListOrder.push_back( nextIndex );
            currentBuild = listManifest[nextIndex]._toBuild;
        }
        return true;
    }
} // namespace sw
