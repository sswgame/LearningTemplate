#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/EnumArray.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/ScopedProgressReport.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"
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
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/SPIRVOpenGLPatch.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Compile/WebGPUShaderContainer.h"

#include <algorithm>

#if defined( SW_HAS_DXC_API )
    #include <dxcapi.h>
#endif
#if defined( SW_PLATFORM_WINDOWS ) && !defined( SW_HAS_DXC_API )
    // SW_HAS_DXC_API 는 EnginePlatformHeaders.h 가 정한다 — 빠지면 DXC 경로가 조용히 꺼지고 테스트는 "컴파일러 없음" 으로 건너뛴다.
    #error "SW_HAS_DXC_API is not defined on Windows - include Engine/Foundation/EnginePlatformHeaders.h"
#endif

namespace sw
{
    // 안쪽 도우미(캐시 쓰기 · 컴파일 단계)도 로그를 남기므로 맨 앞에 둔다.
    namespace
    {
        struct ShaderCompilerInternal
        {
#if defined( SW_PLATFORM_WINDOWS )
            /**
             * @brief FXC 의 `#include` 를 include 규칙(`ShaderIncludeResolver`)으로 푸는 ID3DInclude 입니다. 연 파일을 적어 둡니다.
             * @details 기록은 이 객체 안이다 — 컴파일 호출마다 하나를 만들어 쓰므로 병렬 컴파일에서도 서로 섞이지 않는다(전역 · static 기록 금지).
             */
            class ResolverD3DInclude final : public ID3DInclude
            {
            public:
                ResolverD3DInclude( const ShaderIncludeResolver& resolver, const ShaderCompileInput& input )
                    : _resolver{ resolver }
                    , _input{ input }
                {
                }

                STDMETHOD( Open )
                ( D3D_INCLUDE_TYPE includeType, LPCSTR pFileName, LPCVOID pParentData,
                  LPCVOID* ppData, UINT* pBytes ) override
                {
                    (void)includeType;
                    (void)pParentData;
                    const bool bInvalidIncludeArguments = pFileName == nullptr || ppData == nullptr || pBytes == nullptr;
                    if ( bInvalidIncludeArguments )
                        return E_FAIL;

                    vector<uint8>                bytes;
                    const optional<AbsolutePath> found = _resolver.findInclude( _input, pFileName, bytes );
                    if ( found.has_value() == false || bytes.empty() )
                        return E_FAIL;

                    uint8* pHeap = static_cast<uint8*>( Memory::allocate( bytes.size() ) );
                    if ( pHeap == nullptr )
                        return E_OUTOFMEMORY;
                    Memory::copy( pHeap, bytes.data(), bytes.size() );
                    *ppData = pHeap;
                    *pBytes = static_cast<UINT>( bytes.size() );
                    return S_OK;
                }

                STDMETHOD( Close )
                ( LPCVOID pData ) override
                {
                    if ( pData != nullptr )
                        Memory::free( const_cast<void*>( pData ) );
                    return S_OK;
                }

            private:
                const ShaderIncludeResolver& _resolver;
                const ShaderCompileInput&    _input;
            };
#endif

            /**
             * @brief 셰이더 스테이지와 타깃 포맷에 맞는 프로파일 문자열을 반환합니다.
             * @details DX11 은 SM5.0, DX12 · Vulkan · OpenGL 은 네이티브 bindless 와 Descriptor Indexing 을 위해 SM6.6 입니다. 스테이지마다
             *          다른 것은 `ShaderStageInfo` 표의 줄이고, SM5 에 없는 스테이지(메시 · 앰플리피케이션)는 vs_5_0 으로 폴백합니다.
             */
            static const utf8* getTargetProfile( ShaderStage stage, ShaderTargetFormat targetFormat )
            {
                const ShaderStageInfo&              info    = getShaderStageInfo( stage );
                const ShaderTargetFormatInfo* const pFormat = ShaderTargetFormatTable::find( targetFormat );
                if ( pFormat != nullptr && pFormat->_tool == ShaderCompilerTool::Fxc )
                    return info._pProfileSm5 != nullptr ? info._pProfileSm5 : getShaderStageInfo( ShaderStage::Vertex )._pProfileSm5;
                return info._pProfileSm6;
            }

#if defined( SW_HAS_DXC_API )
    #if defined( SW_PLATFORM_WINDOWS )
            template <typename T>
            using DxcComPtr = Microsoft::WRL::ComPtr<T>;

            template <typename T>
            static T** getDxcAddressOf( DxcComPtr<T>& ptr )
            {
                return ptr.GetAddressOf();
            }

            template <typename T>
            static T* dxcGet( const DxcComPtr<T>& ptr )
            {
                return ptr.Get();
            }
    #elif defined( SW_PLATFORM_POSIX )
            template <typename T>
            using DxcComPtr = CComPtr<T>;

            template <typename T>
            static T** getDxcAddressOf( DxcComPtr<T>& ptr )
            {
                return &ptr;
            }

            template <typename T>
            static T* dxcGet( const DxcComPtr<T>& ptr )
            {
                return ptr;
            }
    #else
        #error "Unsupported platform"
    #endif
#endif

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
                uint64      _fxcHash{ 0 };       ///< DXBC 요청이면 실린 FXC 의 내용 해시(`getFxcLibraryHash`), 아니면 0
                bool        _bUseCache{ false }; ///< 거짓이면 DDC 를 쓰지 않는다(실린 FXC 의 판을 모른다) NOLINT(sw-bool-flag): 집합체다(중괄호로 만든다)
            };

            /** @brief 컴파일러를 부르는 단계입니다. 앞 둘이 보통 컴파일이고, 디버그 코드젠만 소스를 바로 컴파일한다(디버그 정보가 원본 소스를 가리켜야 한다). */
            enum class CompileStep : uint8
            {
                Preprocess,          ///< 소스를 전처리만 한다 — include · define 을 다 펼친 글 하나가 나온다
                CompilePreprocessed, ///< 전처리한 글을 컴파일한다 — define · include 를 더 넣지 않는다
                CompileSource,       ///< 소스를 define · include 와 함께 컴파일한다(디버그 코드젠)
            };

            /** @brief 컴파일러 호출 한 번의 결과입니다. */
            enum class CompilerRunResult : uint8
            {
                Succeeded, ///< 글(전처리) 또는 바이트코드를 얻었다
                Rejected,  ///< 셰이더 오류로 실패했다(오류 글을 담았다)
                NoOutput,  ///< 컴파일러를 못 쓰거나 출력이 없었다
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
                ShaderCompileDesc       _desc; ///< 엔진 define · 단계 매크로를 더한 요청(`makeFullDesc`)
                ShaderSourceResolver    _resolver;
                ShaderCompileInput      _input;
                const DerivedDataCache* _pCache;
                uint64                  _sourceHash; ///< 주 소스의 내용 해시 — 못 읽으면 0(컴파일도 같은 파일을 읽다 실패한다)
                CompileAction           _action;
                string                  _profile;
            };

            /** @brief `compileHLSL` 이 실제로 컴파일하는 요청입니다 — 엔진 define 과 단계 매크로를 더한다. */
            static ShaderCompileDesc makeFullDesc( const ShaderCompileDesc& requestDesc )
            {
                // 엔진 define 은 모든 컴파일에 더한다(appendEngineDefines) — 값이 도구 지문에 섞여 표를 바꾸면 모든 셰이더 캐시가 다시 컴파일된다.
                ShaderCompileDesc desc = requestDesc;
                ShaderCompiler::appendEngineDefines( desc._listDefine, *desc._enginePolicy );
                // 단계 매크로 — 네 백엔드 컴파일 경로(DXC → DXIL · SPIR-V → GL · WGSL, FXC → DXBC)가 모두 이 함수를 지나므로 여기 한 곳에서 넣는다.
                ShaderMacroDefine stageDefine;
                stageDefine._name  = getShaderStageInfo( desc._stage )._pStageMacro;
                stageDefine._value = "1";
                desc._listDefine.push_back( std::move( stageDefine ) );
                return desc;
            }

#if defined( SW_PLATFORM_WINDOWS )
            /**
             * @brief FXC 판 — 이 프로세스가 실은 d3dcompiler(System32 — vcpkg 기록에 없고 OS 업데이트로 바뀐다)의 내용 해시입니다. 못 읽으면 0 이고 오류를 한 번 남긴다.
             * @details DXBC 캐시 키에만 든다(언리얼이 셰이더 형식마다 컴파일러 판을 셰이더 맵 키에 넣는 자리). 도구 지문(`getToolchainFingerprint`)에 넣지 않는 것은
             *          쿠킹 기록이 기계마다(OS 판마다) 갈려 모든 셰이더를 다시 굽지 않게다. d3dcompiler 는 일반 import 라 기동 때 이미 실려 있다.
             */
            [[nodiscard]] static uint64 getFxcLibraryHash()
            {
                static const uint64 s_hash = []
                {
                    const HMODULE hModule = ::GetModuleHandleW( D3DCOMPILER_DLL_W );
                    utf16         arrPath[MAX_PATH]{};
                    const DWORD   length = hModule != nullptr ? ::GetModuleFileNameW( hModule, arrPath, MAX_PATH ) : 0;
                    vector<uint8> bytes;
                    const bool    bRead = length > 0 && length < MAX_PATH && FileUtil::readFile( FileUtil::toDiskPath( StringUtil::utf16ToUTF8( arrPath ) ), bytes ) && bytes.empty() == false;
                    if ( bRead == false )
                    {
                        SW_LOG_ERROR_ID( "ShaderCompiler.FxcVersionUnreadable", "The loaded d3dcompiler library could not be read - DXBC compiles skip the shader cache" );
                        return uint64( 0 );
                    }
                    return ContentHash::computeBytes( bytes );
                }();
                return s_hash;
            }
#endif

