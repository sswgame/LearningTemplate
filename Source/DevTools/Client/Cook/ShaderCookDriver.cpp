/**
 * @file ShaderCookDriver.cpp
 * @brief 오프라인 쿠킹의 **정책**입니다. 요청 모두를 네 RHI 포맷으로 쿠킹하고 리플렉션 매니페스트를 씁니다.
 * @details 셰이더 하나를 컴파일하고 쓰는 법(`ShaderCompiler::compileBatch` · `ShaderCooker::writeBinary`)과 이름 짓기, 최신 판정은 `Shader/Compile` 의 메커니즘이고,
 *          "무엇을 쿠킹하는가"는 파이프라인 XML 과 패스 종류를 아는 렌더러의 지식입니다. 그래서 이 파일은
 *          `Renderer/Cook` 에 있고 `Shader/` 는 `Renderer/` 를 include 하지 않습니다.
 *
 *          **쿠킹은 "깨끗하다" 를 거짓으로 답하지 않는다**(`ShaderCookSummary::isClean` 이 `App --cook-shaders` 의 종료 코드를 정한다).
 *          소스가 없거나 `shaders` 폴더 밖인 요청 · 읽지 못한 바이너리 · 쓰지 못한 매니페스트 · 지우지 못한 옛 바이너리는 모두 실패로 센다. 조용히 건너뛰는 갈래를 두면 요청이 어긋난 채 종료 코드 0 이 된다.
 *          **요청이 만들지 않는 바이너리는 쿠커가 지운다**(`removeUnlistedFiles`). 이 쿠킹은 요청을 모두 훑어 매니페스트를 새로 쓰므로 매니페스트 밖의 바이너리는 옛 퍼뮤테이션 해시나 사라진 셰이더의 것이다.
 *          예전에는 손으로 지워야 했고, 그렇지 않으면 `--verify-shaders` 가 "매니페스트에 없는 바이너리" 로 졌다.
 *
 *          **컴파일 · 리플렉션은 병렬이고 합치기는 직렬이다.** 요청 × 포맷 작업은 계획(직렬) → 컴파일 · 리플렉션(병렬, 작업마다 자기 결과 칸에만 쓴다) → 합치기(직렬, 요청 순서)로 돈다.
 *          매니페스트 합치기 · 카운터 · 실패 소스 기록 · 매니페스트 쓰기는 직렬로 돌 때와 같은 순서라 산출물은 바이트 단위로 같다. 언리얼의 셰이더 컴파일 워커도 컴파일은 워커에서 하고 결과 병합은 한 스레드에서 한다.
 */
#include "pch.h"

