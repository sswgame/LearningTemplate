#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#include "Core/Container/EnumArray.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_map.h"
#include "Core/Diagnostics/ScopedProgressReport.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Content/CookJob.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompileClaims.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompileSession.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompilerBackend.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Compile/ShaderReachableText.h"
#include "Engine/Graphics/Shader/Compile/ShaderSourceRecord.h"

#include <algorithm>

namespace sw
{
    // 안쪽 도우미(캐시 쓰기 · 컴파일 단계)도 로그를 남기므로 맨 앞에 둔다.
    namespace
    {
        struct ShaderCompilerInternal
        {
            /**
             * @brief 셰이더 컴파일의 DDC(`DerivedDataCache` 의 `Shader` 버킷)입니다. 처음 쓸 때 한 번 연다 — 층 설정은 프로세스 동안 같다.
             * @details 예전에는 실행 파일 폴더, 그다음은 자기 폴더 층 하나였다 — 정리 규칙 · 공유 층이 없었고 최신 판정(의존 기록 `.deps`)이 쿠킹 스탬프 · 메모리 지문과 따로였다.
             */
            static const expected<DerivedDataCache>& getDerivedDataCache()
            {
                static const expected<DerivedDataCache> s_cache = DerivedDataCache::open( DerivedDataBucket::Shader );
                return s_cache;
            }

            /** @brief 컴파일 한 건의 동작 — 작업 키의 쿠커 쪽 값과 기록을 찾는 동작 키입니다. */
            struct CompileAction
            {
                CookJobDesc _jobDesc;            ///< 요청 · 도구가 정하는 값(소스 내용은 작업의 입력이라 들지 않는다)
                uint64      _actionKey{ 0 };     ///< 기록을 찾는 키 — 주 소스의 내용까지 든다(가지를 오가도 판마다 기록이 남는다)
                uint64      _sourceHash{ 0 };    ///< 주 소스의 내용 해시
                uint64      _fxcHash{ 0 };       ///< DXBC 요청이면 실린 FXC 의 내용 해시(`getFxcLibraryHash`), 아니면 0
                bool        _bUseCache{ false }; ///< 거짓이면 DDC 를 쓰지 않는다(실린 FXC 의 판을 모른다)
            };

            /** @brief 전처리한 글의 DDC 키와 그 서술입니다(`makeSourceKey`). */
            struct SourceKey
            {
                uint64 _key{ 0 };
                string _description;
            };

            /** @brief 컴파일 한 건의 앞부분 — 요청을 풀어 DDC 에서 찾을 값까지 만든 것입니다. */
            struct CompileSetup
            {
                shared_ptr<const ShaderSourceResolver> _pResolver;
                ShaderBackendCall                      _call; ///< 엔진 define · 단계 매크로를 더한 요청(`makeFullDesc`)과 풀린 소스. include 규칙은 `_pResolver` 의 것이다
                const DerivedDataCache*                _pCache;
                uint64                                 _sourceHash; ///< 주 소스의 내용 해시 — 못 읽으면 0(컴파일도 같은 파일을 읽다 실패한다)
                CompileAction                          _action;
                bool                                   _bWriteRecord{ true }; ///< 요청마다의 기록을 쓴다 — 소스마다 한 파일에 쓰는 묶음(`ShaderRecordScope::Source`)은 쓰지 않는다
            };

            /** @brief `compileHLSL` 이 실제로 컴파일하는 요청입니다 — 엔진 define 과 단계 매크로를 더한다. */
            static ShaderCompileDesc makeFullDesc( const ShaderCompileDesc& requestDesc )
            {
                // 엔진 define 은 모든 컴파일에 더한다(appendEngineDefines) — 값이 동작 키의 define 에 들어 표를 바꾸면 기록이 모두 낡는다(글이 같은 변형은 글 키로 다시 맞는다).
                ShaderCompileDesc desc = requestDesc;
                ShaderCompiler::appendEngineDefines( desc._listDefine, *desc._enginePolicy );
                // 단계 매크로 — 네 백엔드 컴파일 경로(DXC → DXIL · SPIR-V → GL · WGSL, FXC → DXBC)가 모두 이 함수를 지나므로 여기 한 곳에서 넣는다.
                ShaderMacroDefine stageDefine;
                stageDefine._name  = getShaderStageInfo( desc._stage )._pStageMacro;
                stageDefine._value = "1";
                desc._listDefine.push_back( std::move( stageDefine ) );
                return desc;
            }

            /** @brief 컴파일 동작을 만듭니다. @p desc 는 `makeFullDesc` 를 지난 것이고 @p sourceHash 는 주 소스의 내용 해시입니다. */
            static CompileAction makeCompileAction( const ShaderCompileDesc& desc, uint64 sourceHash )
            {
                CompileAction action;
                action._bUseCache  = true;
                action._sourceHash = sourceHash;
                // define 은 정본(정렬)으로 적는다 — 받은 차례로 적으면 같은 조합이 차례만 달라 두 벌이 쌓인다. 헤더는 키에 넣지 않는다 — 어느 헤더를 열었는지는
                // 컴파일하고 나서야 알아(키는 컴파일 전에 필요하다) 작업의 입력으로 기록하고, 맞출 때 그 기록의 입력이 지금도 같은지 본다(`CookRecord::isFresh`).
                string text;
                text.append( "source=" ).append( desc._source.str() ).append( "\nentry=" ).append( desc._entryPoint );
                text.append( "\nstage=" ).append( to_string( static_cast<uint32>( desc._stage ) ) ).append( "\nprofile=" ).append( ShaderBackendCall::getTargetProfile( desc._stage, desc._targetFormat ) );
                for ( const string& define : ShaderCooker::makeCanonicalDefines( desc._listDefine ) )
                {
                    text.append( "\ndefine=" ).append( define );
                }
                // 디버그 코드젠 여부도 키다(같은 소스의 다른 바이트다).
                text.append( "\ncodegen=" ).append( desc._bDebugCodegen != SW_FALSE ? "dbg" : "opt" ).append( "\n" );
#if defined( SW_PLATFORM_WINDOWS )
                // FXC 는 OS 의 것이라 도구 지문(vcpkg 기록)에 없다 — DXBC 항목만 실린 d3dcompiler 의 내용을 키에 더한다. 못 읽으면 판을 모르므로 캐시하지 않는다.
                const ShaderTargetFormatInfo* const pTargetInfo = ShaderTargetFormatTable::find( desc._targetFormat );
                if ( pTargetInfo != nullptr && pTargetInfo->_tool == ShaderCompilerTool::Fxc )
                {
                    action._fxcHash   = ShaderBackendCall::getFxcLibraryHash();
                    action._bUseCache = action._fxcHash != 0;
                    text.append( "fxc=" ).append( to_string( action._fxcHash ) ).append( "\n" );
                }
#endif
                action._jobDesc._cookerName      = "Shader";
                action._jobDesc._codeFingerprint = ShaderCompiler::getToolchainFingerprint(); // 도구 지문 — DXC 나 옵션을 바꾸면 옛 바이트를 돌려주면 안 된다
                action._jobDesc._target          = string( ShaderCooker::getSubfolderForFormat( desc._targetFormat ) );
                action._jobDesc._settingsHash    = HashUtil::computeStringHash64( string_view{ text }, false );

                text.append( "target=" ).append( action._jobDesc._target ).append( "\ntoolchain=" ).append( to_string( action._jobDesc._codeFingerprint ) );
                text.append( "\nsourceHash=" ).append( to_string( sourceHash ) );
                action._actionKey = HashUtil::computeStringHash64( string_view{ text }, false );
                return action;
            }

