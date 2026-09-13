#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/RenderPsoCache.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"

namespace sw
{
    RenderPsoCache::RenderPsoCache()
        : _bindingLayoutCache{}
        , _mapPsoLayout{}
        , _mapPsoDesc{}
        , _layoutMutex{}
        , _mapEnginePso{}
        , _mapPresentPso{}
        , _mapMaterialPso{}
        , _materialPsoMutex{}
    {
    }

    void RenderPsoCache::registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc, RHIBackend backend )
    {
        if ( pso == 0 )
            return;
        const ShaderBindingLayout& layout = _bindingLayoutCache.getOrBuild( desc, backend );
        std::scoped_lock<mutex>    lock{ _layoutMutex };
        _mapPsoLayout[pso] = &layout;
        _mapPsoDesc[pso]   = desc;
    }

    const ShaderBindingLayout* RenderPsoCache::findLayout( RHIPipelineStateHandle pso ) const
    {
        std::scoped_lock<mutex> lock{ _layoutMutex };
        const auto              it = _mapPsoLayout.find( pso );
        return it != _mapPsoLayout.end() ? it->second : nullptr;
    }

    bool RenderPsoCache::findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        std::scoped_lock<mutex> lock{ _layoutMutex };
        const auto              it = _mapPsoDesc.find( pso );
        if ( it == _mapPsoDesc.end() )
            return false;
        outDesc = it->second;
        return true;
    }

    void RenderPsoCache::invalidateLayoutsByShaderPath( string_view shaderPath )
    {
        _bindingLayoutCache.invalidateByShaderPath( shaderPath );
    }

    void RenderPsoCache::collectLayouts( vector<RegisteredLayout>& outListLayout ) const
    {
        std::scoped_lock<mutex> lock{ _layoutMutex };
        outListLayout.clear();
        outListLayout.reserve( _mapPsoLayout.size() );
        for ( const auto& [pso, pLayout] : _mapPsoLayout )
            outListLayout.push_back( RegisteredLayout{ pso, pLayout } );
    }

    void RenderPsoCache::setEnginePso( RenderPassType passType, RHIPipelineStateHandle pso )
    {
        _mapEnginePso.insert_or_assign( passType, pso );
    }

    RHIPipelineStateHandle RenderPsoCache::findEnginePso( RenderPassType passType ) const
    {
        const auto it = _mapEnginePso.find( passType );
        return ( it != _mapEnginePso.end() ) ? it->second : 0;
    }

    void RenderPsoCache::setPresentPso( RHIFormat targetFormat, RHIPipelineStateHandle pso )
    {
        _mapPresentPso.insert_or_assign( targetFormat, pso );
    }

    bool RenderPsoCache::findPresentPso( RHIFormat targetFormat, RHIPipelineStateHandle& outPso ) const
    {
        const auto it = _mapPresentPso.find( targetFormat );
        if ( it == _mapPresentPso.end() )
            return false;
        outPso = it->second;
        return true;
    }

    uint64 RenderPsoCache::materialPsoKey( RHIPipelineStateHandle passPso, uint64 permutationHash, RenderViewMode viewMode )
    {
        uint64 key = static_cast<uint64>( passPso ) * 0x9e3779b97f4a7c15ull;
        key ^= permutationHash + 0x9e3779b97f4a7c15ull + ( key << 6 ) + ( key >> 2 );
        key ^= ( static_cast<uint64>( viewMode ) + 1 ) * 0xff51afd7ed558ccdull;
        return key;
    }

    bool RenderPsoCache::hasMaterialPso( uint64 key ) const
    {
        std::scoped_lock<mutex> lock{ _materialPsoMutex };
        return _mapMaterialPso.find( key ) != _mapMaterialPso.end();
    }

    void RenderPsoCache::setMaterialPso( uint64 key, const MaterialPsoEntry& entry )
    {
        std::scoped_lock<mutex> lock{ _materialPsoMutex };
        _mapMaterialPso.insert_or_assign( key, entry );
    }

    RHIPipelineStateHandle RenderPsoCache::findMaterialPso( uint64 key ) const
    {
        std::scoped_lock<mutex> lock{ _materialPsoMutex };
        const auto              it = _mapMaterialPso.find( key );
        return ( it != _mapMaterialPso.end() ) ? it->second._pso : 0;
    }

    void RenderPsoCache::releaseAll( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || pDevice->getResource() == nullptr )
        {
            forgetAll();
            return;
        }

        // 퍼뮤테이션 변형을 **패스 PSO 보다 먼저** 파괴한다. `_bOwned` 가 0 인 항목은 패스 PSO 를 그대로
        // 담고 있을 뿐이라 여기서 파괴하면 두 번 파괴하는 셈이 된다.
        {
            std::scoped_lock<mutex> lock{ _materialPsoMutex };
            for ( auto& [key, entry] : _mapMaterialPso )
            {
                if ( entry._bOwned != 0 && entry._pso != 0 )
                    pDevice->getResource()->destroyPipelineState( entry._pso );
            }
            _mapMaterialPso.clear();
        }

        for ( auto& [passType, pso] : _mapEnginePso )
        {
            if ( pso != 0 )
                pDevice->getResource()->destroyPipelineState( pso );
        }
        _mapEnginePso.clear();

        for ( auto& [format, pso] : _mapPresentPso )
        {
            if ( pso != 0 )
                pDevice->getResource()->destroyPipelineState( pso );
        }
        _mapPresentPso.clear();

        // 두 맵은 방금 파괴한 PSO 핸들로 키를 잡고 있다. 핸들이 generation 팩드라 되살아난
        // 핸들이 옛 항목을 집는 일은 없지만, 셰이더 리로드마다 재생성을 도는 지금은 그대로 두면
        // 죽은 항목(RHIPipelineStateDesc 통째)이 계속 쌓인다.
        {
            std::scoped_lock<mutex> lock{ _layoutMutex };
            _mapPsoLayout.clear();
            _mapPsoDesc.clear();
        }
    }

    void RenderPsoCache::forgetAll()
    {
        _mapEnginePso.clear();
        _mapPresentPso.clear();
        {
            std::scoped_lock<mutex> lock{ _materialPsoMutex };
            _mapMaterialPso.clear();
        }
        {
            std::scoped_lock<mutex> lock{ _layoutMutex };
            _mapPsoLayout.clear();
            _mapPsoDesc.clear();
        }
    }
} // namespace sw
