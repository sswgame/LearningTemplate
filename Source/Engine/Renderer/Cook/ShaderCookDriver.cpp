/**
 * @file ShaderCookDriver.cpp
 * @brief 오프라인 쿠킹의 **정책**입니다. 요청 모두를 네 RHI 포맷으로 쿠킹하고 리플렉션 매니페스트를 씁니다.
 * @details 셰이더 하나를 쿠킹하는 법(`ShaderCooker::cookShader`)과 이름 짓기, 최신 판정은 `Shader/Compile` 의 메커니즘이고,
 *          "무엇을 쿠킹하는가"는 파이프라인 XML 과 패스 종류를 아는 렌더러의 지식입니다. 그래서 이 파일은
 *          `Renderer/Cook` 에 있고 `Shader/` 는 `Renderer/` 를 include 하지 않습니다.
 *
 *          **쿠킹은 "깨끗하다" 를 거짓으로 답하지 않는다**(`ShaderCookSummary::isClean` 이 `App --cook-shaders` 의 종료 코드를 정한다).
 *          소스가 없거나 `shaders` 폴더 밖인 요청 · 읽지 못한 바이너리 · 쓰지 못한 매니페스트와 스탬프 · 지우지 못한 옛 바이너리는 모두 실패로 센다. 조용히 건너뛰는 갈래를 두면 요청이 어긋난 채 종료 코드 0 이 된다.
 *          **요청이 만들지 않는 바이너리는 쿠커가 지운다**(`ShaderCooker::removeUnlistedBinaries`). 이 쿠킹은 요청을 모두 훑어 매니페스트를 새로 쓰므로 매니페스트 밖의 바이너리는 옛 퍼뮤테이션 해시나 사라진 셰이더의 것이다.
 *          예전에는 손으로 지워야 했고, 그렇지 않으면 `--verify-shaders` 가 "매니페스트에 없는 바이너리" 로 졌다.
 *
 *          **컴파일 · 리플렉션은 병렬이고 합치기는 직렬이다.** 요청 × 포맷 작업은 계획(직렬) → 컴파일 · 리플렉션(병렬, 작업마다 자기 결과 칸에만 쓴다) → 합치기(직렬, 요청 순서)로 돈다.
 *          매니페스트 합치기 · 카운터 · 실패 소스 기록 · 매니페스트와 스탬프 쓰기는 직렬로 돌 때와 같은 순서라 산출물은 바이트 단위로 같다. 언리얼의 셰이더 컴파일 워커도 컴파일은 워커에서 하고 결과 병합은 한 스레드에서 한다.
 */
#include "pch.h"

