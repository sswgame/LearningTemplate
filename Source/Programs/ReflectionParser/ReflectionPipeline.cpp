#include "pch.h"

#include "Programs/ReflectionParser/ReflectionPipeline.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Programs/ReflectionParser/AstVisitor.h"
#include "Programs/ReflectionParser/CodeEmit.h"
#include "Programs/ReflectionParser/CodeGenerator.h"
#include "Programs/ReflectionParser/ParsedHeaderDump.h"
#include "Programs/ReflectionParser/ParserConfig.h"
#include "Programs/ReflectionParser/ParserContext.h"
#include "Programs/ReflectionParser/ParserDefines.h"
#include "Programs/ReflectionParser/ParserOptions.h"
#include "Programs/ReflectionParser/ParserSession.h"
#include "Programs/ReflectionParser/ParserUtil.h"

#include <clang-c/Index.h>

namespace sw
{
    namespace
    {
        struct ReflectionPipelineInternal
        {
            /** @brief 방출 바이트 로그의 백분율 눈금입니다. */
            static constexpr uint64 kPercentScale = 100;

            /**
             * @brief 칸 이름 @p name 의 XML 속성 이름입니다 — `XMLFields::getAttributeName` 과 같은 규칙이다(직접 적은 이름은 그대로, `_` 를 떼고, 불리언 `_bX` 는 `x`).
             * @details 파서는 엔진을 링크하지 않아 같은 규칙을 여기 둔다. 규칙을 바꾸면 둘을 함께 고친다(칸이 두 이름으로 생성 목록에 드는지 파서 픽스처 `IntegrationTests/ReflectionParser/Fixtures/AssetRefParams.h` 가 본다).
             */
            static string makeXMLAttributeName( const string& name )
            {
                if ( name.empty() || name.front() != '_' )
                    return name;
                string     attribute      = name.substr( 1 );
                const bool bBooleanPrefix = attribute.size() > 1 && attribute[0] == 'b' && 'A' <= attribute[1] && attribute[1] <= 'Z';
                if ( bBooleanPrefix == false )
                    return attribute;
                attribute.erase( 0, 1 );
                attribute[0] = static_cast<utf8>( attribute[0] - 'A' + 'a' );
                return attribute;
            }

            /**
             * @brief 한 모듈의 타입 · 열거형(`FQN\t모듈` 줄)에서 짧은 이름(리프)이 겹치면 둘을 적고 false 입니다.
             * @details 레지스트리는 짧은 이름으로도 찾는다(씬 · 프리팹 · `addComponentByName`). 겹치면 나중에 오른 타입이 앞의 것을 가려
             *          런타임 경고만 남기고 다른 타입을 만든다 — 같은 모듈 안이면 생성 단계에서 막는다. 이름 비교는 `hashed_string` 처럼 대소문자를 무시한다.
             *          모듈 사이의 겹침(게임이 엔진 이름을 다시 쓰는 것)은 올리는 순서의 문제라 런타임 경고(`TypeRegistry::registerClass`)에 남긴다.
             */
            static bool validateLeafNameCollision( const vector<string>& listLine, const string& moduleName )
            {
                unordered_map<string, string> mapLeafToFqn;
                bool                          bUnique = true;
                for ( const string& line : listLine )
                {
                    const string_view fqn        = string_view( line ).substr( 0, line.find( '\t' ) );
                    string            leaf       = StringUtil::toLower( ParserUtil::scopeLeaf( fqn ) );
                    const auto [iter, bInserted] = mapLeafToFqn.emplace( std::move( leaf ), string( fqn ) );
                    if ( bInserted || iter->second == fqn )
                        continue;
                    SW_LOG_ERROR_ID( "ReflectionPipeline.LeafNameCollision",
                                     "Reflected types '%#' and '%#' in module '%#' share the short name '%#' - lookups by short name (scenes, prefabs, addComponentByName) would pick one silently. Rename one.",
                                     iter->second, fqn, moduleName, ParserUtil::scopeLeaf( fqn ) );
                    bUnique = false;
                }
                return bUnique;
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
                CXString               realName = clang_File_tryGetRealPathName( includedFile );
                optional<AbsolutePath> path     = clang_getCString( realName ) != nullptr ? AbsolutePath::tryMake( clang_getCString( realName ) ) : nullopt;
                clang_disposeString( realName );
                if ( path.has_value() == false )
                {
                    CXString fileName = clang_getFileName( includedFile );
                    path              = clang_getCString( fileName ) != nullptr ? AbsolutePath::tryMake( clang_getCString( fileName ) ) : nullopt;
                    clang_disposeString( fileName );
                }
                const bool bSkipPath = path.has_value() == false || FileUtil::isPathUnderRoot( collector._outputDirPrefix, path->str() ) ||
                                       ( collector._outputDirRealPrefix.empty() == false && FileUtil::isPathUnderRoot( collector._outputDirRealPrefix, path->str() ) );
                if ( bSkipPath )
                    return;
                const uint64 writeTime = GeneratedFileUtil::getWriteTime( *path );
                if ( writeTime == 0 )
                    return;
                for ( const StampDependency& existing : *collector._pListDependency )
                {
                    if ( existing._path == *path )
                        return;
                }
                // 이번 실행이 시작된 뒤에 바뀐 것은 0 으로 — 파싱하는 동안 저장한 편집을 다음 실행이 놓치지 않게(`markRunStart`).
                collector._pListDependency->push_back( StampDependency{ std::move( *path ), writeTime >= collector._runStartTime ? 0 : writeTime } );
            }