#include "DevTools/Client/Cook/ShaderCookDriver.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "DevTools/Client/Cook/ShaderCookRequestCollector.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingValidatorIssue.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Compile/ShaderSourceGenerator.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        /** @brief `cookAllShaders` 의 단계들과 단계 사이에 넘기는 쿠킹 상태입니다. */
        struct ShaderCookDriverInternal
        {
            /** @brief 쿠킹 한 번의 누적 상태입니다. 병렬 구간에서는 읽기만 하고(`_resolver`), 나머지는 직렬 단계가 씁니다. */
            struct CookState
            {
                ShaderCookSummary _summary{};
                /** @brief 소스 · include 해석기입니다 — 컴파일러와 같은 규칙으로 계획과 컴파일이 함께 쓴다(쿠킹 시작 때 한 번 만든다). */
                optional<ShaderSourceResolver> _resolver;
                /** @brief 쿠킹 출력의 루트(`--cooked-dir`)입니다. 바이너리는 `<여기>/<domain>/shaders/bin/<rhi>` 에 쓴다 — 씬 · 프리팹 쿠킹본과 같은 스테이징이다. */
                AbsolutePath _cookedDir;
                /**
                 * @brief 리플렉션은 RHI 폴더마다 파일 하나로 모은다. 셰이더마다 사이드카를 두면 팩 엔트리와
                 *        압축 해제가 셰이더 수만큼 늘어난다(상용 엔진의 셰이더 라이브러리와 같은 이유).
                 */
                unordered_map<string, ShaderReflectionLibrary::EntryMap> _mapManifest;
                /** @brief 쿠킹 시작(이 쿠킹을 부른 스레드 — 게임 · 헤드리스 주 스레드)에 뜬 엔진 설정 사본입니다. 병렬 구간은 이것만 본다. */
                ShaderEnginePolicy _enginePolicy;

                explicit CookState( const ShaderEnginePolicy& enginePolicy )
                    : _enginePolicy{ enginePolicy }
                {
                }
            };

            /** @brief 요청 하나를 포맷 하나로 쿠킹하는 작업의 입력입니다. 계획 단계(직렬)가 채우고, 병렬 구간에서는 읽기만 합니다. */
            struct FormatTask
            {
                const ShaderCookRequest* _pRequest{ nullptr };
                ShaderSourceID           _source;
                string                   _normPath; ///< 소스의 절대 경로(로그)
                AbsolutePath             _outDir;
                string                   _fileName;
                AbsolutePath             _outPath;
                ShaderTargetFormat       _format{ ShaderTargetFormat::DXBC_D3D11 };
            };

            /** @brief 작업 @p task 의 컴파일 요청입니다. */
            static ShaderCompileDesc makeCompileDesc( const FormatTask& task, const ShaderEnginePolicy& enginePolicy )
            {
                const ShaderCookRequest& request = *task._pRequest;
                return ShaderCooker::makeCompileDesc( task._source, request._entryPoint, request._stage, task._format, enginePolicy, request._listPermutation );
            }

            /** @brief 작업 하나의 결과 칸입니다. 작업마다 자기 칸에만 쓰고(공유 상태를 건드리지 않는다), 합치는 일은 요청 순서대로 직렬로 합니다. */
            struct FormatResult
            {
                shared_ptr<const ShaderReflectionData> _pReflection;         ///< 바이너리의 리플렉션. 바이트가 같은 바이너리는 한 벌을 함께 쓴다
                int64                                  _reflectNanos{ 0 };   ///< 리플렉션 · 계약 검사에 쓴 시간
                uint64                                 _bytecodeHash{ 0 };   ///< 컴파일한 바이트코드의 해시(실패면 0)
                uint32                                 _violationCount{ 0 }; ///< 계약 검사가 센 위반 수(매니페스트에 넣을 때만 더한다)
                bool                                   _bCooked{ false };    ///< 낡아서 새로 컴파일해 썼다
                bool                                   _bCompileFailed{ false };
                bool                                   _bUnreadable{ false }; ///< 컴파일은 성공(또는 최신)인데 리플렉션을 읽지 못했다
            };

            /**
             * @brief 쿠킹 한 번 동안 바이트코드(내용 해시)마다 뽑은 리플렉션입니다 — 변형 대부분은 바이트코드가 같아(엔진 DX12 19,739 개의 서로 다른 바이트코드 2,880 개) 변형마다 리플렉터를 띄우지 않습니다.
             */
            class ReflectionMemo
            {
            public:
                struct Entry
                {
                    shared_ptr<const ShaderReflectionData> _pReflection; ///< 리플렉션을 읽지 못했으면 nullptr
                    uint32                                 _violationCount{ 0 };
                };

                /** @brief 바이트코드의 리플렉션과 계약 위반 수입니다. 처음 본 바이트면 뽑습니다(두 워커가 함께 뽑아도 먼저 넣은 것을 둘 다 쓴다). 여러 워커가 부른다. */
                Entry read( const ShaderCompileResult& compileResult, ShaderTargetFormat format, string_view binaryPath )
                {
                    const uint64 key = HashUtil::appendBytes64( compileResult._record._outputHash, reinterpret_cast<const uint8*>( &format ), sizeof( format ) );
                    {
                        ScopedLock<mutex> lock{ _mutex };
                        const auto        iter = _mapEntry.find( key );
                        if ( iter != _mapEntry.end() )
                            return iter->second;
                    }
                    Entry                          entry;
                    expected<ShaderReflectionData> reflected = ShaderReflectionData::reflect( compileResult._bytecode, format );
                    if ( reflected.has_value() )
                    {
                        // 쿠킹된 바이너리를 계약과 대조한다. 셰이더 헤더와 백엔드 상수가 한쪽만 바뀌면 여기서 이름과 숫자로 드러난다.
                        entry._violationCount = ShaderBindingValidatorIssue::validate( *reflected, format, binaryPath );
                        entry._pReflection    = sw::make_shared<const ShaderReflectionData>( std::move( *reflected ) );
                    }
                    else
                    {
                        SW_LOG_ERROR_ID( "ShaderCooker.ReflectionFailed", "Shader reflection failed for %#: %# (%#)", binaryPath, reflected.error().message(), reflected.error().detail() );
                    }
                    ScopedLock<mutex> lock{ _mutex };
                    return _mapEntry.emplace( key, std::move( entry ) ).first->second;
                }

            private:
                mutex                        _mutex;
                unordered_map<uint64, Entry> _mapEntry; ///< 바이트코드 해시에 형식을 섞은 값 → 리플렉션
            };

            /**
             * @brief 쿠킹 한 번의 묶음 컴파일이 내는 결과를 받습니다(`ShaderCompiler::compileBatch` — 워커에서 동시에 불린다). 컴파일 요청 하나가 **사슬**(같은 바이너리 파일을 만드는 작업의 묶음) 하나라 두 스레드가 한 파일을 쓰지 않습니다.
             * @details `ContentHash` · `ShaderCooker::writeBinary` · `ShaderReflectionData::reflect` · `ShaderBindingValidatorIssue::validate` 가 스레드 안전해야 하고,
             *          그 확인은 각 함수의 주석에 있다.
             */
            struct BatchReceiver
            {
                const vector<FormatTask>*        _pListTask{ nullptr };
                const vector<vector<uint32>>*    _pListChain{ nullptr };
                const vector<ShaderCompileDesc>* _pListDesc{ nullptr };   ///< 사슬과 짝
                FormatResult*                    _pListResult{ nullptr }; ///< 작업 수만큼. 작업 `i` 는 `_pListResult[i]` 에만 쓴다
                ReflectionMemo                   _reflectionMemo{};

                /** @brief 사슬 @p chainIndex 의 컴파일 결과를 씁니다. 사슬의 첫 작업이 쓰고 리플렉션을 뽑으며, 나머지(중복 요청)는 실패만 함께 센다. */
                void finishChain( uint32 chainIndex, const ShaderCompileResult& compileResult )
                {
                    const vector<uint32>& listTaskIndex = ( *_pListChain )[chainIndex];
                    finishTask( ( *_pListTask )[listTaskIndex.front()], ( *_pListDesc )[chainIndex], compileResult, _pListResult[listTaskIndex.front()] );
                    for ( const uint32 taskIndex : listTaskIndex )
                    {
                        _pListResult[taskIndex]._bCompileFailed = _pListResult[listTaskIndex.front()]._bCompileFailed;
                    }
                }

                /** @brief 작업 하나의 바이너리를 쓰고(내용이 다를 때만) 그 리플렉션을 결과 칸에 담습니다. */
                void finishTask( const FormatTask& task, const ShaderCompileDesc& desc, const ShaderCompileResult& compileResult, FormatResult& outResult )
                {
                    // 최신 판정은 컴파일의 쿠킹 기록 하나다 — 기록의 입력(소스 · 연 헤더 · 없던 앞자리 후보)과 도구가 그대로면 DDC 적중이라 컴파일러를 타지 않고, 파일은 내용이 다를 때만 쓴다.
                    outResult._bCompileFailed = ShaderCooker::writeBinary( desc, compileResult, task._outPath, outResult._bCooked ) == false;
                    if ( outResult._bCompileFailed )
                        return;
                    outResult._bytecodeHash = compileResult._record._outputHash;

                    // 새로 쿠킹했든 이미 최신이든 리플렉션은 항상 그 바이트에서 뽑는다 — 이전 매니페스트에서 가져오지 않는다.
                    // 리플렉션이 빠지면 런타임이 조용히 폴백하고, 배포 빌드에서는 그대로 실패한다.
                    // 리플렉션을 읽지 못한 바이너리는 런타임이 빈 레이아웃으로 읽는다 — 읽지 못한 것으로 세어 쿠킹을 실패시킨다.
                    const Stopwatch       reflectWatch;
                    ReflectionMemo::Entry entry = _reflectionMemo.read( compileResult, task._format, task._outPath.str() );
                    outResult._bUnreadable      = entry._pReflection == nullptr;
                    outResult._violationCount   = entry._violationCount;
                    outResult._pReflection      = std::move( entry._pReflection );
                    outResult._reflectNanos     = reflectWatch.getElapsedNanoseconds();
                }
            };

            /** @brief 쿠커 폴더 @p binDirectory 에서 @p manifest 에 없는 파일(바이너리 · 옛 스탬프)을 지우고 수를 @p ioRemovedCount 에 더합니다. 하나라도 못 지우면 false 입니다(그 파일이 남아 판정이 진다). */
            [[nodiscard]] static bool removeUnlistedFiles( const AbsolutePath& binDirectory, const ShaderReflectionLibrary::EntryMap& manifest, uint32& ioRemovedCount )
            {
                vector<AbsolutePath> listFile;
                (void)FileUtil::collectFiles( binDirectory, "", listFile, false ); // 폴더가 없으면 지울 것도 없다
                bool bAllRemoved = true;
                for ( const AbsolutePath& filePath : listFile )
                {
                    const string fileName = FileUtil::getFileNamePart( filePath.str() );
                    if ( fileName == ShaderReflectionLibrary::getManifestFileName() || manifest.find( fileName ) != manifest.end() )
                        continue;
                    if ( FileUtil::removeFile( filePath ) )
                    {
                        ++ioRemovedCount;
                        SW_LOG_INFO_ID( "ShaderCooker.RemovedShaderBinaryNo", "Removed a shader binary no cook request makes: %#", filePath.str() );
                    }
                    else
                    {
                        SW_LOG_ERROR_ID( "ShaderCooker.CouldNotRemoveStale", "Could not remove the stale shader binary: %#", filePath.str() );
                        bAllRemoved = false;
                    }
                }
                return bAllRemoved;
            }

            /** @brief 문제 한 줄을 서식으로 만들어 더합니다. */
            template <typename... Args>
            static void appendProblem( vector<string>& outListProblem, const utf8* pFormat, const Args&... args )
            {
                StringBuilder<constant::buffer::kMax512> builder;
                builder.appendFormat( pFormat, args... );
                outListProblem.emplace_back( builder.view() );
            }

            /**
             * @brief 도메인 하나의 쿠킹본 폴더 @p binDirectory 를 판정합니다 — 바이너리마다 그것을 만드는 요청의 컴파일 기록이 지금도 새것이고 내용이 기록과 같은지, 매니페스트가 모두 덮는지,
             *        요청의 소스마다 바이너리가 하나라도 있는지.
             * @param mapRequest 그 폴더의 바이너리 파일 이름 → 요청
             */
            static void verifyFolder( const AbsolutePath& binDirectory, string_view label, const unordered_map<string, const ShaderCookRequest*>& mapRequest, const unordered_map<const ShaderCookRequest*, uint64>& mapBytecodeHash,
                                      vector<string>& outListProblem )
            {
                vector<AbsolutePath> listFile;
                if ( FileUtil::isDirectory( binDirectory ) == false || FileUtil::collectFiles( binDirectory, "", listFile, false ) == false )
                {
                    appendProblem( outListProblem, "%#: no binary folder, or its files could not all be listed (never cooked?)", label );
                    return;
                }
                const AbsolutePath                manifestPath = binDirectory.join( ShaderReflectionLibrary::getManifestFileName() );
                ShaderReflectionLibrary::EntryMap mapManifest;
                if ( FileUtil::isRegularFile( manifestPath ) == false || ShaderReflectionLibrary::loadManifestFile( manifestPath, mapManifest ) == false )
                {
                    appendProblem( outListProblem, "%#: no readable reflection manifest - cook again", label );
                    return;
                }
                unordered_set<string> uniqueCookedSource;
                for ( const AbsolutePath& filePath : listFile )
                {
                    const string fileName = FileUtil::getFileNamePart( filePath.str() );
                    if ( fileName == ShaderReflectionLibrary::getManifestFileName() )
                        continue;
                    const auto requestIt = mapRequest.find( fileName );
                    if ( requestIt == mapRequest.end() )
                    {
                        appendProblem( outListProblem, "%#: '%#' is made by no cook request", label, fileName );
                        continue;
                    }
                    const ShaderCookRequest& request = *requestIt->second;
                    uniqueCookedSource.insert( request._shaderPath );
                    // 리플렉션 없이 바인딩한 DX12 가 DEVICE_HUNG 으로 죽었다 — 매니페스트 키는 바이너리 파일 이름이다.
                    if ( mapManifest.find( fileName ) == mapManifest.end() )
                        appendProblem( outListProblem, "%#: the manifest misses '%#' - cook again", label, fileName );
                    const auto hashIt = mapBytecodeHash.find( &request );
                    if ( hashIt == mapBytecodeHash.end() )
                        appendProblem( outListProblem, "%#: '%#' (%#) was not cooked again after its source, a header or the toolchain changed", label, fileName, request._shaderPath );
                    else if ( ContentHash::computeFile( filePath ) != hashIt->second )
                        appendProblem( outListProblem, "%#: '%#' is not the bytecode of its compile record - cook again", label, fileName );
                }
                unordered_set<string> uniqueMissingSource;
                for ( const auto& [fileName, pRequest] : mapRequest )
                {
                    if ( uniqueCookedSource.contains( pRequest->_shaderPath ) == false && uniqueMissingSource.insert( pRequest->_shaderPath ).second )
                        appendProblem( outListProblem, "%#: '%#' was never cooked", label, pRequest->_shaderPath );
                }
            }

            /** @brief 쿠킹할 타깃 포맷 목록입니다. `Count` 면 네 RHI 모두입니다. */
            static vector<ShaderTargetFormat> makeTargetFormatList( ShaderTargetFormat targetFormat )
            {
                if ( targetFormat != ShaderTargetFormat::Count )
                    return { targetFormat };
                return { ShaderTargetFormat::DXBC_D3D11, ShaderTargetFormat::DXIL_D3D12, ShaderTargetFormat::SPIRV_Vulkan, ShaderTargetFormat::SPIRV_OpenGL };
            }

            /** @brief 요청 하나 × 형식 하나의 바이너리 자리입니다 — 스테이징 루트 기준 폴더(`<도메인>/shaders/bin/<rhi>`)와 파일 이름. */
            struct BinarySlot
            {
                string _binDirectory;
                string _fileName;
            };

            /**
             * @brief 요청 @p request 가 형식 @p format 으로 만드는 바이너리의 자리입니다. 그 형식에 없는 단계(WebGPU 의 헐 · 도메인 · 지오메트리)면 없음입니다.
             * @details 굽기(`planRequest`)와 팩 쿠커에 넘기는 요청 바이너리 목록(`collectRequestedBinaries`)이 이 규칙 하나를 쓴다.
             */
            static optional<BinarySlot> locateBinary( const ShaderCookRequest& request, ShaderTargetFormat format )
            {
                const bool bOptionalStage = request._stage == ShaderStage::Hull || request._stage == ShaderStage::Domain || request._stage == ShaderStage::Geometry;
                if ( bOptionalStage && ShaderTargetFormatTable::find( format )->_bOptionalStages == 0 )
                    return nullopt;
                const ShaderSourceID source{ request._shaderPath };
                BinarySlot           slot;
                slot._binDirectory = ShaderBinaryLocation::make( source, format )->_binDirectory;
                slot._fileName     = ShaderCooker::computeBinaryFileName( source.getStemLower(), request._stage, request._entryPoint, request._permutationHash, ShaderCooker::getExtensionForFormat( format ) );
                return slot;
            }

            /**
             * @brief 요청 하나를 모든 포맷의 작업으로 풀어 `outListTask` 에 더합니다. 소스 파일이 없거나 `shaders` 폴더 밖이면 쿠킹할 수 없으므로 **실패로 셉니다**(요청은 에셋이 만든 것이라 소스가 있어야 한다).
             */
            static void planRequest( const ShaderCookRequest& request, const vector<ShaderTargetFormat>& listTargetFormat, CookState& ioState, vector<FormatTask>& outListTask )
            {
                // 소스는 컴파일러와 같은 해석기로 푼다. 출력은 쿠킹 스테이징의 같은 상대 자리다 — 런타임이 팩에서 읽는 자리와 같은 규칙 하나(`ShaderBinaryLocation`)로 정한다.
                const ShaderSourceID               source{ request._shaderPath };
                const expected<ShaderCompileInput> input = ioState._resolver->resolve( source );
                if ( input.has_value() == false )
                {
                    ++ioState._summary._failedCount;
                    SW_LOG_ERROR_ID( "ShaderCooker.ShaderCookRequestCannot", "Shader cook request cannot be resolved (%#): %# (%#)", request._entryPoint, input.error().message(), input.error().detail() );
                    return;
                }
                for ( ShaderTargetFormat fmt : listTargetFormat )
                {
                    const optional<BinarySlot> slot = locateBinary( request, fmt );
                    if ( slot.has_value() == false )
                        continue; // 이 형식에는 헐 · 도메인 · 지오메트리가 없다(WebGPU) — 런타임도 그 PSO 를 만들지 않는다
                    FormatTask task;
                    task._pRequest = &request;
                    task._source   = source;
                    task._normPath = input->_source.str();
                    task._format   = fmt;
                    task._outDir   = ioState._cookedDir.join( slot->_binDirectory );
                    task._fileName = slot->_fileName;
                    task._outPath  = task._outDir.join( task._fileName );
                    outListTask.push_back( std::move( task ) );
                }
            }

            /**
             * @brief 작업들을 병렬로 쿠킹하고 결과를 `outListResult` 에 작업 순서대로 담습니다(합치기는 `mergeResults`).
             * @details 같은 바이너리 파일(경로를 소문자로 맞춘 것 — 윈도우는 대소문자를 가리지 않는다)을 만드는 작업은 한 사슬로 묶어 직렬 쿠킹의 순서대로 한 스레드가 돕니다.
             *          퍼뮤테이션 해시가 파일 이름에 들어가 요청마다 파일이 갈리지만, `cookRequests` 는 중복 요청을 받을 수 있어(`collectAllRequests` 만 걸러 준다) 사슬로 막는다.
             *          출력 폴더는 병렬 구간 전에 미리 만든다 — 폴더가 없는 채로 여러 스레드가 동시에 `create_directories` 를 부르지 않게 한다(어차피 매니페스트를 쓸 때 만들어질 폴더다).
             */
            static void cookTasks( const vector<FormatTask>& listTask, const CookState& state, vector<FormatResult>& outListResult )
            {
                outListResult.clear();
                outListResult.resize( listTask.size() );

                unordered_map<string, uint32> mapChainIndexByOutPath;
                vector<vector<uint32>>        listChain;
                unordered_set<string>         uniqueBinFolder;
                vector<AbsolutePath>          listBinFolder;
                for ( uint32 taskIndex = 0; taskIndex < static_cast<uint32>( listTask.size() ); ++taskIndex )
                {
                    const FormatTask& task = listTask[taskIndex];
                    if ( uniqueBinFolder.insert( task._outDir.str() ).second )
                        listBinFolder.push_back( task._outDir );
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
                for ( const AbsolutePath& outDir : listBinFolder )
                {
                    // 못 만들면 ensure 가 폴더와 이유를 남기고, 그 폴더의 작업은 쓰기 실패로 결과(`outListResult`)에 남는다.
                    (void)FileUtil::ensureDirectoryExists( outDir );
                }

                // 사슬 하나에 컴파일 요청 하나다 — 사슬 안의 작업은 같은 요청(같은 소스 · 진입점 · define)의 중복이다.
                vector<ShaderCompileDesc> listDesc;
                listDesc.reserve( listChain.size() );
                for ( const vector<uint32>& listTaskIndex : listChain )
                {
                    listDesc.push_back( makeCompileDesc( listTask[listTaskIndex.front()], state._enginePolicy ) );
                }
                BatchReceiver receiver{ &listTask, &listChain, &listDesc, outListResult.data() };
                ShaderCompiler::compileBatch( listDesc, ShaderRecordScope::Source, SW_DELEGATE_METHOD( ShaderCompiledDelegate, &BatchReceiver::finishChain, &receiver ) );
            }

            /**
             * @brief 작업 결과를 **작업 순서(요청 순서 × 포맷 순서)대로** 카운터와 매니페스트에 합칩니다. 병렬로 돌았어도 직렬 쿠킹과 같은 순서라 매니페스트의 내용과 카운터가 같습니다.
             */
            static void mergeResults( const vector<FormatTask>& listTask, vector<FormatResult>& inoutListResult, CookState& ioState )
            {
                int64 reflectNanos{ 0 };
                for ( uint32 taskIndex = 0; taskIndex < static_cast<uint32>( listTask.size() ); ++taskIndex )
                {
                    const FormatTask& task   = listTask[taskIndex];
                    FormatResult&     result = inoutListResult[taskIndex];
                    reflectNanos += result._reflectNanos;
                    if ( result._bCooked )
                        ++ioState._summary._cookedCount;
                    if ( result._bCompileFailed )
                    {
                        ++ioState._summary._failedCount;
                        SW_LOG_ERROR_ID( "ShaderCooker.ShaderCookFailed", "Shader cook failed: %# (%#) -> %#", task._normPath, task._pRequest->_entryPoint, task._outPath.str() );
                    }

                    // 새로 쿠킹했든 이미 최신이든 매니페스트에는 항상 넣는다. 같은 바이너리를 만드는 뒤 작업은 이미 든 항목을 건너뛴다.
                    ShaderReflectionLibrary::EntryMap& manifest = ioState._mapManifest[task._outDir.str()];
                    if ( manifest.find( task._fileName ) != manifest.end() )
                        continue;
                    if ( result._pReflection != nullptr )
                    {
                        ioState._summary._contractViolationCount += result._violationCount;
                        manifest.emplace( task._fileName, std::move( result._pReflection ) );
                    }
                    else if ( result._bUnreadable )
                    {
                        ++ioState._summary._failedCount;
                        SW_LOG_ERROR_ID( "ShaderCooker.CookedShaderBinaryCould", "Cooked shader binary could not be read for the reflection manifest: %#", task._outPath.str() );
                    }
                }
                ioState._summary._reflectMs = static_cast<uint32>( reflectNanos / constant::time::kNanosecondsPerMillisecond );
            }

            /**
             * @brief 셰이더(소스 · 진입점 · 형식)마다 변형 수와 서로 다른 바이트코드 수를 세어, 겹친 변형이 가장 많은 것부터 남깁니다.
             * @details 바이트가 같은 변형은 컴파일은 한 번이지만 요청 · 전처리 · 바이너리 파일은 변형마다 든다 — 어느 셰이더의 define 이 그 단계에 쓰이지 않는지 이 표로 보고 요청에서 뺀다.
             */
            static void logVariantOverlap( const vector<FormatTask>& listTask, const vector<FormatResult>& listResult )
            {
                struct Overlap
                {
                    string                _label;
                    uint32                _variantCount{ 0 };
                    unordered_set<uint64> _uniqueHash;

                    size_t getRepeatCount() const { return _variantCount - _uniqueHash.size(); }
                };
                static constexpr size_t        kMaxLine = 20;
                unordered_map<string, Overlap> mapOverlap;
                unordered_set<uint64>          uniqueHash;
                uint32                         variantCount = 0;
                for ( size_t index = 0; index < listTask.size(); ++index )
                {
                    if ( listResult[index]._bytecodeHash == 0 )
                        continue;
                    StringBuilder<constant::buffer::kMax256> labelBuilder;
                    labelBuilder.appendFormat( "%# %# [%#]", listTask[index]._source.c_str(), listTask[index]._pRequest->_entryPoint, ShaderCooker::getSubfolderForFormat( listTask[index]._format ) );
                    Overlap& overlap = mapOverlap[string( labelBuilder.view() )];
                    ++overlap._variantCount;
                    overlap._uniqueHash.insert( listResult[index]._bytecodeHash );
                    uniqueHash.insert( listResult[index]._bytecodeHash );
                    ++variantCount;
                }
                vector<const Overlap*> listOverlap;
                for ( auto& [label, overlap] : mapOverlap )
                {
                    overlap._label = label;
                    if ( overlap.getRepeatCount() > 0 )
                        listOverlap.push_back( &overlap );
                }
                VectorUtil::sort( listOverlap, []( const Overlap* pLeft, const Overlap* pRight )
                { return pLeft->getRepeatCount() != pRight->getRepeatCount() ? pLeft->getRepeatCount() > pRight->getRepeatCount() : pLeft->_label < pRight->_label; } );
                SW_LOG_INFO_ID( "ShaderCooker.VariantOverlap", "Shader variants: %# binaries hold %# distinct bytecodes", variantCount, static_cast<uint32>( uniqueHash.size() ) );
                for ( size_t index = 0; index < listOverlap.size() && index < kMaxLine; ++index )
                {
                    SW_LOG_INFO_ID( "ShaderCooker.VariantOverlapShader", "Shader variants: %# - %# variants, %# distinct", listOverlap[index]->_label, listOverlap[index]->_variantCount,
                                    static_cast<uint32>( listOverlap[index]->_uniqueHash.size() ) );
                }
            }

            /**
             * @brief RHI 폴더별 리플렉션 매니페스트를 쓰고, 그 매니페스트에 없는 옛 파일을 지웁니다. 매니페스트를 못 쓴 폴더는 지우지 않습니다(다음 쿠킹이 다시 판정한다).
             * @details 옛 바이너리를 지우는 것은 쿠커의 일이다: 이 쿠킹은 요청을 **모두** 훑어 매니페스트를 새로 썼으므로 매니페스트 밖의 바이너리는 어떤 요청도 만들지 않는 것이다
             *          (퍼뮤테이션 해시가 바뀌었거나 셰이더 · 목록 줄이 사라진 것). 남겨 두면 `--verify-shaders` 가 지고 배포 팩에 실린다.
             */
            static void writeManifests( CookState& ioState )
            {
                for ( const auto& [folderText, manifest] : ioState._mapManifest )
                {
                    const AbsolutePath manifestFolder = FileUtil::toDiskPath( folderText ); // 맵 열쇠 글이다(`_outDir`)
                    if ( ShaderReflectionLibrary::save( manifest, manifestFolder ) == false )
                    {
                        SW_LOG_ERROR_ID( "ShaderCooker.ShaderReflectionManifestCould", "Shader reflection manifest could not be written: %#", folderText );
                        ++ioState._summary._failedCount;
                        continue;
                    }
                    if ( removeUnlistedFiles( manifestFolder, manifest, ioState._summary._removedCount ) == false )
                        ++ioState._summary._failedCount;
                }
                ShaderReflectionLibrary::clearCache();
            }
        };
    } // namespace

    ShaderCookSummary ShaderCookDriver::cookAllShaders( const AbsolutePath& cookedDir,
                                                        const AbsolutePath& resourceRoot,
                                                        ShaderTargetFormat  targetFormat,
                                                        ShaderCookScope     scope )
    {
        // 표본은 빈 스테이징에만 굽는다 — 쿠커가 요청 밖의 바이너리를 지우므로 채워진 스테이징을 표본으로 덮으면 쿠킹본을 잃는다.
        vector<ShaderCookSampleAxis> listSampleAxis;
        if ( scope == ShaderCookScope::Sample )
        {
            vector<AbsolutePath> listExisting;
            (void)FileUtil::collectFiles( cookedDir, "", listExisting, true ); // 폴더가 없으면 빈 목록이다
            const expected<ShaderCookSampleRule> rule = ConfigManager::readConfig( ShaderCookSampleRule::kConfigFile );
            if ( cookedDir.empty() || listExisting.empty() == false || rule.has_value() == false )
            {
                if ( rule.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "ShaderCooker.SampleRuleInvalid", "%#: %#", rule.error().message(), rule.error().detail() );
                }
                else
                {
                    SW_LOG_ERROR_ID( "ShaderCooker.SampleNeedsEmptyStaging", "A sample cook needs an empty cooked folder (it would remove the binaries it does not cook): '%#'", cookedDir.str() );
                }
                ShaderCookSummary nothingCooked{};
                nothingCooked._failedCount = 1;
                return nothingCooked;
            }
            listSampleAxis = rule->_listCoverAxis;
        }

        // 루트를 주지 않으면 찾아 둔 리소스 루트다. 그것도 없으면 쿠킹하지 못한다(작업 폴더 기준 "Resource" 로 짐작하지 않는다).
        const AbsolutePath rootDir = resourceRoot.empty() ? engine::getSubsystem<AssetPaths>().getRootFolderPath() : resourceRoot;
        if ( rootDir.empty() || FileUtil::isDirectory( rootDir ) == false )
        {
            SW_LOG_ERROR_ID( "ShaderCooker.ContentRootDirectoryDoes", "Content root directory does not exist: '%#'", rootDir.c_str() );
            ShaderCookSummary nothingCooked{};
            nothingCooked._failedCount = 1; // 쿠킹하지 못했다. 실패로 센다(`App --cook-shaders` 가 실패로 끝난다)
            return nothingCooked;
        }

        SW_LOG_INFO_ID( "ShaderCooker.StartingBatchShaderCook", "Starting batch shader cook across all domains in '%#'...", rootDir.c_str() );

        // 생성된 셰이더 소스(머티리얼 그래프)를 먼저 맞춘다 — 그 뒤로는 손으로 쓴 셰이더 · 머티리얼과 같은 입력이다(컴파일 기록이 내용 해시로 추적한다).
        // 쿠커는 생성기를 모른다 — 개발 코드가 창구(`IShaderSourceGenerator`)에 정적 등록한다. 배포 구성에는 생성기가 없다.
        if constexpr ( build::kShipping == false )
        {
            uint32 generatedCount = 0;
            if ( IShaderSourceGenerator::refreshAllRegistered( engine::getSubsystem<AssetPaths>(), rootDir, generatedCount ) == false )
            {
                ShaderCookSummary nothingCooked{};
                nothingCooked._failedCount = 1; // 낡은 생성물을 쿠킹하지 않는다. 실패로 센다
                return nothingCooked;
            }
        }

        // 렌더 파이프라인 에셋과 엔진 데이터로 요청을 한꺼번에 모은다
        vector<ShaderCookRequest> listRequest;
        if ( ShaderCookRequestCollector::collectAllRequests( rootDir, listRequest ) == false )
        {
            SW_LOG_ERROR_ID( "ShaderCooker.ShaderCookRequestsCould", "Shader cook requests could not be collected - engine default assets are missing or incomplete" );
            ShaderCookSummary nothingCooked{};
            nothingCooked._failedCount = 1; // 쿠킹하지 못했다. 실패로 센다(`App --cook-shaders` 가 실패로 끝난다)
            return nothingCooked;
        }

        if ( listRequest.empty() )
        {
            SW_LOG_ERROR_ID( "ShaderCooker.ShaderCookNoRequests", "Shader cook has no requests under '%#' - nothing would be cooked", rootDir.c_str() );
            ShaderCookSummary nothingCooked{};
            nothingCooked._failedCount = 1; // 아무것도 쿠킹하지 않은 채 성공으로 끝나지 않는다
            return nothingCooked;
        }
        if ( scope == ShaderCookScope::Sample )
        {
            const size_t requestCount = listRequest.size();
            listRequest               = selectSample( listRequest, listSampleAxis );
            SW_LOG_INFO_ID( "ShaderCooker.SampleSelected", "Sample cook: %# of %# requests cover every value of the rule axes (%#)", listRequest.size(), requestCount, ShaderCookSampleRule::kConfigFile.getPath() );
        }
        return cookRequests( cookedDir, listRequest, targetFormat );
    }

    expected<void> ShaderCookSampleRule::validate() const
    {
        if ( _listCoverAxis.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "_listCoverAxis needs at least one axis", kConfigFile.getPath() } );
        for ( size_t index = 1; index < _listCoverAxis.size(); ++index )
        {
            if ( std::find( _listCoverAxis.begin(), _listCoverAxis.begin() + static_cast<ptrdiff_t>( index ), _listCoverAxis[index] ) != _listCoverAxis.begin() + static_cast<ptrdiff_t>( index ) )
                return unexpected( Error{ ErrorKind::InvalidArgument, "_listCoverAxis takes each axis once", kConfigFile.getPath() } );
        }
        return {};
    }

    vector<ShaderCookRequest> ShaderCookDriver::selectSample( const vector<ShaderCookRequest>& listRequest, const vector<ShaderCookSampleAxis>& listAxis )
    {
        // 축마다 이미 덮은 값. 값 글은 축 안에서만 견주므로 축끼리 섞이지 않는다.
        vector<unordered_set<string>> listCovered( listAxis.size() );
        vector<ShaderCookRequest>     listSample;
        vector<string>                listValue;
        for ( const ShaderCookRequest& request : listRequest )
        {
            bool bAddsValue = false;
            for ( size_t axisIndex = 0; axisIndex < listAxis.size(); ++axisIndex )
            {
                listValue.clear();
                switch ( listAxis[axisIndex] )
                {
                    case ShaderCookSampleAxis::Source:
                        listValue.push_back( request._shaderPath );
                        break;
                    case ShaderCookSampleAxis::EntryPoint:
                    {
                        StringBuilder<constant::buffer::kMax256> valueBuilder;
                        valueBuilder.appendFormat( "%#|%#", request._shaderPath, request._entryPoint );
                        listValue.emplace_back( valueBuilder.view() );
                    }
                    break;
                    case ShaderCookSampleAxis::Stage:
                        listValue.push_back( to_string( static_cast<uint32>( request._stage ) ) );
                        break;
                    case ShaderCookSampleAxis::PermutationDefine:
                        if ( request._listPermutation.empty() )
                            listValue.emplace_back(); // 퍼뮤테이션이 없는 갈래도 한 번 덮는다
                        listValue.insert( listValue.end(), request._listPermutation.begin(), request._listPermutation.end() );
                        break;
                }
                for ( const string& value : listValue )
                    bAddsValue = listCovered[axisIndex].insert( value ).second || bAddsValue;
            }
            if ( bAddsValue )
                listSample.push_back( request );
        }
        return listSample;
    }

    void ShaderCookDriver::collectCookedDomains( const vector<ShaderCookRequest>& listRequest, vector<string>& outListDomain )
    {
        static constexpr string_view kShadersSuffix = "/shaders";
        outListDomain.clear();
        unordered_set<string> uniqueDomain; // 목록은 처음 나온 순서를 지키고, 있는지는 집합으로 본다(요청 수 × 도메인 수를 훑지 않는다)
        for ( const ShaderCookRequest& request : listRequest )
        {
            const ShaderSourceID        source{ request._shaderPath }; // 폴더 값은 이 객체의 글을 가리킨다
            const optional<string_view> shadersFolder = source.getShadersFolder();
            if ( shadersFolder.has_value() == false || StringUtil::endsWith( *shadersFolder, kShadersSuffix ) == false )
                continue;
            string domain{ shadersFolder->substr( 0, shadersFolder->size() - kShadersSuffix.size() ) };
            if ( uniqueDomain.insert( domain ).second )
                outListDomain.push_back( std::move( domain ) );
        }
    }

    void ShaderCookDriver::verifyCookedShaders( const AbsolutePath& cookedDir, ShaderTargetFormat targetFormat, vector<string>& outListProblem )
    {
        using Internal                       = ShaderCookDriverInternal;
        const AssetPaths&         assetPaths = engine::getSubsystem<AssetPaths>();
        vector<ShaderCookRequest> listRequest;
        if ( ShaderCookRequestCollector::collectAllRequests( assetPaths.getRootFolderPath(), listRequest ) == false )
        {
            outListProblem.emplace_back( "shader cook requests could not be collected - cannot tell which domains are cooked" );
            return;
        }
        const optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( assetPaths );
        if ( resolver.has_value() == false )
        {
            outListProblem.emplace_back( "the shader include roots cannot be resolved (see the error above)" );
            return;
        }
        // 폴더마다 그 폴더의 바이너리를 만드는 요청 — 굽는 것(`planRequest`)과 같은 자리 규칙이다.
        unordered_map<string, unordered_map<string, const ShaderCookRequest*>> mapFolder;
        for ( const ShaderCookRequest& request : listRequest )
        {
            optional<Internal::BinarySlot> slot = Internal::locateBinary( request, targetFormat );
            if ( slot.has_value() )
                mapFolder[slot->_binDirectory].emplace( std::move( slot->_fileName ), &request );
        }
        // 요청마다 지금 맞는 바이트코드 해시 — 소스마다의 기록을 한 번씩만 연다.
        const ShaderEnginePolicy         enginePolicy = ShaderEnginePolicy::capture();
        vector<const ShaderCookRequest*> listPlanned;
        vector<ShaderCompileDesc>        listDesc;
        for ( const auto& [binDirectory, mapRequest] : mapFolder )
        {
            for ( const auto& [fileName, pRequest] : mapRequest )
            {
                listPlanned.push_back( pRequest );
                listDesc.push_back( ShaderCooker::makeCompileDesc( ShaderSourceID{ pRequest->_shaderPath }, pRequest->_entryPoint, pRequest->_stage, targetFormat, enginePolicy, pRequest->_listPermutation ) );
            }
        }
        const vector<optional<uint64>>                  listHash = ShaderCompiler::findCurrentBytecodeHashes( listDesc );
        unordered_map<const ShaderCookRequest*, uint64> mapBytecodeHash;
        for ( size_t index = 0; index < listPlanned.size(); ++index )
        {
            if ( listHash[index].has_value() )
                mapBytecodeHash.emplace( listPlanned[index], *listHash[index] );
        }
        vector<string> listDomain;
        collectCookedDomains( listRequest, listDomain );
        for ( const string& domain : listDomain )
        {
            const optional<AbsolutePath> shadersFolder = resolver->findDomainShadersFolder( domain );
            if ( shadersFolder.has_value() )
                (void)resolver->getIncludeResolver().findShadowedHeaders( *shadersFolder, outListProblem ); // 가려진 헤더는 문제로 적힌다
            const string binDirectory = ShaderBinaryLocation::makeForDomain( domain, targetFormat )._binDirectory;
            Internal::verifyFolder( cookedDir.join( binDirectory ), binDirectory, mapFolder[binDirectory], mapBytecodeHash, outListProblem );
        }
    }

    bool ShaderCookDriver::collectRequestedBinaries( ShaderTargetFormat targetFormat, unordered_map<string, vector<vector<string>>>& outMapBinaryOrigin )
    {
        outMapBinaryOrigin.clear();
        vector<ShaderCookRequest> listRequest;
        if ( ShaderCookRequestCollector::collectAllRequests( engine::getSubsystem<AssetPaths>().getRootFolderPath(), listRequest ) == false )
            return false;
        for ( const ShaderCookRequest& request : listRequest )
        {
            const optional<ShaderCookDriverInternal::BinarySlot> slot = ShaderCookDriverInternal::locateBinary( request, targetFormat );
            if ( slot.has_value() == false )
                continue;
            // 바이너리 이름에 경로 · 진입점 · 스테이지 · 퍼뮤테이션이 다 들어 요청 하나가 바이너리 하나다 — 그래도 출처는 합쳐 둔다(같은 이름이면 같은 바이너리).
            vector<vector<string>>& listOrigin = outMapBinaryOrigin[FileUtil::normalizePath( FileUtil::joinPath( slot->_binDirectory, slot->_fileName ) ).str()];
            listOrigin.insert( listOrigin.end(), request._listOrigin.begin(), request._listOrigin.end() );
        }
        return true;
    }

    ShaderCookSummary ShaderCookDriver::cookRequests( const AbsolutePath&              cookedDir,
                                                      const vector<ShaderCookRequest>& listRequest,
                                                      ShaderTargetFormat               targetFormat )
    {
        using Internal = ShaderCookDriverInternal;
        const Stopwatch                  totalWatch;
        const vector<ShaderTargetFormat> listTargetFormat = Internal::makeTargetFormatList( targetFormat );
        Internal::CookState              state{ ShaderEnginePolicy::capture() }; // 쿠킹을 부른 스레드에서 뜬다 — 병렬 구간은 설정을 직접 읽지 않는다
        if ( cookedDir.empty() )
        {
            SW_LOG_ERROR_ID( "ShaderCooker.ShaderCookNeedsCooked", "Shader cook needs -cooked-dir=<folder> (the staging folder of cooked shaders, scenes and prefabs)" );
            state._summary._failedCount = 1; // 쿠킹하지 못했다. 실패로 센다(`App --cook-shaders` 가 실패로 끝난다)
            return state._summary;
        }
        state._resolver = ShaderSourceResolver::make( engine::getSubsystem<AssetPaths>() );
        if ( state._resolver.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ShaderCooker.ShaderCookCannotResolve", "Shader cook cannot resolve the shader include roots (see the error above)" );
            state._summary._failedCount = 1; // 쿠킹하지 못했다. 실패로 센다
            return state._summary;
        }
        state._cookedDir = cookedDir;

        // 같은 바이너리 이름에 다른 define 이 오면 한 파일을 두 변형이 덮어 틀린 셰이더가 조용히 실린다 — 쿠킹 실패로 세고 뒤 요청은 굽지 않는다.
        vector<uint8> listRejected( listRequest.size(), SW_FALSE );
        for ( const ShaderBinaryNameCollision& collision : ShaderCooker::findBinaryNameCollisions( listRequest ) )
        {
            ++state._summary._failedCount;
            listRejected[collision._secondIndex] = SW_TRUE;
            SW_LOG_ERROR_ID( "ShaderCooker.BinaryNameCollision", "Shader binary name %# of %# is claimed by two different define sets (request %# and %#) - the permutation hashes collide; rename a define",
                             collision._fileName, listRequest[collision._secondIndex]._shaderPath, static_cast<uint64>( collision._firstIndex ), static_cast<uint64>( collision._secondIndex ) );
        }
        vector<Internal::FormatTask> listTask;
        for ( size_t index = 0; index < listRequest.size(); ++index )
        {
            if ( listRejected[index] == SW_FALSE )
                Internal::planRequest( listRequest[index], listTargetFormat, state, listTask );
        }
        // 굽기 전에 할 일의 수를 알린다 — 빈 캐시에서는 한 시간 남짓 걸리는데 전체를 모르면 얼마나 남았는지 알 수 없었다(줄마다의 `CookedShader` 와 견준다).
        SW_LOG_INFO_ID( "ShaderCooker.PlannedTasks", "Planned %# shader cook tasks from %# requests across %# format(s)", static_cast<uint32>( listTask.size() ), static_cast<uint32>( listRequest.size() ),
                        static_cast<uint32>( listTargetFormat.size() ) );
        vector<Internal::FormatResult> listResult;
        const Stopwatch                batchWatch;
        Internal::cookTasks( listTask, state, listResult );
        state._summary._compileMs = static_cast<uint32>( batchWatch.getElapsedMilliseconds() );
        Internal::logVariantOverlap( listTask, listResult );
        Internal::mergeResults( listTask, listResult, state );
        Internal::writeManifests( state );
        state._summary._totalMs = static_cast<uint32>( totalWatch.getElapsedMilliseconds() );

        const ShaderCookSummary& summary = state._summary;
        if ( summary._contractViolationCount > 0 )
            SW_LOG_ERROR_ID( "ShaderCooker.BindingContractViolationsSee", "%# binding contract violations - see the [Binding contract] logs above and fix the shader declarations or binding_slots.hlsli.", summary._contractViolationCount );
        if ( summary._failedCount > 0 )
            SW_LOG_ERROR_ID( "ShaderCooker.ShaderCookingFailedItem", "Shader cooking failed for %# item(s) - they have no compile record, so the next cook retries them.", summary._failedCount );
        SW_LOG_INFO_ID( "ShaderCooker.ShaderCookingCompletedBinaries", "Shader cooking completed: %# binaries generated/updated, %# stale binaries removed (compile batch %# ms, of it reflection %# ms summed over workers, total %# ms).",
                        summary._cookedCount, summary._removedCount, summary._compileMs, summary._reflectMs, summary._totalMs );
        return summary;
    }
} // namespace sw
