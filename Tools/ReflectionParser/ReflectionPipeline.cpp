#include "pch.h"

#include "ReflectionParser/ReflectionPipeline.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
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
            /**
             * @brief `--dump` — 헤더 하나에서 뽑은 것을 사람이 읽는 꼴로 표준 출력에 한 번에 씁니다.
             * @details "왜 이 프로퍼티가 인스펙터에 없나 · 왜 이 컴포넌트를 씬이 못 찾나" 를 묻는 자리다 — 파서가 무엇을 봤는지 보여 준다.
             *          타입(부모 · 팩토리 · 추상), 프로퍼티(타입 · 값 자리 · 컨테이너 · 범위 ·
             *          플래그), 함수, enum 값까지 적는다. 헤더 여럿을 동시에 쓸 수 있어 한 덩어리로 내보낸다.
             */
            static void printParsedHeader( const string& inputFile, const ParsedHeader& parsed )
            {
                StringBuilder<constant::kMaxBuffer8192> out;
                out.appendFormat( "== %#\n", inputFile );
                for ( const ParsedTypeInfo& type : parsed._listType )
                {
                    out.appendFormat( "REFLECT %#", type._fullyQualifiedName );
                    if ( type._parentFQN.empty() == false )
                        out.appendFormat( " : %#", type._parentFQN );
                    if ( type._bComponentFactory == SW_TRUE )
                        out.append( "  [component factory]" );
                    if ( type._bAbstract == SW_TRUE )
                        out.append( "  [abstract]" );
                    if ( type._bStatic == SW_TRUE )
                        out.append( "  [static]" );
                    out.append( "\n" );
                    for ( const ParsedPropertyInfo& prop : type._listProperty )
                    {
                        out.appendFormat( "  PROPERTY %# : %#", prop._name, prop._typeName );
                        if ( prop._bIsBitField == SW_TRUE )
                            out.append( "  [bit field - located at runtime]" );
                        else if ( prop._bIsAccessor == SW_TRUE )
                            out.appendFormat( "  [accessor %#()]", prop._memberName );
                        if ( prop._bIsContainer == SW_TRUE )
                        {
                            out.appendFormat( "  [container %#", prop._containerType );
                            if ( prop._keyTypeName.empty() == false )
                                out.appendFormat( " key=%#", prop._keyTypeName );
                            if ( prop._elementTypeName.empty() == false )
                                out.appendFormat( " element=%#", prop._elementTypeName );
                            out.append( "]" );
                        }
                        if ( prop._bHasMinRange == SW_TRUE )
                            out.appendFormat( "  Min=%#", prop._minRange );
                        if ( prop._bHasMaxRange == SW_TRUE )
                            out.appendFormat( "  Max=%#", prop._maxRange );
                        if ( prop._defaultValue.empty() == false )
                            out.appendFormat( "  Default=\"%#\"", prop._defaultValue );
                        if ( prop._bAssetPath == SW_TRUE )
                            out.appendFormat( "  [asset %#]", prop._assetType.empty() ? "any" : prop._assetType.c_str() );
                        const pair<uint8, const utf8*> arrFlag[] = {
                            {       prop._bReadOnly,        "ReadOnly"},
                            {      prop._bTransient,       "Transient"},
                            {   prop._bXmlAttribute,    "XmlAttribute"},
                            {    prop._bPolymorphic,     "Polymorphic"},
                            {    prop._bSkipIfEmpty,     "SkipIfEmpty"},
                            {prop._bHideInInspector, "HideInInspector"},
                        };
                        for ( const auto& [bSet, pFlagName] : arrFlag )
                        {
                            if ( bSet == SW_TRUE )
                                out.appendFormat( "  [%#]", pFlagName );
                        }
                        for ( const string& alias : prop._listAlias )
                            out.appendFormat( "  alias=%#", alias );
                        out.append( "\n" );
                    }
                    for ( const ParsedFunctionInfo& method : type._listMethod )
                    {
                        out.appendFormat( "  FUNCTION %#(", method._name );
                        for ( size_t paramIndex = 0; paramIndex < method._listParameterTypeName.size(); ++paramIndex )
                            out.appendFormat( "%#%#", paramIndex == 0 ? "" : ", ", method._listParameterTypeName[paramIndex] );
                        out.appendFormat( ") -> %#", method._returnTypeName.empty() ? "void" : method._returnTypeName.c_str() );
                        if ( method._bStatic == SW_TRUE )
                            out.append( "  [static]" );
                        if ( method._bCallInEditor == SW_TRUE )
                            out.append( "  [CallInEditor]" );
                        if ( method._editorPreview.empty() == false )
                            out.appendFormat( "  [EditorPreview=%#]", method._editorPreview );
                        if ( method._bConstructor == SW_TRUE )
                            out.append( "  [constructor]" );
                        out.append( "\n" );
                    }
                }
                for ( const ParsedEnumInfo& enumInfo : parsed._listEnum )
                {
                    out.appendFormat( "ENUM %# : %#%#\n", enumInfo._fullyQualifiedName, enumInfo._underlyingType,
                                      enumInfo._bIsBitFlag == SW_TRUE ? "  [Flags]" : "" );
                    for ( const ParsedEnumeratorInfo& enumerator : enumInfo._listEnumerator )
                        out.appendFormat( "  %# = %#\n", enumerator._name, enumerator._value );
                }
                std::fwrite( out.c_str(), 1, out.size(), stdout );
                std::fflush( stdout );
            }

            /** @brief `clang_getInclusions` 가 넘기는 것을 모으는 자리입니다. */
            struct InclusionCollector
            {
                CXTranslationUnit        _translationUnit;
                string                   _outputDirPrefix;     ///< 슬래시 · 끝에 `/` — 이 아래는 이 단계 자신의 산출물이라 의존이 아니다
                string                   _outputDirRealPrefix; ///< 같은 폴더의 실제 경로(8.3 짧은 이름 · 심볼릭 링크를 푼 것). include 경로는 실제 경로로 온다
                uint64                   _runStartTime;
                vector<StampDependency>* _pListDependency;
            };

            /** @brief include 된 파일 하나를 의존으로 적습니다 — 시스템 헤더 · 출력 폴더 · 디스크에 없는 파일(묶음 TU 의 가상 원본)은 뺍니다. */
            static void collectInclusion( CXFile includedFile, CXSourceLocation*, uint32, CXClientData clientData )
            {
                InclusionCollector& collector = *static_cast<InclusionCollector*>( clientData );
                if ( clang_Location_isInSystemHeader( clang_getLocationForOffset( collector._translationUnit, includedFile, 0 ) ) != 0 )
                    return;
                CXString realName = clang_File_tryGetRealPathName( includedFile );
                string   path     = clang_getCString( realName ) != nullptr ? string( clang_getCString( realName ) ) : string();
                clang_disposeString( realName );
                if ( path.empty() )
                {
                    CXString fileName = clang_getFileName( includedFile );
                    path              = clang_getCString( fileName ) != nullptr ? string( clang_getCString( fileName ) ) : string();
                    clang_disposeString( fileName );
                }
                path = FileUtil::normalizeSeparators( path );
                if ( path.empty() || StringUtil::startsWith( path, collector._outputDirPrefix, true ) ||
                     ( collector._outputDirRealPrefix.empty() == false && StringUtil::startsWith( path, collector._outputDirRealPrefix, true ) ) )
                    return;
                const uint64 writeTime = GeneratedFileUtil::getWriteTime( path );
                if ( writeTime == 0 )
                    return;
                for ( const StampDependency& existing : *collector._pListDependency )
                {
                    if ( existing._path == path )
                        return;
                }
                // 이번 실행이 시작된 뒤에 바뀐 것은 0 으로 — 파싱하는 동안 저장한 편집을 다음 실행이 놓치지 않게(`markRunStart`).
                collector._pListDependency->push_back( StampDependency{ std::move( path ), writeTime >= collector._runStartTime ? 0 : writeTime } );
            }

            /** @brief 번역 단위가 include 한 프로젝트 헤더들을 모읍니다(입력 자신도 들어간다 — 해가 없다). */
            static vector<StampDependency> collectDependencies( CXTranslationUnit translationUnit, const string& outputDir, uint64 runStartTime )
            {
                vector<StampDependency> listDependency;
                // 출력 폴더는 받은 꼴과 실제 경로 둘로 거른다 — clang 은 include 한 파일을 실제 경로로 주는데, 받은 경로는 8.3 짧은 이름
                // (`RUNNER~1`)이거나 링크일 수 있다(Windows CI 의 TEMP 가 그렇다).
                std::error_code    errorCode;
                const auto         realOutputDir = std::filesystem::canonical( std::filesystem::path( outputDir.c_str() ), errorCode );
                InclusionCollector collector{ translationUnit, FileUtil::normalizeSeparators( outputDir ),
                                              errorCode ? string() : FileUtil::normalizeSeparators( string( realOutputDir.generic_string().c_str() ) ),
                                              runStartTime, &listDependency };
                for ( string* pPrefix : { &collector._outputDirPrefix, &collector._outputDirRealPrefix } )
                {
                    if ( pPrefix->empty() == false && pPrefix->back() != '/' )
                        pPrefix->push_back( '/' );
                }
                clang_getInclusions( translationUnit, &collectInclusion, &collector );
                return listDependency;
            }

            /** @brief Makefile depfile 의 경로 한 조각 — 슬래시로, 공백 · `#` · `$` 를 이스케이프합니다. */
            static string escapeDepfilePath( string_view path )
            {
                string escaped;
                escaped.reserve( path.size() + 8 );
                for ( const utf8 character : path )
                {
                    if ( character == ' ' || character == '#' )
                        escaped.push_back( '\\' );
                    if ( character == '$' )
                        escaped.push_back( '$' );
                    escaped.push_back( character == '\\' ? '/' : character );
                }
                return escaped;
            }

            /** @brief 묶음 TU 의 주 파일 이름입니다. 디스크에는 없고 내용으로만 넘깁니다. */
            static constexpr const utf8* kBatchSourceName = "ReflectionParser.batch.cpp";

            /**
             * @brief 소스 텍스트에 리플렉션 매크로 키워드(`kSourceKeywordScan`)가 있는지 봅니다.
             * @details 없으면 무거운 libclang 파싱을 통째로 건너뛰고 빈 산출물을 씁니다. 키워드는 표(`kSourceKeywordScan`) 하나에서 옵니다.
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
        , _runStartTime{ 0 }
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

        // 이번 실행의 시작을 먼저 적는다 — 이 뒤에 바뀐 의존은 0 으로 적혀 다음 실행이 다시 본다(`GeneratedFileUtil::markRunStart`).
        _runStartTime = GeneratedFileUtil::markRunStart( _pOptions->_outputDir );

        vector<PendingInput> listPending;
        vector<string>       listVisitedInput;
        for ( const string& inputFile : _pOptions->_listInputFile )
        {
            // 같은 헤더가 두 번 오면 한 번만 처리한다. 묶음 TU 에서는 둘째 대상이 선언을 하나도 못 받아 빈 산출물로 첫째를 덮는다.
            if ( std::find( listVisitedInput.begin(), listVisitedInput.end(), inputFile ) != listVisitedInput.end() )
                continue;
            listVisitedInput.push_back( inputFile );

            GeneratedPaths paths = GeneratedFileUtil::makePaths( _pOptions->_outputDir, inputFile, config );
            // `--dump` 는 무엇을 뽑았는지 보려는 실행이라 최신이어도 다시 파싱한다.
            if ( _pOptions->_bDump == false && _incrementalCheck.isUpToDate( inputFile, paths ) )
            {
                SW_LOG_TRACE( "Up-to-date, skipping AST parsing: %#", inputFile );
                ++upToDate;
                continue;
            }

            // 시각을 **읽기 전에** 잰다. 읽은 뒤에 저장한 편집은 이 값과 달라 다음 실행이 다시 파싱한다(`writeStamp` 설명).
            const uint64 inputWriteTime = GeneratedFileUtil::getWriteTime( inputFile );
            string       content;
            if ( FileUtil::readTextFile( inputFile, content ) == false )
            {
                SW_LOG_ERROR( "Failed to read input: %#", inputFile );
                ++errorCount;
                continue;
            }

            // 리플렉션 매크로가 지워진 뒤에도 이전 registrar 가 남아 계속 컴파일되지 않도록 빈 산출물을 쓴다.
            if ( ReflectionPipelineInternal::hasReflectionKeywords( content ) == false )
            {
                SW_LOG_TRACE( "No reflection annotations found, emitting empty output: %#", inputFile );
                ++noReflect;
                if ( writeOutputs( inputFile, paths, ParsedHeader{}, inputWriteTime, {} ) == false )
                    ++errorCount;
                continue;
            }

            listPending.push_back( PendingInput{ &inputFile, std::move( content ), std::move( paths ), inputWriteTime } );
        }

        SW_LOG_INFO( "Parsing %# of %# input(s) (%# up to date, %# without annotations).", listPending.size(),
                     _pOptions->_listInputFile.size(), upToDate, noReflect );
        errorCount += parsePending( listPending );

        if ( writeFlagOpsUmbrella() == false )
            ++errorCount;
        if ( writeDepfile() == false )
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
        // 묶음의 의존은 묶음 전체로 적는다. `#pragma once` 로 두 번째 include 는 새 파일 항목이 생기지 않아, 헤더별로 가르면 먼저 include 한
        // 헤더에만 붙는다 — 그러면 그 헤더만 다시 파싱되고 나머지는 옛 것을 든다.
        const vector<StampDependency> listDependency =
            ReflectionPipelineInternal::collectDependencies( context.getTranslationUnit(), _pOptions->_outputDir, _runStartTime );
        for ( size_t index = 0; index < listPending.size(); ++index )
        {
            const string& inputFile = *listPending[index]._pInputFile;
            if ( index >= listHeader.size() || listHeader[index]._bHasError == SW_TRUE )
            {
                SW_LOG_ERROR( "AST analysis failed: %#", inputFile );
                ++outErrorCount;
                continue;
            }
            if ( writeOutputs( inputFile, listPending[index]._paths, listHeader[index], listPending[index]._inputWriteTime, listDependency ) == false )
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

        return writeOutputs( inputFile, pending._paths, visitor.getParsedHeaders().front(), pending._inputWriteTime,
                             ReflectionPipelineInternal::collectDependencies( context.getTranslationUnit(), _pOptions->_outputDir, _runStartTime ) );
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

    bool ReflectionPipeline::writeOutputs( const string& inputFile, const GeneratedPaths& paths, const ParsedHeader& parsed, uint64 inputWriteTime,
                                           const vector<StampDependency>& listDependency ) const
    {
        const ParserConfig& config = _pSession->_config;
        if ( _pOptions->_bDump && ( parsed._listType.empty() == false || parsed._listEnum.empty() == false ) )
            ReflectionPipelineInternal::printParsedHeader( inputFile, parsed );

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
                              GeneratedFileUtil::writeStamp( paths._stampPath, inputFile, inputWriteTime, listDependency );
        if ( bWritten == false )
            SW_LOG_ERROR( "Code generation failed: %#", inputFile );
        return bWritten;
    }

    /**
     * @details 이 파일은 타깃의 모든 TU 에 `/FI` 로 강제 include 됩니다. 트레이트는 열거형이 보이는 곳이면 어디서나 함께
     *          보여야 하기 때문입니다. 그래서 **원본 헤더는 절대 들이지 않습니다** — 들이면 그 헤더가 끌어오는 것이 모든 TU 에 들어가
     *          다른 헤더들의 include 누락을 통째로 가립니다. `.gen.h` 자신이 열거형을 전방 선언하므로 그것 하나만 모으면 됩니다.
     */
    bool ReflectionPipeline::writeDepfile() const
    {
        if ( _pOptions->_depfilePath.empty() )
            return true;

        const ParserConfig& config = _pSession->_config;
        vector<string>      listTarget;
        vector<string>      listDependency;
        for ( const string& inputFile : _pOptions->_listInputFile )
        {
            const GeneratedPaths paths = GeneratedFileUtil::makePaths( _pOptions->_outputDir, inputFile, config );
            listTarget.push_back( paths._cppPath );
            listTarget.push_back( paths._headerPath );
            (void)GeneratedFileUtil::readStampDependencies( paths._stampPath, listDependency );
        }
        listTarget.push_back( FileUtil::joinPath( _pOptions->_outputDir, config._emitFlagOpsHeader ) );
        std::sort( listDependency.begin(), listDependency.end() );
        listDependency.erase( std::unique( listDependency.begin(), listDependency.end() ), listDependency.end() );

        // 목표는 이 단계의 모든 산출물(CMake 의 OUTPUT 과 같은 목록), 의존은 스탬프들의 합. 경로는 절대 · 슬래시.
        string text;
        for ( const string& target : listTarget )
            text += ReflectionPipelineInternal::escapeDepfilePath( target ) + " ";
        text += ":";
        for ( const string& dependency : listDependency )
            text += " \\\n  " + ReflectionPipelineInternal::escapeDepfilePath( dependency );
        text += "\n";
        if ( GeneratedFileUtil::writeIfChanged( _pOptions->_depfilePath, text ) == false )
        {
            SW_LOG_ERROR( "Failed to write depfile %#", _pOptions->_depfilePath );
            return false;
        }
        return true;
    }

    bool ReflectionPipeline::writeFlagOpsUmbrella() const
    {
        const ParserConfig& config = _pSession->_config;

        CodeEmitBuffer buffer;
        CodeEmit       emit( buffer );
        emit.line( config._emitAutoGeneratedBanner );
        emit.line( emitdirective::kPragmaOnce );
        emit.blank();
        emit.line( emitdirective::kIfndefParser );

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
            emit.line( emitdirective::kNoEnumFlags );
        emit.line( emitdirective::kEndif );

        return GeneratedFileUtil::writeIfChanged( FileUtil::joinPath( _pOptions->_outputDir, config._emitFlagOpsHeader ), buffer.view() );
    }
} // namespace sw