            /** @brief 요청 하나의 소스 해석기입니다. include 루트를 풀지 못하면 nullptr 입니다(오류는 남았다). */
            static shared_ptr<const ShaderSourceResolver> makeResolver()
            {
                optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( engine::getSubsystem<AssetPaths>() );
                if ( resolver.has_value() == false )
                    return nullptr;
                return sw::make_shared<const ShaderSourceResolver>( std::move( *resolver ) );
            }

            /** @brief 요청 @p requestDesc 를 풉니다. 풀지 못하면 로그를 남기고 까닭이 오류입니다. @p pSession 은 묶음의 표다(없으면 디스크를 본다). */
            [[nodiscard]] static expected<CompileSetup> setUp( const ShaderCompileDesc& requestDesc, ShaderCompileSession* pSession )
            {
                // 엔진 설정 값은 요청이 실어 온 사본만 쓴다 — 컴파일 워커에서 불려 설정을 직접 읽으면 제자리 덮기와 겹친다.
                if ( requestDesc._enginePolicy.has_value() == false )
                {
                    const Error error{ ErrorKind::InvalidArgument, "The shader compile request carries no engine policy (ShaderCompileDesc::_enginePolicy) - the requester captures it",
                                       requestDesc._source.str() };
                    SW_LOG_ERROR_ID( "ShaderCompiler.EnginePolicyMissing", "%# ('%#')", error.message(), error.detail() );
                    return unexpected( error );
                }
                // 소스는 리소스 경로 하나로 푼다(`ShaderSourceResolver`) - 절대 경로 · 작업 폴더로 추측하지 않는다.
                const shared_ptr<const ShaderSourceResolver> pResolver = pSession != nullptr ? pSession->getResolver() : makeResolver();
                if ( pResolver == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "Shader include domains cannot be found (see the _listResourcePriority error above)" } );
                ShaderCompileDesc                  desc  = makeFullDesc( requestDesc );
                const expected<ShaderCompileInput> input = pSession != nullptr ? pSession->resolve( desc._source ) : pResolver->resolve( desc._source );
                if ( input.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "ShaderCompiler.SourceUnresolved", "%#: %#", input.error().message(), input.error().detail() );
                    return unexpected( input.error() );
                }
                const expected<DerivedDataCache>& cache = getDerivedDataCache();
                if ( cache.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "ShaderCompiler.DerivedDataUnavailable", "The shader DDC could not be opened (Config/Dev/DerivedData.json): %# (%#)", cache.error().message(), cache.error().detail() );
                    return unexpected( Error{ ErrorKind::ExternalFailure, "The shader DDC could not be opened (Config/Dev/DerivedData.json)", cache.error().detail() } );
                }
                const uint64  sourceHash = ContentHash::computeFile( input->_source );
                CompileAction action     = makeCompileAction( desc, sourceHash );
                string        profile    = ShaderBackendCall::getTargetProfile( desc._stage, desc._targetFormat );
                return CompileSetup{
                    pResolver, ShaderBackendCall{ std::move( desc ), *input, &pResolver->getIncludeResolver(), std::move( profile ) },
                    &*cache, sourceHash, std::move( action )
                };
            }

            /**
             * @brief 전처리한 글 @p text 를 컴파일한 바이트코드의 DDC 키입니다. 진입점이 닿는 글(`ShaderReachableText`)이 같으면 같은 셰이더다 — 어느 변형(define 조합)이 그 글을 냈는지는 들지 않는다.
             * @details 변형마다 컴파일하지 않고 닿는 글마다 컴파일한다 — 언리얼이 셰이더 작업을 걷어 낸(`ShaderMinifier`) 전처리 소스의 해시로 겹쳐 합치는 것(`r.ShaderCompiler.PreprocessedCache`)과 같다.
             *          닿는 글이 같은 두 글의 바이트코드는 명령이 같다. 드물게 쓰지 않는 선언이 DXC 안의 번호 차례(DXIL 의 내장 함수 선언 순서, SPIR-V 의 id)만 바꾼다.
             */
            static SourceKey makeSourceKey( const CompileSetup& setup, const vector<uint8>& text )
            {
                const string reachable = ShaderReachableText::make( string_view( reinterpret_cast<const utf8*>( text.data() ), text.size() ), setup._call._desc._entryPoint );
                SourceKey    sourceKey;
                sourceKey._description.append( "Shader preprocessed text\ntarget=" ).append( setup._action._jobDesc._target ).append( "\nprofile=" ).append( setup._call._profile );
                sourceKey._description.append( "\nentry=" ).append( setup._call._desc._entryPoint ).append( "\ntoolchain=" ).append( to_string( setup._action._jobDesc._codeFingerprint ) );
                sourceKey._description.append( "\nfxc=" ).append( to_string( setup._action._fxcHash ) ).append( "\nlength=" ).append( to_string( reachable.size() ) );
                sourceKey._description.append( "\ntext=" ).append( to_string( HashUtil::computeStringHash64( reachable, false ) ) );
                sourceKey._key = HashUtil::computeStringHash64( sourceKey._description, false );
                return sourceKey;
            }

            /** @brief 바이트코드의 DDC 항목 서술입니다. 바이트코드는 내용 주소다 — 변형 · 글이 달라도 같은 바이트는 한 벌이다(언리얼 Zen 캐시가 레코드와 내용 주소 본문을 나누는 것과 같다). */
            static string makeBytecodeDescription( uint64 bytecodeHash )
            {
                string description;
                description.append( "Shader bytecode\ncontent=" ).append( to_string( bytecodeHash ) ).append( "\n" );
                return description;
            }

            /** @brief 내용 해시가 @p bytecodeHash 인 바이트코드를 DDC 에서 읽습니다. */
            [[nodiscard]] static bool findBytecode( const DerivedDataCache& cache, uint64 bytecodeHash, vector<uint8>& outBytecode )
            {
                const string description = makeBytecodeDescription( bytecodeHash );
                const uint64 key         = HashUtil::computeStringHash64( description, false );
                const bool   bFound      = cache.find( key, description, outBytecode ) && outBytecode.empty() == false && ContentHash::computeBytes( outBytecode ) == bytecodeHash;
                if ( bFound == false )
                    outBytecode.clear();
                return bFound;
            }

            /** @brief 글 키의 항목(바이트코드의 내용 해시)을 찾아 바이트코드까지 읽습니다. */
            [[nodiscard]] static bool findBytecodeOfText( const DerivedDataCache& cache, const SourceKey& sourceKey, vector<uint8>& outBytecode )
            {
                vector<uint8> bytes;
                if ( cache.find( sourceKey._key, sourceKey._description, bytes ) == false )
                    return false;
                ByteReader   reader{ bytes.data(), bytes.size() };
                const uint64 bytecodeHash = reader.readUint64();
                return reader.hasOverflowed() == false && reader.isFullyRead() && findBytecode( cache, bytecodeHash, outBytecode );
            }

            /** @brief 글 키의 항목과 바이트코드를 DDC 에 씁니다. 글이 없는 동작(디버그 코드젠)은 바이트코드만 쓴다. */
            [[nodiscard]] static bool storeBytecode( const DerivedDataCache& cache, const SourceKey& sourceKey, const vector<uint8>& bytecode, uint64 bytecodeHash )
            {
                const string description = makeBytecodeDescription( bytecodeHash );
                if ( cache.store( HashUtil::computeStringHash64( description, false ), description, bytecode ) == false )
                    return false;
                vector<uint8> bytes;
                ByteWriter    writer{ bytes };
                writer.writeUint64( bytecodeHash );
                return sourceKey._key == 0 || cache.store( sourceKey._key, sourceKey._description, bytes );
            }

            /** @brief DDC 에서 동작의 기록을 찾아 새것이면 바이트코드까지 읽습니다. 둘 다 맞을 때만 true 입니다. */
            [[nodiscard]] static bool findCached( const CompileSetup& setup, ShaderCompileSession* pSession, ShaderCompileResult& outResult )
            {
                // 기록의 입력 중 하나라도 달라졌으면 맞은 것이 아니다 — 그 셰이더만 다시 컴파일한다(다른 헤더를 쓰는 셰이더는 그대로다).
                if ( setup._action._bUseCache == false || setup._pCache->readRecord( setup._action._actionKey, outResult._record ) == false )
                    return false;
                const bool bFresh = pSession != nullptr ? pSession->isRecordFresh( outResult._record, setup._action._jobDesc ) : outResult._record.isFresh( setup._action._jobDesc );
                return bFresh && findBytecode( *setup._pCache, outResult._record._outputHash, outResult._bytecode );
            }

            /** @brief 결과에 작업의 기록을 달고, 동작을 캐시할 수 있으면 기록을 DDC 에 씁니다(바이트코드 항목을 쓴 뒤다 — 기록이 있으면 항목이 있다). 못 쓰면 경고하고 넘어갑니다(다음에 다시 컴파일한다). */
            static void writeRecord( const CompileSetup& setup, const CookJob& job, uint64 bytecodeHash, ShaderCompileResult& inoutResult )
            {
                inoutResult._record = CookRecord::make( job, bytecodeHash );
                if ( setup._action._bUseCache && setup._bWriteRecord && setup._pCache->writeRecord( setup._action._actionKey, inoutResult._record ) == false )
                    SW_LOG_WARNING_ID( "ShaderCompiler.CouldNotWriteShader", "Could not write the shader DDC entry or record of key %#", setup._action._actionKey );
            }

            /** @brief 컴파일러에 넘겨야 하는 요청 하나입니다 — DDC 에 동작의 기록도 글의 바이트코드도 없다. */
            struct PendingCompile
            {
                CompileSetup      _setup;
                vector<CookInput> _listInput; ///< 주 소스와 전처리가 연 것(없던 후보 포함)
                SourceKey         _sourceKey; ///< 글 키. 0 이면 글로 겹치지 않는 요청(디버그 코드젠)이다
                vector<uint8>     _text;      ///< 전처리한 글

                /** @brief 컴파일하지 않을 요청(같은 글을 다른 요청이 컴파일한다)의 컴파일 재료를 놓습니다. 기록에 쓸 것만 남는다. */
                void releaseCompileInput()
                {
                    _setup._call = ShaderBackendCall{};
                    _text        = {};
                }
            };

            /** @brief 오류 글로 실패 결과를 채웁니다. */
            static void setError( const Error& error, ShaderCompileResult& outResult )
            {
                StringBuilder<constant::buffer::kMax256> errorBuilder;
                errorBuilder.appendFormat( "%#: %#", error.message(), error.detail() );
                outResult._errorMessage = string( errorBuilder.view() );
            }

            /** @brief 컴파일러가 바이트코드를 내지 못한 호출 @p runResult 의 오류를 남깁니다. */
            static void reportFailure( const CompileSetup& setup, ShaderBackendRunResult runResult, ShaderCompileResult& inoutResult )
            {
                if ( runResult == ShaderBackendRunResult::Rejected )
                    return; // 컴파일러의 오류 글을 이미 남겼다
                // 컴파일러가 출력을 못 냈으면 실패다 - 예전에는 DX12 타깃을 FXC 로 다시 컴파일했지만 FXC 는 SM6 을 못 해 늘 실패했고, DXC 의 진짜 까닭을 FXC 의 글로 덮었다.
                if ( inoutResult._errorMessage.empty() )
                    inoutResult._errorMessage = "The shader compiler produced no output (see the error above)";
                SW_LOG_ERROR_ID( "ShaderCompiler.Path", "%# (Path: %#)", inoutResult._errorMessage.c_str(), setup._call._desc._source.c_str() );
            }

            /** @brief 요청을 풀고 준비합니다(`prepare`). */
            [[nodiscard]] static optional<PendingCompile> openAndPrepare( const ShaderCompileDesc& requestDesc, ShaderCompileResult& outResult )
            {
                outResult                     = ShaderCompileResult{};
                expected<CompileSetup> opened = setUp( requestDesc, nullptr );
                if ( opened.has_value() )
                    return prepare( std::move( *opened ), nullptr, outResult );
                setError( opened.error(), outResult );
                return nullopt;
            }

            /** @brief DDC 적중이면 @p outResult 를 채우고, 컴파일해야 하면 그 재료를 돌려줍니다. 빈 값인데 성공이 아니면 실패다(오류 글을 담았다). */
            [[nodiscard]] static optional<PendingCompile> prepare( CompileSetup opened, ShaderCompileSession* pSession, ShaderCompileResult& outResult )
            {
                outResult = ShaderCompileResult{};
                PendingCompile      pending{ std::move( opened ), {}, {}, {} };
                const CompileSetup& setup = pending._setup;
                if ( findCached( setup, pSession, outResult ) )
                {
                    outResult._bSuccess = true;
                    return nullopt;
                }
                outResult._record = CookRecord{};

                // 이 컴파일의 작업 — 주 소스와 include 규칙이 읽은 것(없던 후보 포함)이 입력으로 적힌다. 컴파일러의 include 처리기는 이 스레드에서 불린다.
                CookJob job{ setup._action._jobDesc };
                if ( job.recordDiskInput( setup._call._input._source, setup._sourceHash ) == false )
                {
                    outResult._errorMessage = "The shader source could not be recorded as a cook input (see the error above).";
                    return nullopt;
                }

                // 같은 이름의 헤더가 엔진 헤더를 가리면 어느 쪽이 쓰였는지 알 수 없다 — 실제로 컴파일하기 전에 오류로 멈춘다. DDC 적중 뒤에 본다: 폴더를 재귀로 훑는 검사라
                // 요청마다 하면 적중도 느려지고, 맞은 항목의 기록은 없던 후보 자리까지 적어 가리는 헤더가 새로 생기면 이미 낡음이다(`CookRecord::isFresh`).
                // 언리얼도 셰이더 맵 DDC 조회가 컴파일 준비(include 검사)보다 먼저다.
                vector<string>  listShadowed;
                const DiskPath& shadersFolder = setup._call._input._shadersFolder;
                const bool      bClean        = pSession != nullptr ? pSession->findShadowedHeaders( shadersFolder, listShadowed ) : setup._call._pIncludeResolver->findShadowedHeaders( shadersFolder, listShadowed );
                if ( bClean == false )
                {
                    for ( const string& problem : listShadowed )
                    {
                        outResult._errorMessage += problem;
                        outResult._errorMessage += '\n';
                    }
                    if ( outResult._errorMessage.empty() )
                        outResult._errorMessage = "Shader headers could not be checked for shadowing (see the error above).";
                    SW_LOG_ERROR_ID( "ShaderCompiler.HeaderShadowed", "%#", outResult._errorMessage );
                    return nullopt;
                }

                // 전처리한 글로 한 번 더 찾는다 — 이 글을 낸 다른 변형이 이미 컴파일했으면 그 바이트코드다(`makeSourceKey`). include 처리기가 이 스레드의 작업에 입력을 적으므로
                // 적중이어도 이 변형의 기록이 온전하다. 디버그 코드젠은 원본 소스를 바로 컴파일한다.
                if ( setup._call._desc._bDebugCodegen == SW_FALSE )
                {
                    const ShaderBackendRunResult runResult = setup._call.run( ShaderBackendStep::Preprocess, pending._text, outResult );
                    if ( runResult != ShaderBackendRunResult::Succeeded )
                    {
                        reportFailure( setup, runResult, outResult );
                        return nullopt;
                    }
                    pending._sourceKey = makeSourceKey( setup, pending._text );
                    if ( setup._action._bUseCache && findBytecodeOfText( *setup._pCache, pending._sourceKey, outResult._bytecode ) )
                    {
                        outResult._bSuccess = true;
                        writeRecord( setup, job, ContentHash::computeBytes( outResult._bytecode ), outResult );
                        return nullopt;
                    }
                }
                pending._listInput = job.getInputs();
                return pending;
            }

            /** @brief 다른 요청이 컴파일한 바이트코드를 쓰는 요청의 기록을 씁니다. */
            static void writeSharedRecord( const PendingCompile& pending, uint64 bytecodeHash, ShaderCompileResult& inoutResult )
            {
                CookJob job{ pending._setup._action._jobDesc };
                for ( const CookInput& input : pending._listInput )
                {
                    job.recordInput( input );
                }
                writeRecord( pending._setup, job, bytecodeHash, inoutResult );
            }

            /** @brief 요청을 컴파일해 바이트코드와 기록을 DDC 에 씁니다. */
            static void compilePending( PendingCompile& pending, ShaderCompileResult& outResult )
            {
                outResult                      = ShaderCompileResult{};
                const CompileSetup&      setup = pending._setup;
                const ShaderCompileDesc& desc  = setup._call._desc;
                CookJob                  job{ setup._action._jobDesc }; // 디버그 코드젠은 여기서 include 를 연다
                for ( const CookInput& input : pending._listInput )
                {
                    job.recordInput( input );
                }
                // 캐시에 없어 실제로 컴파일하는 것만 알린다 — 빈 캐시의 첫 실행은 수 분 동안 이 줄만 나온다. 이 줄이 "멈춘 것이 아니라 일하는 중" 의 증거다
                // (백엔드마다 실행의 정지 감시가 자식 로그가 자라는지로 본다 — AutomationPerBackend). 캐시가 찬 실행은 한 줄도 내지 않는다(언리얼 셰이더 컴파일 진행 줄과 같은 자리).
                [[maybe_unused]] const int64 compileStartNanos = MonotonicClock::nowNanoseconds();
                // 위 줄은 컴파일이 끝나야 찍힌다 — 한 번이 정지 시한보다 긴 컴파일(fxc 의 큰 셰이더)도 일하는 중임을 간격마다 알린다.
                StringBuilder<constant::buffer::kMax256> progressBuilder;
                progressBuilder.appendFormat( "%# %# (%#) compile", desc._source.c_str(), desc._entryPoint, setup._call._profile );
                const ScopedProgressReport   progress{ string( progressBuilder.view() ) };
                const ShaderBackendStep      step      = desc._bDebugCodegen != SW_FALSE ? ShaderBackendStep::CompileSource : ShaderBackendStep::CompilePreprocessed;
                const ShaderBackendRunResult runResult = setup._call.run( step, pending._text, outResult );
                if ( runResult != ShaderBackendRunResult::Succeeded )
                {
                    reportFailure( setup, runResult, outResult );
                    return;
                }
                const uint64 bytecodeHash = ContentHash::computeBytes( outResult._bytecode );
                const bool   bStored      = setup._action._bUseCache && outResult._bytecode.empty() == false && storeBytecode( *setup._pCache, pending._sourceKey, outResult._bytecode, bytecodeHash );
                if ( setup._action._bUseCache && bStored == false )
                    SW_LOG_WARNING_ID( "ShaderCompiler.CouldNotWriteShader", "Could not write the shader DDC entry or record of key %#", setup._action._actionKey );
                if ( bStored )
                    writeRecord( setup, job, bytecodeHash, outResult );
                else
                    outResult._record = CookRecord::make( job, bytecodeHash );
                SW_LOG_INFO_ID( "ShaderCompiler.CompiledOnCacheMiss", "Compiled %# %# (%#) in %# ms", desc._source.c_str(), desc._entryPoint.c_str(), setup._call._profile.c_str(),
                                ( MonotonicClock::nowNanoseconds() - compileStartNanos ) / constant::time::kNanosecondsPerMillisecond );
            }

            /** @brief 같은 소스 · 형식의 요청이 이어지게 세운 요청 번호입니다. 같은 무리 안은 목록의 차례다. */
            static vector<uint32> makeGroupedOrder( const vector<ShaderCompileDesc>& listDesc )
            {
                vector<uint32> listOrder( listDesc.size() );
                for ( uint32 index = 0; index < static_cast<uint32>( listOrder.size() ); ++index )
                {
                    listOrder[index] = index;
                }
                std::stable_sort( listOrder.begin(), listOrder.end(), [&listDesc]( uint32 left, uint32 right )
                {
                    if ( listDesc[left]._targetFormat != listDesc[right]._targetFormat )
                        return listDesc[left]._targetFormat < listDesc[right]._targetFormat;
                    return listDesc[left]._source.str() < listDesc[right]._source.str();
                } );
                return listOrder;
            }

            /** @brief 세운 순서 @p listOrder 의 @p begin 에서 시작하는 무리(같은 소스 · 형식)의 끝입니다. */
            static size_t findGroupEnd( const vector<ShaderCompileDesc>& listDesc, const vector<uint32>& listOrder, size_t begin )
            {
                const ShaderCompileDesc& first = listDesc[listOrder[begin]];
                size_t                   end   = begin + 1;
                while ( end < listOrder.size() && listDesc[listOrder[end]]._targetFormat == first._targetFormat && listDesc[listOrder[end]]._source.str() == first._source.str() )
                    ++end;
                return end;
            }

            /** @brief 소스 하나의 기록(`ShaderSourceRecord`)과 입력 목록마다 지금도 맞는지입니다. */
            struct LoadedRecord
            {
                string             _description; ///< DDC 항목 서술
                ShaderSourceRecord _record;
                vector<uint8>      _listFresh; ///< 입력 목록마다 1 이면 지금도 맞다

                uint64 getKey() const { return HashUtil::computeStringHash64( _description, false ); }
                /** @brief 동작의 결과입니다. 없거나 입력이 낡았으면 nullptr 입니다. */
                const ShaderSourceRecord::Entry* findFresh( uint64 actionKey ) const
                {
                    const ShaderSourceRecord::Entry* const pEntry = _record.find( actionKey );
                    return pEntry != nullptr && _listFresh[pEntry->_inputListIndex] != 0 ? pEntry : nullptr;
                }
            };

            /** @brief 요청의 소스 기록을 DDC 에서 찾습니다(같은 소스 · 형식의 요청은 모두 같은 기록이다). 없으면 빈 기록입니다(서술은 채운다). */
            static LoadedRecord findSourceRecord( const DerivedDataCache& cache, const ShaderCompileDesc& desc, const CompileAction& action, ShaderCompileSession& session )
            {
                LoadedRecord source;
                source._description = ShaderSourceRecord::makeKeyDescription( desc._source.str(), action._jobDesc._target, action._jobDesc._codeFingerprint, action._sourceHash );
                vector<uint8> bytes;
                if ( cache.find( source.getKey(), source._description, bytes ) == false || source._record.read( bytes ) == false )
                    source._record = ShaderSourceRecord{};
                for ( const vector<CookInput>& listInput : source._record.getInputLists() )
                {
                    source._listFresh.push_back( session.areInputsFresh( listInput ) ? 1 : 0 );
                }
                return source;
            }

            /** @brief 동작의 지금 맞는 바이트코드 해시입니다 — 소스 기록, 없으면 요청마다의 기록(실행 중 컴파일 · 데우기가 쓴 것). */
            static optional<uint64> findCurrentHash( const DerivedDataCache& cache, const LoadedRecord& source, const CompileAction& action, ShaderCompileSession& session )
            {
                const ShaderSourceRecord::Entry* const pEntry = source.findFresh( action._actionKey );
                if ( pEntry != nullptr )
                    return pEntry->_bytecodeHash;
                CookRecord record;
                if ( cache.readRecord( action._actionKey, record ) && session.isRecordFresh( record, action._jobDesc ) )
                    return record._outputHash;
                return nullopt;
            }

            /** @brief 요청마다 동작을 만드는 일의 입력입니다(`findCurrentBytecodeHashes`) — 워커는 제 칸에만 쓴다. */
            struct ActionJob
            {
                const vector<ShaderCompileDesc>* _pListDesc{ nullptr };
                ShaderCompileSession*            _pSession{ nullptr };

                void operator()( uint32 index, optional<CompileAction>& outAction ) const
                {
                    const ShaderCompileDesc& desc = ( *_pListDesc )[index];
                    if ( desc._enginePolicy.has_value() == false )
                        return;
                    const expected<ShaderCompileInput> input = _pSession->resolve( desc._source );
                    if ( input.has_value() )
                        outAction = makeCompileAction( makeFullDesc( desc ), ContentHash::computeFile( input->_source ) );
                }
            };

            /**
             * @brief 묶음 컴파일 한 토막(소스 무리 여럿, `kChunkSize` 요청 남짓)의 상태입니다. 단계마다 병렬로 돌고 단계 사이에서 모인다.
             * @details 토막으로 나누는 것은 컴파일을 기다리는 글(변형마다 ~160 KB)의 상한을 두려는 것이다. 무리는 나누지 않는다 — 소스의 기록 하나가 무리 하나다.
             */
            class BatchChunk
            {
            public:
                static constexpr size_t kChunkSize = 4096;

                /** @param listOrder 세운 요청 번호 가운데 이 토막의 [@p begin, @p end) */
                BatchChunk( const vector<ShaderCompileDesc>& listDesc, const vector<uint32>& listOrder, size_t begin, size_t end, ShaderRecordScope recordScope, ShaderCompileSession& session,
                            const ShaderCompiledDelegate& onCompiled )
                    : _listDesc{ listDesc }
                    , _listRequest( listOrder.begin() + static_cast<ptrdiff_t>( begin ), listOrder.begin() + static_cast<ptrdiff_t>( end ) )
                    , _session{ session }
                    , _onCompiled{ onCompiled }
                    , _listSetup( end - begin )
                    , _listActionKey( end - begin, 0 )
                    , _listPending( end - begin )
                    , _listWritten( end - begin )
                    , _listGroup{}
                    , _listGroupIndex( end - begin, 0 )
                    , _listWinner{}
                    , _listFollower{}
                    , _listResult{}
                    , _mapWinner{}
                    , _mutex{}
                    , _recordScope{ recordScope }
                {
                }

                void run()
                {
                    bindSlots();
                    runStage( _listRequest.size(), SW_DELEGATE_METHOD( ParallelBlockDelegate, &BatchChunk::openRange, this ) );
                    if ( _recordScope == ShaderRecordScope::Source )
                        findSourceRecords();
                    bindSlots();
                    runStage( _listRequest.size(), SW_DELEGATE_METHOD( ParallelBlockDelegate, &BatchChunk::prepareRange, this ) );
                    splitPending();
                    bindSlots();
                    runStage( _listWinner.size(), SW_DELEGATE_METHOD( ParallelBlockDelegate, &BatchChunk::compileRange, this ) );
                    runStage( _listFollower.size(), SW_DELEGATE_METHOD( ParallelBlockDelegate, &BatchChunk::finishRange, this ) );
                    if ( _recordScope == ShaderRecordScope::Source )
                        writeSourceRecords();
                }

            private:
                /** @brief 병렬 단계의 칸 창을 지금 칸으로 다시 잡습니다(한 스레드 — 단계 사이). */
                void bindSlots()
                {
                    _slotSetup      = span<optional<CompileSetup>>( _listSetup.data(), _listSetup.size() );
                    _slotActionKey  = span<uint64>( _listActionKey.data(), _listActionKey.size() );
                    _slotPending    = span<optional<PendingCompile>>( _listPending.data(), _listPending.size() );
                    _slotWritten    = span<WrittenResult>( _listWritten.data(), _listWritten.size() );
                    _slotResult     = span<ShaderCompileResult>( _listResult.data(), _listResult.size() );
                    _slotRequest    = span<const uint32>( _listRequest.data(), _listRequest.size() );
                    _slotGroupIndex = span<const uint32>( _listGroupIndex.data(), _listGroupIndex.size() );
                    _slotGroup      = span<const Group>( _listGroup.data(), _listGroup.size() );
                    _slotWinner     = span<const uint32>( _listWinner.data(), _listWinner.size() );
                    _slotFollower   = span<const Follower>( _listFollower.data(), _listFollower.size() );
                }

                /** @brief 같은 글을 다른 요청이 컴파일하는 요청 하나입니다. */
                struct Follower
                {
                    uint32 _index;       ///< 토막 안 번호
                    uint32 _resultIndex; ///< 글을 컴파일한 요청의 `_listResult` 자리
                };
                /** @brief 소스 기록에 적을 요청 하나의 결과입니다. 해시가 0 이면 없다(실패 · 캐시하지 않는 요청). */
                struct WrittenResult
                {
                    vector<CookInput> _listInput;
                    uint64            _actionKey{ 0 };
                    uint64            _bytecodeHash{ 0 };
                    bool              _bFromRecord{ false }; ///< 소스 기록에서 왔다
                };
                /** @brief 소스 무리 하나입니다(토막 안 [_begin, _end)). */
                struct Group
                {
                    size_t       _begin{ 0 };
                    size_t       _end{ 0 };
                    LoadedRecord _source;
                };

                static void runStage( size_t count, const ParallelBlockDelegate& body ) { engine::runParallel( static_cast<uint32>( count ), 1, body ); }

                /** @brief 요청을 풉니다. 풀지 못한 요청은 여기서 끝난다. */
                void openRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        expected<CompileSetup> opened = setUp( _listDesc[_slotRequest[index]], &_session );
                        if ( opened.has_value() )
                        {
                            opened->_bWriteRecord = _recordScope == ShaderRecordScope::Request;
                            _slotActionKey[index] = opened->_action._actionKey;
                            _slotSetup[index]     = std::move( *opened );
                            continue;
                        }
                        ShaderCompileResult result;
                        setError( opened.error(), result );
                        _onCompiled( _slotRequest[index], result );
                    }
                }

                void findSourceRecords()
                {
                    for ( size_t begin = 0; begin < _listRequest.size(); begin = findGroupEnd( _listDesc, _listRequest, begin ) )
                    {
                        Group group;
                        group._begin = begin;
                        group._end   = findGroupEnd( _listDesc, _listRequest, begin );
                        for ( size_t index = group._begin; index < group._end; ++index )
                        {
                            _listGroupIndex[index]              = static_cast<uint32>( _listGroup.size() );
                            const optional<CompileSetup>& setup = _slotSetup[index];
                            if ( group._source._description.empty() && setup.has_value() && setup->_action._bUseCache )
                                group._source = findSourceRecord( *setup->_pCache, setup->_call._desc, setup->_action, _session );
                        }
                        _listGroup.push_back( std::move( group ) );
                    }
                }

                /** @brief 요청 @p index 의 결과를 소스 기록에서 찾습니다. */
                [[nodiscard]] bool findInSourceRecord( uint32 index, ShaderCompileResult& outResult )
                {
                    if ( _recordScope != ShaderRecordScope::Source || _slotSetup[index]->_action._bUseCache == false )
                        return false;
                    const LoadedRecord&                    source = _slotGroup[_slotGroupIndex[index]]._source;
                    const ShaderSourceRecord::Entry* const pEntry = source.findFresh( _slotSetup[index]->_action._actionKey );
                    if ( pEntry == nullptr || findBytecode( *_slotSetup[index]->_pCache, pEntry->_bytecodeHash, outResult._bytecode ) == false )
                        return false;
                    outResult._record._listInput  = source._record.getInputLists()[pEntry->_inputListIndex];
                    outResult._record._outputHash = pEntry->_bytecodeHash;
                    outResult._record._key        = CookJob::computeKey( _slotSetup[index]->_action._jobDesc, outResult._record._listInput );
                    outResult._bSuccess           = true;
                    _slotWritten[index]           = WrittenResult{ outResult._record._listInput, _slotSetup[index]->_action._actionKey, pEntry->_bytecodeHash, true };
                    return true;
                }

                /** @brief 결과를 소스 기록에 적을 칸에 두고 부른 쪽에 넘깁니다. */
                void deliver( uint32 index, const ShaderCompileResult& result )
                {
                    const bool bRecorded = _recordScope == ShaderRecordScope::Source && result._bSuccess && result._record._listInput.empty() == false;
                    if ( bRecorded )
                        _slotWritten[index] = WrittenResult{ result._record._listInput, _slotActionKey[index], result._record._outputHash, false };
                    _onCompiled( _slotRequest[index], result );
                }

                /** @brief 요청마다 DDC 를 보고, 없으면 전처리해 글 키를 구합니다. 글로 겹치지 않는 요청은 여기서 컴파일합니다. */
                void prepareRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        if ( _slotSetup[index].has_value() == false )
                            continue;
                        ShaderCompileResult result;
                        if ( findInSourceRecord( index, result ) )
                        {
                            _onCompiled( _slotRequest[index], result );
                            continue;
                        }
                        optional<PendingCompile> pending = prepare( std::move( *_slotSetup[index] ), &_session, result );
                        _slotSetup[index].reset();
                        const bool bShared = pending.has_value() && pending->_sourceKey._key != 0 && pending->_setup._action._bUseCache;
                        if ( pending.has_value() && bShared == false )
                            compilePending( *pending, result );
                        if ( bShared == false )
                        {
                            deliver( index, result );
                            continue;
                        }
                        _slotPending[index] = std::move( pending );
                        claimText( index );
                    }
                }

                /** @brief 같은 글 가운데 번호가 가장 작은 요청이 컴파일합니다 — 나머지는 글을 놓습니다. */
                void claimText( uint32 index )
                {
                    ScopedLock<mutex> lock{ _mutex };
                    const auto [iter, bFirst] = _mapWinner.emplace( _slotPending[index]->_sourceKey._key, index );
                    if ( bFirst )
                        return;
                    const uint32 loser = std::max( iter->second, index );
                    iter->second       = std::min( iter->second, index );
                    _slotPending[loser]->releaseCompileInput(); // 앞서 넣은 쪽도 자기 준비를 마친 뒤다
                }

                /** @brief 글마다 컴파일할 요청과 그 결과를 함께 받을 요청을 가릅니다. */
                void splitPending()
                {
                    unordered_map<uint64, uint32> mapResultIndex; // 글 키 → `_listResult` 자리. 컴파일할 요청이 같은 글의 나머지보다 앞선 번호다
                    for ( uint32 index = 0; index < static_cast<uint32>( _listPending.size() ); ++index )
                    {
                        if ( _slotPending[index].has_value() == false )
                            continue;
                        const auto [iter, bWinner] = mapResultIndex.emplace( _slotPending[index]->_sourceKey._key, static_cast<uint32>( _listWinner.size() ) );
                        if ( bWinner )
                            _listWinner.push_back( index );
                        else
                            _listFollower.push_back( Follower{ index, iter->second } );
                    }
                    _listResult.resize( _listWinner.size() );
                }

                void compileRange( uint32 start, uint32 end )
                {
                    for ( uint32 winner = start; winner < end; ++winner )
                    {
                        PendingCompile& pending = *_slotPending[_slotWinner[winner]];
                        compilePending( pending, _slotResult[winner] );
                        pending.releaseCompileInput();
                        deliver( _slotWinner[winner], _slotResult[winner] );
                    }
                }

                /** @brief 글을 컴파일한 요청의 결과에 자기 기록을 달아 넘깁니다. */
                void finishRange( uint32 start, uint32 end )
                {
                    for ( uint32 follower = start; follower < end; ++follower )
                    {
                        const PendingCompile& pending = *_slotPending[_slotFollower[follower]._index];
                        ShaderCompileResult   result  = _slotResult[_slotFollower[follower]._resultIndex];
                        if ( result._bSuccess )
                            writeSharedRecord( pending, result._record._outputHash, result );
                        deliver( _slotFollower[follower]._index, result );
                    }
                }

                /** @brief 소스 기록에서 오지 않은 결과가 하나라도 있는 무리의 기록을 씁니다. 실패한 요청은 빠진다(다음에 다시 전처리한다). */
                void writeSourceRecords()
                {
                    const expected<DerivedDataCache>& cache = getDerivedDataCache();
                    for ( const Group& group : _listGroup )
                    {
                        ShaderSourceRecord record;
                        bool               bChanged = false;
                        record.addFresh( group._source._record, group._source._listFresh );
                        for ( size_t index = group._begin; index < group._end; ++index )
                        {
                            const WrittenResult& written = _slotWritten[index];
                            if ( written._bytecodeHash == 0 )
                                continue;
                            record.add( written._actionKey, written._bytecodeHash, written._listInput );
                            bChanged = bChanged || written._bFromRecord == false;
                        }
                        vector<uint8> bytes;
                        record.write( bytes );
                        if ( bChanged && group._source._description.empty() == false && cache.has_value() && cache->store( group._source.getKey(), group._source._description, bytes ) == false )
                            SW_LOG_WARNING_ID( "ShaderCompiler.CouldNotWriteShader", "Could not write the shader DDC entry or record of key %#", group._source.getKey() );
                    }
                }

            private:
                const vector<ShaderCompileDesc>& _listDesc;
                vector<uint32>                   _listRequest; ///< 토막 안 번호 → 요청 번호
                ShaderCompileSession&            _session;
                const ShaderCompiledDelegate&    _onCompiled;
                vector<optional<CompileSetup>>   _listSetup;      ///< 요청마다 한 칸. 준비에 들어가면 비운다
                vector<uint64>                   _listActionKey;  ///< 요청마다 동작 키
                vector<optional<PendingCompile>> _listPending;    ///< 요청마다 한 칸. 적중 · 실패 · 바로 컴파일한 요청은 비어 있다
                vector<WrittenResult>            _listWritten;    ///< 요청마다 한 칸(소스 기록을 쓰는 묶음만)
                vector<Group>                    _listGroup;      ///< 소스 무리(소스 기록을 쓰는 묶음만)
                vector<uint32>                   _listGroupIndex; ///< 요청마다 `_listGroup` 자리
                vector<uint32>                   _listWinner;     ///< 컴파일하는 요청(토막 안 번호)
                vector<Follower>                 _listFollower;   ///< 같은 글의 나머지 요청
                vector<ShaderCompileResult>      _listResult;     ///< `_listWinner` 와 짝
                unordered_map<uint64, uint32>    _mapWinner;      ///< 글 키 → 컴파일하는 요청
                mutex                            _mutex;          ///< `_mapWinner` 와 진 요청의 글
                ShaderRecordScope                _recordScope;
                // 병렬 단계가 칸을 읽고 쓰는 창 — 워커마다 제 칸만 쓴다. `vector::operator[]` 를 워커에서 부르면 겹친 쓰기로 잡힌다(`RaceDetectContext`). 단계 앞에서(한 스레드) 다시 잡는다.
                span<optional<CompileSetup>>   _slotSetup{};
                span<uint64>                   _slotActionKey{};
                span<optional<PendingCompile>> _slotPending{};
                span<WrittenResult>            _slotWritten{};
                span<ShaderCompileResult>      _slotResult{};
                span<const uint32>             _slotRequest{};
                span<const uint32>             _slotGroupIndex{};
                span<const Group>              _slotGroup{};
                span<const uint32>             _slotWinner{};
                span<const Follower>           _slotFollower{};
            };
        };
    } // namespace
} // namespace sw

