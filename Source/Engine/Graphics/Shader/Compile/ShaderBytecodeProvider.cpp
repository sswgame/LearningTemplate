#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderBytecodeProvider.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderCache" );

    namespace
    {
        struct ShaderBytecodeProviderInternal
        {
            /** @brief 쿠커가 쓰는 파일 이름 그대로입니다(스템, 스테이지, 진입점, 퍼뮤테이션 해시). 규칙은 한 곳(ShaderCooker)뿐입니다. */
            static string makeBinaryFileName( const ShaderCompileDesc& desc )
            {
                return ShaderCooker::computeBinaryFileName( desc._source.getStemLower(), desc._stage, desc._entryPoint,
                                                            ShaderCooker::computePermutationHash( desc._listDefine ),
                                                            ShaderCooker::getExtensionForFormat( desc._targetFormat ) );
            }

            /** @brief 실패 결과 하나를 만듭니다. */
            static ShaderCompileResult makeFailure( string errorMessage )
            {
                ShaderCompileResult failedResult{};
                failedResult._bSuccess     = false;
                failedResult._errorMessage = std::move( errorMessage );
                return failedResult;
            }
        };
    } // namespace

    ShaderCompileResult CookedShaderBytecodeProvider::load( const ShaderCompileDesc& desc ) const
    {
        using Internal                                     = ShaderBytecodeProviderInternal;
        const std::optional<ShaderBinaryLocation> location = ShaderBinaryLocation::make( desc._source, desc._targetFormat );
        if ( location.has_value() == false )
        {
            SW_LOG_ERROR( "Shader source is not inside a domain 'shaders' folder: '%#'", desc._source.c_str() );
            return Internal::makeFailure( "Shader source is not inside a domain 'shaders' folder" );
        }
        const AssetID precookedID = location->makeFileID( Internal::makeBinaryFileName( desc ) );
        vector<uint8> precookedBytes;
        if ( AssetIO::readBinaryAsset( precookedID, precookedBytes ) == false || precookedBytes.empty() )
        {
            // 빠진 것은 파일이 아니라 (경로, 스테이지, 진입점, 퍼뮤테이션) 조합이다. 찾던 이름(RHI 폴더 포함) 그대로 적는다.
            SW_LOG_ERROR( "Precompiled shader binary not found in the cooked content: '%#' [%#] — %#", desc._source.c_str(), desc._entryPoint.c_str(), precookedID.c_str() );
            return Internal::makeFailure( "Shader binary missing in the cooked content" );
        }
        SW_LOG_TRACE( "Loaded cooked shader binary: %# (%zu bytes)", precookedID.c_str(), precookedBytes.size() );
        ShaderCompileResult result{};
        result._bytecode = std::move( precookedBytes );
        result._bSuccess = true;
        return result;
    }

    uint64 CookedShaderBytecodeProvider::computeFingerprint( const ShaderCompileResult& result ) const
    {
        // 팩은 실행 중 바뀌지 않는다 - 넣은 항목은 늘 맞다(예전에는 소스 경로가 없어 지문이 0 이라 한 번도 맞지 않고 매번 팩을 다시 읽었다).
        (void)result;
        return 1u;
    }

    bool CookedShaderBytecodeProvider::getReflection( ShaderCache& cache, const ShaderCompileDesc& desc, ShaderReflectionData& outReflection ) const
    {
        (void)cache;
        // 쿠킹 시점에 뽑아 둔 매니페스트만 읽는다. DXIL 리플렉션은 dxcompiler.dll 이 필요해서 실행 중에 하면 컴파일러를 같이 배포해야 한다.
        if ( ShaderReflectionLibrary::tryGet( desc, outReflection ) )
            return true;
        // 빠진 것은 파일이 아니라 (경로 · 스테이지 · 진입점 · 퍼뮤테이션) 조합이다. 쿠커가 쓰는 파일 이름 그대로
        // 적어야 "그 셰이더는 쿠킹했는데?" 로 끝나지 않는다. define 목록까지 붙여 어느 조합인지 바로 보이게 한다.
        const string missingKey = ShaderBytecodeProviderInternal::makeBinaryFileName( desc );
        string       defineList;
        for ( const ShaderMacroDefine& define : desc._listDefine )
        {
            if ( defineList.empty() == false )
                defineList += ' ';
            defineList += define._name;
            defineList += '=';
            defineList += define._value;
        }
        SW_LOG_ERROR( "The reflection manifest has no entry for '%#' (%#, define: %#) - the shader must be re-cooked.", desc._source.c_str(), missingKey.c_str(), defineList.c_str() );
        return false;
    }

    ShaderCompileResult LiveShaderBytecodeProvider::load( const ShaderCompileDesc& desc ) const
    {
        // 컴파일러가 이 기계의 셰이더 DDC(`Saved/ShaderCompileCache`)를 먼저 본다(언리얼 에디터의 DDC). 코드젠은 `gv_shaderDebugCodegen` 이 정한다(makeLiveCompileDesc).
        return ShaderCompiler::compileHLSL( ShaderCache::makeLiveCompileDesc( desc ) );
    }

    uint64 LiveShaderBytecodeProvider::computeFingerprint( const ShaderCompileResult& result ) const
    {
        return result._sourceFile.empty() ? 0u : ShaderCooker::computeSourceFingerprint( result._sourceFile, result._listIncludedFile );
    }

    bool LiveShaderBytecodeProvider::getReflection( ShaderCache& cache, const ShaderCompileDesc& desc, ShaderReflectionData& outReflection ) const
    {
        // 쿠킹된 매니페스트는 읽지 않는다 - 바이트코드를 실행 중에 얻으므로 리플렉션도 그 바이트코드에서 만든다. 쿠킹본과 섞으면 레이아웃과 바이트코드가 어긋난다
        // (바인더가 슬롯을 빼먹어 DX12 가 빈 루트 디스크립터를 읽고 DEVICE_HUNG 으로 죽은 적이 있다).
        const ShaderCompileResult result = cache.getOrCompile( desc );
        if ( result._bSuccess == false || result._bytecode.empty() )
        {
            SW_LOG_WARNING( "Runtime shader reflection failed: '%#' (%#)", desc._source.c_str(), result._errorMessage.c_str() );
            return false;
        }
        outReflection = ShaderReflection::reflect( result._bytecode, desc._targetFormat );
        return true;
    }
} // namespace sw
