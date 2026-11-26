#include "pch.h"

#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayoutCache.h"
#include "Engine/Graphics/View/RenderViewMode.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    struct RenderPSOCache::Impl
    {
        /** @brief (패스 << 32 | 대상 포맷) 키입니다. */
        static uint64 makeOutputPSOKey( RenderPassKind passType, RHIFormat targetFormat )
        {
            return ( static_cast<uint64>( passType ) << 32 ) | static_cast<uint64>( targetFormat );
        }

        ShaderBindingLayoutCache                                                        _bindingLayoutCache;
        unordered_map<RHIPipelineStateHandle, const ShaderBindingLayout*> _mapPSOLayout SW_GUARDED_BY( _layoutMutex );
        unordered_map<RHIPipelineStateHandle, RHIPipelineStateDesc> _mapPSODesc         SW_GUARDED_BY( _layoutMutex );
        uint64 _layoutSerial                                                            SW_GUARDED_BY( _layoutMutex ){ 0 }; ///< 레이아웃 등록 번호(`getLayoutSerial`)
        mutable shared_mutex                                                            _layoutMutex;                       ///< 기록 워커들이 배치마다 조회만 한다 — 공유 잠금(언리얼 PSO 캐시의 `FRWLock`)
        /// @brief 엔진이 만들어 둔 패스별 PSO 입니다. 키가 enum 이라 조회에 문자열을 만들지 않습니다.
        unordered_map<RenderPassKind, PassPSOEntry> _mapEnginePSO;
        /// @brief 주 출력에 그리는 패스의 PSO 를 대상 렌더 타깃 포맷별로 둡니다(키 = makeOutputPSOKey). 백버퍼와 에디터 뷰 RT 는 포맷이 다를 수 있습니다.
        unordered_map<uint64, RHIPipelineState> _mapOutputPSO;
        /// @brief 코드가 짓는 PSO 입니다(키 = 서술체 지문 `RHIPipelineStateDesc::computeHash`).
        unordered_map<uint64, RHIPipelineState> _mapGlobalPSO;
        /// @brief 렌더 파이프라인 그래픽스 패스의 PSO 입니다(키 = 패스 이름). 머티리얼 변형의 패스 PSO 이기도 하다.
        unordered_map<hashed_string, RHIPipelineState> _mapPipelinePassPSO;
        /// @brief 렌더 파이프라인 컴퓨트 패스의 PSO 입니다(키 = 패스 이름).
        unordered_map<hashed_string, PassPSOEntry>                                          _mapPipelineComputePSO;
        unordered_map<MaterialPSOKey, MaterialPSOEntry, MaterialPSOKeyHash> _mapMaterialPSO SW_GUARDED_BY( _materialPSOMutex );
        mutable shared_mutex                                                                _materialPSOMutex; ///< `_layoutMutex` 와 같은 까닭의 공유 잠금
        /// @brief 머티리얼 변형의 생성 줄과 전용 스레드입니다. 캐시가 소유해 미리보기 렌더러와 함께 씁니다(해제는 `releaseAll` 이 스레드부터 합류합니다).
        MaterialPSOBuilder _materialPSOBuilder;
        uint64             _pollFrameNumber{ ~uint64{ 0 } }; ///< `_buildBudgetNanoseconds` 를 연 장치 프레임(렌더 스레드)
        int64              _buildBudgetNanoseconds{ 0 };     ///< 그 프레임에 남은 생성 예산

        void registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat )
        {
            if ( pso == 0 )
                return;
            const ShaderBindingLayout& layout = _bindingLayoutCache.getOrBuild( desc, targetFormat );
            ScopedLock<shared_mutex>   lock{ _layoutMutex };
            _mapPSOLayout[pso] = &layout;
            _mapPSODesc[pso]   = desc;
            ++_layoutSerial;
        }

        uint64 getLayoutSerial() const
        {
            ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
            return _layoutSerial;
        }

        const ShaderBindingLayout* findLayout( RHIPipelineStateHandle pso ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
            const auto                     it = _mapPSOLayout.find( pso );
            return it != _mapPSOLayout.end() ? it->second : nullptr;
        }

        bool findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
            const auto                     it = _mapPSODesc.find( pso );
            if ( it == _mapPSODesc.end() )
                return false;
            outDesc = it->second;
            return true;
        }

        RHIFormat findDepthStencilFormat( RHIPipelineStateHandle pso ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
            const auto                     it = _mapPSODesc.find( pso );
            return it != _mapPSODesc.end() ? it->second._depthStencilFormat : RHIFormat::Unknown;
        }

        const ShaderBindingSlot* findComputeBufferSlot( RHIPipelineStateHandle pso, uint32 slot, bool bUAV ) const
        {
            const ShaderBindingLayout* pLayout = findLayout( pso );
            if ( pLayout == nullptr )
                return nullptr;
            const ShaderBindingKind kind = bUAV ? ShaderBindingKind::RwStructuredBuffer : ShaderBindingKind::StructuredBuffer;
            for ( const ShaderBindingSlot& candidate : pLayout->getSlots() )
            {
                if ( candidate._kind == kind && candidate._registerIndex == slot )
                    return &candidate;
            }
            return nullptr;
        }

        void registerDesc( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
        {
            if ( pso == 0 )
                return;
            ScopedLock<shared_mutex> lock{ _layoutMutex };
            _mapPSODesc[pso] = desc;
        }

        void collectLayouts( vector<RegisteredLayout>& outListLayout ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _layoutMutex };
            outListLayout.clear();
            outListLayout.reserve( _mapPSOLayout.size() );
            for ( const auto& [pso, pLayout] : _mapPSOLayout )
            {
                outListLayout.push_back( RegisteredLayout{ pso, pLayout } );
            }
        }

        void releaseReplacedPSO( IRHIDevice* pDevice, RHIPipelineState& previous )
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

        void setEnginePSO( IRHIDevice* pDevice, RenderPassKind passType, RHIPipelineState&& pso, const ComputeShaderBindings& computeBindings )
        {
            PassPSOEntry& slot = _mapEnginePSO[passType];
            releaseReplacedPSO( pDevice, slot._pso );
            slot._pso             = std::move( pso );
            slot._computeBindings = computeBindings;
        }

        RHIPipelineStateHandle findEnginePSO( RenderPassKind passType ) const
        {
            const auto it = _mapEnginePSO.find( passType );
            return ( it != _mapEnginePSO.end() ) ? it->second._pso._handle : RHIPipelineStateHandle{ 0 };
        }

        ComputePipeline findEngineComputePSO( RenderPassKind passType ) const
        {
            const auto it = _mapEnginePSO.find( passType );
            return ( it != _mapEnginePSO.end() ) ? ComputePipeline{ it->second._pso._handle, it->second._computeBindings } : ComputePipeline{};
        }

        void setOutputPSO( IRHIDevice* pDevice, RenderPassKind passType, RHIFormat targetFormat, RHIPipelineState&& pso )
        {
            RHIPipelineState& slot = _mapOutputPSO[makeOutputPSOKey( passType, targetFormat )];
            releaseReplacedPSO( pDevice, slot );
            slot = std::move( pso );
        }

        bool findOutputPSO( RenderPassKind passType, RHIFormat targetFormat, RHIPipelineStateHandle& outPipelineState ) const
        {
            const auto it = _mapOutputPSO.find( makeOutputPSOKey( passType, targetFormat ) );
            if ( it == _mapOutputPSO.end() )
                return false;
            outPipelineState = it->second._handle;
            return true;
        }

        void setGlobalPSO( IRHIDevice* pDevice, uint64 descHash, RHIPipelineState&& pso )
        {
            RHIPipelineState& slot = _mapGlobalPSO[descHash];
            releaseReplacedPSO( pDevice, slot );
            slot = std::move( pso );
        }

        bool findGlobalPSO( uint64 descHash, RHIPipelineStateHandle& outPipelineState ) const
        {
            const auto it = _mapGlobalPSO.find( descHash );
            if ( it == _mapGlobalPSO.end() )
                return false;
            outPipelineState = it->second._handle;
            return true;
        }

        void setPipelinePassPSO( IRHIDevice* pDevice, const hashed_string& passName, RHIPipelineState&& pso )
        {
            RHIPipelineState& slot = _mapPipelinePassPSO[passName];
            releaseReplacedPSO( pDevice, slot );
            slot = std::move( pso );
        }

        bool findPipelinePassPSO( const hashed_string& passName, RHIPipelineStateHandle& outPipelineState ) const
        {
            const auto it = _mapPipelinePassPSO.find( passName );
            if ( it == _mapPipelinePassPSO.end() )
                return false;
            outPipelineState = it->second._handle;
            return true;
        }

        void setPipelineComputePSO( IRHIDevice* pDevice, const hashed_string& passName, RHIPipelineState&& pso, const ComputeShaderBindings& computeBindings )
        {
            PassPSOEntry& slot = _mapPipelineComputePSO[passName];
            releaseReplacedPSO( pDevice, slot._pso );
            slot._pso             = std::move( pso );
            slot._computeBindings = computeBindings;
        }

        bool findPipelineComputePSO( const hashed_string& passName, ComputePipeline& outPipeline ) const
        {
            const auto it = _mapPipelineComputePSO.find( passName );
            if ( it == _mapPipelineComputePSO.end() )
                return false;
            outPipeline = ComputePipeline{ it->second._pso._handle, it->second._computeBindings };
            return true;
        }

        bool hasMaterialPSO( const MaterialPSOKey& key ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _materialPSOMutex };
            return _mapMaterialPSO.find( key ) != _mapMaterialPSO.end();
        }

        bool beginMaterialPSO( const MaterialPSOKey& key )
        {
            ScopedLock<shared_mutex> lock{ _materialPSOMutex };
            return _mapMaterialPSO.try_emplace( key, MaterialPSOEntry::building() ).second;
        }

        void setMaterialPSO( const MaterialPSOKey& key, MaterialPSOEntry&& entry )
        {
            SW_ASSERT( entry._state != MaterialPSOState::Absent );
            ScopedLock<shared_mutex> lock{ _materialPSOMutex };
            _mapMaterialPSO.insert_or_assign( key, std::move( entry ) );
        }

        RenderPSOCache::MaterialPSOState findMaterialPSOState( const MaterialPSOKey& key ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _materialPSOMutex };
            const auto                     it = _mapMaterialPSO.find( key );
            return ( it != _mapMaterialPSO.end() ) ? it->second._state : MaterialPSOState::Absent;
        }

        RHIPipelineStateHandle findMaterialPSO( const MaterialPSOKey& key ) const
        {
            ScopedSharedLock<shared_mutex> lock{ _materialPSOMutex };
            const auto                     it     = _mapMaterialPSO.find( key );
            const bool                     bReady = it != _mapMaterialPSO.end() && it->second._state == MaterialPSOState::Ready;
            return bReady ? it->second._pso : 0;
        }

        void requestMaterialPSO( IRHIDevice* pDevice, const MaterialPSOKey& key, RHIPipelineStateDesc&& desc, bool bUrgent, bool bAsync )
        {
            _materialPSOBuilder.submit( pDevice, MaterialPSOBuilder::Job{ key, std::move( desc ) }, bUrgent, bAsync );
        }

        uint32 pollMaterialPSOBuilds( IRHIDevice* pDevice, bool bAsync, uint32 budgetMicroseconds )
        {
            if ( pDevice == nullptr )
                return 0;
            if ( _pollFrameNumber != pDevice->getFrameNumber() )
            {
                _pollFrameNumber        = pDevice->getFrameNumber();
                _buildBudgetNanoseconds = static_cast<int64>( budgetMicroseconds ) * constant::time::kNanosecondsPerMicrosecond;
                SW_PROFILE_COUNT( "RT.PSO.pendingBuilds", getPendingMaterialPSOBuilds() ); // 카운터는 프레임 합이라 프레임에 한 번 낸다(앞 프레임까지 밀린 수)
            }
            const bool bBuildAll = budgetMicroseconds == 0;
            if ( bAsync == false && ( bBuildAll || _buildBudgetNanoseconds > 0 ) )
            {
                const int64  startNanoseconds = MonotonicClock::nowNanoseconds();
                const uint32 leftMicroseconds = static_cast<uint32>( MathUtil::max<int64>( _buildBudgetNanoseconds / constant::time::kNanosecondsPerMicrosecond, 1 ) );
                const uint32 builtCount       = _materialPSOBuilder.buildOnCaller( bBuildAll ? 0u : leftMicroseconds );
                if ( builtCount > 0 )
                    _buildBudgetNanoseconds -= MonotonicClock::nowNanoseconds() - startNanoseconds;
            }
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

        uint32 getPendingMaterialPSOBuilds() const
        {
            return _materialPSOBuilder.getPendingCount();
        }

        void releaseAll( IRHIDevice* pDevice )
        {
            // 빌더 스레드가 디바이스를 쓰므로 가장 먼저 합류한다(줄은 버리고, 끝났으나 가져가지 않은 PSO 는 파괴한다).
            _materialPSOBuilder.shutdown( pDevice );
            if ( pDevice == nullptr || pDevice->getResourceFactory() == nullptr )
            {
                clearAll();
                return;
            }

            // 퍼뮤테이션 변형을 패스 PSO 보다 먼저 놓는다. 패스 PSO 를 그대로 가리키는 항목은 소유한 것이 없어 놓을 것이 없다.
            {
                const ScopedDeferredLog  deferredLog;
                ScopedLock<shared_mutex> lock{ _materialPSOMutex };
                for ( auto& [key, entry] : _mapMaterialPSO )
                {
                    entry._owned.release( pDevice );
                }
                _mapMaterialPSO.clear();
            }

            for ( auto& [passType, entry] : _mapEnginePSO )
            {
                entry._pso.release( pDevice );
            }
            _mapEnginePSO.clear();
            for ( auto& [passName, pso] : _mapPipelinePassPSO )
            {
                pso.release( pDevice );
            }
            _mapPipelinePassPSO.clear();

            for ( auto& [key, pso] : _mapOutputPSO )
            {
                pso.release( pDevice );
            }
            _mapOutputPSO.clear();

            for ( auto& [descHash, pso] : _mapGlobalPSO )
            {
                pso.release( pDevice );
            }
            _mapGlobalPSO.clear();
            for ( auto& [passName, entry] : _mapPipelineComputePSO )
            {
                entry._pso.release( pDevice );
            }
            _mapPipelineComputePSO.clear();

            // 두 맵은 방금 파괴한 PSO 핸들을 키로 가지고 있다. 핸들에 generation 이 들어 있어 같은 핸들이 다시 발급되어도
            // 이전 항목을 집는 일은 없지만, 셰이더 리로드마다 PSO 를 다시 만드는 지금은 그대로 두면
            // 죽은 항목(RHIPipelineStateDesc 통째)이 계속 쌓인다.
            {
                ScopedLock<shared_mutex> lock{ _layoutMutex };
                _mapPSOLayout.clear();
                _mapPSODesc.clear();
            }
        }

        uint32 releasePSOsUsingShader( IRHIDevice* pDevice, string_view shaderPath )
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
                if ( VectorUtil::contains( listAffected, it->second._pso._handle ) == false )
                {
                    ++it;
                    continue;
                }
                destroy( it->second._pso );
                bPassReleased = true;
                it            = _mapEnginePSO.erase( it );
            }
            for ( auto it = _mapPipelinePassPSO.begin(); it != _mapPipelinePassPSO.end(); )
            {
                if ( VectorUtil::contains( listAffected, it->second._handle ) == false )
                {
                    ++it;
                    continue;
                }
                destroy( it->second );
                bPassReleased = true;
                it            = _mapPipelinePassPSO.erase( it );
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
            for ( auto it = _mapGlobalPSO.begin(); it != _mapGlobalPSO.end(); )
            {
                if ( it->second.isValid() && VectorUtil::contains( listAffected, it->second._handle ) == false )
                {
                    ++it;
                    continue;
                }
                destroy( it->second );
                it = _mapGlobalPSO.erase( it );
            }
            for ( auto it = _mapPipelineComputePSO.begin(); it != _mapPipelineComputePSO.end(); )
            {
                if ( it->second._pso.isValid() && VectorUtil::contains( listAffected, it->second._pso._handle ) == false )
                {
                    ++it;
                    continue;
                }
                destroy( it->second._pso );
                it = _mapPipelineComputePSO.erase( it );
            }

            // 3) 머티리얼 변형. 소유한 것만 파괴한다 — `_owned` 가 비면 패스 PSO 그대로다.
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

        void clearAll()
        {
            _materialPSOBuilder.shutdown( nullptr );
            _mapEnginePSO.clear();
            _mapOutputPSO.clear();
            _mapGlobalPSO.clear();
            _mapPipelinePassPSO.clear();
            _mapPipelineComputePSO.clear();
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
    };

    RenderPSOCache::RenderPSOCache()
        : _impl{ make_unique<Impl>() }
    {
    }

    RenderPSOCache::~RenderPSOCache() = default;

    const unordered_map<RenderPassKind, RenderPSOCache::PassPSOEntry>& RenderPSOCache::getEnginePSOs() const
    {
        return _impl->_mapEnginePSO;
    }

    void RenderPSOCache::registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat )
    {
        _impl->registerLayout( pso, desc, targetFormat );
    }

    uint64 RenderPSOCache::getLayoutSerial() const
    {
        return _impl->getLayoutSerial();
    }

    const ShaderBindingLayout* RenderPSOCache::findLayout( RHIPipelineStateHandle pso ) const
    {
        return _impl->findLayout( pso );
    }

    bool RenderPSOCache::findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        return _impl->findDesc( pso, outDesc );
    }

    RHIFormat RenderPSOCache::findDepthStencilFormat( RHIPipelineStateHandle pso ) const
    {
        return _impl->findDepthStencilFormat( pso );
    }

    const ShaderBindingSlot* RenderPSOCache::findComputeBufferSlot( RHIPipelineStateHandle pso, uint32 slot, bool bUAV ) const
    {
        return _impl->findComputeBufferSlot( pso, slot, bUAV );
    }

    void RenderPSOCache::registerDesc( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
    {
        _impl->registerDesc( pso, desc );
    }

    void RenderPSOCache::collectLayouts( vector<RegisteredLayout>& outListLayout ) const
    {
        _impl->collectLayouts( outListLayout );
    }

    void RenderPSOCache::setEnginePSO( IRHIDevice* pDevice, RenderPassKind passType, RHIPipelineState&& pso, const ComputeShaderBindings& computeBindings )
    {
        _impl->setEnginePSO( pDevice, passType, std::move( pso ), computeBindings );
    }

    RHIPipelineStateHandle RenderPSOCache::findEnginePSO( RenderPassKind passType ) const
    {
        return _impl->findEnginePSO( passType );
    }

    ComputePipeline RenderPSOCache::findEngineComputePSO( RenderPassKind passType ) const
    {
        return _impl->findEngineComputePSO( passType );
    }

    void RenderPSOCache::setOutputPSO( IRHIDevice* pDevice, RenderPassKind passType, RHIFormat targetFormat, RHIPipelineState&& pso )
    {
        _impl->setOutputPSO( pDevice, passType, targetFormat, std::move( pso ) );
    }

    bool RenderPSOCache::findOutputPSO( RenderPassKind passType, RHIFormat targetFormat, RHIPipelineStateHandle& outPipelineState ) const
    {
        return _impl->findOutputPSO( passType, targetFormat, outPipelineState );
    }

    void RenderPSOCache::setGlobalPSO( IRHIDevice* pDevice, uint64 descHash, RHIPipelineState&& pso )
    {
        _impl->setGlobalPSO( pDevice, descHash, std::move( pso ) );
    }

    bool RenderPSOCache::findGlobalPSO( uint64 descHash, RHIPipelineStateHandle& outPipelineState ) const
    {
        return _impl->findGlobalPSO( descHash, outPipelineState );
    }

    void RenderPSOCache::setPipelinePassPSO( IRHIDevice* pDevice, const hashed_string& passName, RHIPipelineState&& pso )
    {
        _impl->setPipelinePassPSO( pDevice, passName, std::move( pso ) );
    }

    bool RenderPSOCache::findPipelinePassPSO( const hashed_string& passName, RHIPipelineStateHandle& outPipelineState ) const
    {
        return _impl->findPipelinePassPSO( passName, outPipelineState );
    }

    void RenderPSOCache::setPipelineComputePSO( IRHIDevice* pDevice, const hashed_string& passName, RHIPipelineState&& pso, const ComputeShaderBindings& computeBindings )
    {
        _impl->setPipelineComputePSO( pDevice, passName, std::move( pso ), computeBindings );
    }

    bool RenderPSOCache::findPipelineComputePSO( const hashed_string& passName, ComputePipeline& outPipeline ) const
    {
        return _impl->findPipelineComputePSO( passName, outPipeline );
    }

    bool RenderPSOCache::hasMaterialPSO( const MaterialPSOKey& key ) const
    {
        return _impl->hasMaterialPSO( key );
    }

    bool RenderPSOCache::beginMaterialPSO( const MaterialPSOKey& key )
    {
        return _impl->beginMaterialPSO( key );
    }

    void RenderPSOCache::setMaterialPSO( const MaterialPSOKey& key, MaterialPSOEntry&& entry )
    {
        _impl->setMaterialPSO( key, std::move( entry ) );
    }

    RenderPSOCache::MaterialPSOState RenderPSOCache::findMaterialPSOState( const MaterialPSOKey& key ) const
    {
        return _impl->findMaterialPSOState( key );
    }

    RHIPipelineStateHandle RenderPSOCache::findMaterialPSO( const MaterialPSOKey& key ) const
    {
        return _impl->findMaterialPSO( key );
    }

    void RenderPSOCache::requestMaterialPSO( IRHIDevice* pDevice, const MaterialPSOKey& key, RHIPipelineStateDesc&& desc, bool bUrgent, bool bAsync )
    {
        _impl->requestMaterialPSO( pDevice, key, std::move( desc ), bUrgent, bAsync );
    }

    uint32 RenderPSOCache::pollMaterialPSOBuilds( IRHIDevice* pDevice, bool bAsync, uint32 budgetMicroseconds )
    {
        return _impl->pollMaterialPSOBuilds( pDevice, bAsync, budgetMicroseconds );
    }

    uint32 RenderPSOCache::getPendingMaterialPSOBuilds() const
    {
        return _impl->getPendingMaterialPSOBuilds();
    }

    void RenderPSOCache::releaseAll( IRHIDevice* pDevice )
    {
        _impl->releaseAll( pDevice );
    }

    uint32 RenderPSOCache::releasePSOsUsingShader( IRHIDevice* pDevice, string_view shaderPath )
    {
        return _impl->releasePSOsUsingShader( pDevice, shaderPath );
    }

    void RenderPSOCache::clearAll()
    {
        _impl->clearAll();
    }
} // namespace sw