namespace sw
{

    ShaderEnginePolicy ShaderEnginePolicy::capture()
    {
        const RenderConfig&          render     = engine::getSubsystem<EngineConfig>()._render;
        const ProbeVisibilityConfig& visibility = render._probeVisibility;
        return ShaderEnginePolicy{ render._taaHistoryWeight, visibility._normalBias, visibility._viewBias, visibility._backfaceFloor,
                                   visibility._chebyshevExponent, render._taaMinHistoryWeight, render._taaVarianceClipGamma, render._taaVelocityRejectPixels,
                                   render._autoExposureLowPercent, render._autoExposureHighPercent, render._autoExposureHistogramLogMin, render._autoExposureHistogramLogMax };
    }

    void ShaderCompiler::appendEngineDefines( vector<ShaderMacroDefine>& inoutList, const ShaderEnginePolicy& enginePolicy )
    {
        // 엔진 텍스처 칸 표(shaderslot::emulated) — binding.hlsli 가 PassCB 의 g_<이름>Index 필드와 Bindless::emulatedSlot 을 이 매크로로 펼친다.
        for ( uint32 ordinal = 0; ordinal < shaderslot::kEngineTextureCount; ++ordinal )
        {
            const utf8*                             pName = shaderslot::emulated::kEngineTextureNames[ordinal];
            ShaderMacroDefine                       slotDefine;
            StringBuilder<constant::buffer::kMax64> slotNameBuilder;
            slotNameBuilder.appendFormat( "%#%#", shaderslot::emulated::kSlotMacroPrefix, pName );
            slotDefine._name  = string( slotNameBuilder.view() );
            slotDefine._value = to_string( ordinal );
            inoutList.push_back( std::move( slotDefine ) );
            ShaderMacroDefine                       indexDefine;
            StringBuilder<constant::buffer::kMax64> indexNameBuilder;
            indexNameBuilder.appendFormat( "%#%#", shaderslot::emulated::kIndexMacroPrefix, ordinal );
            indexDefine._name = string( indexNameBuilder.view() );
            StringBuilder<constant::buffer::kMax64> indexValueBuilder;
            indexValueBuilder.appendFormat( "g_%#Index", pName );
            indexDefine._value = string( indexValueBuilder.view() );
            inoutList.push_back( std::move( indexDefine ) );
        }
        // 렌더 정책(셰이더가 읽는 몫) — 설정 파일이 정본이고 셰이더는 기본값을 두지 않는다(없으면 #error). 값이 바뀌면 글에 그 값이 든 셰이더만 다시 컴파일한다.
        struct PolicyDefine
        {
            const utf8* _pName;
            float32     _value;
        };
        const PolicyDefine arrPolicyDefine[] = {
            {             "SW_TAA_HISTORY_WEIGHT",            enginePolicy._taaHistoryWeight},
            {              "SW_PROBE_NORMAL_BIAS",             enginePolicy._probeNormalBias},
            {                "SW_PROBE_VIEW_BIAS",               enginePolicy._probeViewBias},
            {           "SW_PROBE_BACKFACE_FLOOR",          enginePolicy._probeBackfaceFloor},
            {       "SW_PROBE_CHEBYSHEV_EXPONENT",      enginePolicy._probeChebyshevExponent},
            {         "SW_TAA_MIN_HISTORY_WEIGHT",         enginePolicy._taaMinHistoryWeight},
            {        "SW_TAA_VARIANCE_CLIP_GAMMA",        enginePolicy._taaVarianceClipGamma},
            {     "SW_TAA_VELOCITY_REJECT_PIXELS",     enginePolicy._taaVelocityRejectPixels},
            {      "SW_AUTO_EXPOSURE_LOW_PERCENT",      enginePolicy._autoExposureLowPercent},
            {     "SW_AUTO_EXPOSURE_HIGH_PERCENT",     enginePolicy._autoExposureHighPercent},
            {"SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MIN", enginePolicy._autoExposureHistogramLogMin},
            {"SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MAX", enginePolicy._autoExposureHistogramLogMax},
        };
        for ( const PolicyDefine& policyDefine : arrPolicyDefine )
        {
            ShaderMacroDefine define;
            define._name  = policyDefine._pName;
            define._value = to_string( policyDefine._value );
            inoutList.push_back( std::move( define ) );
        }
    }

