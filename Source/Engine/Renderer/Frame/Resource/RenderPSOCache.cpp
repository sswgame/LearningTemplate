#include "pch.h"

#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"

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
        ScopedLock<mutex>          lock{ _layoutMutex };
        _mapPSOLayout[pso] = &layout;
        _mapPSODesc[pso]   = desc;
    }

    const ShaderBindingLayout* RenderPSOCache::findLayout( RHIPipelineStateHandle pso ) const
    {
        ScopedLock<mutex> lock{ _layoutMutex };
        const auto        it = _mapPSOLayout.find( pso );
        return it != _mapPSOLayout.end() ? it->second : nullptr;
    }

    bool RenderPSOCache::findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        ScopedLock<mutex> lock{ _layoutMutex };
        const auto        it = _mapPSODesc.find( pso );
        if ( it == _mapPSODesc.end() )
            return false;
        outDesc = it->second;
        return true;
    }

    void RenderPSOCache::registerDesc( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
    {
        if ( pso == 0 )
            return;
        ScopedLock<mutex> lock{ _layoutMutex };
        _mapPSODesc[pso] = desc;
    }

    void RenderPSOCache::collectLayouts( vector<RegisteredLayout>& outListLayout ) const
    {
        ScopedLock<mutex> lock{ _layoutMutex };
        outListLayout.clear();
        outListLayout.reserve( _mapPSOLayout.size() );
        for ( const auto& [pso, pLayout] : _mapPSOLayout )
        {
            outListLayout.push_back( RegisteredLayout{ pso, pLayout } );
        }
    }

    void RenderPSOCache::releaseReplacedPSO( IRHIDevice* pDevice, RHIPipelineStateHandle previous, RHIPipelineStateHandle next )
    {
        if ( previous == 0 || previous == next )
            return;
        if ( pDevice != nullptr )
            pDevice->releaseHandle( previous ); // 장치가 기록 밖으로, 백엔드 해제 큐가 GPU 완료 뒤로 미룬다(`releasePSOsUsingShader` 와 같은 길)
        {
            ScopedLock<mutex> lock{ _materialPSOMutex };
            for ( auto it = _mapMaterialPSO.begin(); it != _mapMaterialPSO.end(); )
            {
                // 패스 PSO 를 그대로 가리키는(소유하지 않은) 변형은 옛 핸들과 함께 사라진다 — 남기면 놓은 핸들을 그린다.
                if ( it->second._state == MaterialPSOState::Ready && it->second._bOwned == 0 && it->second._pso == previous )
                    it = _mapMaterialPSO.erase( it );
                else
                    ++it;
            }
        }
        ScopedLock<mutex> lock{ _layoutMutex };
        _mapPSOLayout.erase( previous );
        _mapPSODesc.erase( previous );
    }

    void RenderPSOCache::setEnginePSO( IRHIDevice* pDevice, RenderPassKind passType, RHIPipelineStateHandle pso )
    {
        const auto it = _mapEnginePSO.find( passType );
        if ( it != _mapEnginePSO.end() )
            releaseReplacedPSO( pDevice, it->second, pso );
        _mapEnginePSO.insert_or_assign( passType, pso );
    }

    RHIPipelineStateHandle RenderPSOCache::findEnginePSO( RenderPassKind passType ) const
    {
        const auto it = _mapEnginePSO.find( passType );
        return ( it != _mapEnginePSO.end() ) ? it->second : 0;
    }

    void RenderPSOCache::setOutputPSO( IRHIDevice* pDevice, RenderPassKind passType, RHIFormat targetFormat, RHIPipelineStateHandle pso )
    {
        const uint64 key = makeOutputPSOKey( passType, targetFormat );
        const auto   it  = _mapOutputPSO.find( key );
        if ( it != _mapOutputPSO.end() )
            releaseReplacedPSO( pDevice, it->second, pso );
        _mapOutputPSO.insert_or_assign( key, pso );
    }

    bool RenderPSOCache::findOutputPSO( RenderPassKind passType, RHIFormat targetFormat, RHIPipelineStateHandle& outPipelineState ) const
    {
        const auto it = _mapOutputPSO.find( makeOutputPSOKey( passType, targetFormat ) );
        if ( it == _mapOutputPSO.end() )
            return false;
        outPipelineState = it->second;
        return true;
    }

    void RenderPSOCache::setExtensionProgramPSO( IRHIDevice* pDevice, const hashed_string& programKey, RHIPipelineStateHandle pso )
    {
        const auto it = _mapExtensionProgramPSO.find( programKey );
        if ( it != _mapExtensionProgramPSO.end() )
            releaseReplacedPSO( pDevice, it->second, pso );
        _mapExtensionProgramPSO.insert_or_assign( programKey, pso );
    }

    bool RenderPSOCache::findExtensionProgramPSO( const hashed_string& programKey, RHIPipelineStateHandle& outPipelineState ) const
    {
        const auto it = _mapExtensionProgramPSO.find( programKey );
        if ( it == _mapExtensionProgramPSO.end() )
            return false;
        outPipelineState = it->second;
        return true;
    }

    uint64 RenderPSOCache::materialPSOKey( RHIPipelineStateHandle passPSO, uint64 permutationHash, RenderViewMode viewMode, bool bReverseCulling, RHIMeshVertexFormat vertexFormat )
    {
        uint64 key = passPSO.getPacked() * HashUtil::kGoldenRatio64;
        key        = HashUtil::combine( key, permutationHash );
        key ^= ( static_cast<uint64>( viewMode ) + 1 ) * 0xff51afd7ed558ccdull;
        if ( bReverseCulling )
            key ^= 0xc4ceb9fe1a85ec53ull;
        if ( vertexFormat != RHIMeshVertexFormat::Packed )
            key = HashUtil::combine( key, static_cast<uint64>( vertexFormat ) * 0x9e3779b97f4a7c15ull );
        return key;
    }

    bool RenderPSOCache::hasMaterialPSO( uint64 key ) const
    {
        ScopedLock<mutex> lock{ _materialPSOMutex };
        return _mapMaterialPSO.find( key ) != _mapMaterialPSO.end();
    }

    bool RenderPSOCache::beginMaterialPSO( uint64 key )
    {
        ScopedLock<mutex> lock{ _materialPSOMutex };
        return _mapMaterialPSO.try_emplace( key, MaterialPSOEntry::building() ).second;
    }

    void RenderPSOCache::setMaterialPSO( uint64 key, const MaterialPSOEntry& entry )
    {
        SW_ASSERT( entry._state != MaterialPSOState::Absent );
        ScopedLock<mutex> lock{ _materialPSOMutex };
        _mapMaterialPSO.insert_or_assign( key, entry );
    }

    RenderPSOCache::MaterialPSOState RenderPSOCache::findMaterialPSOState( uint64 key ) const
    {
        ScopedLock<mutex> lock{ _materialPSOMutex };
        const auto        it = _mapMaterialPSO.find( key );
        return ( it != _mapMaterialPSO.end() ) ? it->second._state : MaterialPSOState::Absent;
    }

    size_t RenderPSOCache::countMaterialPSOs( MaterialPSOState state ) const
    {
        ScopedLock<mutex> lock{ _materialPSOMutex };
        size_t            count = 0;
        for ( const auto& [key, entry] : _mapMaterialPSO )
        {
            if ( entry._state == state )
                ++count;
        }
        return count;
    }

    RHIPipelineStateHandle RenderPSOCache::findMaterialPSO( uint64 key ) const
    {
        ScopedLock<mutex> lock{ _materialPSOMutex };
        const auto        it     = _mapMaterialPSO.find( key );
        const bool        bReady = it != _mapMaterialPSO.end() && it->second._state == MaterialPSOState::Ready;
        return bReady ? it->second._pso : 0;
    }

    void RenderPSOCache::requestMaterialPSO( IRHIDevice* pDevice, uint64 key, RHIPipelineStateDesc&& desc, bool bUrgent, bool bAsync )
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
        for ( const MaterialPSOBuilder::Result& result : listResult )
        {
            if ( result._handle == 0 )
            {
                // 컴파일 실패다. 패스 PSO(다른 셰이더 · 다른 보기 모드)로 그리면 틀린 그림이 통과하므로 이 조합은 그리지 않고 오류를 남긴다.
                SW_LOG_ERROR_ID( "RenderPSOCache.MaterialPSOVariantFailed", "Material PSO variant '%#' failed to build - these batches are not drawn", result._desc._vertexShaderPath );
                setMaterialPSO( result._key, MaterialPSOEntry::failed() );
                continue;
            }
            registerLayout( result._handle, result._desc, pDevice->getShaderTargetFormat() );
            setMaterialPSO( result._key, MaterialPSOEntry::ready( result._handle, true ) );
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

        // 퍼뮤테이션 변형을 **패스 PSO 보다 먼저** 파괴한다. `_bOwned` 가 0 인 항목은 패스 PSO 를 그대로
        // 담고 있을 뿐이라 여기서 파괴하면 두 번 파괴하는 셈이 된다.
        {
            ScopedLock<mutex> lock{ _materialPSOMutex };
            for ( auto& [key, entry] : _mapMaterialPSO )
            {
                if ( entry._bOwned != 0 && entry._pso != 0 )
                    pDevice->releaseHandle( entry._pso );
            }
            _mapMaterialPSO.clear();
        }

        for ( auto& [passType, pso] : _mapEnginePSO )
        {
            if ( pso != 0 )
                pDevice->releaseHandle( pso );
        }
        _mapEnginePSO.clear();

        for ( auto& [key, pso] : _mapOutputPSO )
        {
            if ( pso != 0 )
                pDevice->releaseHandle( pso );
        }
        _mapOutputPSO.clear();

        for ( auto& [programKey, pso] : _mapExtensionProgramPSO )
        {
            if ( pso != 0 )
                pDevice->releaseHandle( pso );
        }
        _mapExtensionProgramPSO.clear();

        // 두 맵은 방금 파괴한 PSO 핸들을 키로 가지고 있다. 핸들에 generation 이 들어 있어 같은 핸들이 다시 발급되어도
        // 이전 항목을 집는 일은 없지만, 셰이더 리로드마다 PSO 를 다시 만드는 지금은 그대로 두면
        // 죽은 항목(RHIPipelineStateDesc 통째)이 계속 쌓인다.
        {
            ScopedLock<mutex> lock{ _layoutMutex };
            _mapPSOLayout.clear();
            _mapPSODesc.clear();
        }
    }

    uint32 RenderPSOCache::releasePSOsUsingShader( IRHIDevice* pDevice, string_view shaderPath )
    {
        // 1) 그 셰이더를 쓰는 PSO — desc 의 세 경로를 레이아웃 캐시와 같은 규칙으로 본다(지울 레이아웃을 쥔 PSO 가 남지 않게).
        vector<RHIPipelineStateHandle> listAffected;
        {
            ScopedLock<mutex> lock{ _layoutMutex };
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
        const auto                     destroy = [pDevice, &listReleased]( RHIPipelineStateHandle pso )
        {
            if ( pso == 0 )
                return;
            if ( pDevice != nullptr )
                pDevice->releaseHandle( pso ); // 장치가 기록 밖으로, 백엔드 해제 큐가 그 PSO 를 쓴 프레임의 GPU 완료 뒤로 미룬다
            listReleased.push_back( pso );
        };

        // 2) 엔진 · 출력 · 모듈 프로그램 PSO. 패스 PSO 를 놓았는지 기억한다(아래 변형).
        bool bPassReleased{ false };
        for ( auto it = _mapEnginePSO.begin(); it != _mapEnginePSO.end(); )
        {
            if ( VectorUtil::contains( listAffected, it->second ) == false )
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
            if ( VectorUtil::contains( listAffected, it->second ) == false )
            {
                ++it;
                continue;
            }
            destroy( it->second );
            it = _mapOutputPSO.erase( it );
        }
        for ( auto it = _mapExtensionProgramPSO.begin(); it != _mapExtensionProgramPSO.end(); )
        {
            if ( it->second != 0 && VectorUtil::contains( listAffected, it->second ) == false )
            {
                ++it;
                continue;
            }
            destroy( it->second );
            it = _mapExtensionProgramPSO.erase( it );
        }

        // 3) 머티리얼 변형(입자 · 쿠키 아틀라스 · 디퍼드 타일 포함). 소유한 것만 파괴한다 — `_bOwned` 가 0 이면 패스 PSO 그대로다.
        {
            ScopedLock<mutex> lock{ _materialPSOMutex };
            for ( auto it = _mapMaterialPSO.begin(); it != _mapMaterialPSO.end(); )
            {
                const MaterialPSOEntry& entry    = it->second;
                const bool              bRelease = entry._state == MaterialPSOState::Failed ||
                                      ( entry._state == MaterialPSOState::Ready && ( bPassReleased || VectorUtil::contains( listAffected, entry._pso ) ) );
                if ( bRelease == false )
                {
                    ++it;
                    continue;
                }
                if ( entry._bOwned != 0 )
                    destroy( entry._pso );
                it = _mapMaterialPSO.erase( it );
            }
        }

        // 4) 놓은 핸들의 레이아웃 · desc, 그리고 그 셰이더의 레이아웃 캐시. PSO 를 먼저 놓아야 지운 레이아웃을 가리키는 항목이 남지 않는다.
        {
            ScopedLock<mutex> lock{ _layoutMutex };
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
            ScopedLock<mutex> lock{ _materialPSOMutex };
            _mapMaterialPSO.clear();
        }
        {
            ScopedLock<mutex> lock{ _layoutMutex };
            _mapPSOLayout.clear();
            _mapPSODesc.clear();
        }
    }
} // namespace sw
