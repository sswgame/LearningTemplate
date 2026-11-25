#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#include "Core/Container/EnumArray.h"
#include "Core/Container/unordered_set.h"
#include "Core/Diagnostics/ScopedProgressReport.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Content/CookJob.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompilerBackend.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Compile/ShaderReachableText.h"

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
                action._bUseCache = true;
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
                optional<ShaderSourceResolver> resolver = ShaderSourceResolver::make( engine::getSubsystem<AssetPaths>() );
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
                const uint64  sourceHash = ContentHash::computeFile( input->_source );
                CompileAction action     = makeCompileAction( desc, sourceHash );
                string        profile    = ShaderBackendCall::getTargetProfile( desc._stage, desc._targetFormat );
                const auto    pResolver  = sw::make_shared<const ShaderSourceResolver>( std::move( *resolver ) );
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

            /** @brief 요청 @p requestDesc 를 전처리한 글의 키입니다. DDC 의 기록이 이미 새것이거나, 글로 겹치지 않는 요청(디버그 코드젠 · 캐시하지 않는 것)이거나, 전처리하지 못하면 0 입니다. */
            static uint64 computeMissingSourceKey( const ShaderCompileDesc& requestDesc )
            {
                const expected<CompileSetup> setup = setUp( requestDesc );
                if ( setup.has_value() == false || setup->_action._bUseCache == false || setup->_call._desc._bDebugCodegen != SW_FALSE )
                    return 0;
                CookRecord record;
                if ( setup->_pCache->readRecord( setup->_action._actionKey, record ) && record.isFresh( setup->_action._jobDesc ) )
                    return 0;
                const CookJob       job{ setup->_action._jobDesc }; // 쿠킹 세션은 작업 밖의 읽기(include)를 거절한다
                vector<uint8>       text;
                ShaderCompileResult result{};
                return setup->_call.run( ShaderBackendStep::Preprocess, text, result ) == ShaderBackendRunResult::Succeeded ? makeSourceKey( *setup, text )._key : 0;
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
        const Internal::CompileAction action = Internal::makeCompileAction( Internal::makeFullDesc( requestDesc ), ContentHash::computeFile( input->_source ) );
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
        if ( job.recordDiskInput( setup._call._input._source, setup._sourceHash ) == false )
        {
            result._errorMessage = "The shader source could not be recorded as a cook input (see the error above).";
            return result;
        }

        // 같은 이름의 헤더가 엔진 헤더를 가리면 어느 쪽이 쓰였는지 알 수 없다 — 실제로 컴파일하기 전에 오류로 멈춘다. DDC 적중 뒤에 본다: 폴더를 재귀로 훑는 검사라
        // 요청마다 하면 적중도 느려지고, 맞은 항목의 기록은 없던 후보 자리까지 적어 가리는 헤더가 새로 생기면 이미 낡음이다(`CookRecord::isFresh`).
        // 언리얼도 셰이더 맵 DDC 조회가 컴파일 준비(include 검사)보다 먼저다.
        vector<string> listShadowed;
        if ( setup._call._pIncludeResolver->findShadowedHeaders( setup._call._input._shadersFolder, listShadowed ) == false )
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
        const bool             bDebugCodegen = setup._call._desc._bDebugCodegen != SW_FALSE;
        vector<uint8>          text;
        Internal::SourceKey    sourceKey;
        ShaderBackendRunResult runResult = ShaderBackendRunResult::Succeeded;
        if ( bDebugCodegen == false )
        {
            runResult = setup._call.run( ShaderBackendStep::Preprocess, text, result );
            if ( runResult == ShaderBackendRunResult::Succeeded )
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
        if ( runResult == ShaderBackendRunResult::Succeeded )
        {
            // 캐시에 없어 실제로 컴파일하는 것만 알린다 — 빈 캐시의 첫 실행은 수 분 동안 이 줄만 나온다. 이 줄이 "멈춘 것이 아니라 일하는 중" 의 증거다
            // (백엔드마다 실행의 정지 감시가 자식 로그가 자라는지로 본다 — AutomationPerBackend). 캐시가 찬 실행은 한 줄도 내지 않는다(언리얼 셰이더 컴파일 진행 줄과 같은 자리).
            [[maybe_unused]] const int64 compileStartNanos = MonotonicClock::nowNanoseconds();
            // 위 줄은 컴파일이 끝나야 찍힌다 — 한 번이 정지 시한보다 긴 컴파일(fxc 의 큰 셰이더)도 일하는 중임을 간격마다 알린다.
            StringBuilder<constant::buffer::kMax256> progressBuilder;
            progressBuilder.appendFormat( "%# %# (%#) compile", setup._call._desc._source.c_str(), setup._call._desc._entryPoint, setup._call._profile );
            const ScopedProgressReport progress{ string( progressBuilder.view() ) };
            runResult = setup._call.run( bDebugCodegen ? ShaderBackendStep::CompileSource : ShaderBackendStep::CompilePreprocessed, text, result );
            if ( runResult == ShaderBackendRunResult::Succeeded )
            {
                Internal::finishCompile( *setup._pCache, setup._action, job, result );
                if ( setup._action._bUseCache && bDebugCodegen == false && setup._pCache->store( sourceKey._key, sourceKey._description, result._bytecode ) == false )
                    SW_LOG_WARNING_ID( "ShaderCompiler.CouldNotWriteShader", "Could not write the shader DDC entry or record of key %#", sourceKey._key );
                SW_LOG_INFO_ID( "ShaderCompiler.CompiledOnCacheMiss", "Compiled %# %# (%#) in %# ms", setup._call._desc._source.c_str(), setup._call._desc._entryPoint.c_str(), setup._call._profile.c_str(),
                                ( MonotonicClock::nowNanoseconds() - compileStartNanos ) / constant::time::kNanosecondsPerMillisecond );
                return result;
            }
        }
        if ( runResult == ShaderBackendRunResult::Rejected )
            return result;
        // 컴파일러가 출력을 못 냈으면 실패다 - 예전에는 DX12 타깃을 FXC 로 다시 컴파일했지만 FXC 는 SM6 을 못 해 늘 실패했고, DXC 의 진짜 까닭을 FXC 의 글로 덮었다.
        if ( result._errorMessage.empty() )
            result._errorMessage = "The shader compiler produced no output (see the error above)";
        SW_LOG_ERROR_ID( "ShaderCompiler.Path", "%# (Path: %#)", result._errorMessage.c_str(), setup._call._desc._source.c_str() );
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
} // namespace sw