    uint64 ShaderCompiler::getToolchainFingerprint()
    {
        static const uint64 s_fingerprint = []
        {
            // SPIR-V 타깃 환경(-fspv-target-env)은 Vulkan 요구 판이다.
            constexpr uint32 kVulkanTargetVersion = constant::render::kVulkanRequiredMajor * 1000 + constant::render::kVulkanRequiredMinor;
            return makeToolchainFingerprint( CookCodeFingerprint::find( "ShaderCompiler" ), kVulkanTargetVersion, CookCodeFingerprint::getShaderCompilerVersion() );
        }();
        return s_fingerprint;
    }

    uint64 ShaderCompiler::makeToolchainFingerprint( uint64 compilerCodeFingerprint, uint32 vulkanTargetVersion, string_view compilerVersion )
    {
        uint64 hash = HashUtil::computeStringHash64( to_string( compilerCodeFingerprint ), false );
        hash        = HashUtil::computeStringHash64( to_string( vulkanTargetVersion ), false, hash );
        return HashUtil::computeStringHash64( string( compilerVersion ), false, hash );
    }

    vector<optional<uint64>> ShaderCompiler::findCurrentBytecodeHashes( const vector<ShaderCompileDesc>& listDesc )
    {
        using Internal = ShaderCompilerInternal;
        vector<optional<uint64>>          listHash( listDesc.size() );
        ShaderCompileSession              session{ engine::getSubsystem<AssetPaths>() };
        const expected<DerivedDataCache>& cache = Internal::getDerivedDataCache();
        if ( session.getResolver() == nullptr || cache.has_value() == false )
            return listHash;
        vector<optional<Internal::CompileAction>> listAction( listDesc.size() );
        const Internal::ActionJob                 actionJob{ &listDesc, &session };
        engine::runParallelInto<optional<Internal::CompileAction>>( listAction, 1, actionJob );
        const vector<uint32> listOrder = Internal::makeGroupedOrder( listDesc );
        for ( size_t begin = 0; begin < listOrder.size(); )
        {
            const size_t           end = Internal::findGroupEnd( listDesc, listOrder, begin );
            Internal::LoadedRecord source;
            for ( size_t index = begin; index < end && source._description.empty(); ++index )
            {
                const optional<Internal::CompileAction>& action = listAction[listOrder[index]];
                if ( action.has_value() && action->_bUseCache )
                    source = Internal::findSourceRecord( *cache, listDesc[listOrder[index]], *action, session );
            }
            for ( ; begin < end; ++begin )
            {
                const uint32 request = listOrder[begin];
                if ( listAction[request].has_value() )
                    listHash[request] = Internal::findCurrentHash( *cache, source, *listAction[request], session );
            }
        }
        return listHash;
    }

