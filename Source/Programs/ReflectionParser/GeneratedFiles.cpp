#include "pch.h"

#include "Programs/ReflectionParser/GeneratedFiles.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Programs/ReflectionParser/ParserConfig.h"
#include "Programs/ReflectionParser/ParserOptions.h"
#include "Programs/ReflectionParser/ParserUtil.h"

#include "sw/config/ConfigConstants.h"

SW_LOG_CALLER( "GeneratedFiles" );
namespace sw
{
    namespace
    {
        struct GeneratedFilesInternal
        {
            /** @brief 스탬프의 "파싱 전에 본 입력의 쓰기 시각" 줄 머리입니다 — `input <시각>`. */
            static constexpr const utf8* kStampInputTimeKey = "input";
            /** @brief 스탬프의 의존 줄 머리입니다 — `dep <시각> <경로>`. */
            static constexpr const utf8* kStampDependencyKey = "dep";
            /** @brief 스탬프의 타입 줄 머리입니다 — `type <FQN>`. */
            static constexpr const utf8* kStampTypeKey = "type";
            /** @brief ENUM(Flags) 가 있는 입력의 스탬프에만 있는 줄입니다. */
            static constexpr const utf8* kStampBitFlagLine = "flags";
            /** @brief 이번 실행의 시작을 적는 표식 파일 이름입니다(`markRunStart`). */
            static constexpr const utf8* kRunMarkerName = "ReflectionParser.run";

            /**
             * @brief 스탬프에 적은 쓰기 시각을 **부호 없는** 64 비트로 읽습니다.
             * @details 리눅스(libstdc++)의 파일 시계는 기원이 2174 년이라 지금 시각이 음수이고, 그것을 부호 없이 옮겨 적은 값은 int64 를 넘는다.
             *          `parseInt64` 로 읽으면 리눅스에서 늘 실패한다 — 스탬프가 시각 없는 꼴로 읽혀 매 실행이 모두 다시 파싱하고 depfile 의 의존이 빈다.
             */
            [[nodiscard]] static bool parseWriteTime( string_view text, uint64& outTime )
            {
                return StringUtil::parseUint64( StringUtil::trim( text ), outTime );
            }

            /** @brief `"<키> <나머지>"` 꼴 줄에서 키 뒤의 나머지를 꺼냅니다. 키가 다르면 false. */
            static bool takeKeyedRest( string_view line, string_view key, string_view& outRest )
            {
                const bool bKeyMismatch = line.size() <= key.size() || line.substr( 0, key.size() ) != key || line[key.size()] != ' ';
                if ( bKeyMismatch )
                    return false;
                outRest = line.substr( key.size() + 1 );
                return true;
            }

            /**
             * @brief 스탬프 글을 읽습니다(`GeneratedStamp` 의 줄 꼴). 시각이 없거나 모르는 줄이 있으면 false.
             * @details 경로에는 공백이 있을 수 있어 `dep` 줄은 시각 다음의 **나머지 전부**가 경로다.
             */
            [[nodiscard]] static bool parseStamp( string_view text, GeneratedStamp& outRecord )
            {
                size_t lineStart = 0;
                bool   bHasInput = false;
                for ( uint32 lineIndex = 0; lineStart <= text.size(); ++lineIndex )
                {
                    const size_t      lineEnd = text.find( '\n', lineStart );
                    const string_view line    = StringUtil::trim( text.substr( lineStart, lineEnd == string_view::npos ? string_view::npos : lineEnd - lineStart ) );
                    lineStart                 = lineEnd == string_view::npos ? text.size() + 1 : lineEnd + 1;
                    if ( lineIndex == 0 )
                    {
                        outRecord._sourcePath = string( line );
                        continue;
                    }
                    if ( line.empty() )
                        continue;
                    string_view rest;
                    uint64      time{ 0 };
                    if ( takeKeyedRest( line, kStampInputTimeKey, rest ) )
                    {
                        bHasInput                 = parseWriteTime( rest, time );
                        outRecord._inputWriteTime = time;
                    }
                    else if ( takeKeyedRest( line, kStampDependencyKey, rest ) )
                    {
                        const size_t space = rest.find( ' ' );
                        if ( space == string_view::npos || parseWriteTime( rest.substr( 0, space ), time ) == false )
                            return false;
                        outRecord._listDependency.push_back( StampDependency{ string( rest.substr( space + 1 ) ), time } );
                    }
                    else if ( takeKeyedRest( line, kStampTypeKey, rest ) )
                    {
                        outRecord._listTypeFqn.emplace_back( rest );
                    }
                    else if ( line == kStampBitFlagLine )
                    {
                        outRecord._bHasBitFlagEnum = true;
                    }
                    else
                    {
                        // 모르는 줄은 다른 판의 파서가 쓴 스탬프다 - 최신으로 보지 않는다(다시 만든다).
                        return false;
                    }
                }
                return bHasInput;
            }

