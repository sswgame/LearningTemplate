#include "pch.h"

#include "Engine/Renderer/Frame/RenderPSOCache.h"

#include "Core/Common/HashUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"

namespace sw
{
    RenderPSOCache::RenderPSOCache()
        : _bindingLayoutCache{}
        , _mapPSOLayout{}
        , _mapPSODesc{}
        , _layoutMutex{}
        , _mapEnginePSO{}
        , _mapOutputPSO{}
        , _mapMaterialPSO{}
        , _materialPSOMutex{}
    {
    }

    void RenderPSOCache::registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc, RHIBackend backend )
    {
        if ( pso == 0 )
            return;
        const ShaderBindingLayout& layout = _bindingLayoutCache.getOrBuild( desc, backend );
        std::scoped_lock<mutex>    lock{ _layoutMutex };
        _mapPSOLayout[pso] = &layout;
        _mapPSODesc[pso]   = desc;
    }

    const ShaderBindingLayout* RenderPSOCache::findLayout( RHIPipelineStateHandle pso ) const
    {
        std::scoped_lock<mutex> lock{ _layoutMutex };
        const auto              it = _mapPSOLayout.find( pso );
        return it != _mapPSOLayout.end() ? it->second : nullptr;
    }

    bool RenderPSOCache::findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        std::scoped_lock<mutex> lock{ _layoutMutex };
        const auto              it = _mapPSODesc.find( pso );
        if ( it == _mapPSODesc.end() )
            return false;
        outDesc = it->second;
        return true;
    }

    void RenderPSOCache::invalidateLayoutsByShaderPath( string_view shaderPath )
    {
        _bindingLayoutCache.invalidateByShaderPath( shaderPath );
    }

    void RenderPSOCache::collectLayouts( vector<RegisteredLayout>& outListLayout ) const
    {
        std::scoped_lock<mutex> lock{ _layoutMutex };
        outListLayout.clear();
        outListLayout.reserve( _mapPSOLayout.size() );
        for ( const auto& [pso, pLayout] : _mapPSOLayout )
        {
            outListLayout.push_back( RegisteredLayout{ pso, pLayout } );
        }
    }

    void RenderPSOCache::setEnginePSO( RenderPassType passType, RHIPipelineStateHandle pso )
    {
        _mapEnginePSO.insert_or_assign( passType, pso );
    }

    RHIPipelineStateHandle RenderPSOCache::findEnginePSO( RenderPassType passType ) const
    {
        const auto it = _mapEnginePSO.find( passType );
        return ( it != _mapEnginePSO.end() ) ? it->second : 0;
    }

    void RenderPSOCache::setOutputPSO( RenderPassType passType, RHIFormat targetFormat, RHIPipelineStateHandle pso )
    {
        _mapOutputPSO.insert_or_assign( makeOutputPSOKey( passType, targetFormat ), pso );
    }

    bool RenderPSOCache::findOutputPSO( RenderPassType passType, RHIFormat targetFormat, RHIPipelineStateHandle& outPipelineState ) const
    {
        const auto it = _mapOutputPSO.find( makeOutputPSOKey( passType, targetFormat ) );
        if ( it == _mapOutputPSO.end() )
            return false;
        outPipelineState = it->second;
        return true;
    }

    uint64 RenderPSOCache::materialPSOKey( RHIPipelineStateHandle passPSO, uint64 permutationHash, RenderViewMode viewMode, bool bReverseCulling )
    {
        uint64 key = static_cast<uint64>( passPSO ) * HashUtil::kGoldenRatio64;
        key        = HashUtil::combine( key, permutationHash );
        key ^= ( static_cast<uint64>( viewMode ) + 1 ) * 0xff51afd7ed558ccdull;
        if ( bReverseCulling )
            key ^= 0xc4ceb9fe1a85ec53ull;
        return key;
    }

    bool RenderPSOCache::hasMaterialPSO( uint64 key ) const
    {
        std::scoped_lock<mutex> lock{ _materialPSOMutex };
        return _mapMaterialPSO.find( key ) != _mapMaterialPSO.end();
    }

    void RenderPSOCache::setMaterialPSO( uint64 key, const MaterialPSOEntry& entry )
    {
        std::scoped_lock<mutex> lock{ _materialPSOMutex };
        _mapMaterialPSO.insert_or_assign( key, entry );
    }

    RHIPipelineStateHandle RenderPSOCache::findMaterialPSO( uint64 key ) const
    {
        std::scoped_lock<mutex> lock{ _materialPSOMutex };
        const auto              it = _mapMaterialPSO.find( key );
        return ( it != _mapMaterialPSO.end() ) ? it->second._pso : 0;
    }

    void RenderPSOCache::releaseAll( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
        {
            forgetAll();
            return;
        }

        // 퍼뮤테이션 변형을 **패스 PSO 보다 먼저** 파괴한다. `_bOwned` 가 0 인 항목은 패스 PSO 를 그대로
        // 담고 있을 뿐이라 여기서 파괴하면 두 번 파괴하는 셈이 된다.
        {
            std::scoped_lock<mutex> lock{ _materialPSOMutex };
            for ( auto& [key, entry] : _mapMaterialPSO )
            {
                if ( entry._bOwned != 0 && entry._pso != 0 )
                    pDevice->getResourceFactory()->destroyPipelineState( entry._pso );
            }
            _mapMaterialPSO.clear();
        }

        for ( auto& [passType, pso] : _mapEnginePSO )
        {
            if ( pso != 0 )
                pDevice->getResourceFactory()->destroyPipelineState( pso );
        }
        _mapEnginePSO.clear();

        for ( auto& [key, pso] : _mapOutputPSO )
        {
            if ( pso != 0 )
                pDevice->getResourceFactory()->destroyPipelineState( pso );
        }
        _mapOutputPSO.clear();

        // 두 맵은 방금 파괴한 PSO 핸들로 키를 잡고 있다. 핸들에 generation 이 들어 있어 되살아난
        // 핸들이 옛 항목을 집는 일은 없지만, 셰이더 리로드마다 재생성을 도는 지금은 그대로 두면
        // 죽은 항목(RHIPipelineStateDesc 통째)이 계속 쌓인다.
        {
            std::scoped_lock<mutex> lock{ _layoutMutex };
            _mapPSOLayout.clear();
            _mapPSODesc.clear();
        }
    }

    void RenderPSOCache::forgetAll()
    {
        _mapEnginePSO.clear();
        _mapOutputPSO.clear();
        {
            std::scoped_lock<mutex> lock{ _materialPSOMutex };
            _mapMaterialPSO.clear();
        }
        {
            std::scoped_lock<mutex> lock{ _layoutMutex };
            _mapPSOLayout.clear();
            _mapPSODesc.clear();
        }
    }
} // namespace sw