            /** @brief 번역 단위가 include 한 프로젝트 헤더들을 모읍니다(입력 자신도 들어간다 — 해가 없다). */
            static vector<StampDependency> collectDependencies( CXTranslationUnit translationUnit, const AbsolutePath& outputDir, uint64 runStartTime )
            {
                vector<StampDependency> listDependency;
                // 출력 폴더는 받은 꼴과 실제 경로 둘로 거른다 — clang 은 include 한 파일을 실제 경로로 주는데, 받은 경로는 8.3 짧은 이름
                // (`RUNNER~1`)이거나 링크일 수 있다(Windows CI 의 TEMP 가 그렇다).
                const AbsolutePath realOutputDir = FileUtil::makeCanonicalPath( outputDir ).value_or( AbsolutePath{} );
                InclusionCollector collector{ translationUnit, outputDir.str(), realOutputDir.str(),
                                              runStartTime, &listDependency };
                for ( string* pPrefix : { &collector._outputDirPrefix, &collector._outputDirRealPrefix } )
                {
                    if ( pPrefix->empty() == false && pPrefix->back() != '/' )
                        pPrefix->push_back( '/' );
                }
                clang_getInclusions( translationUnit, &collectInclusion, &collector );
                return listDependency;
            }

            /** @brief `includesFile` 이 찾는 파일과 그 답입니다. */
            struct InclusionSearch
            {
                CXFile _target;
                uint8  _bFound : 1;
            };

            static void findInclusion( CXFile includedFile, CXSourceLocation*, uint32, CXClientData clientData )
            {
                InclusionSearch& search = *static_cast<InclusionSearch*>( clientData );
                if ( clang_File_isEqual( includedFile, search._target ) != 0 )
                    search._bFound = SW_TRUE;
            }