            /** @brief 컴파일 동작을 만듭니다. @p desc 는 `makeFullDesc` 를 지난 것이고 @p sourceHash 는 주 소스의 내용 해시입니다. */
            static CompileAction makeCompileAction( const ShaderCompileDesc& desc, uint64 sourceHash )
            {
                CompileAction action;
                action._bUseCache = true;
                // define 은 정본(정렬)으로 적는다 — 받은 차례로 적으면 같은 조합이 차례만 달라 두 벌이 쌓인다. 헤더는 키에 넣지 않는다 — 어느 헤더를 열었는지는
                // 컴파일하고 나서야 알아(키는 컴파일 전에 필요하다) 작업의 입력으로 기록하고, 맞출 때 그 기록의 입력이 지금도 같은지 본다(`CookRecord::isFresh`).
                string text;
                text.append( "source=" ).append( desc._source.str() ).append( "\nentry=" ).append( desc._entryPoint );
                text.append( "\nstage=" ).append( to_string( static_cast<uint32>( desc._stage ) ) );
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
                    action._fxcHash   = getFxcLibraryHash();
                    action._bUseCache = action._fxcHash != 0;
                    text.append( "fxc=" ).append( to_string( action._fxcHash ) ).append( "\n" );
                }
#endif
                action._jobDesc._cookerName      = "Shader";
                action._jobDesc._codeFingerprint = ShaderCompiler::getToolchainFingerprint( *desc._enginePolicy ); // 도구 지문 — DXC 나 옵션을 바꾸면 옛 바이트를 돌려주면 안 된다
                action._jobDesc._target          = string( ShaderCooker::getSubfolderForFormat( desc._targetFormat ) );
                action._jobDesc._settingsHash    = HashUtil::computeStringHash64( string_view{ text }, false );

                text.append( "target=" ).append( action._jobDesc._target ).append( "\ntoolchain=" ).append( to_string( action._jobDesc._codeFingerprint ) );
                text.append( "\nsourceHash=" ).append( to_string( sourceHash ) );
                action._actionKey = HashUtil::computeStringHash64( string_view{ text }, false );
                return action;
            }

            /** @brief 요청 @p requestDesc 를 풉니다. 풀지 못하면 로그를 남기고 까닭이 오류입니다. */
            [[nodiscard]] static expected<CompileSetup> setUp( const ShaderCompileDesc& requestDesc )
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
                const optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( engine::getSubsystem<AssetPaths>() );
                if ( resolver.has_value() == false )
                    return unexpected( Error{ ErrorKind::NotFound, "Shader include domains cannot be found (see the _listResourcePriority error above)" } );
                ShaderCompileDesc                  desc  = makeFullDesc( requestDesc );
                const expected<ShaderCompileInput> input = resolver->resolve( desc._source );
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
                const uint64  sourceHash = ContentHash::computeFile( input->_source.str() );
                CompileAction action     = makeCompileAction( desc, sourceHash );
                string        profile    = getTargetProfile( desc._stage, desc._targetFormat );
                return CompileSetup{ std::move( desc ), *resolver, *input, &*cache, sourceHash, std::move( action ), std::move( profile ) };
            }

            /** @brief 컴파일러가 낸 글을 @p outText 에 담습니다 — 글 끝의 0(두 컴파일러 모두 크기에 넣는다)은 글이 아니다. */
            static void assignText( vector<uint8>& outText, const uint8* pData, size_t size )
            {
                outText.assign( pData, pData + size );
                while ( outText.empty() == false && outText.back() == 0 )
                    outText.pop_back();
            }

            /**
             * @brief 전처리한 글 @p text 를 컴파일한 바이트코드의 DDC 키입니다. 글이 같으면 바이트코드도 같다 — 어느 변형(define 조합)이 그 글을 냈는지는 들지 않는다.
             * @details 셰이더는 define 의 일부만 읽어 변형 둘 중 하나는 다른 변형과 같은 글이 된다. 변형마다 컴파일하지 않고 글마다 컴파일한다 —
             *          언리얼이 셰이더 작업을 전처리한 소스의 해시로 겹쳐 합치는 것(`r.ShaderCompiler.PreprocessedCache`)과 같다.
             */
            static SourceKey makeSourceKey( const CompileSetup& setup, const vector<uint8>& text )
            {
                const string normalized = ShaderCompiler::normalizePreprocessedText( string_view( reinterpret_cast<const utf8*>( text.data() ), text.size() ) );
                SourceKey    sourceKey;
                sourceKey._description.append( "Shader preprocessed text\ntarget=" ).append( setup._action._jobDesc._target ).append( "\nprofile=" ).append( setup._profile );
                sourceKey._description.append( "\nentry=" ).append( setup._desc._entryPoint ).append( "\ntoolchain=" ).append( to_string( setup._action._jobDesc._codeFingerprint ) );
                sourceKey._description.append( "\nfxc=" ).append( to_string( setup._action._fxcHash ) ).append( "\nlength=" ).append( to_string( normalized.size() ) );
                sourceKey._description.append( "\ntext=" ).append( to_string( HashUtil::computeStringHash64( normalized, false ) ) );
                sourceKey._key = HashUtil::computeStringHash64( sourceKey._description, false );
                return sourceKey;
            }

            /** @brief DDC 에서 동작의 기록을 찾아 새것이면 바이트코드까지 읽습니다. 둘 다 맞을 때만 true 입니다. */
            [[nodiscard]] static bool findCached( const DerivedDataCache& cache, const CompileAction& action, CookRecord& outRecord, vector<uint8>& outBytecode )
            {
                // 기록의 입력 중 하나라도 달라졌으면 맞은 것이 아니다 — 그 셰이더만 다시 컴파일한다(다른 헤더를 쓰는 셰이더는 그대로다).
                // 항목은 키(입력까지 든 작업 키)와 서술 · 본문 해시가 맞을 때만 읽힌다. 다른 프로세스가 둘 중 하나만 바꾼 순간은 해시가 어긋나 다시 컴파일한다.
                return cache.readRecord( action._actionKey, outRecord ) && outRecord.isFresh( action._jobDesc ) &&
                       cache.find( outRecord._key, CookJob::makeKeyDescription( action._jobDesc, outRecord._listInput ), outBytecode ) && outBytecode.empty() == false &&
                       ContentHash::computeBytes( outBytecode ) == outRecord._outputHash;
            }

            /** @brief 컴파일한 결과에 작업의 기록을 달고, 동작을 캐시할 수 있으면 바이트코드 항목과 기록을 DDC 에 씁니다. 못 쓰면 경고하고 넘어갑니다(다음에 다시 컴파일한다). */
            static void finishCompile( const DerivedDataCache& cache, const CompileAction& action, const CookJob& job, ShaderCompileResult& inoutResult )
            {
                inoutResult._record = CookRecord::make( job, ContentHash::computeBytes( inoutResult._bytecode ) );
                if ( action._bUseCache == false || inoutResult._bytecode.empty() )
                    return;
                // 기록은 항목을 쓴 뒤에 쓴다 — 기록이 있으면 항목이 있다.
                if ( cache.store( inoutResult._record._key, job.makeKeyDescription(), inoutResult._bytecode ) == false || cache.writeRecord( action._actionKey, inoutResult._record ) == false )
                    SW_LOG_WARNING_ID( "ShaderCompiler.CouldNotWriteShader", "Could not write the shader DDC entry or record of key %#", action._actionKey );
            }