    bool ShaderCompiler::isResultCurrent( const ShaderCompileResult& result, const ShaderCompileDesc& requestDesc )
    {
        using Internal = ShaderCompilerInternal;
        if ( result._record._listInput.empty() || requestDesc._enginePolicy.has_value() == false )
            return false;
        // 소스 내용은 동작 키에만 들고 기록의 입력이 따로 본다 — 여기서는 작업 키의 값만 있으면 된다.
        return result._record.isFresh( Internal::makeCompileAction( Internal::makeFullDesc( requestDesc ), 0 )._jobDesc );
    }

    ShaderCompileResult ShaderCompiler::compileHLSL( const ShaderCompileDesc& requestDesc, ShaderCompileClaims* pClaims )
    {
        using Internal = ShaderCompilerInternal;
        ShaderCompileResult                result{};
        optional<Internal::PendingCompile> pending = Internal::openAndPrepare( requestDesc, result );
        if ( pending.has_value() == false )
            return result;
        const bool bShared = pClaims != nullptr && pending->_sourceKey._key != 0 && pending->_setup._action._bUseCache;
        if ( bShared == false )
        {
            Internal::compilePending( *pending, result );
            return result;
        }
        // 맡은 뒤 한 번 더 찾는다 — 앞서 맡은 스레드가 이 글을 막 컴파일했을 수 있다.
        const ShaderCompileClaims::Scope claim{ *pClaims, pending->_sourceKey._key };
        if ( Internal::findBytecodeOfText( *pending->_setup._pCache, pending->_sourceKey, result._bytecode ) )
        {
            result._bSuccess = true;
            Internal::writeSharedRecord( *pending, ContentHash::computeBytes( result._bytecode ), result );
            return result;
        }
        Internal::compilePending( *pending, result );
        return result;
    }

    void ShaderCompiler::compileBatch( const vector<ShaderCompileDesc>& listDesc, ShaderRecordScope recordScope, const ShaderCompiledDelegate& onCompiled )
    {
        using Internal = ShaderCompilerInternal;
        ShaderCompileSession session{ engine::getSubsystem<AssetPaths>() };
        const vector<uint32> listOrder = Internal::makeGroupedOrder( listDesc );
        for ( size_t begin = 0; begin < listOrder.size(); )
        {
            size_t end = begin;
            while ( end < listOrder.size() && end - begin < Internal::BatchChunk::kChunkSize )
                end = Internal::findGroupEnd( listDesc, listOrder, end );
            Internal::BatchChunk chunk{ listDesc, listOrder, begin, end, recordScope, session, onCompiled };
            chunk.run();
            begin = end;
        }
    }
} // namespace sw