            /** @brief 번역 단위가 @p path 를 include 해 읽었으면 true 입니다. 파일 동일성으로 비교한다(경로 철자 · 슬래시가 달라도 같다). */
            static bool includesFile( CXTranslationUnit translationUnit, const AbsolutePath& path )
            {
                InclusionSearch search{ clang_getFile( translationUnit, path.str().c_str() ), SW_FALSE };
                if ( search._target == nullptr )
                    return false;
                clang_getInclusions( translationUnit, &findInclusion, &search );
                return search._bFound == SW_TRUE;
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

            /** @brief 모듈 PCH 의 원본 헤더 · PCH 파일 이름(출력 폴더 안)입니다. 원본은 `pch.h` 하나를 include 한다. */
            static constexpr const utf8* kModulePchHeaderName = "ReflectionParser.pch.h";
            static constexpr const utf8* kModulePchName       = "ReflectionParser.pch";
            /** @brief @p listExtra 가운데 @p inoutList 에 없는 의존을 더합니다(PCH 를 구울 때 모은 의존을 번역 단위의 의존에 합친다). */
            static void appendDependencies( vector<StampDependency>& inoutList, const vector<StampDependency>& listExtra )
            {
                const size_t ownCount = inoutList.size();
                for ( const StampDependency& extra : listExtra )
                {
                    bool bFound = false;
                    for ( size_t index = 0; index < ownCount && bFound == false; ++index )
                        bFound = inoutList[index]._path == extra._path;
                    if ( bFound == false )
                        inoutList.push_back( extra );
                }
            }

            /** @brief PCH 를 읽는 인자입니다. PCH 가 없으면 비어 있다. */
            static vector<string> makePchArgs( const AbsolutePath* pPchPath )
            {
                if ( pPchPath == nullptr )
                    return {};
                return vector<string>{ "-include-pch", pPchPath->str() };
            }

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

    ReflectionPipeline::ReflectionPipeline( const ParserOptions& options, const ParserSession& session )
        : _pOptions{ &options }
        , _pSession{ &session }
        , _incrementalCheck{ options, session._config }
        , _runStartTime{ 0 }
        , _listIncludePath{}
        , _arrEmitBytes{}
        , _arrPhaseMicroseconds{}
    {
        for ( atomic<uint64>& bytes : _arrEmitBytes )
        {
            bytes.store( 0, std::memory_order_relaxed );
        }
        for ( atomic<uint64>& microseconds : _arrPhaseMicroseconds )
        {
            microseconds.store( 0, std::memory_order_relaxed );
        }
        // 출력 디렉터리를 include 경로 맨 앞에 한 번만 넣는다(원본이 다른 생성 헤더를 include 할 수 있다).
        _listIncludePath.reserve( options._listIncludePath.size() + 1 );
        _listIncludePath.push_back( options._outputDir.str() );
        for ( const string& includePath : options._listIncludePath )
        {
            _listIncludePath.push_back( includePath );
        }
    }

    int32 ReflectionPipeline::run( const float32 setupSeconds )
    {
        Stopwatch runTimer;
        Stopwatch phaseTimer;
        addPhaseTime( ParsePhase::Setup, setupSeconds );
        int32                   errorCount = 0;
        [[maybe_unused]] uint32 upToDate   = 0;
        [[maybe_unused]] uint32 noReflect  = 0;

        // 이번 실행의 시작을 먼저 적는다 — 이 뒤에 바뀐 의존은 0 으로 적혀 다음 실행이 다시 본다(`GeneratedFileUtil::markRunStart`).
        _runStartTime = GeneratedFileUtil::markRunStart( _pOptions->_outputDir );

        // 기록은 입력 수만큼 먼저 잡는다. 파싱 대기 입력이 기록을 가리키므로 자라며 옮겨지면 안 된다.
        vector<InputRecord> listInput;
        listInput.reserve( _pOptions->_listInputFile.size() );
        vector<PendingInput>  listPending;       // 헤더 입력(한 TU 로 묶는다)
        vector<PendingInput>  listPendingSource; // .cpp 입력(하나씩 파싱한다)
        unordered_set<string> setVisitedInput;
        for ( const AbsolutePath& inputFile : _pOptions->_listInputFile )
        {
            // 같은 헤더가 두 번 오면 한 번만 처리한다. 묶음 TU 에서는 둘째 대상이 선언을 하나도 못 받아 빈 산출물로 첫째를 덮는다.
            if ( setVisitedInput.insert( inputFile.str() ).second == false )
                continue;

            listInput.push_back( InputRecord{ &inputFile, GeneratedPaths::make( _pOptions->_outputDir, inputFile ), GeneratedStamp{}, SW_FALSE, 0 } );
            InputRecord&          record = listInput.back();
            const GeneratedPaths& paths  = record._paths;
            record._bHasStamp            = GeneratedStamp::read( paths._stampPath, record._stamp ) ? SW_TRUE : SW_FALSE;
            // `--dump` 는 무엇을 뽑았는지 보려는 실행이라 최신이어도 다시 파싱한다.
            if ( _pOptions->_bDump == false && _incrementalCheck.isUpToDate( inputFile, paths, record._bHasStamp == SW_TRUE ? &record._stamp : nullptr ) )
            {
                SW_LOG_TRACE_ID( "ReflectionPipeline.UpDateSkippingAST", "Up-to-date, skipping AST parsing: %#", inputFile.str() );
                ++upToDate;
                continue;
            }

            // 시각을 읽기 전에 측정한다. 읽은 뒤에 저장한 편집은 이 값과 달라 다음 실행이 다시 파싱한다(`GeneratedStamp::write` 설명).
            const uint64 inputWriteTime = GeneratedFileUtil::getWriteTime( inputFile );
            string       content;
            if ( FileUtil::readTextFile( inputFile, content ) == false )
            {
                SW_LOG_ERROR_ID( "ReflectionPipeline.FailedReadInput", "Failed to read input: %#", inputFile.str() );
                ++errorCount;
                continue;
            }

            // 리플렉션 매크로가 지워진 뒤에도 이전 registrar 가 남아 계속 컴파일되지 않도록 빈 산출물을 쓴다.
            if ( ReflectionPipelineInternal::hasReflectionKeywords( content ) == false )
            {
                SW_LOG_TRACE_ID( "ReflectionPipeline.NoReflectionAnnotationsFound", "No reflection annotations found, emitting empty output: %#", inputFile.str() );
                ++noReflect;
                if ( writeOutputs( record, ParsedHeader{}, inputWriteTime, {} ) == false )
                    ++errorCount;
                continue;
            }

            // .cpp 는 끝에서 자기 산출물(inl)을 include 한다. 파서가 그 .cpp 를 읽을 때 파일이 있어야 한다(CMake 도 구성 때 자리 표시자를 심는다).
            // 자리 표시자에는 스탬프가 없어 최신으로 보이지 않는다.
            if ( paths.isInline() && FileUtil::exists( paths._cppPath ) == false &&
                 GeneratedFileUtil::writeIfChanged( paths._cppPath, "// AUTO-GENERATED placeholder - regenerated by ReflectionParser\n" ) == false )
            {
                ++errorCount;
                continue;
            }

            vector<PendingInput>& listTarget = paths.isInline() ? listPendingSource : listPending;
            listTarget.push_back( PendingInput{ &record, std::move( content ), inputWriteTime } );
        }

        addPhaseTime( ParsePhase::Check, static_cast<float32>( phaseTimer.lapSeconds() ) );
        SW_LOG_INFO_ID( "ReflectionPipeline.ParsingInputUpDate", "Parsing %# of %# input(s) (%# up to date, %# without annotations).", listPending.size() + listPendingSource.size(),
                        _pOptions->_listInputFile.size(), upToDate, noReflect );
        // .cpp 는 첫 줄이 `pch.h` 라 번역 단위 비용이 거의 그 공통 앞부분이다. 묶음 TU 도 같은 앞부분을 다시 읽는다.
        // 그래서 번역 단위가 문턱(`tuning.min_translation_units_for_pch`) 이상이고 .cpp 가 있으면 `pch.h` 를 한 번 PCH 로 굽고 모두 그것을 읽는다(언리얼 UBT 의 모듈 공유 PCH).
        const size_t translationUnitCount = ( listPending.empty() ? 0 : 1 ) + listPendingSource.size();
        ModulePch    pch;
        const bool   bWantPch  = listPendingSource.empty() == false && translationUnitCount >= static_cast<size_t>( _pSession->_config._minTranslationUnitsForPch );
        const bool   bPchBaked = bWantPch && bakeModulePch( pch );
        addPhaseTime( ParsePhase::Pch, static_cast<float32>( phaseTimer.lapSeconds() ) );
        if ( bWantPch && bPchBaked == false )
        {
            ++errorCount; // 까닭은 bakeModulePch 가 남겼다. .cpp 입력도 같은 `pch.h` 로 시작하므로 PCH 없이 다시 하지 않는다
        }
        else
        {
            const ModulePch* pPch = bPchBaked ? &pch : nullptr;
            errorCount += parsePending( listPending, pPch );
            // .cpp 는 한 TU 로 묶지 않는다. 내부 연결 이름(익명 이름공간 · static)과 using 지시문이 .cpp 끼리 부딪힌다(헤더만 읽는 UHT 는 없는 문제다).
            errorCount += parseEachInParallel( listPendingSource, pPch );
        }
        // PCH 는 이번 실행의 인자 · 헤더로 구운 것이라 남겨 두지 않는다(모듈마다 수십 MB).
        if ( bPchBaked && FileUtil::removeFile( pch._pchPath ) == false )
            SW_LOG_WARNING_ID( "ReflectionPipeline.ModulePchNotRemoved", "Could not remove the module PCH %#.", pch._pchPath.str() );
        logEmitBytes();
        static_cast<void>( phaseTimer.lapSeconds() ); // 파싱 · 방문 · 쓰기는 입력마다 더했다

        if ( writeFlagOpsUmbrella( listInput ) == false )
            ++errorCount;
        if ( writeReflectedTypesList( listInput ) == false )
            ++errorCount;
        if ( writeAssetFieldList( listInput ) == false )
            ++errorCount;
        if ( writeLayoutSchemaList( listInput ) == false )
            ++errorCount;
        if ( writeDepfile( listInput ) == false )
            ++errorCount;
        addPhaseTime( ParsePhase::Lists, static_cast<float32>( phaseTimer.lapSeconds() ) );
        logPhaseTimes( listPending.size() + listPendingSource.size(), static_cast<float64>( setupSeconds ) + runTimer.getElapsedSeconds() );
        return errorCount;
    }

    /**
     * @details 파싱 비용의 거의 전부는 헤더 자신이 아니라 공통 include 다. 강제 include(`Core/CoreMinimal.h`) 하나만 파싱하는 데
     *          0.52 초, 엔진 공통 헤더(Windows · D3D)까지 가면 1.0 초인데, Engine 의 입력 27 개를 한 TU 로 묶어도 1.04 초였다
     *          (Release, 실측). 헤더마다 TU 를 따로 만들면 같은 것을 헤더 수만큼 다시 파싱하고, 워커 16 개가 한꺼번에 파싱하면
     *          메모리 대역을 다퉈 한 개가 1.0 → 2.1 초로 늘어난다.
     */
    int32 ReflectionPipeline::parsePending( const vector<PendingInput>& listPending, const ModulePch* pPch ) const
    {
        if ( listPending.empty() )
            return 0;
        // PCH 를 읽을 때는 헤더 하나도 묶음으로 읽는다. 헤더를 주 파일로 읽으면 PCH 에 이미 든 같은 헤더와 정의가 겹친다(`#pragma once` 는 주 파일에 듣지 않는다).
        if ( listPending.size() == 1 && pPch == nullptr )
            return parseAndGenerate( listPending.front(), nullptr ) ? 0 : 1;

        int32 errorCount = 0;
        if ( parseBatch( listPending, pPch, errorCount ) )
            return errorCount;

        // 묶음이 clang 오류로 실패했다. 어느 헤더가 깨졌는지 헤더마다 파싱해 그 헤더의 오류로 알리고, 나머지는 그대로 만든다(위의 까닭으로 PCH 없이).
        SW_LOG_WARNING_ID( "ReflectionPipeline.OneTranslationUnitInput", "One translation unit for %# input(s) did not parse; parsing them one by one.", listPending.size() );
        return parseEachInParallel( listPending, nullptr );
    }

    bool ReflectionPipeline::bakeModulePch( ModulePch& outPch ) const
    {
        // 원본은 내용이 같으면 다시 쓰지 않는다. 출력 폴더 안이라 의존(스탬프 · depfile)에 들지 않는다.
        const AbsolutePath headerPath = _pOptions->_outputDir.join( ReflectionPipelineInternal::kModulePchHeaderName );
        if ( GeneratedFileUtil::writeIfChanged( headerPath, "#include \"pch.h\"\n" ) == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.ModulePchHeaderNotWritten", "Could not write the module PCH header %#.", headerPath.str() );
            return false;
        }
        // `-x c++-header` 는 설정의 `-x c++` 뒤에 와서 이긴다. 헤더로 파싱해야 PCH 로 쓸 수 있다.
        const vector<string> listHeaderArg{ "-x", "c++-header" };
        ParserContext        context( _pSession->_config );
        if ( context.parse( headerPath.str(), _listIncludePath, {}, true, listHeaderArg, CXTranslationUnit_ForSerialization ) == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.ModulePchNotParsed", "The module PCH (pch.h) did not parse - the .cpp inputs start with it, so fix the errors above." );
            return false;
        }
        outPch._pchPath = _pOptions->_outputDir.join( ReflectionPipelineInternal::kModulePchName );
        if ( context.save( outPch._pchPath.str() ) == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.ModulePchNotSaved", "Could not save the module PCH to %#.", outPch._pchPath.str() );
            return false;
        }
        outPch._listDependency = ReflectionPipelineInternal::collectDependencies( context.getTranslationUnit(), _pOptions->_outputDir, _runStartTime );
        return true;
    }

    bool ReflectionPipeline::parseBatch( const vector<PendingInput>& listPending, const ModulePch* pPch, int32& outErrorCount ) const
    {
        // 묶음 TU 의 원본 — 입력 헤더를 차례로 include 한다. 디스크에 쓰지 않고 내용으로 넘긴다(입력 헤더들도 이미 읽은 내용으로).
        const string              batchPath = _pOptions->_outputDir.join( ReflectionPipelineInternal::kBatchSourceName ).str();
        string                    batchSource;
        vector<string>            listTargetFile;
        vector<ParserUnsavedFile> listUnsaved;
        listTargetFile.reserve( listPending.size() );
        listUnsaved.reserve( listPending.size() + 1 );
        listUnsaved.push_back( ParserUnsavedFile{ &batchPath, &batchSource } );
        StringBuilder<constant::buffer::kMax8192> batchSourceText;
        for ( const PendingInput& pending : listPending )
        {
            const AbsolutePath& inputFile = *pending._pRecord->_pInputFile;
            batchSourceText.appendFormat( "#include \"%#\"\n", inputFile.str() );
            listTargetFile.push_back( inputFile.str() );
            // PCH 안에 든 파일을 내용으로 덮으면 clang 이 PCH 를 거절한다. PCH 를 읽을 때는 입력 헤더를 디스크에서 읽는다.
            if ( pPch == nullptr )
                listUnsaved.push_back( ParserUnsavedFile{ &inputFile.str(), &pending._content } );
        }
        batchSource = string( batchSourceText.view() );

        // clang 오류는 여기서 알리지 않는다 — 헤더마다 다시 파싱할 때 그 헤더의 오류로 나온다.
        ParserContext context( _pSession->_config );
        Stopwatch     phaseTimer;
        const bool    bParsed = context.parse( batchPath, _listIncludePath, listUnsaved, false, ReflectionPipelineInternal::makePchArgs( pPch != nullptr ? &pPch->_pchPath : nullptr ) );
        addPhaseTime( ParsePhase::Parse, static_cast<float32>( phaseTimer.lapSeconds() ) );
        if ( bParsed == false )
            return false;

        AstVisitor visitor( context.getTranslationUnit(), *_pSession, listTargetFile );
        visitor.visit();
        addPhaseTime( ParsePhase::Visit, static_cast<float32>( phaseTimer.lapSeconds() ) );
        const vector<ParsedHeader>& listHeader = visitor.getParsedHeaders();
        // 묶음의 의존은 묶음 전체로 적는다. `#pragma once` 로 두 번째 include 는 새 파일 항목이 생기지 않아, 헤더별로 가르면 먼저 include 한
        // 헤더에만 붙는다 — 그러면 그 헤더만 다시 파싱되고 나머지는 옛 것을 든다.
        vector<StampDependency> listDependency = ReflectionPipelineInternal::collectDependencies( context.getTranslationUnit(), _pOptions->_outputDir, _runStartTime );
        if ( pPch != nullptr )
            ReflectionPipelineInternal::appendDependencies( listDependency, pPch->_listDependency );
        for ( size_t index = 0; index < listPending.size(); ++index )
        {
            const AbsolutePath& inputFile = *listPending[index]._pRecord->_pInputFile;
            if ( index >= listHeader.size() || listHeader[index]._bHasError == SW_TRUE )
            {
                SW_LOG_ERROR_ID( "ReflectionPipeline.ASTAnalysisFailed", "AST analysis failed: %#", inputFile.str() );
                ++outErrorCount;
                continue;
            }
            if ( writeOutputs( *listPending[index]._pRecord, listHeader[index], listPending[index]._inputWriteTime, listDependency ) == false )
                ++outErrorCount;
        }
        addPhaseTime( ParsePhase::Emit, static_cast<float32>( phaseTimer.lapSeconds() ) );
        return true;
    }

    bool ReflectionPipeline::parseAndGenerate( const PendingInput& pending, const ModulePch* pPch ) const
    {
        const AbsolutePath&   inputFile = *pending._pRecord->_pInputFile;
        const GeneratedPaths& paths     = pending._pRecord->_paths;
        SW_LOG_TRACE_ID( "ReflectionPipeline.Parsing", "── Parsing: %#", inputFile.str() );

        ParserContext                   context( _pSession->_config );
        const vector<ParserUnsavedFile> listUnsaved{
            ParserUnsavedFile{ &inputFile.str(), &pending._content }
        };
        Stopwatch  phaseTimer;
        const bool bParsed = context.parse( inputFile.str(), _listIncludePath, listUnsaved, true, ReflectionPipelineInternal::makePchArgs( pPch != nullptr ? &pPch->_pchPath : nullptr ) );
        addPhaseTime( ParsePhase::Parse, static_cast<float32>( phaseTimer.lapSeconds() ) );
        if ( bParsed == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.ParseFailed", "Parse failed: %#", inputFile.str() );
            return false;
        }

        AstVisitor visitor( context.getTranslationUnit(), *_pSession );
        const bool bVisited = visitor.visit() && visitor.hasError() == false;
        addPhaseTime( ParsePhase::Visit, static_cast<float32>( phaseTimer.lapSeconds() ) );
        if ( bVisited == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.ASTAnalysisFailed", "AST analysis failed: %#", inputFile.str() );
            return false;
        }

        // .cpp 의 등록 코드는 그 .cpp 가 include 하는 inl 에만 있다. include 를 빠뜨리면 컴파일 · 링크는 되고 형식만 조용히 등록되지 않으므로 여기서 멈춘다.
        // 글자를 찾지 않고 번역 단위가 그 파일을 실제로 읽었는지 본다(주석 · `#if 0` 아래의 include 는 읽지 않은 것이다).
        const ParsedHeader& parsed         = visitor.getParsedHeaders().front();
        const bool          bHasReflection = parsed._listType.empty() == false || parsed._listEnum.empty() == false;
        if ( paths.isInline() && bHasReflection && ReflectionPipelineInternal::includesFile( context.getTranslationUnit(), paths._cppPath ) == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.InlineNotIncluded", "%# has reflected types but does not include \"%#\" - add that include at the end of the file, at global scope (the registrars are generated there)",
                             inputFile.str(), FileUtil::getFileNamePart( paths._cppPath.str() ) );
            return false;
        }

        // PCH 안의 헤더가 이 번역 단위의 include 목록에 나오는지는 libclang 판에 달렸다. PCH 를 구울 때 모은 의존을 늘 더한다.
        vector<StampDependency> listDependency = ReflectionPipelineInternal::collectDependencies( context.getTranslationUnit(), _pOptions->_outputDir, _runStartTime );
        if ( pPch != nullptr )
            ReflectionPipelineInternal::appendDependencies( listDependency, pPch->_listDependency );
        const bool bWritten = writeOutputs( *pending._pRecord, parsed, pending._inputWriteTime, listDependency );
        addPhaseTime( ParsePhase::Emit, static_cast<float32>( phaseTimer.lapSeconds() ) );
        return bWritten;
    }

