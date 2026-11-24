#include "pch.h"

#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/ScopedDeferredLog.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/View/RenderViewMode.h"

namespace sw
{
    RenderPSOCache::RenderPSOCache()
        : _bindingLayoutCache{}
        , _mapPSOLayout{}
        , _mapPSODesc{}
        , _layoutMutex{}
        , _mapEnginePSO{}
        , _mapOutputPSO{}
        , _mapExtensionProgramPSO{}
        , _mapMaterialPSO{}
        , _materialPSOMutex{}
    {
    }

    void RenderPSOCache::registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat )
    {
        if ( pso == 0 )
            return;
        const ShaderBindingLayout& layout = _bindingLayoutCache.getOrBuild( desc, targetFormat );
        ScopedLock<shared_mutex>   lock{ _layoutMutex };
        _mapPSOLayout[pso] = &layout;
        _mapPSODesc[pso]   = desc;
    }

    const ShaderBindingLayout* RenderPSOCache::findLayout( RHIPipelineStateHandle pso ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
        const auto                     it = _mapPSOLayout.find( pso );
        return it != _mapPSOLayout.end() ? it->second : nullptr;
    }

    bool RenderPSOCache::findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
        const auto                     it = _mapPSODesc.find( pso );
        if ( it == _mapPSODesc.end() )
            return false;
        outDesc = it->second;
        return true;
    }

    RHIFormat RenderPSOCache::findDepthStencilFormat( RHIPipelineStateHandle pso ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
        const auto                     it = _mapPSODesc.find( pso );
        return it != _mapPSODesc.end() ? it->second._depthStencilFormat : RHIFormat::Unknown;
    }

    const ShaderBindingSlot* RenderPSOCache::findComputeBufferSlot( RHIPipelineStateHandle pso, uint32 slot, bool bUAV, ShaderTargetFormat targetFormat )
    {
        RHIPipelineStateDesc desc{};
        if ( findDesc( pso, desc ) == false || desc._computeShaderPath.empty() )
            return nullptr;
        const ShaderBindingKind kind = bUAV ? ShaderBindingKind::RwStructuredBuffer : ShaderBindingKind::StructuredBuffer;
        for ( const ShaderBindingSlot& candidate : _bindingLayoutCache.getOrBuild( desc, targetFormat ).getSlots() )
        {
            if ( candidate._kind == kind && candidate._registerIndex == slot )
                return &candidate;
        }
        return nullptr;
    }

    void RenderPSOCache::registerDesc( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
    {
        if ( pso == 0 )
            return;
        ScopedLock<shared_mutex> lock{ _layoutMutex };
        _mapPSODesc[pso] = desc;
    }

    void RenderPSOCache::collectLayouts( vector<RegisteredLayout>& outListLayout ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
        outListLayout.clear();
        outListLayout.reserve( _mapPSOLayout.size() );
        for ( const auto& [pso, pLayout] : _mapPSOLayout )
        {
            outListLayout.push_back( RegisteredLayout{ pso, pLayout } );
        }
    }

    void RenderPSOCache::releaseReplacedPSO( IRHIDevice* pDevice, RHIPipelineState& previous )
    {
        if ( previous.isValid() == false )
            return;
        const RHIPipelineStateHandle handle = previous._handle;
        previous.release( pDevice ); // 장치가 기록 밖으로, 백엔드 해제 큐가 GPU 완료 뒤로 미룬다(`releasePSOsUsingShader` 와 같은 길)
        {
            ScopedLock<shared_mutex> lock{ _materialPSOMutex };
            for ( auto it = _mapMaterialPSO.begin(); it != _mapMaterialPSO.end(); )
            {
                // 패스 PSO 를 그대로 가리키는(소유하지 않은) 변형은 옛 핸들과 함께 사라진다 — 남기면 놓은 핸들을 그린다.
                if ( it->second._state == MaterialPSOState::Ready && it->second._owned.isValid() == false && it->second._pso == handle )
                    it = _mapMaterialPSO.erase( it );
                else
                    ++it;
            }
        }
        ScopedLock<shared_mutex> lock{ _layoutMutex };
        _mapPSOLayout.erase( handle );
        _mapPSODesc.erase( handle );
    }

    void RenderPSOCache::setEnginePSO( IRHIDevice* pDevice, RenderPassKind passType, RHIPipelineState&& pso )
    {
        RHIPipelineState& slot = _mapEnginePSO[passType];
        releaseReplacedPSO( pDevice, slot );
        slot = std::move( pso );
    }

    RHIPipelineStateHandle RenderPSOCache::findEnginePSO( RenderPassKind passType ) const
    {
        const auto it = _mapEnginePSO.find( passType );
        return ( it != _mapEnginePSO.end() ) ? it->second._handle : RHIPipelineStateHandle{ 0 };
    }

    void RenderPSOCache::setOutputPSO( IRHIDevice* pDevice, RenderPassKind passType, RHIFormat targetFormat, RHIPipelineState&& pso )
    {
        RHIPipelineState& slot = _mapOutputPSO[makeOutputPSOKey( passType, targetFormat )];
        releaseReplacedPSO( pDevice, slot );
        slot = std::move( pso );
    }

    bool RenderPSOCache::findOutputPSO( RenderPassKind passType, RHIFormat targetFormat, RHIPipelineStateHandle& outPipelineState ) const
    {
        const auto it = _mapOutputPSO.find( makeOutputPSOKey( passType, targetFormat ) );
        if ( it == _mapOutputPSO.end() )
            return false;
        outPipelineState = it->second._handle;
        return true;
    }

    void RenderPSOCache::setExtensionProgramPSO( IRHIDevice* pDevice, const hashed_string& programKey, RHIPipelineState&& pso )
    {
        RHIPipelineState& slot = _mapExtensionProgramPSO[programKey];
        releaseReplacedPSO( pDevice, slot );
        slot = std::move( pso );
    }

    bool RenderPSOCache::findExtensionProgramPSO( const hashed_string& programKey, RHIPipelineStateHandle& outPipelineState ) const
    {
        const auto it = _mapExtensionProgramPSO.find( programKey );
        if ( it == _mapExtensionProgramPSO.end() )
            return false;
        outPipelineState = it->second._handle;
        return true;
    }

    bool RenderPSOCache::hasMaterialPSO( const MaterialPSOKey& key ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _materialPSOMutex };
        return _mapMaterialPSO.find( key ) != _mapMaterialPSO.end();
    }

    bool RenderPSOCache::beginMaterialPSO( const MaterialPSOKey& key )
    {
        ScopedLock<shared_mutex> lock{ _materialPSOMutex };
        return _mapMaterialPSO.try_emplace( key, MaterialPSOEntry::building() ).second;
    }

    void RenderPSOCache::setMaterialPSO( const MaterialPSOKey& key, MaterialPSOEntry&& entry )
    {
        SW_ASSERT( entry._state != MaterialPSOState::Absent );
        ScopedLock<shared_mutex> lock{ _materialPSOMutex };
        _mapMaterialPSO.insert_or_assign( key, std::move( entry ) );
    }

    RenderPSOCache::MaterialPSOState RenderPSOCache::findMaterialPSOState( const MaterialPSOKey& key ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _materialPSOMutex };
        const auto                     it = _mapMaterialPSO.find( key );
        return ( it != _mapMaterialPSO.end() ) ? it->second._state : MaterialPSOState::Absent;
    }

    RHIPipelineStateHandle RenderPSOCache::findMaterialPSO( const MaterialPSOKey& key ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _materialPSOMutex };
        const auto                     it     = _mapMaterialPSO.find( key );
        const bool                     bReady = it != _mapMaterialPSO.end() && it->second._state == MaterialPSOState::Ready;
        return bReady ? it->second._pso : 0;
    }

    void RenderPSOCache::requestMaterialPSO( IRHIDevice* pDevice, const MaterialPSOKey& key, RHIPipelineStateDesc&& desc, bool bUrgent, bool bAsync )
    {
        _materialPSOBuilder.submit( pDevice, MaterialPSOBuilder::Job{ key, std::move( desc ) }, bUrgent, bAsync );
    }

    uint32 RenderPSOCache::pollMaterialPSOBuilds( IRHIDevice* pDevice, bool bAsync, uint32 budgetMicroseconds )
    {
        if ( pDevice == nullptr )
            return 0;
        if ( bAsync == false )
            (void)_materialPSOBuilder.buildOnCaller( budgetMicroseconds );
        vector<MaterialPSOBuilder::Result> listResult;
        _materialPSOBuilder.takeResults( listResult );
        uint32 readyCount{ 0 };
        for ( MaterialPSOBuilder::Result& result : listResult )
        {
            if ( result._bDiscarded )
            {
                // 핫 리로드 앞에 시작한 생성이다 — 옛 바이트코드일 수 있고 그 키의 항목은 이미 지웠거나 새로 만드는 중이다.
                result._pso.release( pDevice );
                continue;
            }
            if ( result._pso.isValid() == false )
            {
                // 컴파일 실패다. 패스 PSO(다른 셰이더 · 다른 보기 모드)로 그리면 틀린 그림이 통과하므로 이 조합은 그리지 않고 오류를 남긴다.
                SW_LOG_ERROR_ID( "RenderPSOCache.MaterialPSOVariantFailed", "Material PSO variant '%#' failed to build - these batches are not drawn", result._desc._vertexShaderPath );
                setMaterialPSO( result._key, MaterialPSOEntry::failed() );
                continue;
            }
            registerLayout( result._pso._handle, result._desc, pDevice->getShaderTargetFormat() );
            setMaterialPSO( result._key, MaterialPSOEntry::ready( std::move( result._pso ) ) );
            ++readyCount;
        }
        return readyCount;
    }

    uint32 RenderPSOCache::getPendingMaterialPSOBuilds() const
    {
        return _materialPSOBuilder.getPendingCount();
    }

    void RenderPSOCache::releaseAll( IRHIDevice* pDevice )
    {
        // 빌더 스레드가 디바이스를 쓰므로 가장 먼저 합류한다(줄은 버리고, 끝났으나 가져가지 않은 PSO 는 파괴한다).
        _materialPSOBuilder.shutdown( pDevice );
        if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
        {
            clearAll();
            return;
        }

        // 퍼뮤테이션 변형을 **패스 PSO 보다 먼저** 놓는다. 패스 PSO 를 그대로 가리키는 항목은 소유한 것이 없어 놓을 것이 없다.
        {
            const ScopedDeferredLog  deferredLog;
            ScopedLock<shared_mutex> lock{ _materialPSOMutex };
            for ( auto& [key, entry] : _mapMaterialPSO )
            {
                entry._owned.release( pDevice );
            }
            _mapMaterialPSO.clear();
        }

        for ( auto& [passType, pso] : _mapEnginePSO )
        {
            pso.release( pDevice );
        }
        _mapEnginePSO.clear();

        for ( auto& [key, pso] : _mapOutputPSO )
        {
            pso.release( pDevice );
        }
        _mapOutputPSO.clear();

        for ( auto& [programKey, pso] : _mapExtensionProgramPSO )
        {
            pso.release( pDevice );
        }
        _mapExtensionProgramPSO.clear();

        // 두 맵은 방금 파괴한 PSO 핸들을 키로 가지고 있다. 핸들에 generation 이 들어 있어 같은 핸들이 다시 발급되어도
        // 이전 항목을 집는 일은 없지만, 셰이더 리로드마다 PSO 를 다시 만드는 지금은 그대로 두면
        // 죽은 항목(RHIPipelineStateDesc 통째)이 계속 쌓인다.
        {
            ScopedLock<shared_mutex> lock{ _layoutMutex };
            _mapPSOLayout.clear();
            _mapPSODesc.clear();
        }
    }

    uint32 RenderPSOCache::releasePSOsUsingShader( IRHIDevice* pDevice, string_view shaderPath )
    {
        // 1) 그 셰이더를 쓰는 PSO — desc 의 세 경로를 레이아웃 캐시와 같은 규칙으로 본다(지울 레이아웃을 쥔 PSO 가 남지 않게).
        vector<RHIPipelineStateHandle> listAffected;
        {
            ScopedLock<shared_mutex> lock{ _layoutMutex };
            for ( const auto& [pso, desc] : _mapPSODesc )
            {
                const bool bUses = ShaderBindingLayoutCache::matchesShaderPath( desc._vertexShaderPath, shaderPath ) ||
                                   ShaderBindingLayoutCache::matchesShaderPath( desc._pixelShaderPath, shaderPath ) ||
                                   ShaderBindingLayoutCache::matchesShaderPath( desc._computeShaderPath, shaderPath );
                if ( bUses )
                    listAffected.push_back( pso );
            }
        }
        vector<RHIPipelineStateHandle> listReleased;
        const auto                     destroy = [pDevice, &listReleased]( RHIPipelineState& pso )
        {
            if ( pso.isValid() == false )
                return;
            listReleased.push_back( pso._handle );
            pso.release( pDevice ); // 장치가 기록 밖으로, 백엔드 해제 큐가 그 PSO 를 쓴 프레임의 GPU 완료 뒤로 미룬다
        };

        // 2) 엔진 · 출력 · 모듈 프로그램 PSO. 패스 PSO 를 놓았는지 기억한다(아래 변형).
        bool bPassReleased{ false };
        for ( auto it = _mapEnginePSO.begin(); it != _mapEnginePSO.end(); )
        {
            if ( VectorUtil::contains( listAffected, it->second._handle ) == false )
            {
                ++it;
                continue;
            }
            destroy( it->second );
            bPassReleased = true;
            it            = _mapEnginePSO.erase( it );
        }
        for ( auto it = _mapOutputPSO.begin(); it != _mapOutputPSO.end(); )
        {
            if ( VectorUtil::contains( listAffected, it->second._handle ) == false )
            {
                ++it;
                continue;
            }
            destroy( it->second );
            it = _mapOutputPSO.erase( it );
        }
        for ( auto it = _mapExtensionProgramPSO.begin(); it != _mapExtensionProgramPSO.end(); )
        {
            if ( it->second.isValid() && VectorUtil::contains( listAffected, it->second._handle ) == false )
            {
                ++it;
                continue;
            }
            destroy( it->second );
            it = _mapExtensionProgramPSO.erase( it );
        }

        // 3) 머티리얼 변형(입자 · 쿠키 아틀라스 · 디퍼드 타일 포함). 소유한 것만 파괴한다 — `_owned` 가 비면 패스 PSO 그대로다.
        // 만드는 중인 것은 모두 무효로 하고 지운다 — 옛 바이트코드를 집었을 수 있고, 패스 PSO 를 놓았으면 그 키를 다시 찾는 쪽이 없다. 다음 셋업이 다시 넣는다.
        _materialPSOBuilder.discardPending();
        {
            ScopedLock<shared_mutex> lock{ _materialPSOMutex };
            for ( auto it = _mapMaterialPSO.begin(); it != _mapMaterialPSO.end(); )
            {
                MaterialPSOEntry& entry    = it->second;
                const bool        bRelease = entry._state == MaterialPSOState::Failed || entry._state == MaterialPSOState::Building ||
                                      ( entry._state == MaterialPSOState::Ready && ( bPassReleased || VectorUtil::contains( listAffected, entry._pso ) ) );
                if ( bRelease == false )
                {
                    ++it;
                    continue;
                }
                destroy( entry._owned );
                it = _mapMaterialPSO.erase( it );
            }
        }

        // 4) 놓은 핸들의 레이아웃 · desc, 그리고 그 셰이더의 레이아웃 캐시. PSO 를 먼저 놓아야 지운 레이아웃을 가리키는 항목이 남지 않는다.
        {
            ScopedLock<shared_mutex> lock{ _layoutMutex };
            for ( const RHIPipelineStateHandle pso : listReleased )
            {
                _mapPSOLayout.erase( pso );
                _mapPSODesc.erase( pso );
            }
        }
        _bindingLayoutCache.invalidateByShaderPath( shaderPath );
        return static_cast<uint32>( listReleased.size() );
    }

    void RenderPSOCache::clearAll()
    {
        _materialPSOBuilder.shutdown( nullptr );
        _mapEnginePSO.clear();
        _mapOutputPSO.clear();
        _mapExtensionProgramPSO.clear();
        {
            ScopedLock<shared_mutex> lock{ _materialPSOMutex };
            _mapMaterialPSO.clear();
        }
        {
            ScopedLock<shared_mutex> lock{ _layoutMutex };
            _mapPSOLayout.clear();
            _mapPSODesc.clear();
        }
    }
} // namespace sw
