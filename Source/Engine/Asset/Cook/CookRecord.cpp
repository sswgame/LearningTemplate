#include "pch.h"

#include "Engine/Asset/Cook/CookRecord.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Encoding/HexUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/Constants.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct CookRecordInternal
        {
            /** @brief 16 자리 소문자 hex 입니다. */
            static string toHex( uint64 value )
            {
                string text;
                HexUtil::appendUint64( text, value );
                return text;
            }

            /** @brief 16 자리 hex 를 읽습니다. 형식이 아니면 false 입니다. */
            [[nodiscard]] static bool parseHex( string_view text, uint64& outValue )
            {
                return text.size() == HexDigitUtil::kMaxDigitCount && HexDigitUtil::tryParseFixed( text, outValue );
            }

            /** @brief 버킷의 폴더 이름입니다. */
            static string_view getBucketName( DerivedDataBucket bucket )
            {
                static constexpr string_view kArrBucketName[] = { "Cook", "Import", "Shader" };
                static_assert( SW_COUNT_OF( kArrBucketName ) == static_cast<size_t>( DerivedDataBucket::Count ), "every DDC bucket needs a folder name" );
                return kArrBucketName[static_cast<size_t>( bucket )];
            }

            /** @brief 설정의 버킷 상한(`_listBucket` 의 `_maxSizeGB`)입니다. 버킷 줄이 없거나 0 이면 빈 값입니다. */
            static optional<uint32> readBucketMaxGB( const JSONValue& root, string_view bucketName )
            {
                const JSONValue buckets = root.get( "_listBucket" );
                for ( size_t index = 0; buckets.isArray() && index < buckets.size(); ++index )
                {
                    const JSONValue        entry = buckets.at( index );
                    const optional<string> name  = entry.readRequired<string>( "_name" );
                    if ( name.has_value() == false || *name != bucketName )
                        continue;
                    const optional<uint32> maxGB = entry.readRequired<uint32>( "_maxSizeGB" );
                    return maxGB.has_value() && *maxGB != 0 ? maxGB : nullopt;
                }
                return nullopt;
            }

            /** @brief 정리 규칙이 있는 층의 루트(버킷 폴더의 부모)에서 버킷이 아닌 폴더를 지웁니다 — 버킷이 없던 때의 항목 폴더(`<키 앞 두 글자>`)는 아무도 열지 않는다. */
            static uint32 removeForeignFolders( const DerivedDataLayer& layer )
            {
                const AbsolutePath root = layer._rootPath.getParent();
                if ( layer._evictionPolicy.has_value() == false || FileUtil::isDirectory( root ) == false )
                    return 0;
                vector<AbsolutePath> listForeign;
                (void)FileUtil::forEachDirectoryEntry( root, false, [&listForeign]( const DirectoryEntry& entry )
                {
                    const string name    = FileUtil::getFileNamePart( entry._path );
                    bool         bBucket = false;
                    for ( uint32 index = 0; index < static_cast<uint32>( DerivedDataBucket::Count ); ++index )
                        bBucket = bBucket || name == getBucketName( static_cast<DerivedDataBucket>( index ) );
                    if ( entry._bDirectory && bBucket == false )
                        listForeign.push_back( entry.makePath() );
                    return true;
                } ); // 도중에 멈추면 남은 폴더는 다음 정리가 지운다
                uint32 removed = 0;
                for ( const AbsolutePath& folder : listForeign )
                    removed += FileUtil::removeDirectory( folder ) ? 1u : 0u;
                if ( removed > 0 )
                    SW_LOG_INFO_ID( "DDC.ForeignFoldersRemoved", "DDC '%#': removed %# folders that belong to no bucket", root.str(), removed );
                return removed;
            }

            /** @brief DDC 항목 머리 크기입니다: "SWDDC 1 <서술 길이 hex16> <본문 해시 hex16>\n" — 고정 폭이라 서술 앞까지 글자 수가 정해져 있다. */
            static constexpr size_t kEntryHeaderSize = DerivedDataCache::kEntryHeader.size() + 1 + HexDigitUtil::kMaxDigitCount + 1 + HexDigitUtil::kMaxDigitCount + 1;

            /** @brief 항목의 본문 앞부분(머리 + 키 서술)입니다. */
            static string makeEntryHead( string_view keyDescription, uint64 payloadHash )
            {
                string head( DerivedDataCache::kEntryHeader );
                head.append( " " ).append( toHex( keyDescription.size() ) ).append( " " ).append( toHex( payloadHash ) );
                head.push_back( '\n' );
                head.append( keyDescription );
                return head;
            }

            /**
             * @brief 항목 @p path 를 검증하며 본문을 조각(`ContentHash::kStreamChunkBytes`)으로 @p sink( pData, size ) 에 넘깁니다 — 항목을 한 벡터로 들지 않는다(큰 쿠킹 출력).
             * @details 본문 해시는 다 흘린 뒤에야 안다 — Valid 가 아니면 부르는 쪽이 받은 조각을 버린다(파일 출력은 `AtomicFileWriter` 를 커밋하지 않는다).
             *          @p sink 가 false 면 Unreadable 로 멈춘다. @p outPayloadHash 는 머리에 적힌 본문 해시다. 로그를 남기지 않는다.
             */
            template <typename SinkFn>
            [[nodiscard]] static DerivedDataEntryState scanEntry( const AbsolutePath& path, string_view keyDescription, SinkFn&& sink, uint64& outPayloadHash )
            {
                outPayloadHash = 0;
                if ( FileUtil::isRegularFile( path ) == false )
                    return DerivedDataEntryState::Missing;
                const size_t  headSize = kEntryHeaderSize + keyDescription.size();
                vector<uint8> chunk;
                if ( FileUtil::readFile( path, chunk, 0, headSize ) == false )
                    return DerivedDataEntryState::Unreadable;
                const string_view text{ reinterpret_cast<const char*>( chunk.data() ), chunk.size() };
                const string_view header          = DerivedDataCache::kEntryHeader;
                uint64            descriptionSize = 0;
                const bool        bHeaderValid    = text.size() >= kEntryHeaderSize && text.substr( 0, header.size() ) == header && text[kEntryHeaderSize - 1] == '\n' &&
                                          parseHex( text.substr( header.size() + 1, HexDigitUtil::kMaxDigitCount ), descriptionSize ) &&
                                          parseHex( text.substr( kEntryHeaderSize - 1 - HexDigitUtil::kMaxDigitCount, HexDigitUtil::kMaxDigitCount ), outPayloadHash );
                if ( bHeaderValid == false )
                    return DerivedDataEntryState::InvalidFormat;
                if ( descriptionSize != keyDescription.size() )
                    return DerivedDataEntryState::KeyCollision;
                if ( text.size() < headSize )
                    return DerivedDataEntryState::InvalidFormat; // 서술 중간에서 잘린 파일
                if ( text.substr( kEntryHeaderSize ) != keyDescription )
                    return DerivedDataEntryState::KeyCollision;
                uint64 state = ContentHash::kInitialState;
                for ( uint64 offset = headSize;; )
                {
                    if ( FileUtil::readFile( path, chunk, offset, ContentHash::kStreamChunkBytes ) == false )
                        return DerivedDataEntryState::Unreadable;
                    state = ContentHash::appendBytes( state, chunk.data(), chunk.size() );
                    if ( chunk.empty() == false && sink( chunk.data(), chunk.size() ) == false )
                        return DerivedDataEntryState::Unreadable;
                    offset += chunk.size();
                    if ( chunk.size() < ContentHash::kStreamChunkBytes )
                        break;
                }
                return state == outPayloadHash ? DerivedDataEntryState::Valid : DerivedDataEntryState::PayloadCorrupt;
            }

            /** @brief 찾기에서 본 항목 상태를 알립니다 — 없으면 조용히, 그 밖의 미스는 이유를 남깁니다. Valid 면 true 입니다. */
            [[nodiscard]] static bool reportEntry( const AbsolutePath& path, DerivedDataEntryState state )
            {
                switch ( state )
                {
                    case DerivedDataEntryState::Valid:
                        return true;
                    case DerivedDataEntryState::Missing:
                        return false;
                    case DerivedDataEntryState::Unreadable:
                        SW_LOG_WARNING_ID( "DDC.EntryUnreadable", "DDC entry '%#' cannot be read - treated as a miss", path.str() );
                        return false;
                    case DerivedDataEntryState::InvalidFormat:
                        // 옛 형식 항목(형식을 올린 뒤 한 번)이 대부분이라 정보 로그다 — 다시 구우면 새 형식으로 덮인다.
                        SW_LOG_INFO_ID( "DDC.EntryInvalid", "DDC entry '%#' is not a '%#' entry - treated as a miss", path.str(), DerivedDataCache::kEntryHeader );
                        return false;
                    case DerivedDataEntryState::KeyCollision:
                        SW_LOG_WARNING_ID( "DDC.KeyCollision", "DDC entry '%#' was written for another job with the same key - treated as a miss", path.str() );
                        return false;
                    case DerivedDataEntryState::PayloadCorrupt:
                        SW_LOG_WARNING_ID( "DDC.PayloadCorrupt", "DDC entry '%#' does not match its payload hash - treated as a miss", path.str() );
                        return false;
                }
                return false;
            }

            /** @brief 히트한 항목의 쓰기 시각을 지금으로 올립니다(정리의 "마지막 사용" — 언리얼 파일 DDC 의 touch). 하루 안에 올린 항목은 건너뛴다(히트마다 메타데이터를 쓰지 않게). */
            static void touchEntry( const DerivedDataLayer& layer, const AbsolutePath& path )
            {
                constexpr int64 kTouchIntervalTicks = constant::time::kSecondsPerDay * FileUtil::kFileTimeTicksPerSecond;
                const int64     now                 = FileUtil::getCurrentFileWriteTime();
                int64           writeTicks          = 0;
                if ( layer._evictionPolicy.has_value() && FileUtil::getFileWriteTime( path, writeTicks ) && now - writeTicks > kTouchIntervalTicks )
                    (void)FileUtil::setFileWriteTime( path, now ); // 실패는 setFileWriteTime 이 알린다 — 히트를 무르지 않는다(다음 정리에 일찍 지워질 뿐)
            }

            /** @brief 파일 @p sourcePath 를 조각으로 @p writer 끝에 붙입니다(한 벡터로 들지 않는다). */
            [[nodiscard]] static bool appendFile( AtomicFileWriter& writer, const AbsolutePath& sourcePath )
            {
                vector<uint8> chunk;
                for ( uint64 offset = 0;; )
                {
                    if ( FileUtil::readFile( sourcePath, chunk, offset, ContentHash::kStreamChunkBytes ) == false || writer.append( chunk.data(), chunk.size() ).has_value() == false )
                        return false;
                    offset += chunk.size();
                    if ( chunk.size() < ContentHash::kStreamChunkBytes )
                        return true;
                }
            }

            /**
             * @brief 항목 파일 @p path 를 씁니다 — 머리 + 서술 뒤에 @p appendPayload( writer ) 가 본문을 붙인다. 같은 서술 · 같은 본문이 이미 있으면 쓰지 않고 true 입니다.
             * @details 쓰기는 임시 파일 + 원자 교체(`AtomicFileWriter`)다(반쯤 쓴 항목을 읽는 쪽이 보지 않는다). 교체에 졌으면(다른 프로세스 · 스레드가 같은 자리를 막 바꿨다) 그 자리가 같은 항목인지 본다.
             *          로그 없는 커밋(`tryCommit`)을 쓴다 — 교체에 진 것은 경쟁의 정상 결과라 오류 로그를 남기면 그 실행의 테스트가 "예상하지 않은 Error" 로 진다.
             *          같은 항목인지는 본문까지 본다 — 결정적이지 않은 생산자의 새 바이트는 덮어써야 기록과 맞는다.
             */
            template <typename AppendPayloadFn>
            [[nodiscard]] static bool writeEntry( const AbsolutePath& path, string_view keyDescription, uint64 payloadHash, AppendPayloadFn&& appendPayload )
            {
                const auto isSameEntry = [&]()
                {
                    uint64 existingHash = 0;
                    return scanEntry( path, keyDescription, []( const uint8*, size_t )
                    { return true; }, existingHash ) == DerivedDataEntryState::Valid &&
                           existingHash == payloadHash;
                };
                if ( isSameEntry() )
                    return true;
                const string     head = makeEntryHead( keyDescription, payloadHash );
                AtomicFileWriter writer;
                FileWriteResult  result = FileWriteResult::WriteFailed;
                if ( writer.open( path ).has_value() &&
                     writer.append( reinterpret_cast<const uint8*>( head.data() ), head.size() ).has_value() && appendPayload( writer ) )
                    result = writer.tryCommit();
                if ( result == FileWriteResult::Written || ( result == FileWriteResult::ReplaceFailed && isSameEntry() ) )
                    return true;
                SW_LOG_WARNING_ID( "DDC.EntryNotWritten", "DDC entry '%#' could not be written (%#) - the next run cooks or imports it again", path.str(),
                                   result == FileWriteResult::ReplaceFailed ? "replace lost to a different entry" : "refused or write failed" );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CookRecord CookRecord::make( const CookJob& job, uint64 outputHash )
    {
        CookRecord record;
        record._key        = job.computeKey();
        record._outputHash = outputHash;
        record._listInput  = job.getInputs();
        return record;
    }

    bool CookRecord::write( const AbsolutePath& absPath ) const
    {
        string text( kHeader );
        text.append( "\nkey " ).append( CookRecordInternal::toHex( _key ) );
        text.append( "\noutput " ).append( CookRecordInternal::toHex( _outputHash ) );
        for ( const CookInput& input : _listInput )
        {
            text.append( "\n" ).append( CookInput::getKindName( input._kind ) ).append( " " ).append( CookRecordInternal::toHex( input._contentHash ) ).append( " " );
            if ( input._kind == CookInputKind::Listing )
                text.append( input._extension ).append( " " );
            text.append( input._source );
        }
        text.append( "\n" );
        return FileUtil::writeTextFile( absPath, text );
    }

    bool CookRecord::read( const AbsolutePath& absPath, CookRecord& outRecord )
    {
        outRecord = CookRecord{};
        string                 text;
        const OptionalFileRead read = FileUtil::readOptionalTextFile( absPath, text );
        if ( read == OptionalFileRead::Missing )
            return false;
        if ( read == OptionalFileRead::Failed )
        {
            SW_LOG_WARNING_ID( "CookRecord.CookRecordCannotRead", "Cook record '%#' cannot be read - the job is cooked again", absPath.str() );
            return false;
        }
        vector<string_view> listLine;
        const string_view   textView{ text };
        for ( size_t lineStart = 0; lineStart < textView.size(); )
        {
            const size_t lineEnd = textView.find( '\n', lineStart );
            const size_t lineLen = ( lineEnd == string_view::npos ? textView.size() : lineEnd ) - lineStart;
            listLine.push_back( textView.substr( lineStart, lineLen ) );
            lineStart += lineLen + 1;
        }
        if ( listLine.empty() || StringUtil::trim( listLine[0] ) != kHeader )
        {
            SW_LOG_WARNING_ID( "Cook.RecordInvalid", "Cook record '%#' is not '%#' - the job is cooked again", absPath.str(), kHeader );
            return false;
        }
        bool bHasKey    = false;
        bool bHasOutput = false;
        for ( size_t lineIndex = 1; lineIndex < listLine.size(); ++lineIndex )
        {
            const string_view line = StringUtil::trim( listLine[lineIndex] );
            if ( line.empty() )
                continue;
            const size_t      firstSpace   = line.find( ' ' );
            const string_view field        = line.substr( 0, firstSpace );
            const string_view rest         = firstSpace == string_view::npos ? string_view{} : line.substr( firstSpace + 1 );
            bool              bParsed      = false;
            const bool        bHashed      = rest.size() > 17 && rest[16] == ' '; // 16 자리 해시, 빈칸, 나머지
            const string_view afterHash    = bHashed ? rest.substr( 17 ) : string_view{};
            const size_t      extensionEnd = afterHash.find( ' ' ); // 목록: 확장자, 빈칸, 폴더 id
            const bool        bHasFolder   = extensionEnd != string_view::npos && extensionEnd + 1 < afterHash.size();
            if ( field == "key" )
                bParsed = bHasKey = CookRecordInternal::parseHex( rest, outRecord._key );
            else if ( field == "output" )
                bParsed = bHasOutput = CookRecordInternal::parseHex( rest, outRecord._outputHash );
            else if ( ( field == CookInput::getKindName( CookInputKind::Resource ) || field == CookInput::getKindName( CookInputKind::DiskFile ) ) && bHashed )
            {
                CookInput input;
                input._kind   = field == CookInput::getKindName( CookInputKind::DiskFile ) ? CookInputKind::DiskFile : CookInputKind::Resource;
                input._source = string( afterHash );
                bParsed       = CookRecordInternal::parseHex( rest.substr( 0, 16 ), input._contentHash );
                outRecord._listInput.push_back( std::move( input ) );
            }
            else if ( field == CookInput::getKindName( CookInputKind::Listing ) && bHasFolder )
            {
                CookInput input;
                input._kind      = CookInputKind::Listing;
                input._extension = string( afterHash.substr( 0, extensionEnd ) );
                input._source    = string( afterHash.substr( extensionEnd + 1 ) );
                bParsed          = CookRecordInternal::parseHex( rest.substr( 0, 16 ), input._contentHash );
                outRecord._listInput.push_back( std::move( input ) );
            }
            if ( bParsed == false )
            {
                SW_LOG_WARNING_ID( "Cook.RecordInvalid", "Cook record '%#' line %# is malformed - the job is cooked again", absPath.str(), lineIndex + 1 );
                outRecord = CookRecord{};
                return false;
            }
        }
        if ( bHasKey == false || bHasOutput == false )
        {
            SW_LOG_WARNING_ID( "Cook.RecordInvalid", "Cook record '%#' has no key or output line - the job is cooked again", absPath.str() );
            outRecord = CookRecord{};
            return false;
        }
        return true;
    }

    bool CookRecord::isFresh( const CookJobDesc& desc ) const
    {
        return findChangedInput() == nullptr && CookJob::computeKey( desc, _listInput ) == _key;
    }

    const CookInput* CookRecord::findChangedInput() const
    {
        return VectorUtil::findIf( _listInput, [&]( const CookInput& input )
        { return input.computeCurrentHash() != input._contentHash; } );
    }

    DerivedDataCache::DerivedDataCache( vector<DerivedDataLayer> listLayer )
        : _listLayer{ std::move( listLayer ) }
    {
    }

    expected<DerivedDataCache> DerivedDataCache::open( DerivedDataBucket bucket )
    {
        const string_view bucketName = CookRecordInternal::getBucketName( bucket );
        AbsolutePath      overrideRoot;
        if ( engine::areEngineServicesBound() && engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::DDC_DIR, overrideRoot ) )
            return DerivedDataCache{ vector<DerivedDataLayer>{ DerivedDataLayer{ overrideRoot.join( bucketName ), DerivedDataAccess::ReadWrite } } };
        const AbsolutePath path = ProcessRoots::get()._project.join( kConfigFile );
        JSONDocument       document;
        if ( document.loadFromFile( path ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the DDC layer config could not be read", path.str() } );
        const JSONValue layers = document.getRoot().get( "_listLayer" );
        if ( layers.isArray() == false || layers.size() == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the DDC layer config needs a non-empty _listLayer array", path.str() } );
        const optional<uint32> bucketMaxGB = CookRecordInternal::readBucketMaxGB( document.getRoot(), bucketName );
        if ( bucketMaxGB.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the DDC config needs a _listBucket entry with _maxSizeGB of at least 1 for every bucket", bucketName } );
        vector<DerivedDataLayer> listLayer;
        for ( size_t index = 0; index < layers.size(); ++index )
        {
            const JSONValue        entry        = layers.at( index );
            const optional<string> name         = entry.readRequired<string>( "_name" );
            const optional<string> root         = entry.readRequired<string>( "_root" );
            const optional<string> environment  = entry.readRequired<string>( "_rootEnvironment" );
            const optional<string> access       = entry.readRequired<string>( "_access" );
            const bool             bAccessKnown = access.has_value() && ( *access == "ReadOnly" || *access == "ReadWrite" );
            // 폴더와 환경 변수 중 정확히 하나 — 둘 다 적으면 어느 쪽이 이기는지가 숨은 규칙이 된다.
            if ( name.has_value() == false || root.has_value() == false || environment.has_value() == false || bAccessKnown == false || root->empty() == environment->empty() )
                return unexpected( Error{ ErrorKind::InvalidArgument, "a DDC layer needs _name, _access (ReadOnly or ReadWrite) and exactly one of _root or _rootEnvironment", path.str() } );
            const DerivedDataAccess             layerAccess = *access == "ReadWrite" ? DerivedDataAccess::ReadWrite : DerivedDataAccess::ReadOnly;
            optional<DerivedDataEvictionPolicy> evictionPolicy;
            if ( layerAccess == DerivedDataAccess::ReadWrite )
            {
                // 쓰는 층은 정리 규칙이 필수다 — 없으면 쿠킹 팩 항목이 코드 지문마다 쌓여 디스크를 채운다(2026-11-19 로컬 111 GB).
                const optional<uint32> ageDays = entry.readRequired<uint32>( "_unusedFileAgeDays" );
                if ( ageDays.has_value() == false || *ageDays == 0 )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a ReadWrite DDC layer needs _unusedFileAgeDays of at least 1", path.str() } );
                evictionPolicy = DerivedDataEvictionPolicy{ *ageDays, uint64{ *bucketMaxGB } * constant::memory::kBytesPerGigabyte };
            }
            string environmentRoot;
            if ( root->empty() && ProcessEnvironment::tryReadNonEmpty( *environment, environmentRoot ) == false )
            {
                SW_LOG_INFO_ID( "DDC.SharedLayerOff", "DDC layer '%#' is off - environment variable %# is not set", *name, *environment );
                continue;
            }
            // 환경 변수는 머신마다 다른 절대 경로다 — 상대 글이면 toDiskPath 가 오류를 남기고 빈 경로라 그 층의 I/O 가 실패한다(폴백 없음).
            const AbsolutePath rootPath = root->empty() ? FileUtil::toDiskPath( environmentRoot ) : ProcessRoots::get()._derivedData.join( *root );
            listLayer.push_back( DerivedDataLayer{ rootPath.join( bucketName ), layerAccess, std::move( evictionPolicy ) } );
        }
        return DerivedDataCache{ std::move( listLayer ) };
    }

    uint32 DerivedDataCache::evictAllBuckets()
    {
        uint32 removed = 0;
        for ( uint32 bucketIndex = 0; bucketIndex < static_cast<uint32>( DerivedDataBucket::Count ); ++bucketIndex )
        {
            const expected<DerivedDataCache> cache = open( static_cast<DerivedDataBucket>( bucketIndex ) );
            if ( cache.has_value() == false )
            {
                SW_LOG_ERROR_ID( "DDC.EvictionSkipped", "DDC eviction skipped: %# (%#)", cache.error().message(), cache.error().detail() );
                return removed;
            }
            if ( bucketIndex == 0 )
            {
                for ( const DerivedDataLayer& layer : cache->_listLayer )
                    removed += CookRecordInternal::removeForeignFolders( layer );
            }
            removed += cache->evictUnusedEntries();
        }
        return removed;
    }

    uint32 DerivedDataCache::evictUnusedEntries() const
    {
        struct Entry
        {
            AbsolutePath _path;
            uint64       _sizeBytes{ 0 };
            uint64       _writeTime{ 0 };
        };
        uint32      removedTotal = 0;
        const int64 now          = FileUtil::getCurrentFileWriteTime();
        for ( const DerivedDataLayer& layer : _listLayer )
        {
            if ( layer._evictionPolicy.has_value() == false || FileUtil::isDirectory( layer._rootPath ) == false )
                continue;
            vector<Entry> listEntry;
            uint64        totalBytes = 0;
            const bool    bScanned   = FileUtil::forEachDirectoryEntry( layer._rootPath, true, [&]( const DirectoryEntry& entry )
                 {
                FileStamp stamp;
                if ( entry._bDirectory == false && FileUtil::getFileStamp( entry.makePath(), stamp ) )
                {
                    listEntry.push_back( Entry{ entry.makePath(), stamp._sizeBytes, stamp._writeTime } );
                    totalBytes += stamp._sizeBytes;
                }
                return true;
            } );
            if ( bScanned == false )
                continue; // forEachDirectoryEntry 가 경고했다 — 반만 본 목록으로 상한을 재지 않는다
            VectorUtil::stableSort( listEntry, []( const Entry& lhs, const Entry& rhs )
            { return lhs._writeTime < rhs._writeTime; } ); // 같은 시각은 순회 순서(이름 순) — 지우는 항목이 실행마다 같다
            const int64 ageTicks   = int64{ layer._evictionPolicy->_unusedFileAgeDays } * constant::time::kSecondsPerDay * FileUtil::kFileTimeTicksPerSecond;
            const int64 cutoff     = now - ageTicks;
            uint32      removed    = 0;
            uint64      freedBytes = 0;
            // 오래된 것부터 — 나이도 지나지 않았고 합계도 상한 안이면 뒤는 모두 남긴다.
            for ( const Entry& entry : listEntry )
            {
                if ( static_cast<int64>( entry._writeTime ) >= cutoff && totalBytes <= layer._evictionPolicy->_maxSizeBytes )
                    break;
                if ( FileUtil::tryRemoveFile( entry._path ) == false )
                    continue; // 다른 프로세스가 읽는 중 — 다음 정리가 지운다
                totalBytes -= entry._sizeBytes;
                freedBytes += entry._sizeBytes;
                ++removed;
            }
            if ( removed > 0 )
                SW_LOG_INFO_ID( "DDC.EntriesEvicted", "DDC '%#': removed %# entries (%# MB) unused for %# days or over the %# GB limit - %# MB kept", layer._rootPath.str(), removed,
                                freedBytes / constant::memory::kBytesPerMegabyte, layer._evictionPolicy->_unusedFileAgeDays, layer._evictionPolicy->_maxSizeBytes / constant::memory::kBytesPerGigabyte, totalBytes / constant::memory::kBytesPerMegabyte );
            removedTotal += removed;
        }
        return removedTotal;
    }

    AbsolutePath DerivedDataCache::makePath( size_t layerIndex, uint64 key ) const
    {
        const string hex = CookRecordInternal::toHex( key );
        return _listLayer[layerIndex]._rootPath.join( string_view( hex ).substr( 0, 2 ) ).join( hex );
    }

    AbsolutePath DerivedDataCache::makeRecordPath( size_t layerIndex, uint64 actionKey ) const
    {
        StringBuilder<constant::buffer::kMax64> name;
        const string                            hex = CookRecordInternal::toHex( actionKey );
        name.appendFormat( "%#.cookrec", hex );
        return _listLayer[layerIndex]._rootPath.join( string_view( hex ).substr( 0, 2 ) ).join( name.view() );
    }

    bool DerivedDataCache::readRecord( uint64 actionKey, CookRecord& outRecord ) const
    {
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            const AbsolutePath path = makeRecordPath( layerIndex, actionKey );
            if ( CookRecord::read( path, outRecord ) == false )
                continue;
            CookRecordInternal::touchEntry( _listLayer[layerIndex], path );
            return true;
        }
        return false;
    }

    bool DerivedDataCache::writeRecord( uint64 actionKey, const CookRecord& record ) const
    {
        bool bAllWritten = true;
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            if ( _listLayer[layerIndex]._access == DerivedDataAccess::ReadWrite )
                bAllWritten = record.write( makeRecordPath( layerIndex, actionKey ) ) && bAllWritten;
        }
        return bAllWritten;
    }

    bool DerivedDataCache::find( uint64 key, string_view keyDescription, vector<uint8>& outBytes ) const
    {
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            const AbsolutePath path = makePath( layerIndex, key );
            if ( CookRecordInternal::reportEntry( path, validateEntry( path, keyDescription, outBytes ) ) == false )
                continue;
            CookRecordInternal::touchEntry( _listLayer[layerIndex], path );
            // 뒤 층 히트는 앞의 쓰기 가능 층에 채운다 — 다음 찾기는 가까운 층에서 끝난다. 채우기 실패는 히트를 무르지 않는다(다음에 다시 채운다).
            const uint64 payloadHash = ContentHash::computeBytes( outBytes );
            for ( size_t fillIndex = 0; fillIndex < layerIndex; ++fillIndex )
            {
                const bool bFilled = _listLayer[fillIndex]._access == DerivedDataAccess::ReadOnly ||
                                     CookRecordInternal::writeEntry( makePath( fillIndex, key ), keyDescription, payloadHash, [&]( AtomicFileWriter& writer )
                { return writer.append( outBytes.data(), outBytes.size() ).has_value(); } );
                if ( bFilled == false )
                    SW_LOG_WARNING_ID( "DDC.FillFailed", "DDC: could not fill '%#' from a later layer", makePath( fillIndex, key ).str() );
            }
            return true;
        }
        outBytes.clear();
        return false;
    }

    bool DerivedDataCache::find( uint64 key, string_view keyDescription, const AbsolutePath& outputPath ) const
    {
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            const AbsolutePath path = makePath( layerIndex, key );
            if ( FileUtil::isRegularFile( path ) == false )
                continue;
            // 출력은 임시 파일에 흘려 쓰고 본문 해시가 맞을 때만 바꿔 끼운다 — 어긋난 항목이 출력 자리를 건드리지 않는다.
            AtomicFileWriter   writer;
            const AbsolutePath outputDiskPath = outputPath;
            if ( writer.open( outputDiskPath ).has_value() == false )
            {
                SW_LOG_WARNING_ID( "DDC.OutputNotWritable", "DDC: cannot write '%#' from the cache - treated as a miss", outputPath.str() );
                return false;
            }
            uint64                      payloadHash = 0;
            const DerivedDataEntryState state       = CookRecordInternal::scanEntry(
                path, keyDescription, [&]( const uint8* pData, size_t size )
            { return writer.append( pData, size ).has_value(); }, payloadHash );
            if ( CookRecordInternal::reportEntry( path, state ) == false )
                continue; // writer 가 소멸하며 임시 파일을 지운다
            if ( writer.commit().has_value() == false )
                return false;
            CookRecordInternal::touchEntry( _listLayer[layerIndex], path );
            for ( size_t fillIndex = 0; fillIndex < layerIndex; ++fillIndex )
            {
                const bool bFilled = _listLayer[fillIndex]._access == DerivedDataAccess::ReadOnly ||
                                     CookRecordInternal::writeEntry( makePath( fillIndex, key ), keyDescription, payloadHash, [&]( AtomicFileWriter& fillWriter )
                { return CookRecordInternal::appendFile( fillWriter, outputPath ); } );
                if ( bFilled == false )
                    SW_LOG_WARNING_ID( "DDC.FillFailed", "DDC: could not fill '%#' from a later layer", makePath( fillIndex, key ).str() );
            }
            return true;
        }
        return false;
    }

    bool DerivedDataCache::store( uint64 key, string_view keyDescription, const vector<uint8>& bytes ) const
    {
        const uint64 payloadHash = ContentHash::computeBytes( bytes );
        bool         bAllWritten = true;
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            if ( _listLayer[layerIndex]._access == DerivedDataAccess::ReadOnly )
                continue;
            bAllWritten = CookRecordInternal::writeEntry( makePath( layerIndex, key ), keyDescription, payloadHash, [&]( AtomicFileWriter& writer )
            { return writer.append( bytes.data(), bytes.size() ).has_value(); } ) &&
                          bAllWritten;
        }
        return bAllWritten;
    }

    bool DerivedDataCache::store( uint64 key, string_view keyDescription, const AbsolutePath& sourcePath ) const
    {
        const expected<uint64> payloadHash = ContentHash::computeFileStream( sourcePath );
        if ( payloadHash.has_value() == false )
        {
            SW_LOG_WARNING_ID( "DDC.SourceUnreadable", "DDC: cannot read '%#' to store it - the next run cooks it again", sourcePath.str() );
            return false;
        }
        bool bAllWritten = true;
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            if ( _listLayer[layerIndex]._access == DerivedDataAccess::ReadOnly )
                continue;
            bAllWritten = CookRecordInternal::writeEntry( makePath( layerIndex, key ), keyDescription, *payloadHash, [&]( AtomicFileWriter& writer )
            { return CookRecordInternal::appendFile( writer, sourcePath ); } ) &&
                          bAllWritten;
        }
        return bAllWritten;
    }

    DerivedDataEntryState DerivedDataCache::validateEntry( const AbsolutePath& path, string_view keyDescription, vector<uint8>& outBytes )
    {
        outBytes.clear();
        uint64                      payloadHash = 0;
        const DerivedDataEntryState state       = CookRecordInternal::scanEntry(
            path, keyDescription,
            [&]( const uint8* pData, size_t size )
        {
            outBytes.insert( outBytes.end(), pData, pData + size );
            return true;
        },
            payloadHash );
        if ( state != DerivedDataEntryState::Valid )
            outBytes.clear();
        return state;
    }
} // namespace sw