    int32 ReflectionPipeline::parseEachInParallel( const vector<PendingInput>& listPending, const ModulePch* pPch ) const
    {
        if ( listPending.empty() )
            return 0;
        if ( listPending.size() == 1 )
            return parseAndGenerate( listPending.front(), pPch ) ? 0 : 1;

        // 동시에 여는 TU 는 설정의 상한(`tuning.max_parallel_parses`)까지다 — TU 마다 공통 헤더 전체를 들어 하드웨어 스레드만큼 띄우면 메모리와 대역이 먼저 찬다.
        uint32 workerCount = std::thread::hardware_concurrency();
        if ( workerCount == 0 )
            workerCount = 1;
        workerCount = MathUtil::min( workerCount, _pSession->_config._maxParallelParses );
        if ( workerCount > listPending.size() )
            workerCount = static_cast<uint32>( listPending.size() );
        SW_LOG_INFO_ID( "ReflectionPipeline.ParallelFallback", "Parsing %# inputs one by one on %# workers (cap tuning.max_parallel_parses = %#)",
                        static_cast<uint32>( listPending.size() ), workerCount, _pSession->_config._maxParallelParses );

        TaskManager taskManager;
        if ( taskManager.initialize( workerCount ) == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.FailedInitializeTaskManager", "Failed to initialize TaskManager." );
            return static_cast<int32>( listPending.size() );
        }

        atomic<int32> errorCount{ 0 };
        for ( const PendingInput& pending : listPending )
        {
            const PendingInput* pPending = &pending;
            TaskHandle          handle   = taskManager.emplaceTask(
                "ParseHeader",
                SW_DELEGATE_LAMBDA( TaskDelegate, [this, pPending, pPch, &errorCount]()
                       {
                if ( parseAndGenerate( *pPending, pPch ) == false )
                    errorCount.fetch_add( 1 );
            } ) );
            handle.submit();
        }

        taskManager.waitAll( "ReflectionPipeline" );
        taskManager.shutdown();
        return errorCount.load();
    }