#if defined( SW_PLATFORM_WINDOWS )
            /**
             * @brief FXC 로 @p step 을 합니다(DXBC — D3D11, 행 우선). 전처리면 @p inoutText 에 글을, 컴파일이면 @p outResult 에 바이트코드를 담습니다.
             * @param inoutText 전처리한 글 — `Preprocess` 가 쓰고 `CompilePreprocessed` 가 읽는다
             */
            [[nodiscard]] static CompilerRunResult runFxc( const CompileSetup& setup, CompileStep step, vector<uint8>& inoutText, ShaderCompileResult& outResult )
            {
                const ShaderCompileDesc& desc         = setup._desc;
                UINT                     compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
                if ( desc._bDebugCodegen != SW_FALSE )
                    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

                // 소스는 엔진 파일 입구로 읽는다(`D3DCompileFromFile` 은 자기 파일 API 다). 실패 로그는 readFile 이 남긴다.
                const bool bPreprocessed = step == CompileStep::CompilePreprocessed;
                if ( bPreprocessed == false && FileUtil::readFile( FileUtil::toDiskPath( setup._input._source.str() ), inoutText ) == false )
                    return CompilerRunResult::NoOutput;

                vector<D3D_SHADER_MACRO> listMacro;
                listMacro.reserve( desc._listDefine.size() + 3 );
                listMacro.push_back( { "DX11", "1" } );
                if ( desc._stage == ShaderStage::Compute )
                    listMacro.push_back( { "SW_STAGE_COMPUTE", "1" } );
                for ( const ShaderMacroDefine& def : desc._listDefine )
                {
                    if ( def._name.empty() )
                        continue;
                    listMacro.push_back( { def._name.c_str(), def._value.c_str() } );
                }
                listMacro.push_back( { nullptr, nullptr } );

                ResolverD3DInclude               includeHandler( setup._resolver.getIncludeResolver(), setup._input );
                Microsoft::WRL::ComPtr<ID3DBlob> outputBlob;
                Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
                const utf8* const                pSourceName = setup._input._source.c_str();
                HRESULT                          hr          = S_OK;
                if ( step == CompileStep::Preprocess )
                    hr = D3DPreprocess( inoutText.data(), inoutText.size(), pSourceName, listMacro.data(), &includeHandler, outputBlob.GetAddressOf(), errorBlob.GetAddressOf() );
                else // 전처리한 글에는 펼칠 매크로 · include 가 없다
                    hr = D3DCompile( inoutText.data(), inoutText.size(), pSourceName, bPreprocessed ? nullptr : listMacro.data(), bPreprocessed ? nullptr : &includeHandler,
                                     desc._entryPoint.c_str(), setup._profile.c_str(), compileFlags, 0, outputBlob.GetAddressOf(), errorBlob.GetAddressOf() );
                if ( FAILED( hr ) )
                {
                    if ( errorBlob != nullptr )
                        outResult._errorMessage = static_cast<const utf8*>( errorBlob->GetBufferPointer() );
                    else
                        outResult._errorMessage = "Unknown DXBC compilation error.";
                    SW_LOG_ERROR_ID( "ShaderCompiler.DXBCFailed", "%#", outResult._errorMessage.c_str() );
                    return CompilerRunResult::Rejected;
                }
                if ( outputBlob == nullptr || outputBlob->GetBufferSize() == 0 )
                    return CompilerRunResult::NoOutput;

                const uint8* pData = static_cast<const uint8*>( outputBlob->GetBufferPointer() );
                if ( step == CompileStep::Preprocess )
                {
                    assignText( inoutText, pData, outputBlob->GetBufferSize() );
                    return CompilerRunResult::Succeeded;
                }
                outResult._bytecode.assign( pData, pData + outputBlob->GetBufferSize() );
                outResult._bSuccess = true;
                SW_LOG_TRACE_ID( "ShaderCompiler.TargetD3D11DXBCRow", "Target: D3D11 (DXBC Row-Major), Size: %# bytes", static_cast<uint32>( outResult._bytecode.size() ) );
                return CompilerRunResult::Succeeded;
            }
#endif // SW_PLATFORM_WINDOWS