            /**
             * @brief 파일의 마지막 쓰기 시각입니다. 없으면 0 입니다.
             * @details `getFileTimestamp` 는 **초 단위**라 생성 직후 같은 초에 고친 편집을 "최신" 으로 봅니다. 여기서는 파일끼리
             *          선후만 비교하므로 플랫폼 단위 그대로의 `getFileStamp` 시각을 씁니다.
             */
            static uint64 getWriteTime( const string_view path )
            {
                FileStamp stamp;
                if ( path.empty() || FileUtil::getFileStamp( path, stamp ) == false )
                    return 0;
                return stamp._writeTime;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GeneratedPaths GeneratedFileUtil::makePaths( const string& outputDir, const string& inputFile )
    {
        GeneratedPaths paths;
        paths._cppPath    = ParserUtil::makeGeneratedPath( outputDir, inputFile, sw::config::kExtReflectionGenCpp );
        paths._headerPath = ParserUtil::makeGeneratedPath( outputDir, inputFile, sw::config::kExtReflectionGenHeader );
        paths._stampPath  = paths._cppPath + sw::config::kExtReflectionStamp;
        return paths;
    }

    bool GeneratedFileUtil::writeIfChanged( const string& path, const string_view content )
    {
        string     existingContent;
        const bool bContentUnchanged =
            FileUtil::readOptionalTextFile( path, existingContent ) == OptionalFileRead::Read && existingContent.empty() == false && string_view( existingContent ) == content;
        if ( bContentUnchanged )
            return true;

        if ( FileUtil::ensureParentDirectoryExists( path ) == false || FileUtil::writeTextFile( path, content ) == false )
        {
            SW_LOG_ERROR( "Failed to write %#", path );
            return false;
        }
        SW_LOG_TRACE( "Generated  : %#", path );
        return true;
    }

    bool GeneratedFileUtil::writeStamp( const string& stampPath, const GeneratedStamp& stamp )
    {
        string stampText = formatToString( "%#\n%# %#\n", stamp._sourcePath, string( GeneratedFilesInternal::kStampInputTimeKey ), stamp._inputWriteTime );
        if ( stamp._bHasBitFlagEnum )
            stampText += formatToString( "%#\n", string( GeneratedFilesInternal::kStampBitFlagLine ) );
        for ( const string& typeFqn : stamp._listTypeFqn )
        {
            stampText += formatToString( "%# %#\n", string( GeneratedFilesInternal::kStampTypeKey ), typeFqn );
        }
        for ( const StampDependency& dependency : stamp._listDependency )
        {
            stampText += formatToString( "%# %# %#\n", string( GeneratedFilesInternal::kStampDependencyKey ), dependency._writeTime, dependency._path );
        }
        if ( FileUtil::writeTextFile( stampPath, stampText ) == false )
        {
            SW_LOG_ERROR( "Failed to write %#", stampPath );
            return false;
        }
        return true;
    }

    uint64 GeneratedFileUtil::getWriteTime( string_view path )
    {
        return GeneratedFilesInternal::getWriteTime( path );
    }

    bool GeneratedFileUtil::readStamp( const string& stampPath, GeneratedStamp& outStamp )
    {
        outStamp = GeneratedStamp{};
        string stampText;
        if ( FileUtil::readOptionalTextFile( stampPath, stampText ) != OptionalFileRead::Read )
            return false;
        return GeneratedFilesInternal::parseStamp( stampText, outStamp );
    }

    uint64 GeneratedFileUtil::markRunStart( const string& outputDir )
    {
        const string markerPath = FileUtil::joinPath( outputDir, GeneratedFilesInternal::kRunMarkerName );
        if ( FileUtil::writeTextFile( markerPath, "ReflectionParser run marker - its write time is the start of the last run\n" ) == false )
            return 0;
        return GeneratedFilesInternal::getWriteTime( markerPath );
    }

    IncrementalCheck::IncrementalCheck( const ParserOptions& options, const ParserConfig& config )
        : _newestToolWriteTime{ 0 }
    {
        vector<string> listToolFile;
        listToolFile.push_back( FileUtil::getExecutablePath() );
        listToolFile.push_back( options._builtinsPath );
        listToolFile.push_back( options._annotationMetaPath );
        for ( const string& configFile : config._listLoadedFile )
        {
            listToolFile.push_back( configFile );
        }
        const bool bTemplateDir = options._emitTemplatesDir.empty() == false && FileUtil::isDirectory( options._emitTemplatesDir );
        const bool bListFailed  = bTemplateDir && FileUtil::collectFiles( options._emitTemplatesDir, sw::config::kExtReflectionTemplate, listToolFile, false ) == false;

        for ( const string& toolFile : listToolFile )
        {
            const uint64 writeTime = GeneratedFilesInternal::getWriteTime( toolFile );
            if ( writeTime > _newestToolWriteTime )
                _newestToolWriteTime = writeTime;
        }
        // 템플릿 목록을 다 읽지 못했으면 바뀐 템플릿을 놓칠 수 있다 — 모두 낡은 것으로 보고 다시 만든다(경로는 collectFiles 가 알린다).
        if ( bListFailed )
            _newestToolWriteTime = MathUtil::kMaxUInt64;
    }

    bool IncrementalCheck::isUpToDate( const string& inputFile, const GeneratedPaths& paths ) const
    {
        const uint64 stampTime     = GeneratedFilesInternal::getWriteTime( paths._stampPath );
        const bool   bStampMissing = stampTime == 0 || FileUtil::exists( paths._cppPath ) == false || FileUtil::exists( paths._headerPath ) == false;
        if ( bStampMissing )
            return false;

        // 스탬프 파일의 시각은 **도구**가 더 새로운지만 가린다. 입력은 스탬프에 적힌 "파싱 전에 본 시각" 과 같은지로 본다(아래).
        const uint64 inputTime = GeneratedFilesInternal::getWriteTime( inputFile );
        if ( inputTime == 0 || stampTime < _newestToolWriteTime )
            return false;

        // 자리 표시자는 따로 알아보지 않는다 - CMake 가 자리 표시자를 심을 때 스탬프를 지우므로 위에서 "스탬프 없음" 으로 걸린다(생성 글을 읽어 추측하지 않는다).

        // 헤더를 **옮기기만** 하면 내용도 시각도 그대로라 시각 비교는 "최신" 이라고 답한다. 그런데 .gen.cpp 는 원본을 절대
        // 경로로 #include 하므로 그대로 두면 없는 경로를 가리켜 빌드가 깨진다. 스탬프에 적힌 원본 경로를 대조해 이동을 잡는다.
        // 시각을 읽지 못하는 스탬프는 최신이 아니다 — 다시 만든다.
        GeneratedStamp record;
        if ( GeneratedFileUtil::readStamp( paths._stampPath, record ) == false || record._sourcePath != inputFile )
            return false;
        // **같아야** 최신이다. 파싱하는 동안 저장한 편집은 스탬프보다 오래된 시각을 가질 수 있다 — 크고 작음이 아니라 같음으로 본다.
        if ( record._inputWriteTime != inputTime )
            return false;
        // include 한 헤더도 같아야 한다. 없어졌으면(시각 0) 다시 만든다.
        for ( const StampDependency& dependency : record._listDependency )
        {
            auto cacheIt = _mapDependencyWriteTime.find( dependency._path );
            if ( cacheIt == _mapDependencyWriteTime.end() )
                cacheIt = _mapDependencyWriteTime.emplace( dependency._path, GeneratedFilesInternal::getWriteTime( dependency._path ) ).first;
            if ( dependency._writeTime == 0 || cacheIt->second != dependency._writeTime )
                return false;
        }
        return true;
    }
} // namespace sw