    bool ReflectionPipeline::writeOutputs( InputRecord& record, const ParsedHeader& parsed, uint64 inputWriteTime, const vector<StampDependency>& listDependency ) const
    {
        const AbsolutePath&   inputFile = *record._pInputFile;
        const GeneratedPaths& paths     = record._paths;
        LayoutSchemaGenerator layoutSchema( parsed, inputFile.str() );
        const bool            bLayoutPlanned = layoutSchema.makePlan();
        if ( _pOptions->_bDump && ( parsed._listType.empty() == false || parsed._listEnum.empty() == false ) )
            printParsedHeaderDump( inputFile.str(), parsed, layoutSchema );
        if ( bLayoutPlanned == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.CodeGenerationFailedLayout", "Code generation failed (layout schema annotations): %#", inputFile.str() );
            return false;
        }

        const CodeGenerator generator( parsed, layoutSchema, inputFile.str(), *_pSession, _pOptions->_moduleName );
        string              headerText;
        if ( generator.makeHeaderText( headerText ) == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.CodeGenerationFailed", "Code generation failed: %#", inputFile.str() );
            return false;
        }

        // 다른 헤더가 이미 이 이름으로 썼는가. 생성 파일 이름은 소스의 파일 이름만 으로 짓는다
        // (`GeneratedPaths::make`). 그래서 한 모듈 안에 같은 이름의 헤더가 둘 있으면 나중에 도는 쪽이 앞의 것을
        // 덮고, 앞 헤더의 타입들은 아무 말 없이 등록되지 않는다. 증상은 한참 뒤 "씬이 그 컴포넌트를 못 찾는다" 로
        // 나타나서 원인을 여기서 찾기 어렵다. 머리에 적어 둔 소스 경로로 그 상황을 잡는다.
        //
        // 헤더를 옮긴 경우(옛 경로가 더는 없다)는 정상이므로 조용히 덮어쓴다. 그러지 않으면 파일을 옮길 때마다 빌드가 막힌다.
        // 앞 주인은 그 산출물의 스탬프가 안다(스탬프가 없으면 자리 표시자 - 주인이 없다).
        if ( record._bHasStamp == SW_TRUE )
        {
            // 대소문자만 바뀐 폴더 이름(`GUI` → `GUI`)은 같은 파일이다 — Windows 에서는 옛 철자로도 exists 가 참이다.
            const AbsolutePath& recordedSource = record._stamp._sourcePath;
            const bool          bOtherOwner    = recordedSource.empty() == false && FileUtil::isSameDiskPath( recordedSource.str(), inputFile.str() ) == false &&
                                     FileUtil::exists( recordedSource );
            if ( bOtherOwner )
            {
                SW_LOG_ERROR_ID( "ReflectionPipeline.GeneratedFileNameCollision", "Generated file name collision: '%#' and '%#' both generate '%#'. "
                                                                                  "Two reflected headers in the same module cannot share a file name.",
                                 recordedSource.str(), inputFile.str(), paths._cppPath.str() );
                return false;
            }
        }

        GeneratedStamp stamp;
        stamp._sourcePath     = inputFile;
        stamp._inputWriteTime = inputWriteTime;
        stamp._argsHash       = _incrementalCheck.getArgsHash();
        stamp._listDependency = listDependency;
        for ( size_t typeIndex = 0; typeIndex < parsed._listType.size(); ++typeIndex )
        {
            stamp._listTypeFqn.push_back( parsed._listType[typeIndex]._fullyQualifiedName );
            stamp._listLayoutSchema.push_back( layoutSchema.makeSchemaLine( typeIndex ) );
            for ( const ParsedPropertyInfo& prop : parsed._listType[typeIndex]._listProperty )
            {
                if ( prop._bAssetPath == SW_FALSE )
                    continue;
                const string                             target = prop._assetType.empty() ? string( "-" ) : prop._assetType;
                StringBuilder<constant::buffer::kMax256> textBuilder3;
                textBuilder3.appendFormat( "%#\t%#", prop._name, target );
                stamp._listAssetField.push_back( string( textBuilder3.view() ) );
                // 속성으로 읽는 문서(`XMLFields::readProperties`)는 C++ 이름이 아니라 속성 이름(`_shaderPath` → `shaderPath`)으로 적는다 — 그 이름도 낸다.
                // 예전에는 그 이름을 손 표(`AssetReferenceFields.json`)가 따로 적었다.
                const string attribute = ReflectionPipelineInternal::makeXMLAttributeName( prop._name );
                if ( attribute != prop._name )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder2;
                    textBuilder2.appendFormat( "%#\t%#", attribute, target );
                    stamp._listAssetField.push_back( string( textBuilder2.view() ) );
                }
                for ( const string& alias : prop._listAlias )
                {
                    StringBuilder<constant::buffer::kMax256> textBuilder;
                    textBuilder.appendFormat( "%#\t%#", alias, target );
                    stamp._listAssetField.push_back( string( textBuilder.view() ) );
                }
            }
        }
        for ( const ParsedEnumInfo& enumInfo : parsed._listEnum )
        {
            stamp._listTypeFqn.push_back( enumInfo._fullyQualifiedName );
            if ( enumInfo._bIsBitFlag == SW_FALSE )
                continue;
            if ( paths.isInline() == false )
            {
                stamp._bHasBitFlagEnum = true;
                continue;
            }
            // 트레이트(.gen.h)는 열거형을 쓰는 곳보다 먼저 보여야 하는데 .cpp 의 생성 코드는 그 끝에 온다. 클래스 안 ENUM(Flags) 와 같이 연산자만 빠지고
            // 레지스트리의 비트 플래그 표시는 남는다. `|` 를 쓰면 그 자리에서 컴파일이 멈추므로 조용히 틀어지지 않는다.
            SW_LOG_INFO_ID( "ReflectionPipeline.EnumFlagsInSource", "ENUM(Flags) %# is in a .cpp, so bit operators are not generated - specialize sw::IsBitFlagEnum right after the enum to get them: %#",
                            enumInfo._fullyQualifiedName, inputFile.str() );
        }
        const string sourceText = paths.isInline() ? generator.makeInlineText() : generator.makeSourceText();
        for ( size_t section = 0; section < _arrEmitBytes.size(); ++section )
        {
            _arrEmitBytes[section].fetch_add( generator.getEmitBytes()._arrBytes[section], std::memory_order_relaxed );
        }
        const bool bWritten = GeneratedFileUtil::writeIfChanged( paths._cppPath, sourceText ) &&
                              ( paths.isInline() || ( GeneratedFileUtil::writeIfChanged( paths._headerPath, headerText ) &&
                                                      GeneratedFileUtil::writeIfChanged( paths._editorMetadataPath, generator.makeEditorMetadataText() ) ) ) &&
                              stamp.write( paths._stampPath );
        if ( bWritten == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.CodeGenerationFailed", "Code generation failed: %#", inputFile.str() );
            // 스탬프를 쓰기 전에 멈췄을 수 있다. 디스크에 남은 것을 다시 본다(목록은 디스크와 같은 사실을 쓴다).
            record._bHasStamp = GeneratedStamp::read( paths._stampPath, record._stamp ) ? SW_TRUE : SW_FALSE;
            return false;
        }
        record._stamp     = std::move( stamp );
        record._bHasStamp = SW_TRUE;
        return true;
    }

