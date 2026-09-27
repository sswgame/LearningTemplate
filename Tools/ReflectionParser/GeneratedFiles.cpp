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

        FileUtil::createParentDirectory( path );
        if ( FileUtil::writeTextFile( path, content ) == false )
        {
            SW_LOG_ERROR( "Failed to write %#", path );
            return false;
        }
        SW_LOG_TRACE( "Generated  : %#", path );
        return true;
    }

    bool GeneratedFileUtil::writeStamp( const string& stampPath, const string& inputFile )
    {
        if ( FileUtil::writeTextFile( stampPath, inputFile ) == false )
        {
            SW_LOG_ERROR( "Failed to write %#", stampPath );
            return false;
        }
        return true;
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

        const uint64 inputTime = GeneratedFilesInternal::getWriteTime( inputFile );
        if ( inputTime == 0 || stampTime < inputTime || stampTime < _newestToolWriteTime )
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
        string recordedSource;
        if ( FileUtil::readTextFile( paths._stampPath, recordedSource ) == false )
            return false;
        return recordedSource == inputFile;
    }
} // namespace sw
