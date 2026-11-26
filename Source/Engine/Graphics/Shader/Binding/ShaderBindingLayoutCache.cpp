#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingLayoutCache.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
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

            /**
             * @brief SPIR-V 의 구조 버퍼 번호(종류별 시프트 + 레지스터)를 레지스터 번호로 되돌리고 읽기 · 쓰기를 번호 띠로 가릅니다.
             * @details 바인더는 레지스터 번호로 건다(`IRHIPassCommandList::bindStructuredBuffer`) — WebGPU 리플렉션이 이미 하는 일을 Vulkan(t 16.. · u 32..) · GL(u 16..)도 여기서 같게 한다.
             *          예전에는 Vulkan 커맨드 컨텍스트가 받은 번호의 범위로 어림했고, 버퍼 보기는 Vulkan · GL 의 컴퓨트 슬롯 이름을 찾지 못했다. 계약 검사(`ShaderBindingValidatorIssue`)는 네이티브 번호를 본 뒤다.
             */
            static void toRegisterNumbers( ShaderTargetFormat targetFormat, ShaderReflectionData& inoutReflection )
            {
                const bool   bVulkan = targetFormat == ShaderTargetFormat::SPIRV_Vulkan;
                const bool   bOpenGL = targetFormat == ShaderTargetFormat::SPIRV_OpenGL;
                const uint32 uShift  = bVulkan ? shaderslot::vk::kUShift : shaderslot::gl::kUAVBinding0;
                const uint32 tShift  = bVulkan ? shaderslot::vk::kTShift : 0u;
                if ( bVulkan == false && bOpenGL == false )
                    return;
                for ( ShaderReflectedBinding& resource : inoutReflection._listResource )
                {
                    // GL 의 컴퓨트 RW 텍스처 칸은 이미지 유닛 번호다(common.hlsli 의 명시 binding) — 레지스터 u5..u7 로 되돌린다(컴퓨트 매개변수 표가 그 번호로 건다).
                    if ( bOpenGL && resource._kind == ShaderBindingKind::RwTexture && resource._registerSpace == 0 && resource._bindPoint >= shaderslot::gl::kImageUnit0 )
                    {
                        resource._bindPoint = shaderslot::kComputeTextureUAV0 + ( resource._bindPoint - shaderslot::gl::kImageUnit0 );
                        continue;
                    }
                    if ( resource._kind != ShaderBindingKind::StructuredBuffer || resource._registerSpace != 0 )
                        continue;
                    const bool bWritable = resource._bindPoint >= uShift;
                    resource._kind       = bWritable ? ShaderBindingKind::RwStructuredBuffer : ShaderBindingKind::StructuredBuffer;
                    resource._bindPoint -= bWritable ? uShift : MathUtil::min( tShift, resource._bindPoint );
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
                toRegisterNumbers( targetFormat, outReflection );
                return true;
            }
        };
    } // namespace

    hashed_string ShaderBindingLayoutCache::makeCacheKey( const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat )
    {
        // 구분자는 경로 · 식별자 · define 값에 올 수 없는 줄바꿈이다(`ShaderCache::makeCacheKey` 와 같은 규칙) — `|` 로 이으면 값 안의 `|` 가 칸 경계와 같아졌다.
        // 진입점도 키에 든다 — 같은 파일의 다른 진입점은 다른 리플렉션이다.
        constexpr utf8                           kSeparator = '\n';
        StringBuilder<constant::buffer::kMax512> combined;
        combined.append( desc._vertexShaderPath ).append( kSeparator ).append( desc._vertexEntryPoint ).append( kSeparator );
        combined.append( desc._pixelShaderPath ).append( kSeparator ).append( desc._pixelEntryPoint ).append( kSeparator );
        combined.append( desc._computeShaderPath ).append( kSeparator ).append( desc._computeEntryPoint ).append( kSeparator );
        combined.append( desc._geometryEntryPoint ).append( kSeparator ).append( desc._hullEntryPoint ).append( kSeparator ).append( desc._domainEntryPoint ).append( kSeparator );
        combined.append( static_cast<uint32>( targetFormat ) );

        vector<string> listSortedDefine = desc._listShaderDefine;
        VectorUtil::sort( listSortedDefine );
        for ( const string& define : listSortedDefine )
        {
            combined.append( kSeparator );
            combined.append( define );
        }
        return hashed_string( combined.c_str() );
    }

    const ShaderBindingLayout& ShaderBindingLayoutCache::getOrBuild( const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat )
    {
        const ScopedMemoryTag memoryScope{ kMemoryTag };
        const hashed_string   key = makeCacheKey( desc, targetFormat );

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

        // 무효화 매칭용 경로는 리플렉션 성패와 무관하게 적는다 — 셰이더가 깨진 동안 지은 빈 레이아웃도 고친 뒤의 재컴파일이 버려야 한다.
        const bool bCompute = desc._computeShaderPath.empty() == false;
        if ( bCompute )
        {
            listSourcePath.push_back( desc._computeShaderPath );
            if ( LayoutCacheInternal::reflectStage( desc._computeShaderPath, desc._computeEntryPoint, ShaderStage::Compute,
                                                    targetFormat, listDefine, csReflection ) )
                listStage.push_back( { ShaderStage::Compute, &csReflection } );
        }
        else
        {
            if ( desc._vertexShaderPath.empty() == false )
                listSourcePath.push_back( desc._vertexShaderPath );
            if ( desc._pixelShaderPath.empty() == false && desc._pixelShaderPath != desc._vertexShaderPath )
                listSourcePath.push_back( desc._pixelShaderPath );
            if ( desc._vertexShaderPath.empty() == false &&
                 LayoutCacheInternal::reflectStage( desc._vertexShaderPath, desc._vertexEntryPoint, ShaderStage::Vertex,
                                                    targetFormat, listDefine, vsReflection ) )
                listStage.push_back( { ShaderStage::Vertex, &vsReflection } );
            if ( desc._pixelShaderPath.empty() == false &&
                 LayoutCacheInternal::reflectStage( desc._pixelShaderPath, desc._pixelEntryPoint, ShaderStage::Pixel,
                                                    targetFormat, listDefine, psReflection ) )
                listStage.push_back( { ShaderStage::Pixel, &psReflection } );
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

    bool ShaderBindingLayoutCache::matchesShaderPath( string_view sourcePath, string_view shaderPath )
    {
        if ( sourcePath.empty() || shaderPath.empty() )
            return false;
        return sourcePath.find( shaderPath ) != string_view::npos || shaderPath.find( sourcePath ) != string_view::npos;
    }

    void ShaderBindingLayoutCache::invalidateByShaderPath( string_view shaderRelativePath )
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( auto it = _mapEntry.begin(); it != _mapEntry.end(); )
        {
            bool bMatch = false;
            for ( const string& source : it->second->_listSourcePath )
            {
                if ( matchesShaderPath( source, shaderRelativePath ) )
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