    /**
     * @details 이 파일은 타깃의 모든 TU 에 `/FI` 로 강제 include 됩니다. 트레이트는 열거형이 보이는 곳이면 어디서나 함께
     *          보여야 하기 때문입니다. 그래서 원본 헤더는 절대 들이지 않습니다 — 들이면 그 헤더가 끌어오는 것이 모든 TU 에 들어가
     *          다른 헤더들의 include 누락을 통째로 가립니다. `.gen.h` 자신이 열거형을 전방 선언하므로 그것 하나만 모으면 됩니다.
     */
    bool ReflectionPipeline::writeDepfile( const vector<InputRecord>& listInput ) const
    {
        if ( _pOptions->_depfilePath.empty() )
            return true;

        vector<string> listTarget;
        vector<string> listDependency;
        for ( const InputRecord& record : listInput )
        {
            const GeneratedPaths& paths = record._paths;
            listTarget.push_back( paths._cppPath.str() );
            if ( paths.isInline() == false )
            {
                listTarget.push_back( paths._headerPath.str() );
                listTarget.push_back( paths._editorMetadataPath.str() );
            }
            if ( record._bHasStamp == SW_FALSE )
                continue; // 스탬프가 없으면(이번 실행에 실패한 입력) 더할 의존이 없다 - 실패는 이미 오류로 셌다
            for ( const StampDependency& dependency : record._stamp._listDependency )
            {
                listDependency.push_back( dependency._path.str() );
            }
        }
        listTarget.push_back( _pOptions->_outputDir.join( sw::config::kFileReflectionFlagOps ).str() );
        listTarget.push_back( _pOptions->_outputDir.join( sw::config::kFileReflectedTypesList ).str() );
        listTarget.push_back( _pOptions->_outputDir.join( sw::config::kFileAssetReferenceFieldsList ).str() );
        VectorUtil::sortUnique( listDependency );

        // 목표는 이 단계의 모든 산출물(CMake 의 OUTPUT 과 같은 목록), 의존은 스탬프들의 합. 경로는 절대 · 슬래시.
        StringBuilder<constant::buffer::kMax8192> text;
        for ( const string& target : listTarget )
        {
            text.appendFormat( "%# ", ReflectionPipelineInternal::escapeDepfilePath( target ) );
        }
        text.append( ':' );
        for ( const string& dependency : listDependency )
        {
            text.appendFormat( " \\\n  %#", ReflectionPipelineInternal::escapeDepfilePath( dependency ) );
        }
        text.append( '\n' );
        if ( GeneratedFileUtil::writeIfChanged( _pOptions->_depfilePath, text.view() ) == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.FailedWriteDepfile", "Failed to write depfile %#", _pOptions->_depfilePath.str() );
            return false;
        }
        return true;
    }

