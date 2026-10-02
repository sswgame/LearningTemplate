#include "pch.h"

#include "ReflectionParser/GeneratedFiles.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "ReflectionParser/ParserConfig.h"
#include "ReflectionParser/ParserOptions.h"
#include "ReflectionParser/ParserUtil.h"

SW_LOG_CALLER( "GeneratedFiles" );
namespace sw
{
    namespace
    {
        struct GeneratedFilesInternal
        {
            /** @brief 스탬프 확장자입니다 — `<.gen.cpp>.stamp`. CMake 가 ReflectBuiltins 에 쓰는 규칙과 같습니다. */
            static constexpr const utf8* kStampSuffix = ".stamp";
            /** @brief 스탬프의 "파싱 전에 본 입력의 쓰기 시각" 줄 머리입니다 — `input <시각>`. */
            static constexpr const utf8* kStampInputTimeKey = "input";
            /** @brief 스탬프의 의존 줄 머리입니다 — `dep <시각> <경로>`. */
            static constexpr const utf8* kStampDependencyKey = "dep";
            /** @brief 이번 실행의 시작을 적는 표식 파일 이름입니다(`markRunStart`). */
            static constexpr const utf8* kRunMarkerName = "ReflectionParser.run";

            /** @brief 읽은 스탬프 한 장입니다. */
            struct StampRecord
            {
                string_view                       _sourcePath;
                uint64                            _inputWriteTime{ 0 };
                vector<pair<uint64, string_view>> _listDependency; ///< (시각, 경로) — 경로는 스탬프 글을 가리킨다
            };

            /**
             * @brief 스탬프에 적은 쓰기 시각을 **부호 없는** 64 비트로 읽습니다.
             * @details 리눅스(libstdc++)의 파일 시계는 기원이 2174 년이라 지금 시각이 음수이고, 그것을 부호 없이 옮겨 적은 값은 int64 를 넘는다.
             *          예전에는 `parseInt64` 로 읽어 리눅스에서 늘 실패했다 — 스탬프가 옛 꼴로 읽혀 매 실행이 모두 다시 파싱했고 depfile 의 의존이 비었다.
             */
            static bool parseWriteTime( string_view text, uint64& outTime )
            {
                text                         = StringUtil::trim( text );
                const auto [pEnd, errorCode] = std::from_chars( text.data(), text.data() + text.size(), outTime );
                return errorCode == std::errc() && pEnd == text.data() + text.size();
            }

            /** @brief `"<키> <나머지>"` 꼴 줄에서 키 뒤의 나머지를 꺼냅니다. 키가 다르면 false. */
            static bool takeKeyedRest( string_view line, string_view key, string_view& outRest )
            {
                if ( line.size() <= key.size() || line.substr( 0, key.size() ) != key || line[key.size()] != ' ' )
                    return false;
                outRest = line.substr( key.size() + 1 );
                return true;
            }