#include "Engine/Renderer/Cook/ShaderCookDriver.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingValidator.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderCooker" );

    namespace
    {
        /** @brief `cookAllShaders` 의 단계들과 단계 사이에 넘기는 쿠킹 상태입니다. */
        struct ShaderCookDriverInternal
        {
            /** @brief 쿠킹 한 번의 누적 상태입니다. 병렬 구간에서는 읽기만 하고(`_resourceRoot` · `_listPriority`), 나머지는 직렬 단계가 씁니다. */
            struct CookState
            {
                ShaderCookSummary _summary{};
                /** @brief 다른 include 루트 도메인의 헤더를 찾는 리소스 루트와 우선순위입니다. 전역(`AssetPaths`)이 아니라 이 쿠킹이 받은 것을 스탬프 쓰기 · 최신 판정에 같이 쓴다. */
                string         _resourceRoot;
                vector<string> _listPriority;
                /** @brief 쿠킹 출력의 루트(`--cooked-dir`)입니다. 바이너리는 `<여기>/<domain>/shaders/bin/<rhi>` 에 쓴다 — 씬 · 프리팹 쿠킹본과 같은 스테이징이다. */
                string _cookedDir;
                /** @brief 출력 폴더(`.../bin/<rhi>`)마다 그 바이너리의 소스 폴더(`<리소스 루트>/<domain>/shaders`)입니다. 스탬프 키가 소스 폴더 기준이다(계획 단계가 채운다). */
                unordered_map<string, string> _mapSourceShadersDirByOutDir;
                /** @brief 컴파일에 실패한 소스(정규화한 절대 경로)를 RHI 폴더마다 적는다. 그 소스는 스탬프에서 빼서 다음 쿠킹이 다시 시도한다. */
                unordered_map<string, unordered_set<string>> _mapFailedSourceByDir;
                /**
                 * @brief RHI 폴더마다 소스(정규화한 절대 경로)가 연 헤더(절대 경로)의 합집합입니다. 컴파일한 작업은 컴파일 결과의 목록을, 최신이라 건너뛴 작업은 스탬프에 적힌 목록을 낸다.
                 *        스탬프가 소스별 "연 헤더" 로 적는다(`ShaderCooker::writeCookStamp`). 합치는 단계(직렬)만 쓴다.
                 */
                unordered_map<string, unordered_map<string, unordered_set<string>>> _mapIncludedByDir;
                /**
                 * @brief 리플렉션은 RHI 폴더마다 파일 하나로 모은다. 셰이더마다 사이드카를 두면 팩 엔트리와
                 *        압축 해제가 셰이더 수만큼 늘어난다(상용 엔진의 셰이더 라이브러리와 같은 이유).
                 */
                unordered_map<string, ShaderReflectionLibrary::EntryMap> _mapManifest;
                /** @brief 이 쿠킹의 스탬프 읽기 캐시입니다. 병렬 구간의 최신 판정이 함께 쓴다(안에서 잠근다 — 그래서 const 상태 안에서도 쓸 수 있다). */
                mutable ShaderCookStampCache _stampCache;
            };

            /** @brief 요청 하나를 포맷 하나로 쿠킹하는 작업의 입력입니다. 계획 단계(직렬)가 채우고, 병렬 구간에서는 읽기만 합니다. */
            struct FormatTask
            {
                const ShaderCookRequest* _pRequest{ nullptr };
                ShaderSourceID           _source;
                string                   _normPath;
                string                   _outDir;
                string                   _fileName;
                string                   _outPath;
                ShaderTargetFormat       _format{ ShaderTargetFormat::DXBC_D3D11 };
            };

            /** @brief 작업 하나의 결과 칸입니다. 작업마다 자기 칸에만 쓰고(공유 상태를 건드리지 않는다), 합치는 일은 요청 순서대로 직렬로 합니다. */
            struct FormatResult
            {
                ShaderReflectionData _reflection;
                vector<string>       _listIncludedFile;    ///< 이 작업의 소스가 연 헤더(컴파일 성공 또는 최신일 때만 채운다 — 작업 스레드는 자기 칸에만 쓴다)
                int64                _compileNanos{ 0 };   ///< 최신 판정과 컴파일에 쓴 시간
                int64                _reflectNanos{ 0 };   ///< 바이너리 읽기 · 리플렉션 · 계약 검사에 쓴 시간
                uint32               _violationCount{ 0 }; ///< 계약 검사가 센 위반 수(매니페스트에 넣을 때만 더한다)
                bool                 _bCooked{ false };    ///< 낡아서 새로 컴파일해 썼다
                bool                 _bCompileFailed{ false };
                bool                 _bReflected{ false };  ///< 바이너리를 읽어 `_reflection` 을 채웠다
                bool                 _bUnreadable{ false }; ///< 컴파일은 성공(또는 최신)인데 바이너리를 읽지 못했다
            };

            /**
             * @brief 병렬로 도는 쿠킹 한 번의 입력과 본문입니다. 병렬 단위는 **사슬**(같은 바이너리 파일을 만드는 작업의 묶음)이라 두 스레드가 한 파일을 쓰지 않습니다.
             * @details 컴파일러(DXC · FXC) · `ContentHash` · `isCookedOutputCurrent`(쿠킹 상태의 스탬프 캐시 잠금) · `ShaderReflection::reflect` · `ShaderBindingValidator::validate` 가 스레드 안전해야 하고,
             *          그 확인은 각 함수의 주석에 있다.
             */
            struct CookJob
            {
                const CookState*              _pState{ nullptr };
                const vector<FormatTask>*     _pListTask{ nullptr };
                const vector<vector<uint32>>* _pListChain{ nullptr };
                FormatResult*                 _pListResult{ nullptr }; ///< 작업 수만큼. 작업 `i` 는 `_pListResult[i]` 에만 쓴다
                bool                          _bForceAll{ false };

                /** @brief 사슬 [start, end) 를 돕니다. 사슬 안은 직렬 쿠킹의 순서 그대로 돕니다. */
                void runRange( uint32 start, uint32 end )
                {
                    for ( uint32 chainIndex = start; chainIndex < end; ++chainIndex )
                    {
                        bool bReflected = false;
                        for ( const uint32 taskIndex : ( *_pListChain )[chainIndex] )
                        {
                            FormatResult& result = _pListResult[taskIndex];
                            cookTask( ( *_pListTask )[taskIndex], bReflected, result );
                            bReflected = bReflected || result._bReflected;
                        }
                    }
                }

                /**
                 * @brief 작업 하나를 쿠킹하고(낡았을 때만) 그 바이너리의 리플렉션을 결과 칸에 담습니다.
                 * @param bSkipReflection 같은 바이너리의 앞 작업이 이미 리플렉션을 담았다. 직렬 쿠킹이 매니페스트에 이미 있는 항목을 건너뛰던 것과 같다
                 */
                void cookTask( const FormatTask& task, bool bSkipReflection, FormatResult& outResult ) const
                {
                    const ShaderCookRequest& request = *task._pRequest;
                    const Stopwatch          compileWatch;

                    // **파일 시간이 아니라 내용 해시로 판정한다.** `git pull` 은 소스의 mtime 을 체크아웃 시각으로 덮어써
                    // "산출물이 더 새것" 이 거꾸로 나올 수 있다. 낡은 바이너리는 한 백엔드만 다른 이미지를 내 백엔드 버그처럼 보인다.
                    const bool bUpToDate = ( _bForceAll == false ) && FileUtil::exists( task._outPath ) &&
                                           ShaderCooker::isCookedOutputCurrent( _pState->_resourceRoot, _pState->_listPriority, task._outDir, task._normPath, _pState->_stampCache,
                                                                                &outResult._listIncludedFile );
                    if ( bUpToDate == false )
                    {
                        outResult._listIncludedFile.clear();
                        ShaderCookResult cookResult{};
                        outResult._bCooked        = ShaderCooker::cookShader( task._source, task._outPath, request._entryPoint, request._stage, task._format, &request._listPermutation, &cookResult );
                        outResult._bCompileFailed = outResult._bCooked == false;
                        if ( outResult._bCooked )
                            outResult._listIncludedFile = std::move( cookResult._listIncludedFile );
                    }
                    outResult._compileNanos = compileWatch.getElapsedNanoseconds();
                    if ( bSkipReflection )
                        return;

                    // 새로 쿠킹했든 이미 최신이든 리플렉션은 항상 바이너리에서 뽑는다. 바이너리만 최신이고
                    // 리플렉션이 빠지면 런타임이 조용히 폴백하고, 배포 빌드에서는 그대로 실패한다.
                    // 최신 바이너리의 리플렉션을 이전 매니페스트에서 가져오지 않는다.
                    const Stopwatch reflectWatch;
                    vector<uint8>   bytecode;
                    const bool      bOutputRead = FileUtil::exists( task._outPath ) && FileUtil::readFile( task._outPath, bytecode ); // 컴파일이 실패하면 출력이 없다 - 아래 분기가 그 오류를 센다
                    if ( bOutputRead && bytecode.empty() == false )
                    {
                        outResult._reflection = ShaderReflection::reflect( bytecode, task._format );
                        // 쿠킹된 바이너리를 계약과 대조한다. 셰이더 헤더와 백엔드 상수가 한쪽만 바뀌면 여기서 이름과 숫자로 드러난다.
                        outResult._violationCount = ShaderBindingValidator::validate( outResult._reflection, task._format, task._outPath );
                        outResult._bReflected     = true;
                    }
                    else if ( outResult._bCompileFailed == false )
                    {
                        outResult._bUnreadable = true; // 쿠킹은 성공했다는데 바이너리를 읽지 못했다 — 합치는 단계가 실패로 센다(실패한 쿠킹은 이미 셌다)
                    }
                    outResult._reflectNanos = reflectWatch.getElapsedNanoseconds();
                }
            };

            /** @brief 쿠킹할 타깃 포맷 목록입니다. `Count` 면 네 RHI 모두입니다. */
            static vector<ShaderTargetFormat> makeTargetFormatList( ShaderTargetFormat targetFormat )
            {
                if ( targetFormat != ShaderTargetFormat::Count )
                    return { targetFormat };
                return { ShaderTargetFormat::DXBC_D3D11, ShaderTargetFormat::DXIL_D3D12, ShaderTargetFormat::SPIRV_Vulkan, ShaderTargetFormat::SPIRV_OpenGL };
            }

            /**
             * @brief 요청 하나를 모든 포맷의 작업으로 풀어 `outListTask` 에 더합니다. 소스 파일이 없거나 `shaders` 폴더 밖이면 쿠킹할 수 없으므로 **실패로 셉니다**(요청은 에셋이 만든 것이라 소스가 있어야 한다).
             */
            static void planRequest( const ShaderCookRequest& request, const vector<ShaderTargetFormat>& listTargetFormat, CookState& ioState, vector<FormatTask>& outListTask )
            {
                const std::optional<AbsolutePath> sourceFile = ShaderCompiler::findSourceFile( request._shaderPath ); // 컴파일러와 같은 규칙
                const string                      absPath    = sourceFile.has_value() ? sourceFile->str() : string{};
                if ( absPath.empty() )
                {
                    ++ioState._summary._failedCount;
                    SW_LOG_ERROR( "Shader cook request has no source file: '%#' (%#)", request._shaderPath, request._entryPoint );
                    return;
                }
                const string normPath  = FileUtil::normalizeSeparators( absPath );
                const string shaderDir = ShaderCooker::findShadersDirectory( normPath );
                if ( shaderDir.empty() )
                {
                    ++ioState._summary._failedCount;
                    SW_LOG_ERROR( "Shader cook request's source is not inside a 'shaders' folder: '%#' (%#)", normPath, request._entryPoint );
                    return;
                }
                // 출력은 쿠킹 스테이징의 같은 상대 자리다 — 런타임이 팩에서 읽는 자리와 같은 규칙 하나(`ShaderBinaryLocation`)로 정한다.
                const ShaderSourceID source{ request._shaderPath };
                if ( source.getShadersFolder().has_value() == false )
                {
                    ++ioState._summary._failedCount;
                    SW_LOG_ERROR( "Shader cook request's source id has no domain 'shaders' folder: '%#' (%#)", request._shaderPath, request._entryPoint );
                    return;
                }
                const string stemLower = source.getStemLower();
                for ( ShaderTargetFormat fmt : listTargetFormat )
                {
                    FormatTask task;
                    task._pRequest                                     = &request;
                    task._source                                       = source;
                    task._normPath                                     = normPath;
                    task._format                                       = fmt;
                    task._outDir                                       = FileUtil::normalizeSeparators( FileUtil::joinPath( ioState._cookedDir, ShaderBinaryLocation::make( source, fmt )->_binDirectory ) );
                    task._fileName                                     = ShaderCooker::computeBinaryFileName( stemLower, request._stage, request._entryPoint, request._permutationHash, ShaderCooker::getExtensionForFormat( fmt ) );
                    task._outPath                                      = FileUtil::joinPath( task._outDir, task._fileName );
                    ioState._mapSourceShadersDirByOutDir[task._outDir] = shaderDir;
                    outListTask.push_back( std::move( task ) );
                }
            }

            /**
             * @brief 작업들을 병렬로 쿠킹하고 결과를 `outListResult` 에 작업 순서대로 담습니다(합치기는 `mergeResults`).
             * @details 같은 바이너리 파일(경로를 소문자로 맞춘 것 — 윈도우는 대소문자를 가리지 않는다)을 만드는 작업은 한 사슬로 묶어 직렬 쿠킹의 순서대로 한 스레드가 돕니다.
             *          퍼뮤테이션 해시가 파일 이름에 들어가 요청마다 파일이 갈리지만, `cookRequests` 는 중복 요청을 받을 수 있어(`collectAllRequests` 만 걸러 준다) 사슬로 막는다.
             *          출력 폴더는 병렬 구간 전에 미리 만든다 — 폴더가 없는 채로 여러 스레드가 동시에 `create_directories` 를 부르지 않게 한다(어차피 매니페스트를 쓸 때 만들어질 폴더다).
             *          워커가 없거나 엔진 서비스가 묶이지 않았으면 `engine::runParallel` 이 이 스레드에서 사슬을 순서대로 돈다.
             */
            static void cookTasks( const vector<FormatTask>& listTask, bool bForceAll, const CookState& state, vector<FormatResult>& outListResult )
            {
                outListResult.clear();
                outListResult.resize( listTask.size() );

                unordered_map<string, uint32> mapChainIndexByOutPath;
                vector<vector<uint32>>        listChain;
                unordered_set<string>         uniqueBinFolder;
                for ( uint32 taskIndex = 0; taskIndex < static_cast<uint32>( listTask.size() ); ++taskIndex )
                {
                    const FormatTask& task = listTask[taskIndex];
                    uniqueBinFolder.insert( task._outDir );
                    const string chainKey = StringUtil::toLower( task._outPath.c_str() );
                    const auto   chainIt  = mapChainIndexByOutPath.find( chainKey );
                    if ( chainIt == mapChainIndexByOutPath.end() )
                    {
                        mapChainIndexByOutPath.emplace( chainKey, static_cast<uint32>( listChain.size() ) );
                        listChain.push_back( vector<uint32>{ taskIndex } );
                    }
                    else
                    {
                        listChain[chainIt->second].push_back( taskIndex );
                    }
                }
                for ( const string& outDir : uniqueBinFolder )
                {
                    // 못 만들면 ensure 가 폴더와 이유를 남기고, 그 폴더의 작업은 쓰기 실패로 결과(`outListResult`)에 남는다.
                    (void)FileUtil::ensureDirectoryExists( outDir );
                }

                CookJob job{};
                job._pState      = &state;
                job._pListTask   = &listTask;
                job._pListChain  = &listChain;
                job._pListResult = outListResult.data();
                job._bForceAll   = bForceAll;
                engine::runParallel( static_cast<uint32>( listChain.size() ), 1, SW_DELEGATE_METHOD( ParallelBlockDelegate, &CookJob::runRange, &job ) );
            }

            /**
             * @brief 작업 결과를 **작업 순서(요청 순서 × 포맷 순서)대로** 카운터와 매니페스트에 합칩니다. 병렬로 돌았어도 직렬 쿠킹과 같은 순서라 매니페스트의 내용과 카운터가 같습니다.
             */
            static void mergeResults( const vector<FormatTask>& listTask, vector<FormatResult>& inoutListResult, CookState& ioState )
            {
                int64 compileNanos{ 0 };
                int64 reflectNanos{ 0 };
                for ( uint32 taskIndex = 0; taskIndex < static_cast<uint32>( listTask.size() ); ++taskIndex )
                {
                    const FormatTask& task   = listTask[taskIndex];
                    FormatResult&     result = inoutListResult[taskIndex];
                    compileNanos += result._compileNanos;
                    reflectNanos += result._reflectNanos;
                    if ( result._bCooked )
                        ++ioState._summary._cookedCount;
                    if ( result._listIncludedFile.empty() == false )
                    {
                        unordered_set<string>& uniqueHeader = ioState._mapIncludedByDir[task._outDir][task._normPath];
                        for ( const string& header : result._listIncludedFile )
                        {
                            uniqueHeader.insert( header );
                        }
                    }
                    if ( result._bCompileFailed )
                    {
                        ++ioState._summary._failedCount;
                        ioState._mapFailedSourceByDir[task._outDir].insert( task._normPath );
                        SW_LOG_ERROR( "Shader cook failed: %# (%#) -> %#", task._normPath, task._pRequest->_entryPoint, task._outPath );
                    }

                    // 새로 쿠킹했든 이미 최신이든 매니페스트에는 항상 넣는다. 같은 바이너리를 만드는 뒤 작업은 이미 든 항목을 건너뛴다.
                    ShaderReflectionLibrary::EntryMap& manifest = ioState._mapManifest[task._outDir];
                    if ( manifest.find( task._fileName ) != manifest.end() )
                        continue;
                    if ( result._bReflected )
                    {
                        ioState._summary._contractViolationCount += result._violationCount;
                        manifest.emplace( task._fileName, std::move( result._reflection ) );
                    }
                    else if ( result._bUnreadable )
                    {
                        ++ioState._summary._failedCount;
                        SW_LOG_ERROR( "Cooked shader binary could not be read for the reflection manifest: %#", task._outPath );
                    }
                }
                ioState._summary._compileMs = static_cast<uint32>( compileNanos / constant::kNanosecondsPerMillisecond );
                ioState._summary._reflectMs = static_cast<uint32>( reflectNanos / constant::kNanosecondsPerMillisecond );
            }

            /**
             * @brief RHI 폴더별 리플렉션 매니페스트를 쓰고, 그 매니페스트에 없는 옛 바이너리를 지우고, 쿠킹 스탬프를 씁니다.
             *        매니페스트를 못 쓴 폴더는 바이너리도 지우지 않고 스탬프도 찍지 않습니다(다음 쿠킹이 다시 판정한다).
             * @details 옛 바이너리를 지우는 것은 쿠커의 일이다: 이 쿠킹은 요청을 **모두** 훑어 매니페스트를 새로 썼으므로 매니페스트 밖의 바이너리는 어느 요청도 만들지 않는 것이다
             *          (퍼뮤테이션 해시가 바뀌었거나 셰이더 · 목록 줄이 사라진 것). 남겨 두면 `--verify-shaders` 가 지고 배포 팩에 실린다.
             */
            static void writeManifestsAndStamps( CookState& ioState )
            {
                for ( const auto& manifestPair : ioState._mapManifest )
                {
                    if ( ShaderReflectionLibrary::save( manifestPair.second, manifestPair.first ) == false )
                    {
                        SW_LOG_ERROR( "Shader reflection manifest could not be written: %#", manifestPair.first );
                        ++ioState._summary._failedCount;
                        continue;
                    }
                    unordered_set<string> uniqueKeepFileName;
                    for ( const auto& entry : manifestPair.second )
                    {
                        uniqueKeepFileName.insert( entry.first );
                    }
                    uint32     removedCount{ 0 };
                    const bool bAllRemoved = ShaderCooker::removeUnlistedBinaries( manifestPair.first, uniqueKeepFileName, removedCount );
                    ioState._summary._removedCount += removedCount;
                    if ( bAllRemoved == false )
                        ++ioState._summary._failedCount;
                    const auto                            failedIt = ioState._mapFailedSourceByDir.find( manifestPair.first );
                    unordered_map<string, vector<string>> mapIncludedFile;
                    const auto                            includedIt = ioState._mapIncludedByDir.find( manifestPair.first );
                    if ( includedIt != ioState._mapIncludedByDir.end() )
                    {
                        for ( const auto& [sourcePath, uniqueHeader] : includedIt->second )
                        {
                            mapIncludedFile[sourcePath].assign( uniqueHeader.begin(), uniqueHeader.end() );
                        }
                    }
                    const string& sourceShadersDir = ioState._mapSourceShadersDirByOutDir[manifestPair.first];
                    if ( ShaderCooker::writeCookStamp( ioState._resourceRoot, ioState._listPriority, sourceShadersDir, manifestPair.first,
                                                       ( failedIt != ioState._mapFailedSourceByDir.end() ) ? &failedIt->second : nullptr, mapIncludedFile,
                                                       ioState._stampCache ) == false )
                    {
                        SW_LOG_ERROR( "Shader cook stamp could not be written: %#", manifestPair.first );
                        ++ioState._summary._failedCount;
                    }
                }
                ShaderReflectionLibrary::clearCache();
            }
        };
    } // namespace

    ShaderCookSummary ShaderCookDriver::cookAllShaders( string_view        cookedDir,
                                                        string_view        resourceRoot,
                                                        ShaderTargetFormat targetFormat,
                                                        bool               bForceAll )
    {
        // 루트를 주지 않으면 찾아 둔 리소스 루트다. 그것도 없으면 쿠킹하지 못한다(작업 폴더 기준 "Resource" 로 짐작하지 않는다).
        const string rootDir = resourceRoot.empty() ? engine::getSubsystem<AssetPaths>().getRootFolderPath() : string( resourceRoot );
        if ( rootDir.empty() || FileUtil::isDirectory( rootDir ) == false )
        {
            SW_LOG_ERROR( "Content root directory does not exist: '%#'", rootDir.c_str() );
            ShaderCookSummary nothingCooked{};
            nothingCooked._failedCount = 1; // 쿠킹하지 못했다. 실패로 센다(`App --cook-shaders` 가 실패로 끝난다)
            return nothingCooked;
        }

        SW_LOG_INFO( "Starting batch shader cook across all domains in '%#'...", rootDir.c_str() );

        // 렌더 파이프라인 에셋과 엔진 데이터로 요청을 한꺼번에 모은다
        vector<ShaderCookRequest> listRequest;
        if ( collectAllRequests( rootDir, listRequest ) == false )
        {
            SW_LOG_ERROR( "Shader cook requests could not be collected - engine default assets are missing or incomplete" );
            ShaderCookSummary nothingCooked{};
            nothingCooked._failedCount = 1; // 쿠킹하지 못했다. 실패로 센다(`App --cook-shaders` 가 실패로 끝난다)
            return nothingCooked;
        }

        if ( listRequest.empty() )
        {
            SW_LOG_ERROR( "Shader cook has no requests under '%#' - nothing would be cooked", rootDir.c_str() );
            ShaderCookSummary nothingCooked{};
            nothingCooked._failedCount = 1; // 아무것도 쿠킹하지 않은 채 성공으로 끝나지 않는다
            return nothingCooked;
        }
        return cookRequests( rootDir, engine::getSubsystem<AssetPaths>().getSearchPriority(), cookedDir, listRequest, targetFormat, bForceAll );
    }

    ShaderCookSummary ShaderCookDriver::cookRequests( string_view                      resourceRoot,
                                                      const vector<string>&            listPriority,
                                                      string_view                      cookedDir,
                                                      const vector<ShaderCookRequest>& listRequest,
                                                      ShaderTargetFormat               targetFormat,
                                                      bool                             bForceAll )
    {
        using Internal = ShaderCookDriverInternal;
        const Stopwatch                  totalWatch;
        const vector<ShaderTargetFormat> listTargetFormat = Internal::makeTargetFormatList( targetFormat );
        Internal::CookState              state;
        if ( cookedDir.empty() )
        {
            SW_LOG_ERROR( "Shader cook needs -cooked-dir=<folder> (the staging folder of cooked shaders, scenes and prefabs)" );
            state._summary._failedCount = 1; // 쿠킹하지 못했다. 실패로 센다(`App --cook-shaders` 가 실패로 끝난다)
            return state._summary;
        }
        state._resourceRoot = FileUtil::normalizeSeparators( resourceRoot );
        state._listPriority = listPriority;
        state._cookedDir    = FileUtil::normalizeSeparators( cookedDir );

        vector<Internal::FormatTask> listTask;
        for ( const ShaderCookRequest& request : listRequest )
        {
            Internal::planRequest( request, listTargetFormat, state, listTask );
        }
        vector<Internal::FormatResult> listResult;
        Internal::cookTasks( listTask, bForceAll, state, listResult );
        Internal::mergeResults( listTask, listResult, state );
        Internal::writeManifestsAndStamps( state );
        state._summary._totalMs = static_cast<uint32>( totalWatch.getElapsedMilliseconds() );

        const ShaderCookSummary& summary = state._summary;
        if ( summary._contractViolationCount > 0 )
            SW_LOG_ERROR( "%# binding contract violations - see the [Binding contract] logs above and fix the shader declarations or bindingslots.hlsli.", summary._contractViolationCount );
        if ( summary._failedCount > 0 )
            SW_LOG_ERROR( "Shader cooking failed for %# item(s) - their sources are left out of cook.stamp so the next cook retries them.", summary._failedCount );
        SW_LOG_INFO( "Shader cooking completed: %# binaries generated/updated, %# stale binaries removed (compile %# ms, reflection %# ms - summed over workers, total %# ms).",
                     summary._cookedCount, summary._removedCount, summary._compileMs, summary._reflectMs, summary._totalMs );
        return summary;
    }
} // namespace sw