    /**
     * @details 진단 파일입니다. 빌드는 읽지 않으므로 CMake 산출물 목록에 넣지 않습니다. 스키마 해시가 왜 바뀌었는지 빌드 두 개의 이 파일을
     *          비교해 봅니다. 판정 줄은 생성기가 스탬프에 적은 사실이다(이번 실행에 최신이라 다시 쓰지 않은 입력도 스탬프에 남아 있다).
     */
    bool ReflectionPipeline::writeLayoutSchemaList( const vector<InputRecord>& listInput ) const
    {
        StringBuilder<constant::buffer::kMax8192> text;
        for ( const InputRecord& record : listInput )
        {
            // 입력마다 스탬프가 있다(애노테이션이 없는 헤더도 빈 산출물과 스탬프를 쓴다). 없으면 목록이 조용히 빠지므로 실패로 알린다.
            if ( record._bHasStamp == SW_FALSE )
            {
                SW_LOG_ERROR_ID( "ReflectionPipeline.LayoutSchemaListNot", "Layout schema list is not written - '%#' has no stamp (it failed above)", record._paths._stampPath.str() );
                return false;
            }
            for ( const string& schemaLine : record._stamp._listLayoutSchema )
            {
                text.append( schemaLine );
                text.append( '\n' );
            }
        }
        return GeneratedFileUtil::writeIfChanged( _pOptions->_outputDir.join( layoutschema::kSchemaListFile ), text.view() );
    }

