#include "pch.h"

#include "ReflectionParser/ReflectionPipeline.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"

#include "ReflectionParser/AstVisitor.h"
#include "ReflectionParser/CodeEmit.h"
#include "ReflectionParser/CodeGenerator.h"
#include "ReflectionParser/ParserContext.h"
#include "ReflectionParser/ParserDefines.h"
#include "ReflectionParser/ParserOptions.h"
#include "ReflectionParser/ParserSession.h"
#include "ReflectionParser/ParserUtil.h"

SW_LOG_CALLER( "ReflectionPipeline" );
namespace sw
{
    namespace
    {
        struct ReflectionPipelineInternal
        {
            /** @brief 묶음 TU 의 주 파일 이름입니다. 디스크에는 없고 내용으로만 넘깁니다. */
            static constexpr const utf8* kBatchSourceName = "ReflectionParser.batch.cpp";

            /**
             * @brief 소스 텍스트에 리플렉션 매크로 키워드(`kSourceKeywordScan`)가 있는지 봅니다.
             * @details 없으면 무거운 libclang 파싱을 통째로 건너뛰고 빈 산출물을 씁니다. 예전에는 키워드 표가 따로 있는데도
             *          이 함수가 첫 글자 네 개와 철자를 손으로 들고 있었습니다.
             */
            static bool hasReflectionKeywords( const string_view source )
            {
                for ( const utf8* pKeyword : kSourceKeywordScan )
                {
                    if ( source.find( pKeyword ) != string_view::npos )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ReflectionPipeline::ReflectionPipeline( const ParserOptions& options, const ParserSession& session )
        : _pOptions{ &options }
        , _pSession{ &session }
        , _incrementalCheck{ options, session._config }
        , _listIncludePath{}
    {
        // 출력 디렉터리를 include 경로 맨 앞에 한 번만 넣는다(원본이 다른 생성 헤더를 include 할 수 있다).
        _listIncludePath.reserve( options._listIncludePath.size() + 1 );
        _listIncludePath.push_back( options._outputDir );
        for ( const string& includePath : options._listIncludePath )
            _listIncludePath.push_back( includePath );
    }

    int32 ReflectionPipeline::run()
    {
        const ParserConfig&     config     = _pSession->_config;
        int32                   errorCount = 0;
        [[maybe_unused]] uint32 upToDate   = 0;
        [[maybe_unused]] uint32 noReflect  = 0;

        vector<PendingInput> listPending;
        vector<string>       listVisitedInput;
        for ( const string& inputFile : _pOptions->_listInputFile )
        {
            // 같은 헤더가 두 번 오면 한 번만 처리한다. 묶음 TU 에서는 둘째 대상이 선언을 하나도 못 받아 빈 산출물로 첫째를 덮는다.
            if ( std::find( listVisitedInput.begin(), listVisitedInput.end(), inputFile ) != listVisitedInput.end() )
                continue;
            listVisitedInput.push_back( inputFile );

            GeneratedPaths paths = GeneratedFileUtil::makePaths( _pOptions->_outputDir, inputFile, config );
            if ( _incrementalCheck.isUpToDate( inputFile, paths ) )
            {
                SW_LOG_TRACE( "Up-to-date, skipping AST parsing: %#", inputFile );
                ++upToDate;
                continue;
            }

            string content;
            if ( FileUtil::readTextFile( inputFile, content ) == false )
            {
                SW_LOG_ERROR( "Failed to read input: %#", inputFile );
                ++errorCount;
                continue;
            }

            // 리플렉션 매크로가 지워진 뒤에도 예전 registrar 가 남아 계속 컴파일되지 않도록 빈 산출물을 쓴다.
            if ( ReflectionPipelineInternal::hasReflectionKeywords( content ) == false )
            {
                SW_LOG_TRACE( "No reflection annotations found, emitting empty output: %#", inputFile );
                ++noReflect;
                if ( writeOutputs( inputFile, paths, ParsedHeader{} ) == false )
                    ++errorCount;
                continue;
            }

            listPending.push_back( PendingInput{ &inputFile, std::move( content ), std::move( paths ) } );
        }

        SW_LOG_INFO( "Parsing %# of %# input(s) (%# up to date, %# without annotations).", listPending.size(),
                     _pOptions->_listInputFile.size(), upToDate, noReflect );
        errorCount += parsePending( listPending );

        if ( writeFlagOpsUmbrella() == false )
            ++errorCount;
        return errorCount;
    }

    /**
     * @details **파싱 비용의 거의 전부는 헤더 자신이 아니라 공통 include 다.** 강제 include(`Core/CoreMinimal.h`) 하나만 파싱하는 데
     *          0.52 초, 엔진 공통 헤더(Windows · D3D)까지 가면 1.0 초인데, Engine 의 입력 27 개를 한 TU 로 묶어도 1.04 초였다
     *          (Release, 실측). 헤더마다 TU 를 따로 만들면 같은 것을 헤더 수만큼 다시 파싱하고, 워커 16 개가 한꺼번에 파싱하면
     *          메모리 대역을 다퉈 한 개가 1.0 → 2.1 초로 늘어난다.
     */
    int32 ReflectionPipeline::parsePending( const vector<PendingInput>& listPending ) const
    {
        if ( listPending.empty() )
            return 0;
        if ( listPending.size() == 1 )
            return parseAndGenerate( listPending.front() ) ? 0 : 1;

        int32 errorCount = 0;
        if ( parseBatch( listPending, errorCount ) )
            return errorCount;

        // 묶음이 clang 오류로 실패했다. 어느 헤더가 깨졌는지 헤더마다 파싱해 그 헤더의 오류로 알리고, 나머지는 그대로 만든다.
        SW_LOG_WARNING( "One translation unit for %# input(s) did not parse; parsing them one by one.", listPending.size() );
        return parseEachInParallel( listPending );
    }

    bool ReflectionPipeline::parseBatch( const vector<PendingInput>& listPending, int32& outErrorCount ) const
    {
        // 묶음 TU 의 원본 — 입력 헤더를 차례로 include 한다. 디스크에 쓰지 않고 내용으로 넘긴다(입력 헤더들도 이미 읽은 내용으로).
        const string              batchPath = FileUtil::joinPath( _pOptions->_outputDir, ReflectionPipelineInternal::kBatchSourceName );
        string                    batchSource;
        vector<string>            listTargetFile;
        vector<ParserUnsavedFile> listUnsaved;
        listTargetFile.reserve( listPending.size() );
        listUnsaved.reserve( listPending.size() + 1 );
        listUnsaved.push_back( ParserUnsavedFile{ &batchPath, &batchSource } );
        for ( const PendingInput& pending : listPending )
        {
            batchSource += "#include \"";
            batchSource += *pending._pInputFile;
            batchSource += "\"\n";
            listTargetFile.push_back( *pending._pInputFile );
            listUnsaved.push_back( ParserUnsavedFile{ pending._pInputFile, &pending._content } );
        }

        // clang 오류는 여기서 알리지 않는다 — 헤더마다 다시 파싱할 때 그 헤더의 오류로 나온다.
        ParserContext context( _pSession->_config );
        if ( context.parse( batchPath, _listIncludePath, listUnsaved, false ) == false )
            return false;

        AstVisitor visitor( context.getTranslationUnit(), *_pSession, listTargetFile );
        visitor.visit();
        const vector<ParsedHeader>& listHeader = visitor.getParsedHeaders();
        for ( size_t index = 0; index < listPending.size(); ++index )
        {
            const string& inputFile = *listPending[index]._pInputFile;
            if ( index >= listHeader.size() || listHeader[index]._bHasError == SW_TRUE )
            {
                SW_LOG_ERROR( "AST analysis failed: %#", inputFile );
                ++outErrorCount;
                continue;
            }
            if ( writeOutputs( inputFile, listPending[index]._paths, listHeader[index] ) == false )
                ++outErrorCount;
        }
        return true;
    }

    bool ReflectionPipeline::parseAndGenerate( const PendingInput& pending ) const
    {
        const string& inputFile = *pending._pInputFile;
        SW_LOG_TRACE( "── Parsing: %#", inputFile );

        ParserContext                   context( _pSession->_config );
        const vector<ParserUnsavedFile> listUnsaved{
            ParserUnsavedFile{ pending._pInputFile, &pending._content }
        };
        if ( context.parse( inputFile, _listIncludePath, listUnsaved ) == false )
        {
            SW_LOG_ERROR( "Parse failed: %#", inputFile );
            return false;
        }

        AstVisitor visitor( context.getTranslationUnit(), *_pSession );
        if ( visitor.visit() == false || visitor.hasError() )
        {
            SW_LOG_ERROR( "AST analysis failed: %#", inputFile );
            return false;
        }

        return writeOutputs( inputFile, pending._paths, visitor.getParsedHeaders().front() );
    }

    int32 ReflectionPipeline::parseEachInParallel( const vector<PendingInput>& listPending ) const
    {
        if ( listPending.empty() )
            return 0;

        uint32 workerCount = std::thread::hardware_concurrency();
        if ( workerCount == 0 )
            workerCount = 1;
        if ( workerCount > listPending.size() )
            workerCount = static_cast<uint32>( listPending.size() );

        TaskManager taskManager;
        if ( taskManager.initialize( workerCount ) == false )
        {
            SW_LOG_ERROR( "Failed to initialize TaskManager." );
            return static_cast<int32>( listPending.size() );
        }

        atomic<int32> errorCount{ 0 };
        for ( const PendingInput& pending : listPending )
        {
            const PendingInput* pPending = &pending;
            TaskHandle          handle   = taskManager.emplaceTask(
                "ParseHeader",
                SW_DELEGATE_LAMBDA( TaskDelegate, [this, pPending, &errorCount]()
                       {
                if ( parseAndGenerate( *pPending ) == false )
                    errorCount.fetch_add( 1 );
            } ) );
            handle.submit();
        }

        taskManager.waitAll();
        taskManager.shutdown();
        return errorCount.load();
    }

    bool ReflectionPipeline::writeOutputs( const string& inputFile, const GeneratedPaths& paths, const ParsedHeader& parsed ) const
    {
        const ParserConfig& config = _pSession->_config;

        const CodeGenerator generator( parsed, inputFile, *_pSession, _pOptions->_sourceRoot );
        string              headerText;
        if ( generator.makeHeaderText( headerText ) == false )
        {
            SW_LOG_ERROR( "Code generation failed: %#", inputFile );
            return false;
        }

        // **다른 헤더가 이미 이 이름으로 썼는가.** 생성 파일 이름은 소스의 **파일 이름만** 으로 짓는다
        // (`ParserUtil::makeGeneratedPath`). 그래서 한 모듈 안에 같은 이름의 헤더가 둘 있으면 나중에 도는 쪽이 앞의 것을
        // 덮고, **앞 헤더의 타입들은 아무 말 없이 등록되지 않는다.** 증상은 한참 뒤 "씬이 그 컴포넌트를 못 찾는다" 로
        // 나타나서 원인을 여기서 찾기 어렵다. 머리에 적어 둔 소스 경로로 그 상황을 잡는다.
        //
        // 헤더를 **옮긴** 경우(옛 경로가 더는 없다)는 정상이므로 조용히 덮어쓴다. 그러지 않으면 파일을 옮길 때마다 빌드가 막힌다.
        string existingCpp;
        if ( FileUtil::fileExists( paths._cppPath ) && FileUtil::readTextFile( paths._cppPath, existingCpp ) )
        {
            const string_view recordedSource = GeneratedFileUtil::findRecordedSourcePath( existingCpp, config );
            const bool        bOtherOwner    = recordedSource.empty() == false &&
                                     FileUtil::normalizeSeparators( recordedSource ) != FileUtil::normalizeSeparators( inputFile ) &&
                                     FileUtil::fileExists( recordedSource );
            if ( bOtherOwner )
            {
                SW_LOG_ERROR( "Generated file name collision: '%#' and '%#' both generate '%#'. "
                              "Two reflected headers in the same module cannot share a file name.",
                              recordedSource, inputFile, paths._cppPath );
                return false;
            }
        }

        const bool bWritten = GeneratedFileUtil::writeIfChanged( paths._cppPath, generator.makeSourceText() ) &&
                              GeneratedFileUtil::writeIfChanged( paths._headerPath, headerText ) &&
                              GeneratedFileUtil::writeStamp( paths._stampPath, inputFile );
        if ( bWritten == false )
            SW_LOG_ERROR( "Code generation failed: %#", inputFile );
        return bWritten;
    }

    /**
     * @details 이 파일은 타깃의 모든 TU 에 `/FI` 로 강제 include 됩니다. 트레이트는 열거형이 보이는 곳이면 어디서나 함께
     *          보여야 하기 때문입니다. 그래서 **원본 헤더는 절대 들이지 않습니다.** 예전에는 `#include "<원본>.h"` 와
     *          `#include "<원본>.gen.h"` 를 쌍으로 적었고, 그 바람에 Graphics 헤더 넷(다시 `Engine/Common/Common.h` 까지)이
     *          모든 TU 에 들어가 다른 헤더들의 include 누락을 통째로 가렸습니다. 지금은 `.gen.h` 자신이 열거형을 전방 선언하므로
     *          그것 하나만 모으면 됩니다.
     */
    bool ReflectionPipeline::writeFlagOpsUmbrella() const
    {
        const ParserConfig& config = _pSession->_config;

        CodeEmitBuffer buffer;
        CodeEmit       emit( buffer );
        emit.line( config._emitAutoGeneratedBanner );
        emit.line( emitDirectiveConstants::kPragmaOnce );
        emit.blank();
        emit.line( emitDirectiveConstants::kIfndefParser );

        bool bAnyFlags = false;
        for ( const string& inputFile : _pOptions->_listInputFile )
        {
            const string genHeader = ParserUtil::makeGeneratedPath( _pOptions->_outputDir, inputFile, config._emitHeaderExtension );
            string       genText;
            if ( FileUtil::fileExists( genHeader ) == false || FileUtil::readTextFile( genHeader, genText ) == false )
                continue;
            if ( genText.find( config._emitFlagOpsMarker ) == string::npos )
                continue;

            bAnyFlags = true;
            emit.linef( "#include \"%#\"", FileUtil::getFileNamePart( genHeader ) );
        }

        if ( bAnyFlags == false )
            emit.line( emitDirectiveConstants::kNoEnumFlags );
        emit.line( emitDirectiveConstants::kEndif );

        return GeneratedFileUtil::writeIfChanged( FileUtil::joinPath( _pOptions->_outputDir, config._emitFlagOpsHeader ), buffer.view() );
    }
} // namespace sw
