#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingLayoutCache.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingContract.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

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
                    outListDefine.push_back( ShaderMacroDefine::parse( define ) );
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

                // 매니페스트 우선 / 개발 빌드는 런타임 리플렉션 폴백 — 정책은 ShaderReflectionLibrary 한 곳에 있다.
                if ( ShaderReflectionLibrary::getOrReflect( compileDesc, outReflection ) == false )
                    return false;

                // 리플렉션을 얻는 순간 계약과 대조한다 — 어긋나면 로그에 이름·숫자로 남는다 (검증 에러는 안 난다).
                ShaderBindingContract::validate( outReflection, targetFormat, shaderPath );
                return true;
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
} // namespace sw