    bool ReflectionPipeline::writeReflectedTypesList( const vector<InputRecord>& listInput ) const
    {
        vector<string> listLine;
        bool           bComplete = true;
        for ( const InputRecord& record : listInput )
        {
            if ( record._bHasStamp == SW_FALSE )
            {
                // 목록에서 조용히 빠지면 콘텐츠 검사가 그 타입을 "엔진 것" 으로 본다 - 쓰지 않는다.
                bComplete = false;
                continue;
            }
            for ( const string& typeFqn : record._stamp._listTypeFqn )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#\t%#", typeFqn, _pOptions->_moduleName );
                listLine.push_back( string( textBuilder.view() ) );
            }
        }
        if ( bComplete == false )
        {
            SW_LOG_ERROR_ID( "ReflectionPipeline.NotWrittenSomeInputs", "%# is not written - some inputs have no stamp (they failed above)", sw::config::kFileReflectedTypesList );
            return false;
        }
        VectorUtil::sortUnique( listLine );
        if ( ReflectionPipelineInternal::validateLeafNameCollision( listLine, _pOptions->_moduleName ) == false )
            return false;
        StringBuilder<constant::buffer::kMax8192> text;
        for ( const string& line : listLine )
        {
            text.append( line );
            text.append( '\n' );
        }
        return GeneratedFileUtil::writeIfChanged( _pOptions->_outputDir.join( sw::config::kFileReflectedTypesList ), text.view() );
    }

    bool ReflectionPipeline::writeAssetFieldList( const vector<InputRecord>& listInput ) const
    {
        vector<string> listLine;
        for ( const InputRecord& record : listInput )
        {
            // 빠진 입력의 칸을 "경로가 아니다" 로 보면 끊어진 경로를 못 잡는다 - 쓰지 않는다.
            if ( record._bHasStamp == SW_FALSE )
            {
                SW_LOG_ERROR_ID( "ReflectionPipeline.AssetFieldListNot", "%# is not written - some inputs have no stamp (they failed above)", sw::config::kFileAssetReferenceFieldsList );
                return false;
            }
            listLine.insert( listLine.end(), record._stamp._listAssetField.begin(), record._stamp._listAssetField.end() );
        }
        VectorUtil::sortUnique( listLine );
        StringBuilder<constant::buffer::kMax8192> text;
        for ( const string& line : listLine )
        {
            text.append( line );
            text.append( '\n' );
        }
        return GeneratedFileUtil::writeIfChanged( _pOptions->_outputDir.join( sw::config::kFileAssetReferenceFieldsList ), text.view() );
    }

    void ReflectionPipeline::logEmitBytes() const
    {
        uint64 total = 0;
        for ( const atomic<uint64>& bytes : _arrEmitBytes )
        {
            total += bytes.load( std::memory_order_relaxed );
        }
        if ( total == 0 )
            return;
        // 한 줄에 구역마다 `이름=바이트(백분율)` — 표로 바꿀 후보는 합계의 25 % 를 넘는 구역이다. 이번 실행이 다시 만든 헤더만 센다.
        StringBuilder<constant::buffer::kMax1024> text;
        for ( size_t section = 0; section < _arrEmitBytes.size(); ++section )
        {
            const uint64 bytes = _arrEmitBytes[section].load( std::memory_order_relaxed );
            text.appendFormat( " %#=%#(%#", CodeEmitByteReport::toString( static_cast<CodeEmitSection>( section ) ), bytes, bytes * ReflectionPipelineInternal::kPercentScale / total );
            text.append( "%)" );
        }
        SW_LOG_INFO_ID( "ReflectionParser.EmitBytes", "Generated .gen.cpp bytes by emitter (headers generated in this run, total %#):%#", total, text.view() );
    }

    void ReflectionPipeline::addPhaseTime( const ParsePhase phase, const float32 seconds ) const
    {
        const float64 microseconds = static_cast<float64>( seconds ) * static_cast<float64>( constant::time::kMicrosecondsPerSecond );
        _arrPhaseMicroseconds[static_cast<size_t>( phase )].fetch_add( microseconds > 0.0 ? static_cast<uint64>( microseconds ) : 0u, std::memory_order_relaxed );
    }

    void ReflectionPipeline::logPhaseTimes( const size_t parsedCount, const float64 totalSeconds ) const
    {
        const auto toMs = [this]( const ParsePhase phase )
        { return static_cast<float64>( _arrPhaseMicroseconds[static_cast<size_t>( phase )].load( std::memory_order_relaxed ) ) / static_cast<float64>( constant::time::kMicrosecondsPerMillisecond ); };
        SW_LOG_INFO_ID( "ReflectionParser.PhaseTime", "Module %# (%# of %# input(s) parsed): total %# ms, setup %# ms, check %# ms, pch %# ms, parse %# ms, visit %# ms, emit %# ms, lists %# ms",
                        _pOptions->_moduleName, parsedCount, _pOptions->_listInputFile.size(), totalSeconds * static_cast<float64>( constant::time::kMillisecondsPerSecond ),
                        toMs( ParsePhase::Setup ), toMs( ParsePhase::Check ), toMs( ParsePhase::Pch ), toMs( ParsePhase::Parse ), toMs( ParsePhase::Visit ), toMs( ParsePhase::Emit ),
                        toMs( ParsePhase::Lists ) );
    }

    bool ReflectionPipeline::writeFlagOpsUmbrella( const vector<InputRecord>& listInput ) const
    {
        CodeEmitBuffer buffer;
        CodeEmit       emit( buffer );
        emit.line( sw::config::kTextReflectionBanner );
        emit.line( emitdirective::kPragmaOnce );
        emit.blank();
        emit.line( emitdirective::kIfndefParser );

        // 입력마다 Flags 여부는 스탬프가 안다 - 생성 글에서 표식 글자를 찾지 않는다. 스탬프가 없는 입력은 이번 실행에 실패한 것이다(이미 오류로 셌다).
        bool bAnyFlags = false;
        for ( const InputRecord& record : listInput )
        {
            if ( record._bHasStamp == SW_FALSE || record._stamp._bHasBitFlagEnum == false )
                continue;

            bAnyFlags = true;
            emit.linef( "#include \"%#\"", FileUtil::getFileNamePart( record._paths._headerPath.str() ) );
        }

        if ( bAnyFlags == false )
            emit.line( emitdirective::kNoEnumFlags );
        emit.line( emitdirective::kEndif );

        return GeneratedFileUtil::writeIfChanged( _pOptions->_outputDir.join( sw::config::kFileReflectionFlagOps ), buffer.view() );
    }
} // namespace sw