#if defined( SW_HAS_DXC_API )
            /**
             * @brief DXC 가 고른 include 후보를 include 규칙(`ShaderIncludeResolver::readInclude`)으로 읽어 넘기고 `LoadSource` 가 연 파일을 적는 `IDxcIncludeHandler` 입니다.
             * @details DXC 기본 처리기(`CreateDefaultIncludeHandler`)는 자기 파일 API 로 디스크를 읽는다 - 엔진 파일 입구(경로 · 진단 · 디스크 흉내)를 건너뛰므로 쓰지 않는다.
             * @details 컴파일 호출마다 하나를 만든다. 기록은 이 객체 안이고 전역 · static 이 아니라서 컴파일을 병렬로 돌려도 섞이지 않는다.
             *          전이 include 도 DXC 가 이 처리기를 거쳐 읽으므로 모두 잡힌다. 참조 수로 사는 COM 객체다 — `make_unique(...).release()` 로 만들어
             *          `Attach` 로 소유권을 `DxcComPtr` 에 넘기고, 마지막 `Release()` 가 푼다. 기록은 소유자가 살아 있는 동안 읽는다. 해석기는 컴파일 호출보다 오래 산다.
             */
            class ResolverDXCInclude final : public IDxcIncludeHandler
            {
            public:
                ResolverDXCInclude( DxcComPtr<IDxcUtils> pUtils, const ShaderIncludeResolver& resolver )
                    : _pUtils{ std::move( pUtils ) }
                    , _resolver{ resolver }
                    , _refCount{ 1 }
                {
                }

                ResolverDXCInclude( const ResolverDXCInclude& )            = delete;
                ResolverDXCInclude& operator=( const ResolverDXCInclude& ) = delete;

                HRESULT STDMETHODCALLTYPE LoadSource( LPCWSTR pFilename, IDxcBlob** ppIncludeSource ) override
                {
                    const bool bInvalidArguments = pFilename == nullptr || ppIncludeSource == nullptr || _pUtils == nullptr;
                    if ( bInvalidArguments )
                        return E_FAIL;
                    *ppIncludeSource = nullptr;
                    // DXC 는 include 폴더마다 후보 이름을 물어본다 - 없는 후보는 오류 로그 없이 실패로 돌려줘 다음 폴더로 넘어가게 한다.
                    vector<uint8>                bytes;
                    const string                 candidate = FileUtil::normalizeSeparators( StringUtil::utf16ToUTF8( pFilename ) );
                    const optional<AbsolutePath> found     = _resolver.readInclude( candidate, bytes );
                    if ( found.has_value() == false )
                        return E_FAIL; // 연 파일 · 없던 후보는 include 규칙이 작업(`CookJob`)에 적었다
                    DxcComPtr<IDxcBlobEncoding> blob;
                    const HRESULT               hr = _pUtils->CreateBlob( bytes.data(), static_cast<UINT32>( bytes.size() ), DXC_CP_UTF8, getDxcAddressOf( blob ) );
                    if ( FAILED( hr ) )
                        return hr;
                    *ppIncludeSource = blob.Detach();
                    return S_OK;
                }

                ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>( ++_refCount ); }

                ULONG STDMETHODCALLTYPE Release() override
                {
                    const uint32 remaining = _refCount.fetch_sub( 1 ) - 1;
                    if ( remaining == 0 )
                        sw_delete( this );
                    return static_cast<ULONG>( remaining );
                }

                HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppOutObject ) override
                {
                    if ( ppOutObject == nullptr )
                        return E_INVALIDARG;
                    *ppOutObject          = nullptr;
                    const bool bSupported = IsEqualGUID( riid, __uuidof( IUnknown ) ) || IsEqualGUID( riid, __uuidof( IDxcIncludeHandler ) );
                    if ( bSupported == false )
                        return E_NOINTERFACE;
                    *ppOutObject = static_cast<IDxcIncludeHandler*>( this );
                    AddRef();
                    return S_OK;
                }

            private:
                DxcComPtr<IDxcUtils>         _pUtils;
                const ShaderIncludeResolver& _resolver;
                atomic<uint32>               _refCount;
            };

            /** @brief DXC 라이브러리의 `DxcCreateInstance` 주소를 찾습니다(`ShaderCompiler::findDXCCreateInstance` 가 한 번 부른다). 빌드가 DXC 를 실행 파일 옆에 둔다(배포 구성의 테스트는 TestBin) - 다른 곳을 찾아다니지 않는다. */
            static void* loadDXCCreateInstance()
            {
                const string                 libraryText = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ), ModuleLibraryUtil::formatSharedLibraryName( "dxcompiler" ) );
                const optional<AbsolutePath> libraryPath = AbsolutePath::tryMake( libraryText );
                if ( libraryPath.has_value() == false )
                {
                    SW_LOG_ERROR_ID( "ShaderCompiler.DXCPathNotAbsolute", "DXC path is not absolute: %#", libraryText.c_str() );
                    return nullptr;
                }
                void* const pLibrary = ModuleLibraryUtil::loadLibraryAt( *libraryPath );
                if ( pLibrary == nullptr )
                    return nullptr;
                void* const pCreateInstance = ModuleLibraryUtil::getDynamicSymbol( pLibrary, "DxcCreateInstance" );
                if ( pCreateInstance == nullptr )
                    SW_LOG_WARNING_ID( "ShaderCompiler.LoadedButDxcCreateInstanceNot", "%# loaded but DxcCreateInstance not found", libraryText.c_str() );
                return pCreateInstance;
            }

            /** @brief Vulkan SPIR-V 인자입니다. */
            static void appendSpirvVulkanArguments( vector<LPCWSTR>& listArgument )
            {
                // 타깃 버전은 디바이스가 요구하는 최소 버전과 같은 값이다(constant::render::kVulkanRequiredMajor · kVulkanRequiredMinor). 이 타깃(1.2)에서 DXC 는 HLSL `discard` 를
                // OpKill 로 낸다 — 셰이더는 미분 읽기를 다 마친 뒤 버린다(binding.hlsli 8-1 절).
                static const wstring s_targetEnv = []()
                {
                    fixed_string<constant::buffer::kMax64> text;
                    formatstring( text.data(), text.capacity(), "-fspv-target-env=vulkan%#.%#", constant::render::kVulkanRequiredMajor, constant::render::kVulkanRequiredMinor );
                    return StringUtil::utf8ToUTF16( text.c_str() );
                }();
                listArgument.push_back( L"-spirv" );
                listArgument.push_back( s_targetEnv.c_str() );
                listArgument.push_back( L"-fvk-use-dx-position-w" );
                // cbuffer · StructuredBuffer 를 DX 와 같은 규칙으로 패킹한다. 머티리얼 데이터 원소(g_SwMaterials)를
                // 엔진이 한 레이아웃으로 채우므로 백엔드마다 stride · 오프셋이 달라지면 안 된다(std430 은 float3 을 16 정렬).
                listArgument.push_back( L"-fvk-use-dx-layout" );
                // 세트 0 의 binding = 레지스터 종류별 시프트 + 번호(binding_slots.hlsli 6). 셰이더는 register(b#/t#/u#) 만
                // 쓰고 [[vk::binding]] 을 적지 않는다. 파이프라인 레이아웃(VulkanRHIDevice.cpp)이 같은 값으로 만든다.
                static const wstring s_bShift = StringUtil::utf8ToUTF16( to_string( shaderslot::vk::kBShift ).c_str() );
                static const wstring s_tShift = StringUtil::utf8ToUTF16( to_string( shaderslot::vk::kTShift ).c_str() );
                static const wstring s_uShift = StringUtil::utf8ToUTF16( to_string( shaderslot::vk::kUShift ).c_str() );
                listArgument.push_back( L"-fvk-b-shift" );
                listArgument.push_back( s_bShift.c_str() );
                listArgument.push_back( L"0" );
                listArgument.push_back( L"-fvk-t-shift" );
                listArgument.push_back( s_tShift.c_str() );
                listArgument.push_back( L"0" );
                listArgument.push_back( L"-fvk-u-shift" );
                listArgument.push_back( s_uShift.c_str() );
                listArgument.push_back( L"0" );
            }

            /** @brief OpenGL SPIR-V 인자입니다. */
            static void appendSpirvOpenGLArguments( vector<LPCWSTR>& listArgument )
            {
                listArgument.push_back( L"-spirv" );
                listArgument.push_back( L"-fspv-target-env=vulkan1.1" );
                listArgument.push_back( L"-fvk-use-dx-position-w" );
                listArgument.push_back( L"-fvk-use-dx-layout" ); // Vulkan 과 같은 이유. 네 백엔드 원소 레이아웃 일치
                listArgument.push_back( L"-fvk-b-shift" );
                listArgument.push_back( L"16" );
                listArgument.push_back( L"0" );
                // GL 의 u# → SSBO binding 은 계약(SW_GL_UAV_BINDING0)이 정한다. common.hlsli 의 OPENGL 용
                // RW 버퍼 매크로가 명시 binding 을 두지 않아 이 시프트가 적용된다. b/t 는 명시 binding 이라 시프트와 무관하다.
                static const wstring s_uavShift = StringUtil::utf8ToUTF16( to_string( shaderslot::gl::kUAVBinding0 ).c_str() );
                listArgument.push_back( L"-fvk-u-shift" );
                listArgument.push_back( s_uavShift.c_str() );
                listArgument.push_back( L"0" );
            }

            /**
             * @brief WebGPU SPIR-V 인자입니다. 바인드 그룹 0 하나에 b · t · s · u 를 띠로 나눠 둔다(`shaderslot::wgpu`). WEBGPU 는 DX11 과 같은 고정 슬롯 갈래를 쓰고
             *        루트 상수는 b2 상수 버퍼 에뮬이다(common.hlsli). 웨이브 연산은 뺀다(`SW_NO_WAVE_OPS` — WebGPU 의 subgroups 는 선택 기능).
             */
            static void appendSpirvWebGPUArguments( vector<LPCWSTR>& listArgument )
            {
                namespace wgpu = shaderslot::wgpu;
                listArgument.push_back( L"-spirv" );
                listArgument.push_back( L"-fspv-target-env=vulkan1.1" ); // Tint 의 SPIR-V 입력이 받는 판(기억) — 1.2 의 기능은 쓰지 않는다
                listArgument.push_back( L"-fvk-use-dx-position-w" );
                listArgument.push_back( L"-fvk-use-dx-layout" ); // 다른 백엔드와 같은 원소 레이아웃(머티리얼 데이터를 한 벌로 채운다)
                static const wstring s_bShift      = StringUtil::utf8ToUTF16( to_string( wgpu::kBShift ).c_str() );
                static const wstring s_tShift      = StringUtil::utf8ToUTF16( to_string( wgpu::kTShift ).c_str() );
                static const wstring s_sShift      = StringUtil::utf8ToUTF16( to_string( wgpu::kSShift ).c_str() );
                static const wstring s_uShift      = StringUtil::utf8ToUTF16( to_string( wgpu::kUShift ).c_str() );
                const LPCWSTR        arrShift[][2] = {
                    {L"-fvk-b-shift", s_bShift.c_str()},
                    {L"-fvk-t-shift", s_tShift.c_str()},
                    {L"-fvk-s-shift", s_sShift.c_str()},
                    {L"-fvk-u-shift", s_uShift.c_str()},
                };
                for ( const auto& shift : arrShift )
                {
                    listArgument.push_back( shift[0] );
                    listArgument.push_back( shift[1] );
                    listArgument.push_back( L"0" );
                }
                listArgument.push_back( L"-D" );
                listArgument.push_back( L"SW_NO_WAVE_OPS=1" );
            }

            /** @brief GL 패치를 후처리 표의 모양(실패를 알리는 함수)으로 부릅니다. 패치는 실패하지 않는다. */
            static expected<void> applyOpenGLPatch( vector<uint8>& inoutBytecode )
            {
                SPIRVOpenGLPatch::apply( inoutBytecode );
                return {};
            }

            /**
             * @brief SPIR-V 를 Tint 로 WGSL 로 옮기고 둘을 한 꾸러미로 묶습니다(`WebGPUShaderContainer`). Tint 는 DXC 처럼 실행 파일 옆에 둔다(`tint` — Dawn 빌드가 낸다).
             * @details Tint 를 라이브러리로 링크하지 않는다 — 쿠커는 `dawn[tint-tools]` 가 낸 실행 파일만 쓴다(빌드가 App 옆에 두는 `sw_copy_tint_tool`, docs/agents/Platform.md 규칙). 실행 파일이 없으면 오류로 멈춘다(WGSL 없이 구울 수 없다).
             *          임시 파일은 시스템 임시 폴더에 두고 부를 때마다 새 이름을 잡는다(`reserveUniquePath`) — 병렬 쿠킹이 같은 파일을 쓰거나 지우지 않는다.
             *          `--allow-non-uniform-derivatives`: WGSL 만 미분(textureSample)을 균일 제어 흐름으로 묶는다 — HLSL · SPIR-V 에는 그 규칙이 없어 같은 셰이더가
             *          다른 백엔드에서 이미 돈다. 모듈 머리에 `diagnostic(off, derivative_uniformity)` 를 넣는다(Dawn 이 받는 표준 지시문).
             */
            static expected<void> convertSpirvToWgsl( vector<uint8>& inoutBytecode )
            {
    #if defined( SW_PLATFORM_WINDOWS )
                constexpr const utf8* kTintName = "tint.exe";
    #elif defined( SW_PLATFORM_POSIX )
                constexpr const utf8* kTintName = "tint";
    #else
        #error "Unsupported platform"
    #endif
                const string                 tintText = FileUtil::joinPath( FileUtil::getDirectoryPart( FileUtil::getExecutablePath() ), kTintName );
                const optional<AbsolutePath> tintPath = AbsolutePath::tryMake( tintText );
                if ( tintPath.has_value() == false || FileUtil::isRegularFile( FileUtil::toDiskPath( tintText ) ) == false )
                    return unexpected( Error{ ErrorKind::NotFound, "Tint (SPIR-V -> WGSL) is not next to the executable - the WebGPU shader target needs it (docs/agents/Platform.md)", tintText } );
                // 이름은 부를 때마다 새로 잡는다 — 같은 바이트(같은 해시)를 병렬 작업 둘이 같은 이름으로 쓰면 먼저 끝난 쪽이 지운 파일을 다른 쪽이 못 열었다
                // ("Failed to open sw_tint_<해시>.spv"). 만들기가 곧 자리 잡기(`reserveUniquePath`)라 두 작업이 같은 이름을 갖지 않는다.
                const uint64                           hash = HashUtil::appendBytes64( HashUtil::kFNVOffset64, inoutBytecode.data(), inoutBytecode.size() );
                fixed_string<constant::buffer::kMax32> tintStem;
                formatstring( tintStem.data(), tintStem.capacity(), "sw_tint_%#", hash );
                const string spirvPath = FileUtil::reserveUniquePath( FileUtil::toDiskPath( FileUtil::joinPath( FileUtil::getTempDirectory(), tintStem ) ), ".spv", false );
                if ( spirvPath.empty() )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "could not reserve a temporary SPIR-V file for Tint", FileUtil::getTempDirectory() } );
                StringBuilder<constant::buffer::kMax512> wgslBuilder;
                wgslBuilder.appendFormat( "%#.wgsl", FileUtil::removeExtension( spirvPath ) );
                const string wgslPath( wgslBuilder.view() ); // 잡은 이름에서 나온다 — 다른 작업과 겹치지 않는다
                if ( FileUtil::writeFile( FileUtil::toDiskPath( spirvPath ), inoutBytecode.data(), inoutBytecode.size() ) == false )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "could not write the SPIR-V for Tint", spirvPath } );
                string                toolOutput;
                const auto            onOutput = SW_DELEGATE_LAMBDA( ProcessOutputDelegate, [&toolOutput]( string_view line )
                           {
                    toolOutput += line;
                    toolOutput += ' ';
                } );
                const expected<int32> exitCode = Process::execute( ProcessCommand::at( *tintPath, { "--format", "wgsl", "--allow-non-uniform-derivatives", "true", "-o", wgslPath, spirvPath } ), {}, onOutput );
                vector<uint8>         wgslBytes;
                const bool            bRead = exitCode.has_value() && *exitCode == 0 && FileUtil::readFile( FileUtil::toDiskPath( wgslPath ), wgslBytes );
                (void)FileUtil::removeFile( FileUtil::toDiskPath( spirvPath ) );
                (void)FileUtil::removeFile( FileUtil::toDiskPath( wgslPath ) );
                if ( exitCode.has_value() == false )
                    return unexpected( exitCode.error() );
                if ( bRead == false )
                {
                    StringBuilder<constant::buffer::kMax256> detailBuilder;
                    detailBuilder.appendFormat( "exit %#: %#", *exitCode, toolOutput );
                    return unexpected( Error{ ErrorKind::ExternalFailure, "Tint failed", detailBuilder.view() } );
                }
                vector<uint8> container;
                WebGPUShaderContainer::pack( inoutBytecode, string_view( reinterpret_cast<const utf8*>( wgslBytes.data() ), wgslBytes.size() ), container );
                inoutBytecode = std::move( container );
                return {};
            }

            /** @brief 대상 형식 하나의 DXC 쪽 일입니다. 행 순서는 `ShaderTargetFormat` 과 같습니다. 새 대상은 이 표와 `ShaderTargetFormatInfo` 에 한 줄씩이고, 후처리(GL 패치 · 앞으로의 Tint)도 여기에 적습니다. */
            struct DxcTargetRow
            {
                ShaderTargetFormat _format;
                LPCWSTR            _pBackendDefine;                                  ///< 셰이더가 백엔드를 가르는 매크로
                void ( *_pfnAppendArguments )( vector<LPCWSTR>& listArgument );      ///< 형식별 인자. 없으면 nullptr
                expected<void> ( *_pfnPostProcess )( vector<uint8>& inoutBytecode ); ///< 나온 바이트코드 후처리. 없으면 nullptr. 실패하면 그 이유
                const utf8* _pLogName;                                               ///< 로그 이름
                uint8       _bNativeBindless : 1;                                    ///< 네이티브 bindless 텍스처 배열을 쓴다(common.hlsli SW_NATIVE_BINDLESS). DX12 는 t0 space1 무제한 배열(버퍼는 space0 슬롯), Vulkan 은 세트 1 배열(버퍼는 세트 0 의 시프트된 binding)
            };

            static const DxcTargetRow& getDxcTargetRow( ShaderTargetFormat format )
            {
                static const auto kArrDxcTargetRow = makeEnumTable<ShaderTargetFormat, DxcTargetRow>( {
                    {  ShaderTargetFormat::DXBC_D3D11,   L"DX11=1",                     nullptr,             nullptr,                       "D3D11 (DXBC)", 0},
                    {  ShaderTargetFormat::DXIL_D3D12,   L"DX12=1",                     nullptr,             nullptr,             "D3D12 (DXIL Row-Major)", 1},
                    {ShaderTargetFormat::SPIRV_Vulkan, L"VULKAN=1", &appendSpirvVulkanArguments,             nullptr, "Vulkan (SPIR-V, Vulkan API target)", 1},
                    {ShaderTargetFormat::SPIRV_OpenGL, L"OPENGL=1", &appendSpirvOpenGLArguments,   &applyOpenGLPatch,          "OpenGL (SPIR-V vulkan1.1)", 0},
                    { ShaderTargetFormat::WGSL_WebGPU, L"WEBGPU=1", &appendSpirvWebGPUArguments, &convertSpirvToWgsl,      "WebGPU (SPIR-V -> WGSL, Tint)", 0},
                } );
                SW_ASSERT( format < ShaderTargetFormat::Count && kArrDxcTargetRow[format]._format == format );
                return kArrDxcTargetRow[format];
            }

            /** @brief 타깃(SPIR-V 레이아웃 · 레지스터 시프트, 타깃 매크로)과 스테이지에 따른 DXC 인자를 더합니다. 넣는 글은 모두 정적 수명입니다. */
            static void appendDXCTargetArguments( const ShaderCompileDesc& desc, vector<LPCWSTR>& listArgument )
            {
                const DxcTargetRow& target = getDxcTargetRow( desc._targetFormat );
                if ( target._pfnAppendArguments != nullptr )
                    target._pfnAppendArguments( listArgument );

                listArgument.push_back( L"-D" );
                listArgument.push_back( target._pBackendDefine );
                if ( target._bNativeBindless != 0 )
                {
                    listArgument.push_back( L"-D" );
                    listArgument.push_back( L"SW_BINDLESS=1" );
                }
                if ( desc._stage == ShaderStage::Compute )
                {
                    // 컴퓨트에서만 선언되는 자원(RW 텍스처 배열 · 슬롯, binding.hlsli "텍스처 읽기" 절)의 스위치.
                    listArgument.push_back( L"-D" );
                    listArgument.push_back( L"SW_STAGE_COMPUTE=1" );
                }
            }

            /**
             * @brief DXC 로 @p step 을 합니다(DXIL · SPIR-V). 전처리면 @p inoutText 에 글을, 컴파일이면 @p outResult 에 바이트코드(OpenGL 이면 내장 변수를 바꾼 것)를 담습니다.
             * @details 생성 · 소스 읽기 · 인자 · 컴파일 · 결과 읽기의 순서가 곧 의미이고 인자 글의 수명이 이 함수 안이라 더 나누지 않는다.
             * @param inoutText 전처리한 글 — `Preprocess` 가 쓰고 `CompilePreprocessed` 가 읽는다
             */
            [[nodiscard]] static CompilerRunResult runDXC( const CompileSetup& setup, CompileStep step, vector<uint8>& inoutText, ShaderCompileResult& outResult )
            {
                const ShaderCompileDesc&  desc                = setup._desc;
                const ShaderCompileInput& input               = setup._input;
                const string&             profile             = setup._profile;
                DxcCreateInstanceProc     fnDXCCreateInstance = reinterpret_cast<DxcCreateInstanceProc>( ShaderCompiler::findDXCCreateInstance() );
                if ( fnDXCCreateInstance == nullptr )
                {
                    SW_LOG_ERROR_ID( "ShaderCompiler.DxcCreateInstanceUnavailableLibdxcompilerNot", "DxcCreateInstance unavailable (libdxcompiler not loaded)" );
                    return CompilerRunResult::NoOutput;
                }

    #if defined( SW_SANITIZER_THREAD )
                // libdxcompiler 는 계측되지 않은 서드파티라 TSan 이 그 안의 원자 연산 · 지연 초기화 동기화(ManagedStatic · 락 객체 게시)를 보지 못한다.
                // 병렬 쿠킹의 컴파일이 겹치면 DXC 안에서 할당한 블록을 다른 스레드가 해제하는 것으로 보여 경쟁 9 건이 난다(2026-10-25 CI TSan).
                // 억제 목록은 libc 프레임만 남은 보고를 못 맞추고 엔진 콜백의 진짜 경쟁까지 가리므로, 이 구성에서만 DXC 호출을 직렬로 돌려 선후를 TSan 이 보게 한다.
                // 다른 구성은 그대로 병렬이다. 이 구성에서는 DXC 안쪽의 경쟁을 검사하지 않는다.
                // libdxcompiler 를 부르는 다른 자리 `ShaderReflectionDx`(DXIL 리플렉션)는 Windows 전용 파일이고 TSan 구성은 리눅스 전용(Sanitizer.cmake)이라 그 구성에 없다.
                // 리눅스에서 DXIL 리플렉션을 켜면 이 직렬화를 두 호출처가 함께 쓰는 한 함수로 올린다(뮤텍스를 하나 더 두면 서로를 막지 못한다).
                static mutex      s_mutexDXCSerial;
                ScopedLock<mutex> dxcSerialLock{ s_mutexDXCSerial };
    #endif

                DxcComPtr<IDxcUtils>     utils;
                DxcComPtr<IDxcCompiler3> compiler;
                HRESULT                  hrInit = fnDXCCreateInstance( CLSID_DxcUtils, IID_PPV_ARGS( getDxcAddressOf( utils ) ) );
                if ( SUCCEEDED( hrInit ) )
                    fnDXCCreateInstance( CLSID_DxcCompiler, IID_PPV_ARGS( getDxcAddressOf( compiler ) ) );
                else
                    SW_LOG_ERROR_ID( "ShaderCompiler.DxcCreateInstanceCLSIDDxcUtilsFailed", "DxcCreateInstance(CLSID_DxcUtils) failed: 0x%#", hrInit );
                if ( utils == nullptr || compiler == nullptr )
                    return CompilerRunResult::NoOutput;

                DxcComPtr<IDxcIncludeHandler> includeHandler;
                includeHandler.Attach( make_unique<ResolverDXCInclude>( utils, setup._resolver.getIncludeResolver() ).release() );

                // 소스는 엔진 파일 입구로 읽는다(DXC `LoadFile` 은 자기 파일 API 다). 실패 로그는 readFile 이 남긴다.
                const wstring wPath         = StringUtil::utf8ToUTF16( input._source.c_str() );
                const bool    bPreprocessed = step == CompileStep::CompilePreprocessed;
                if ( bPreprocessed == false && FileUtil::readFile( FileUtil::toDiskPath( input._source.str() ), inoutText ) == false )
                    return CompilerRunResult::NoOutput;

                wstring wEntryPoint = StringUtil::utf8ToUTF16( desc._entryPoint.c_str() );
                wstring wProfile    = StringUtil::utf8ToUTF16( profile.c_str() );

                // 인자 글의 수명: 아래 지역 벡터(경로 · 매크로)와 정적 글이 Compile 이 끝날 때까지 산다.
                vector<LPCWSTR> listArgument;
                vector<wstring> listDefineArg;
                listDefineArg.reserve( desc._listDefine.size() );

                listArgument.push_back( wPath.c_str() );
                listArgument.push_back( L"-E" );
                listArgument.push_back( wEntryPoint.c_str() );
                listArgument.push_back( L"-T" );
                listArgument.push_back( wProfile.c_str() );
                listArgument.push_back( L"-Zpr" );

                vector<wstring> listIncludeDirW;
                listIncludeDirW.reserve( input._listIncludeRoot.size() );
                for ( const AbsolutePath& dir : input._listIncludeRoot )
                {
                    listIncludeDirW.push_back( StringUtil::utf8ToUTF16( dir.c_str() ) );
                    listArgument.push_back( L"-I" );
                    listArgument.push_back( listIncludeDirW.back().c_str() );
                }

                appendDXCTargetArguments( desc, listArgument );

                for ( const ShaderMacroDefine& def : desc._listDefine )
                {
                    if ( def._name.empty() || bPreprocessed ) // 전처리한 글에는 펼칠 매크로가 없다
                        continue;
                    StringBuilder<constant::buffer::kMax256> defineBuilder;
                    defineBuilder.appendFormat( "%#=%#", def._name, def._value );
                    listDefineArg.push_back( StringUtil::utf8ToUTF16( defineBuilder.c_str() ) );
                    listArgument.push_back( L"-D" );
                    listArgument.push_back( listDefineArg.back().c_str() );
                }

                // 디버그 코드젠은 빌드 구성이 아니라 **요청**이 정한다 (ShaderCompileDesc::_bDebugCodegen).
                if ( desc._bDebugCodegen != SW_FALSE )
                {
                    listArgument.push_back( L"-Zi" );
                    listArgument.push_back( L"-Od" );
                }

                if ( step == CompileStep::Preprocess )
                    listArgument.push_back( L"-P" );

                DxcBuffer sourceBuffer{};
                sourceBuffer.Ptr      = inoutText.data();
                sourceBuffer.Size     = inoutText.size();
                sourceBuffer.Encoding = DXC_CP_UTF8;

                DxcComPtr<IDxcResult> compileResult;
                HRESULT               hrCompile = compiler->Compile(
                    &sourceBuffer,
                    listArgument.data(),
                    static_cast<uint32>( listArgument.size() ),
                    dxcGet( includeHandler ),
                    IID_PPV_ARGS( getDxcAddressOf( compileResult ) ) );
                if ( FAILED( hrCompile ) )
                {
                    SW_LOG_ERROR_ID( "ShaderCompiler.DXCCompilerCompileFailed", "DXC compiler->Compile failed with HRESULT: 0x%#", hrCompile );
                    return CompilerRunResult::NoOutput;
                }

                HRESULT status = S_OK;
                compileResult->GetStatus( &status );

                DxcComPtr<IDxcBlobUtf8> errors;
                compileResult->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( getDxcAddressOf( errors ) ), nullptr );
                if ( errors != nullptr && errors->GetStringLength() > 0 )
                    outResult._errorMessage = errors->GetStringPointer();
                if ( FAILED( status ) )
                {
                    SW_LOG_ERROR_ID( "ShaderCompiler.DXCFailed", "%#", outResult._errorMessage.c_str() );
                    return CompilerRunResult::Rejected;
                }

                DxcComPtr<IDxcBlob> shaderBlob;
                compileResult->GetOutput( step == CompileStep::Preprocess ? DXC_OUT_HLSL : DXC_OUT_OBJECT, IID_PPV_ARGS( getDxcAddressOf( shaderBlob ) ), nullptr );
                if ( shaderBlob == nullptr || shaderBlob->GetBufferSize() == 0 )
                    return CompilerRunResult::NoOutput;

                const uint8* pData = static_cast<const uint8*>( shaderBlob->GetBufferPointer() );
                if ( step == CompileStep::Preprocess )
                {
                    assignText( inoutText, pData, shaderBlob->GetBufferSize() );
                    return CompilerRunResult::Succeeded;
                }
                outResult._bytecode.assign( pData, pData + shaderBlob->GetBufferSize() );
                const auto           pfnPostProcess = getDxcTargetRow( desc._targetFormat )._pfnPostProcess;
                const expected<void> postProcessed  = pfnPostProcess != nullptr ? pfnPostProcess( outResult._bytecode ) : expected<void>{};
                if ( postProcessed.has_value() == false )
                {
                    StringBuilder<constant::buffer::kMax256> errorBuilder;
                    errorBuilder.appendFormat( "%#: %#", postProcessed.error().message(), postProcessed.error().detail() );
                    outResult._errorMessage = string( errorBuilder.view() );
                    SW_LOG_ERROR_ID( "ShaderCompiler.PostProcessFailed", "%# (%#)", outResult._errorMessage.c_str(), desc._source.c_str() );
                    outResult._bytecode.clear();
                    return CompilerRunResult::Rejected;
                }
                outResult._bSuccess = true;
                SW_LOG_TRACE_ID( "ShaderCompiler.Target", "Target: %#", getDxcTargetRow( desc._targetFormat )._pLogName ); // Trace 를 컴파일하지 않는 구성은 인자를 평가하지 않는다(SW_LOG_DISCARDED)
                return CompilerRunResult::Succeeded;
            }

