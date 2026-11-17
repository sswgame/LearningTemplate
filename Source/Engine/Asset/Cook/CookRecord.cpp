#include "pch.h"

#include "Engine/Asset/Cook/CookRecord.h"

#include "Core/Algorithm/VectorUtil.h"
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
            [[nodiscard]] static DerivedDataEntryState scanEntry( string_view path, string_view keyDescription, SinkFn&& sink, uint64& outPayloadHash )
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
            [[nodiscard]] static bool reportEntry( const string& path, DerivedDataEntryState state )
            {
                switch ( state )
                {
                    case DerivedDataEntryState::Valid:
                        return true;
                    case DerivedDataEntryState::Missing:
                        return false;
                    case DerivedDataEntryState::Unreadable:
                        SW_LOG_WARNING_ID( "DDC.EntryUnreadable", "DDC entry '%#' cannot be read - treated as a miss", path );
                        return false;
                    case DerivedDataEntryState::InvalidFormat:
                        // 옛 형식 항목(형식을 올린 뒤 한 번)이 대부분이라 정보 로그다 — 다시 구우면 새 형식으로 덮인다.
                        SW_LOG_INFO_ID( "DDC.EntryInvalid", "DDC entry '%#' is not a '%#' entry - treated as a miss", path, DerivedDataCache::kEntryHeader );
                        return false;
                    case DerivedDataEntryState::KeyCollision:
                        SW_LOG_WARNING_ID( "DDC.KeyCollision", "DDC entry '%#' was written for another job with the same key - treated as a miss", path );
                        return false;
                    case DerivedDataEntryState::PayloadCorrupt:
                        SW_LOG_WARNING_ID( "DDC.PayloadCorrupt", "DDC entry '%#' does not match its payload hash - treated as a miss", path );
                        return false;
                }
                return false;
            }

            /** @brief 파일 @p sourcePath 를 조각으로 @p writer 끝에 붙입니다(한 벡터로 들지 않는다). */
            [[nodiscard]] static bool appendFile( AtomicFileWriter& writer, string_view sourcePath )
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
            [[nodiscard]] static bool writeEntry( const string& path, string_view keyDescription, uint64 payloadHash, AppendPayloadFn&& appendPayload )
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
                if ( FileUtil::ensureParentDirectoryExists( path ) && writer.open( path ).has_value() &&
                     writer.append( reinterpret_cast<const uint8*>( head.data() ), head.size() ).has_value() && appendPayload( writer ) )
                    result = writer.tryCommit();
                if ( result == FileWriteResult::Written || ( result == FileWriteResult::ReplaceFailed && isSameEntry() ) )
                    return true;
                SW_LOG_WARNING_ID( "DDC.EntryNotWritten", "DDC entry '%#' could not be written (%#) - the next run cooks or imports it again", path,
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

    bool CookRecord::write( string_view absPath ) const
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
        return FileUtil::ensureParentDirectoryExists( absPath ) && FileUtil::writeTextFile( absPath, text );
    }

    bool CookRecord::read( string_view absPath, CookRecord& outRecord )
    {
        outRecord = CookRecord{};
        string                 text;
        const OptionalFileRead read = FileUtil::readOptionalTextFile( absPath, text );
        if ( read == OptionalFileRead::Missing )
            return false;
        if ( read == OptionalFileRead::Failed )
        {
            SW_LOG_WARNING_ID( "CookRecord.CookRecordCannotRead", "Cook record '%#' cannot be read - the job is cooked again", absPath );
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
            SW_LOG_WARNING_ID( "Cook.RecordInvalid", "Cook record '%#' is not '%#' - the job is cooked again", absPath, kHeader );
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
                SW_LOG_WARNING_ID( "Cook.RecordInvalid", "Cook record '%#' line %# is malformed - the job is cooked again", absPath, lineIndex + 1 );
                outRecord = CookRecord{};
                return false;
            }
        }
        if ( bHasKey == false || bHasOutput == false )
        {
            SW_LOG_WARNING_ID( "Cook.RecordInvalid", "Cook record '%#' has no key or output line - the job is cooked again", absPath );
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

    string DerivedDataCache::makeLocalRootPath()
    {
        return FileUtil::joinPath( ProcessRoots::get()._derivedData.str(), "DDC" );
    }

    expected<DerivedDataCache> DerivedDataCache::loadFromConfig()
    {
        const string path = FileUtil::joinPath( ProcessRoots::get()._project.str(), kConfigFile );
        JSONDocument document;
        if ( document.loadFromFile( path ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the DDC layer config could not be read", path } );
        const JSONValue layers = document.getRoot().get( "_listLayer" );
        if ( layers.isArray() == false || layers.size() == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the DDC layer config needs a non-empty _listLayer array", path } );
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
                return unexpected( Error{ ErrorKind::InvalidArgument, "a DDC layer needs _name, _access (ReadOnly or ReadWrite) and exactly one of _root or _rootEnvironment", path } );
            DerivedDataLayer layer;
            layer._access = *access == "ReadWrite" ? DerivedDataAccess::ReadWrite : DerivedDataAccess::ReadOnly;
            if ( root->empty() == false )
                layer._rootPath = FileUtil::joinPath( ProcessRoots::get()._derivedData.str(), *root );
            else if ( ProcessEnvironment::tryReadNonEmpty( *environment, layer._rootPath ) == false )
            {
                SW_LOG_INFO_ID( "DDC.SharedLayerOff", "DDC layer '%#' is off - environment variable %# is not set", *name, *environment );
                continue;
            }
            listLayer.push_back( std::move( layer ) );
        }
        return DerivedDataCache{ std::move( listLayer ) };
    }

    string DerivedDataCache::makePath( size_t layerIndex, uint64 key ) const
    {
        const string hex = CookRecordInternal::toHex( key );
        return FileUtil::joinPath( FileUtil::joinPath( _listLayer[layerIndex]._rootPath, hex.substr( 0, 2 ) ), hex );
    }

    bool DerivedDataCache::find( uint64 key, string_view keyDescription, vector<uint8>& outBytes ) const
    {
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            const string path = makePath( layerIndex, key );
            if ( CookRecordInternal::reportEntry( path, checkEntry( path, keyDescription, outBytes ) ) == false )
                continue;
            // 뒤 층 히트는 앞의 쓰기 가능 층에 채운다 — 다음 찾기는 가까운 층에서 끝난다. 채우기 실패는 히트를 무르지 않는다(다음에 다시 채운다).
            const uint64 payloadHash = ContentHash::computeBytes( outBytes );
            for ( size_t fillIndex = 0; fillIndex < layerIndex; ++fillIndex )
            {
                const bool bFilled = _listLayer[fillIndex]._access == DerivedDataAccess::ReadOnly ||
                                     CookRecordInternal::writeEntry( makePath( fillIndex, key ), keyDescription, payloadHash, [&]( AtomicFileWriter& writer )
                { return writer.append( outBytes.data(), outBytes.size() ).has_value(); } );
                if ( bFilled == false )
                    SW_LOG_WARNING_ID( "DDC.FillFailed", "DDC: could not fill '%#' from a later layer", makePath( fillIndex, key ) );
            }
            return true;
        }
        outBytes.clear();
        return false;
    }

    bool DerivedDataCache::find( uint64 key, string_view keyDescription, string_view outputPath ) const
    {
        for ( size_t layerIndex = 0; layerIndex < _listLayer.size(); ++layerIndex )
        {
            const string path = makePath( layerIndex, key );
            if ( FileUtil::isRegularFile( path ) == false )
                continue;
            // 출력은 임시 파일에 흘려 쓰고 본문 해시가 맞을 때만 바꿔 끼운다 — 어긋난 항목이 출력 자리를 건드리지 않는다.
            AtomicFileWriter writer;
            if ( FileUtil::ensureParentDirectoryExists( outputPath ) == false || writer.open( outputPath ).has_value() == false )
            {
                SW_LOG_WARNING_ID( "DDC.OutputNotWritable", "DDC: cannot write '%#' from the cache - treated as a miss", outputPath );
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
            for ( size_t fillIndex = 0; fillIndex < layerIndex; ++fillIndex )
            {
                const bool bFilled = _listLayer[fillIndex]._access == DerivedDataAccess::ReadOnly ||
                                     CookRecordInternal::writeEntry( makePath( fillIndex, key ), keyDescription, payloadHash, [&]( AtomicFileWriter& fillWriter )
                { return CookRecordInternal::appendFile( fillWriter, outputPath ); } );
                if ( bFilled == false )
                    SW_LOG_WARNING_ID( "DDC.FillFailed", "DDC: could not fill '%#' from a later layer", makePath( fillIndex, key ) );
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

    bool DerivedDataCache::store( uint64 key, string_view keyDescription, string_view sourcePath ) const
    {
        const expected<uint64> payloadHash = ContentHash::computeFileStream( sourcePath );
        if ( payloadHash.has_value() == false )
        {
            SW_LOG_WARNING_ID( "DDC.SourceUnreadable", "DDC: cannot read '%#' to store it - the next run cooks it again", sourcePath );
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

    DerivedDataEntryState DerivedDataCache::checkEntry( string_view path, string_view keyDescription, vector<uint8>& outBytes )
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