            /**
             * @brief 스탬프 글을 읽습니다 — 첫 줄 원본 경로, `input <시각>`, 그리고 `dep <시각> <경로>` 줄들. 옛 꼴(시각 없음)이면 false.
             * @details 경로에는 공백이 있을 수 있어 `dep` 줄은 시각 다음의 **나머지 전부**가 경로다.
             */
            static bool parseStamp( string_view text, StampRecord& outRecord )
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
                        outRecord._sourcePath = line;
                        continue;
                    }
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
                        outRecord._listDependency.emplace_back( time, rest.substr( space + 1 ) );
                    }
                }
                return bHasInput;
            }

            /**
             * @brief 증분 판정은 앞부분만 읽습니다. 머리말(원본 경로)과 자리 표시자 표식이 모두 여기 들어옵니다.
             */
            static constexpr uint32 kHeadProbeBytes = 4096;

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

            static bool readHead( const string& path, vector<uint8>& outBytes )
            {
                return FileUtil::readFile( path, outBytes, 0, kHeadProbeBytes );
            }

            static string_view toView( const vector<uint8>& bytes )
            {
                return string_view( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GeneratedPaths GeneratedFileUtil::makePaths( const string& outputDir, const string& inputFile, const ParserConfig& config )
    {
        GeneratedPaths paths;
        paths._cppPath    = ParserUtil::makeGeneratedPath( outputDir, inputFile, config._emitCppExtension );
        paths._headerPath = ParserUtil::makeGeneratedPath( outputDir, inputFile, config._emitHeaderExtension );
        paths._stampPath  = paths._cppPath + GeneratedFilesInternal::kStampSuffix;
        return paths;
    }

    bool GeneratedFileUtil::writeIfChanged( const string& path, const string_view content )
    {
        if ( FileUtil::fileExists( path ) )
        {
            string existingContent;
            if ( FileUtil::readTextFile( path, existingContent ) && existingContent.empty() == false && string_view( existingContent ) == content )
                return true;
        }

        FileUtil::ensureParentDirectoryExists( path );
        if ( FileUtil::writeTextFile( path, content ) == false )
        {
            SW_LOG_ERROR( "Failed to write %#", path );
            return false;
        }
        SW_LOG_TRACE( "Generated  : %#", path );
        return true;
    }

    bool GeneratedFileUtil::writeStamp( const string& stampPath, const string& inputFile, uint64 inputWriteTime, const vector<StampDependency>& listDependency )
    {
        string stampText = inputFile + "\n" + GeneratedFilesInternal::kStampInputTimeKey + " " + to_string( inputWriteTime ) + "\n";
        for ( const StampDependency& dependency : listDependency )
            stampText += string( GeneratedFilesInternal::kStampDependencyKey ) + " " + to_string( dependency._writeTime ) + " " + dependency._path + "\n";
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

    bool GeneratedFileUtil::readStampDependencies( const string& stampPath, vector<string>& outListPath )
    {
        string stampText;
        if ( FileUtil::readTextFile( stampPath, stampText ) == false )
            return false;
        GeneratedFilesInternal::StampRecord record;
        if ( GeneratedFilesInternal::parseStamp( stampText, record ) == false )
            return false;
        for ( const auto& [writeTime, path] : record._listDependency )
            outListPath.emplace_back( path );
        return true;
    }

    uint64 GeneratedFileUtil::markRunStart( const string& outputDir )
    {
        const string markerPath = FileUtil::joinPath( outputDir, GeneratedFilesInternal::kRunMarkerName );
        if ( FileUtil::writeTextFile( markerPath, "ReflectionParser run marker - its write time is the start of the last run\n" ) == false )
            return 0;
        return GeneratedFilesInternal::getWriteTime( markerPath );
    }

    string_view GeneratedFileUtil::findRecordedSourcePath( const string_view generatedText, const ParserConfig& config )
    {
        const string& marker    = config._emitSourcePathMarker;
        const size_t  markerPos = generatedText.find( marker );
        if ( markerPos == string_view::npos )
            return {};

        const size_t valueStart = markerPos + marker.size();
        size_t       lineEnd    = generatedText.find( '\n', valueStart );
        if ( lineEnd == string_view::npos )
            lineEnd = generatedText.size();
        return StringUtil::trim( generatedText.substr( valueStart, lineEnd - valueStart ) );
    }

    bool GeneratedFileUtil::isPlaceholder( const string_view generatedText, const ParserConfig& config )
    {
        if ( generatedText.find( config._emitPlaceholderMarker ) != string_view::npos )
            return true;
        return generatedText.find( config._emitRegenByParserMarker ) != string_view::npos &&
               generatedText.find( config._emitRegisterTypeMarker ) == string_view::npos &&
               generatedText.find( config._emitRegisterEnumMarker ) == string_view::npos &&
               generatedText.find( config._emitFlagOpsMarker ) == string_view::npos;
    }

    IncrementalCheck::IncrementalCheck( const ParserOptions& options, const ParserConfig& config )
        : _pConfig{ &config }
        , _newestToolWriteTime{ 0 }
    {
        vector<string> listToolFile;
        listToolFile.push_back( FileUtil::getExecutablePath() );
        listToolFile.push_back( options._builtinsPath );
        listToolFile.push_back( options._annotationMetaPath );
        for ( const string& configFile : config._listLoadedFile )
            listToolFile.push_back( configFile );
        if ( options._emitTemplatesDir.empty() == false )
            FileUtil::collectFiles( options._emitTemplatesDir, config._emitTemplateExtension, listToolFile, false );

        for ( const string& toolFile : listToolFile )
        {
            const uint64 writeTime = GeneratedFilesInternal::getWriteTime( toolFile );
            if ( writeTime > _newestToolWriteTime )
                _newestToolWriteTime = writeTime;
        }
    }

    bool IncrementalCheck::isUpToDate( const string& inputFile, const GeneratedPaths& paths ) const
    {
        const uint64 stampTime = GeneratedFilesInternal::getWriteTime( paths._stampPath );
        if ( stampTime == 0 || FileUtil::fileExists( paths._cppPath ) == false || FileUtil::fileExists( paths._headerPath ) == false )
            return false;

        // 스탬프 파일의 시각은 **도구**가 더 새로운지만 가린다. 입력은 스탬프에 적힌 "파싱 전에 본 시각" 과 같은지로 본다(아래).
        const uint64 inputTime = GeneratedFilesInternal::getWriteTime( inputFile );
        if ( inputTime == 0 || stampTime < _newestToolWriteTime )
            return false;

        // 시각이 최신이어도 내용이 자리 표시자면 다시 만든다(산출물이 지워져 CMake 가 구성 때 다시 심은 경우).
        vector<uint8> cppHeadBytes;
        vector<uint8> headerHeadBytes;
        if ( GeneratedFilesInternal::readHead( paths._cppPath, cppHeadBytes ) == false ||
             GeneratedFilesInternal::readHead( paths._headerPath, headerHeadBytes ) == false )
            return false;
        if ( GeneratedFileUtil::isPlaceholder( GeneratedFilesInternal::toView( cppHeadBytes ), *_pConfig ) ||
             GeneratedFileUtil::isPlaceholder( GeneratedFilesInternal::toView( headerHeadBytes ), *_pConfig ) )
            return false;

        // 헤더를 **옮기기만** 하면 내용도 시각도 그대로라 시각 비교는 "최신" 이라고 답한다. 그런데 .gen.cpp 는 원본을 절대
        // 경로로 #include 하므로 그대로 두면 없는 경로를 가리켜 빌드가 깨진다. 스탬프에 적힌 원본 경로를 대조해 이동을 잡는다.
        //
        // 예전에는 .gen.cpp 머리말(`// Source: …`)을 대조했는데, 리플렉트된 타입이 **없는** 헤더의 산출물에는 그 머리말이
        // 없어서 그런 헤더는 한 번도 "최신" 이 되지 못하고 **파서가 돌 때마다 다시 파싱**됐다(Engine 의 TypeRegistry.h ·
        // ReflectionMacros.h, GameFramework 의 한 개 — Engine 파서 호출마다 약 1 초).
        string stampText;
        if ( FileUtil::readTextFile( paths._stampPath, stampText ) == false )
            return false;
        // 첫 줄은 원본 경로, 다음은 `input <파싱 전에 본 쓰기 시각>` 과 `dep <시각> <경로>` 줄들. 옛 꼴(경로 한 줄)은 시각이 없으니 한 번 다시 만든다.
        GeneratedFilesInternal::StampRecord record;
        if ( GeneratedFilesInternal::parseStamp( stampText, record ) == false || record._sourcePath != string_view( inputFile ) )
            return false;
        // **같아야** 최신이다. 파싱하는 동안 저장한 편집은 스탬프보다 오래된 시각을 가질 수 있다 — 크고 작음이 아니라 같음으로 본다.
        if ( record._inputWriteTime != inputTime )
            return false;
        // include 한 헤더도 같아야 한다. 없어졌으면(시각 0) 다시 만든다.
        for ( const auto& [recordedTime, dependencyPath] : record._listDependency )
        {
            const string dependencyKey( dependencyPath );
            auto         cacheIt = _mapDependencyWriteTime.find( dependencyKey );
            if ( cacheIt == _mapDependencyWriteTime.end() )
                cacheIt = _mapDependencyWriteTime.emplace( dependencyKey, GeneratedFilesInternal::getWriteTime( dependencyPath ) ).first;
            if ( recordedTime == 0 || cacheIt->second != recordedTime )
                return false;
        }
        return true;
    }
} // namespace sw
