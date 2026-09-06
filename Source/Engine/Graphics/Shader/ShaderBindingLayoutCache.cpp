#include "pch.h"

#include "Engine/Graphics/Shader/ShaderBindingLayoutCache.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/ShaderBindingContract.h"
#include "Engine/Graphics/Shader/ShaderCache.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include "Engine/Graphics/Shader/ShaderReflectionLibrary.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderBindingLayoutCache" );

    namespace
    {
        struct LayoutCacheInternal
        {
            /** @brief "NAME" 또는 "NAME=VALUE" 문자열을 ShaderMacroDefine 으로. */
            static void fillDefines( const vector<string>& listDefineText, vector<ShaderMacroDefine>& outListDefine )
            {
                for ( const string& define : listDefineText )
                {
                    if ( define.empty() )
                        continue;
                    ShaderMacroDefine macro{};
                    const size_t      eq = define.find( '=' );
                    if ( eq == string::npos )
                    {
                        macro._name  = define;
                        macro._value = "1";
                    }
                    else
                    {
                        macro._name  = define.substr( 0, eq );
                        macro._value = define.substr( eq + 1 );
                    }
                    outListDefine.push_back( std::move( macro ) );
                }
            }

            /** @brief 한 스테이지를 컴파일·리플렉션합니다. 실패 시 false. */
            static bool reflectStage( string_view shaderPath, string_view entryPoint, ShaderStage stage,
                                      ShaderTargetFormat targetFormat, const vector<ShaderMacroDefine>& listDefine,
                                      ShaderReflectionData& outReflection )
            {
                ShaderCompileDesc compileDesc{};
                compileDesc._filePath     = shaderPath;
                compileDesc._entryPoint   = entryPoint.empty() ? string( stage == ShaderStage::Compute ? "CSMain" : ( stage == ShaderStage::Vertex ? "VSMain" : "PSMain" ) ) : string( entryPoint );
                compileDesc._stage        = stage;
                compileDesc._targetFormat = targetFormat;
                compileDesc._listDefine   = listDefine;

                // 1순위: 쿠킹 시점에 구운 리플렉션 매니페스트. 배포 빌드는 이 경로만 쓴다 —
                // DXIL 리플렉션은 dxcompiler.dll 이 필요해서 런타임에 하면 컴파일러를 같이 배포해야 한다.
                if ( ShaderReflectionLibrary::tryGet( compileDesc, outReflection ) )
                {
                    // 바이너리를 읽는 순간 계약과 대조한다 — 어긋나면 로그에 이름·숫자로 남는다 (검증 에러는 안 난다).
                    ShaderBindingContract::validate( outReflection, targetFormat, shaderPath );
                    return true;
                }

#if defined( SW_SHIPPING )
                SW_LOG_ERROR( "리플렉션 매니페스트에 '%#' 가 없습니다 — 셰이더를 다시 베이킹해야 합니다.",
                              string( shaderPath ).c_str() );
                return false;
#else
                // 2순위(개발 빌드 전용): 바이트코드를 얻어 그 자리에서 리플렉션. 셰이더를 막 고쳐
                // 아직 베이킹하지 않은 상태를 위한 폴백이다.
                const ShaderCompileResult result = engine::areEngineServicesBound()
                                                     ? engine::getShaderCache().getOrCompile( compileDesc )
                                                     : ShaderCompiler::compileHLSL( compileDesc );
                if ( result._bSuccess == false || result._bytecode.empty() )
                {
                    SW_LOG_TRACE( "Layout: stage %# compile failed for '%#' (%#).",
                                  static_cast<uint32>( stage ), string( shaderPath ).c_str(), result._errorMessage.c_str() );
                    return false;
                }

                outReflection = ShaderReflection::reflect( result._bytecode, targetFormat );
                ShaderBindingContract::validate( outReflection, targetFormat, shaderPath );
                return true;
#endif
            }
        };
    } // namespace

    hashed_string ShaderBindingLayoutCache::makeCacheKey( const RHIPipelineStateDesc& desc, RHIBackend backend ) const
    {
        string combined;
        combined.reserve( 128 );
        combined += "vs:";
        combined += desc._vertexShaderPath;
        combined += "|ps:";
        combined += desc._pixelShaderPath;
        combined += "|cs:";
        combined += desc._computeShaderPath;
        combined += "|fmt:";
        combined += to_string( static_cast<uint32>( RHI::getShaderTargetFormat( backend ) ) );

        vector<string> listSortedDefine = desc._listShaderDefine;
        std::sort( listSortedDefine.begin(), listSortedDefine.end() );
        for ( const string& define : listSortedDefine )
        {
            combined += "|";
            combined += define;
        }
        return hashed_string( combined.c_str() );
    }

    const ShaderBindingLayout& ShaderBindingLayoutCache::getOrBuild( const RHIPipelineStateDesc& desc, RHIBackend backend )
    {
        const hashed_string key = makeCacheKey( desc, backend );

        {
            std::scoped_lock<mutex> lock{ _mutex };
            auto                    it = _mapEntry.find( key );
            if ( it != _mapEntry.end() )
                return it->second->_layout;
        }

        const ShaderTargetFormat targetFormat = RHI::getShaderTargetFormat( backend );

        vector<ShaderMacroDefine> listDefine;
        LayoutCacheInternal::fillDefines( desc._listShaderDefine, listDefine );

        vector<pair<ShaderStage, const ShaderReflectionData*>> listStage;
        ShaderReflectionData                                   vsReflection{};
        ShaderReflectionData                                   psReflection{};
        ShaderReflectionData                                   csReflection{};
        vector<string>                                         listSourcePath;

        const bool bCompute = desc._computeShaderPath.empty() == false;
        if ( bCompute )
        {
            if ( LayoutCacheInternal::reflectStage( desc._computeShaderPath, desc._computeEntryPoint, ShaderStage::Compute,
                                                    targetFormat, listDefine, csReflection ) )
            {
                listStage.push_back( { ShaderStage::Compute, &csReflection } );
                listSourcePath.push_back( desc._computeShaderPath );
            }
        }
        else
        {
            if ( desc._vertexShaderPath.empty() == false &&
                 LayoutCacheInternal::reflectStage( desc._vertexShaderPath, desc._vertexEntryPoint, ShaderStage::Vertex,
                                                    targetFormat, listDefine, vsReflection ) )
            {
                listStage.push_back( { ShaderStage::Vertex, &vsReflection } );
                listSourcePath.push_back( desc._vertexShaderPath );
            }
            if ( desc._pixelShaderPath.empty() == false &&
                 LayoutCacheInternal::reflectStage( desc._pixelShaderPath, desc._pixelEntryPoint, ShaderStage::Pixel,
                                                    targetFormat, listDefine, psReflection ) )
            {
                listStage.push_back( { ShaderStage::Pixel, &psReflection } );
                if ( desc._pixelShaderPath != desc._vertexShaderPath )
                    listSourcePath.push_back( desc._pixelShaderPath );
            }
        }

        unique_ptr<CacheEntry> entry = make_unique<CacheEntry>();
        entry->_layout               = ShaderBindingLayout::build( listStage );
        entry->_listSourcePath       = std::move( listSourcePath );

        std::scoped_lock<mutex> lock{ _mutex };
        auto                    it = _mapEntry.find( key );
        if ( it == _mapEntry.end() )
            it = _mapEntry.emplace( key, std::move( entry ) ).first;
        return it->second->_layout;
    }

    void ShaderBindingLayoutCache::invalidateByShaderPath( string_view shaderRelativePath )
    {
        const string needle{ shaderRelativePath };

        std::scoped_lock<mutex> lock{ _mutex };
        for ( auto it = _mapEntry.begin(); it != _mapEntry.end(); )
        {
            bool bMatch = false;
            for ( const string& source : it->second->_listSourcePath )
            {
                if ( source.find( needle ) != string::npos || needle.find( source ) != string::npos )
                {
                    bMatch = true;
                    break;
                }
            }
            if ( bMatch )
                it = _mapEntry.erase( it );
            else
                ++it;
        }
    }

    void ShaderBindingLayoutCache::clear()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _mapEntry.clear();
    }

    uint32 ShaderBindingLayoutCache::getEntryCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return static_cast<uint32>( _mapEntry.size() );
    }
} // namespace sw