#endif // SW_HAS_DXC_API

            /** @brief 요청 형식의 컴파일러(FXC · DXC)로 @p step 을 합니다. */
            [[nodiscard]] static CompilerRunResult runCompiler( const CompileSetup& setup, CompileStep step, vector<uint8>& inoutText, ShaderCompileResult& outResult )
            {
                const ShaderTargetFormatInfo* const pTargetInfo = ShaderTargetFormatTable::find( setup._desc._targetFormat );
                if ( pTargetInfo != nullptr && pTargetInfo->_tool == ShaderCompilerTool::Fxc )
                {
#if defined( SW_PLATFORM_WINDOWS )
                    return runFxc( setup, step, inoutText, outResult );
#elif defined( SW_PLATFORM_POSIX )
                    // DXBC 를 낼 수 있는 건 FXC(d3dcompiler) 뿐이고 그건 Windows 전용이다. 이 타깃이 DXC 로 흘러가면 vs_5_0 같은 SM5 프로파일을
                    // 모른다며 "invalid profile vs_5_0" 로 죽는다. 그 메시지로는 "이 플랫폼엔 D3D11 이 없다" 는
                    // 진짜 이유가 드러나지 않아 부르는 쪽이 환경 문제와 셰이더 문제를 구분하지 못한다.
                    outResult._errorMessage = "DXBC (D3D11) target is unavailable on this platform (requires Windows D3DCompiler).";
                    return CompilerRunResult::Rejected;
#else
    #error "Unsupported platform"
#endif
                }
#if defined( SW_HAS_DXC_API )
                return runDXC( setup, step, inoutText, outResult );
#else
                return CompilerRunResult::NoOutput;
#endif
            }

            /** @brief 요청 @p requestDesc 를 전처리한 글의 키입니다. DDC 의 기록이 이미 새것이거나, 글로 겹치지 않는 요청(디버그 코드젠 · 캐시하지 않는 것)이거나, 전처리하지 못하면 0 입니다. */
            static uint64 computeMissingSourceKey( const ShaderCompileDesc& requestDesc )
            {
                const expected<CompileSetup> setup = setUp( requestDesc );
                if ( setup.has_value() == false || setup->_action._bUseCache == false || setup->_desc._bDebugCodegen != SW_FALSE )
                    return 0;
                CookRecord record;
                if ( setup->_pCache->readRecord( setup->_action._actionKey, record ) && record.isFresh( setup->_action._jobDesc ) )
                    return 0;
                const CookJob       job{ setup->_action._jobDesc }; // 쿠킹 세션은 작업 밖의 읽기(include)를 거절한다
                vector<uint8>       text;
                ShaderCompileResult result{};
                return runCompiler( *setup, CompileStep::Preprocess, text, result ) == CompilerRunResult::Succeeded ? makeSourceKey( *setup, text )._key : 0;
            }

            /** @brief 병렬로 전처리해 글 키를 구하는 일의 입력입니다(`compileDistinct`) — 워커는 제 칸에만 쓴다. */
            struct SourceKeyJob
            {
                const vector<ShaderCompileDesc>* _pListDesc{ nullptr };

                /** @brief 요청 @p index 의 글 키를 제 칸 @p outKey 에 씁니다(`runParallelInto` 본문). 컴파일할 것이 없으면 0 입니다. */
                void operator()( uint32 index, uint64& outKey ) const { outKey = computeMissingSourceKey( ( *_pListDesc )[index] ); }
            };

            /** @brief 글이 서로 다른 요청을 병렬로 컴파일하는 일의 입력입니다(`compileDistinct`). 결과는 DDC 에 남고, 실패는 같은 요청을 다시 컴파일하는 쪽이 센다. */
            struct DistinctCompileJob
            {
                const vector<const ShaderCompileDesc*>* _pListDesc{ nullptr };

                /** @brief 요청 @p index 를 컴파일하고 성공했는지를 제 칸 @p outDone 에 씁니다(`runParallelInto` 본문). */
                void operator()( uint32 index, uint8& outDone ) const { outDone = ShaderCompiler::compileHLSL( *( *_pListDesc )[index] )._bSuccess ? 1 : 0; }
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
        // 렌더 정책(셰이더가 읽는 몫) — 설정 파일이 정본이고 셰이더는 기본값을 두지 않는다(없으면 #error). 도구 지문에 섞여 값이 바뀌면 셰이더 캐시가 함께 낡는다.
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

    void* ShaderCompiler::findDXCCreateInstance()
    {
#if defined( SW_HAS_DXC_API )
        static void* const s_pCreateInstance = ShaderCompilerInternal::loadDXCCreateInstance();
        return s_pCreateInstance;
#else
        return nullptr;
#endif
    }

    uint64 ShaderCompiler::getToolchainFingerprint( const ShaderEnginePolicy& enginePolicy )
    {
        static const uint64 s_baseFingerprint = []
        {
            // SPIR-V 타깃 환경(-fspv-target-env)은 Vulkan 요구 판이다.
            constexpr uint32 kVulkanTargetVersion = constant::render::kVulkanRequiredMajor * 1000 + constant::render::kVulkanRequiredMinor;
            return makeToolchainFingerprint( CookCodeFingerprint::find( "ShaderCompiler" ), kVulkanTargetVersion, CookCodeFingerprint::getShaderCompilerVersion() );
        }();
        // 엔진이 모든 컴파일에 넣는 define 도 도구 입력이다 — 렌더 정책 값은 설정을 다시 읽으면 바뀌므로 부를 때마다 요청의 사본으로 섞는다
        // (한 번 잡아 두면 새 값으로 컴파일한 바이트와 옛 값의 바이트를 구별하지 못한다).
        uint64                    hash = s_baseFingerprint;
        vector<ShaderMacroDefine> listDefine;
        appendEngineDefines( listDefine, enginePolicy );
        for ( const ShaderMacroDefine& define : listDefine )
        {
            hash = HashUtil::computeStringHash64( define._name, false, hash );
            hash = HashUtil::computeStringHash64( define._value, false, hash );
        }
        return hash;
    }

    uint64 ShaderCompiler::makeToolchainFingerprint( uint64 compilerCodeFingerprint, uint32 vulkanTargetVersion, string_view compilerVersion )
    {
        uint64 hash = HashUtil::computeStringHash64( to_string( compilerCodeFingerprint ), false );
        hash        = HashUtil::computeStringHash64( to_string( vulkanTargetVersion ), false, hash );
        return HashUtil::computeStringHash64( string( compilerVersion ), false, hash );
    }

    optional<CookRecord> ShaderCompiler::findCurrentRecord( const ShaderCompileDesc& requestDesc )
    {
        using Internal = ShaderCompilerInternal;
        if ( requestDesc._enginePolicy.has_value() == false )
            return nullopt;
        const optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( engine::getSubsystem<AssetPaths>() );
        if ( resolver.has_value() == false )
            return nullopt;
        const expected<ShaderCompileInput> input = resolver->resolve( requestDesc._source );
        const expected<DerivedDataCache>&  cache = Internal::getDerivedDataCache();
        if ( input.has_value() == false || cache.has_value() == false )
            return nullopt;
        const Internal::CompileAction action = Internal::makeCompileAction( Internal::makeFullDesc( requestDesc ), ContentHash::computeFile( input->_source.str() ) );
        CookRecord                    record;
        if ( cache->readRecord( action._actionKey, record ) == false || record.isFresh( action._jobDesc ) == false )
            return nullopt;
        return record;
    }

    bool ShaderCompiler::isResultCurrent( const ShaderCompileResult& result, const ShaderCompileDesc& requestDesc )
    {
        using Internal = ShaderCompilerInternal;
        if ( result._record._listInput.empty() || requestDesc._enginePolicy.has_value() == false )
            return false;
        // 소스 내용은 동작 키에만 들고 기록의 입력이 따로 본다 — 여기서는 작업 키의 값만 있으면 된다.
        return result._record.isFresh( Internal::makeCompileAction( Internal::makeFullDesc( requestDesc ), 0 )._jobDesc );
    }

    ShaderCompileResult ShaderCompiler::compileHLSL( const ShaderCompileDesc& requestDesc )
    {
        using Internal = ShaderCompilerInternal;
        ShaderCompileResult result{};
        result._bSuccess                              = false;
        const expected<Internal::CompileSetup> opened = Internal::setUp( requestDesc );
        if ( opened.has_value() == false )
        {
            StringBuilder<constant::buffer::kMax256> errorBuilder;
            errorBuilder.appendFormat( "%#: %#", opened.error().message(), opened.error().detail() );
            result._errorMessage = string( errorBuilder.view() );
            return result;
        }
        const Internal::CompileSetup& setup = *opened;
        result._bytecode.reserve( 4096 );
        if ( setup._action._bUseCache && Internal::findCached( *setup._pCache, setup._action, result._record, result._bytecode ) )
        {
            result._bSuccess = true;
            return result;
        }
        result._record = CookRecord{};
        result._bytecode.clear();

        // 이 컴파일의 작업 — 주 소스와 include 규칙이 읽은 것(없던 후보 포함)이 입력으로 적힌다. 컴파일러의 include 처리기는 이 스레드에서 불린다.
        CookJob job{ setup._action._jobDesc };
        if ( job.recordDiskInput( setup._input._source.str(), setup._sourceHash ) == false )
        {
            result._errorMessage = "The shader source could not be recorded as a cook input (see the error above).";
            return result;
        }

        // 같은 이름의 헤더가 엔진 헤더를 가리면 어느 쪽이 쓰였는지 알 수 없다 — 실제로 컴파일하기 전에 오류로 멈춘다. DDC 적중 뒤에 본다: 폴더를 재귀로 훑는 검사라
        // 요청마다 하면 적중도 느려지고, 맞은 항목의 기록은 없던 후보 자리까지 적어 가리는 헤더가 새로 생기면 이미 낡음이다(`CookRecord::isFresh`).
        // 언리얼도 셰이더 맵 DDC 조회가 컴파일 준비(include 검사)보다 먼저다.
        vector<string> listShadowed;
        if ( setup._resolver.getIncludeResolver().findShadowedHeaders( setup._input._shadersFolder, listShadowed ) == false )
        {
            for ( const string& problem : listShadowed )
            {
                result._errorMessage += problem;
                result._errorMessage += '\n';
            }
            if ( result._errorMessage.empty() )
                result._errorMessage = "Shader headers could not be checked for shadowing (see the error above).";
            SW_LOG_ERROR_ID( "ShaderCompiler.HeaderShadowed", "%#", result._errorMessage );
            return result;
        }

        // 전처리한 글로 한 번 더 찾는다 — 이 글을 낸 다른 변형이 이미 컴파일했으면 그 바이트코드다(`makeSourceKey`). include 처리기가 이 스레드의 작업에 입력을 적으므로
        // 적중이어도 이 변형의 기록이 온전하다. 디버그 코드젠은 원본 소스를 바로 컴파일한다.
        const bool                  bDebugCodegen = setup._desc._bDebugCodegen != SW_FALSE;
        vector<uint8>               text;
        Internal::SourceKey         sourceKey;
        Internal::CompilerRunResult runResult = Internal::CompilerRunResult::Succeeded;
        if ( bDebugCodegen == false )
        {
            runResult = Internal::runCompiler( setup, Internal::CompileStep::Preprocess, text, result );
            if ( runResult == Internal::CompilerRunResult::Succeeded )
            {
                sourceKey = Internal::makeSourceKey( setup, text );
                if ( setup._action._bUseCache && setup._pCache->find( sourceKey._key, sourceKey._description, result._bytecode ) && result._bytecode.empty() == false )
                {
                    result._bSuccess = true;
                    Internal::finishCompile( *setup._pCache, setup._action, job, result );
                    return result;
                }
                result._bytecode.clear();
            }
        }
        if ( runResult == Internal::CompilerRunResult::Succeeded )
        {
            // 캐시에 없어 실제로 컴파일하는 것만 알린다 — 빈 캐시의 첫 실행은 수 분 동안 이 줄만 나온다. 이 줄이 "멈춘 것이 아니라 일하는 중" 의 증거다
            // (백엔드마다 실행의 정지 감시가 자식 로그가 자라는지로 본다 — AutomationPerBackend). 캐시가 찬 실행은 한 줄도 내지 않는다(언리얼 셰이더 컴파일 진행 줄과 같은 자리).
            [[maybe_unused]] const int64 compileStartNanos = MonotonicClock::nowNanoseconds();
            // 위 줄은 컴파일이 끝나야 찍힌다 — 한 번이 정지 시한보다 긴 컴파일(fxc 의 큰 셰이더)도 일하는 중임을 간격마다 알린다.
            StringBuilder<constant::buffer::kMax256> progressBuilder;
            progressBuilder.appendFormat( "%# %# (%#) compile", setup._desc._source.c_str(), setup._desc._entryPoint, setup._profile );
            const ScopedProgressReport progress{ string( progressBuilder.view() ) };
            runResult = Internal::runCompiler( setup, bDebugCodegen ? Internal::CompileStep::CompileSource : Internal::CompileStep::CompilePreprocessed, text, result );
            if ( runResult == Internal::CompilerRunResult::Succeeded )
            {
                Internal::finishCompile( *setup._pCache, setup._action, job, result );
                if ( setup._action._bUseCache && bDebugCodegen == false && setup._pCache->store( sourceKey._key, sourceKey._description, result._bytecode ) == false )
                    SW_LOG_WARNING_ID( "ShaderCompiler.CouldNotWriteShader", "Could not write the shader DDC entry or record of key %#", sourceKey._key );
                SW_LOG_INFO_ID( "ShaderCompiler.CompiledOnCacheMiss", "Compiled %# %# (%#) in %# ms", setup._desc._source.c_str(), setup._desc._entryPoint.c_str(), setup._profile.c_str(),
                                ( MonotonicClock::nowNanoseconds() - compileStartNanos ) / constant::time::kNanosecondsPerMillisecond );
                return result;
            }
        }
        if ( runResult == Internal::CompilerRunResult::Rejected )
            return result;
        // 컴파일러가 출력을 못 냈으면 실패다 - 예전에는 DX12 타깃을 FXC 로 다시 컴파일했지만 FXC 는 SM6 을 못 해 늘 실패했고, DXC 의 진짜 까닭을 FXC 의 글로 덮었다.
        if ( result._errorMessage.empty() )
            result._errorMessage = "The shader compiler produced no output (see the error above)";
        SW_LOG_ERROR_ID( "ShaderCompiler.Path", "%# (Path: %#)", result._errorMessage.c_str(), setup._desc._source.c_str() );
        return result;
    }

    void ShaderCompiler::compileDistinct( const vector<ShaderCompileDesc>& listDesc )
    {
        using Internal = ShaderCompilerInternal;
        vector<uint64>               listKey( listDesc.size(), 0 );
        const Internal::SourceKeyJob keyJob{ &listDesc };
        engine::runParallelInto<uint64>( listKey, 1, keyJob );

        vector<const ShaderCompileDesc*> listDistinct;
        unordered_set<uint64>            uniqueKey;
        for ( size_t index = 0; index < listDesc.size(); ++index )
        {
            if ( listKey[index] != 0 && uniqueKey.insert( listKey[index] ).second )
                listDistinct.push_back( &listDesc[index] );
        }
        vector<uint8>                      listDone( listDistinct.size(), 0 );
        const Internal::DistinctCompileJob compileJob{ &listDistinct };
        engine::runParallelInto<uint8>( listDone, 1, compileJob );
    }

    string ShaderCompiler::normalizePreprocessedText( string_view text )
    {
        constexpr string_view kLineDirective = "#line";
        string                normalized;
        normalized.reserve( text.size() );
        utf8   pendingSeparator = 0; // 다음 글자 앞에 쓸 빈칸 하나 또는 줄바꿈 하나
        bool   bLineStart       = true;
        size_t index            = 0;
        while ( index < text.size() )
        {
            const utf8 character = text[index];
            if ( character == '\n' )
            {
                pendingSeparator = '\n';
                bLineStart       = true;
                ++index;
                continue;
            }
            if ( character == ' ' || character == '\t' || character == '\r' )
            {
                if ( pendingSeparator == 0 )
                    pendingSeparator = ' ';
                ++index;
                continue;
            }
            if ( bLineStart && text.substr( index, kLineDirective.size() ) == kLineDirective )
            {
                while ( index < text.size() && text[index] != '\n' )
                    ++index;
                continue;
            }
            bLineStart = false;
            if ( pendingSeparator != 0 && normalized.empty() == false )
                normalized.push_back( pendingSeparator );
            pendingSeparator = 0;
            normalized.push_back( character );
            ++index;
        }
        return normalized;
    }
} // namespace sw
