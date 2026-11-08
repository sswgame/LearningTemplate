#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingLayoutCache.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingValidatorIssue.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"

namespace sw
{
    namespace
    {
        struct LayoutCacheInternal
        {
            /** @brief "NAME" 또는 "NAME=VALUE" 문자열을 ShaderMacroDefine 으로 바꿉니다. */
            static void fillDefines( const vector<string>& listDefineText, vector<ShaderMacroDefine>& outListDefine )
            {
                for ( const string& define : listDefineText )
                {
                    if ( define.empty() )
                        continue;
                    outListDefine.push_back( ShaderMacroDefine::parse( define ) );
                }
            }

            /** @brief 한 스테이지를 컴파일하고 리플렉션합니다. 실패하면 false 입니다. */
            static bool reflectStage( string_view shaderPath, string_view entryPoint, ShaderStage stage,
                                      ShaderTargetFormat targetFormat, const vector<ShaderMacroDefine>& listDefine,
                                      ShaderReflectionData& outReflection )
            {
                ShaderCompileDesc compileDesc{};
                compileDesc._source       = ShaderSourceID{ shaderPath };
                compileDesc._entryPoint   = string( resolveEntryPoint( entryPoint, stage ) );
                compileDesc._stage        = stage;
                compileDesc._targetFormat = targetFormat;
                compileDesc._listDefine   = listDefine;

                // 출처(쿠킹된 매니페스트 · 실행 중 바이트코드)는 셰이더 캐시가 기동 때 고른 공급자 하나가 정한다.
                if ( engine::getSubsystem<ShaderCache>().getReflection( compileDesc, outReflection ) == false )
                    return false;

                // 리플렉션을 얻는 순간 계약과 대조한다. 어긋나면 로그에 이름과 숫자로 남는다(검증 에러는 안 난다).
                ShaderBindingValidatorIssue::validate( outReflection, targetFormat, shaderPath );
                return true;
            }
        };
    } // namespace

    hashed_string ShaderBindingLayoutCache::makeCacheKey( const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat ) const
    {
        string combined;
        combined.reserve( 128 );
        combined += "vs:";
        combined += desc._vertexShaderPath;
        combined += "|ps:";
        combined += desc._pixelShaderPath;
        combined += "|cs:";
        combined += desc._computeShaderPath;
        combined += "|hs:";
        combined += desc._hullEntryPoint;
        combined += "|ds:";
        combined += desc._domainEntryPoint;
        combined += "|fmt:";
        combined += to_string( static_cast<uint32>( targetFormat ) );

        vector<string> listSortedDefine = desc._listShaderDefine;
        VectorUtil::sort( listSortedDefine );
        for ( const string& define : listSortedDefine )
        {
            combined += "|";
            combined += define;
        }
        return hashed_string( combined.c_str() );
    }

    const ShaderBindingLayout& ShaderBindingLayoutCache::getOrBuild( const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat )
    {
        SW_MEMORY_SCOPE( Shader );
        const hashed_string key = makeCacheKey( desc, targetFormat );

        {
            ScopedLock<mutex> lock{ _mutex };
            auto              it = _mapEntry.find( key );
            if ( it != _mapEntry.end() )
                return it->second->_layout;
        }

        vector<ShaderMacroDefine> listDefine;
        LayoutCacheInternal::fillDefines( desc._listShaderDefine, listDefine );

        vector<pair<ShaderStage, const ShaderReflectionData*>> listStage;
        ShaderReflectionData                                   vsReflection{};
        ShaderReflectionData                                   psReflection{};
        ShaderReflectionData                                   csReflection{};
        ShaderReflectionData                                   hsReflection{};
        ShaderReflectionData                                   dsReflection{};
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
            // 헐 · 도메인만 읽는 자원(변위 머티리얼의 높이 맵 · 머티리얼 버퍼 — 깊이 패스에는 픽셀 스테이지가 없다)도 묶이게 두 스테이지를 함께 본다. 소스는 정점과 같은 파일이다.
            const bool bTessellation = desc._hullEntryPoint.empty() == false && desc._domainEntryPoint.empty() == false;
            if ( bTessellation &&
                 LayoutCacheInternal::reflectStage( desc._vertexShaderPath, desc._hullEntryPoint, ShaderStage::Hull, targetFormat, listDefine, hsReflection ) )
                listStage.push_back( { ShaderStage::Hull, &hsReflection } );
            if ( bTessellation &&
                 LayoutCacheInternal::reflectStage( desc._vertexShaderPath, desc._domainEntryPoint, ShaderStage::Domain, targetFormat, listDefine, dsReflection ) )
                listStage.push_back( { ShaderStage::Domain, &dsReflection } );
        }

        unique_ptr<CacheEntry> entry = make_unique<CacheEntry>();
        entry->_layout               = ShaderBindingLayout::make( listStage );
        entry->_listSourcePath       = std::move( listSourcePath );

        ScopedLock<mutex> lock{ _mutex };
        auto              it = _mapEntry.find( key );
        if ( it == _mapEntry.end() )
            it = _mapEntry.emplace( key, std::move( entry ) ).first;
        return it->second->_layout;
    }

    void ShaderBindingLayoutCache::invalidateByShaderPath( string_view shaderRelativePath )
    {
        const string needle{ shaderRelativePath };

        ScopedLock<mutex> lock{ _mutex };
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
        ScopedLock<mutex> lock{ _mutex };
        _mapEntry.clear();
    }
} // namespace sw
