#include "pch.h"

#include "Engine/Renderer/Lighting/GPULightBuffer.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/2D/ShadowCaster2DComponent.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/LightProbeVolumeComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/ReflectionProbeComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Renderer/Lighting/SceneVoxelizer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/VFX/VFXComponent.h"

namespace sw
{
    SW_LOG_CALLER( "GPULightBuffer" );

    /**
     * @brief `-gv_realtimeProbeRayCount=N`: 실시간 프로브 볼륨의 프로브당 광선 수를 N 으로 덮어씁니다(0 이면 볼륨의 `_realtimeRayCount`) — 광선 수별 GPU 시간(RealtimeProbeUpdateSweep)을 한 씬으로 잽니다.
     * @details 프로브 수는 볼륨의 `_spacing` 이 정해서 씬 파일로 가른다(`game/empty/maps/realtimegi_dense` · `realtimegi_sparse`).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_realtimeProbeRayCount, 0, "실시간 프로브 볼륨의 프로브당 광선 수 덮어쓰기(0 이면 볼륨의 값 — 스윕용)" );
    /** @brief `-gv_realtimeProbeUpdatePeriod=N`: 실시간 프로브 볼륨의 갱신 주기(프레임)를 N 으로 덮어씁니다(0 이면 볼륨의 `_realtimeUpdatePeriod`). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_realtimeProbeUpdatePeriod, 0, "실시간 프로브 볼륨의 갱신 주기(프레임) 덮어쓰기(0 이면 볼륨의 값 — 스윕용)" );

    namespace
    {
        constexpr RHIBufferUsage kLightBufferUsage        = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        constexpr RHIBufferUsage kLightBufferUsageWithUAV = kLightBufferUsage | RHIBufferUsage::UnorderedAccess;

        struct GPULightBufferInternal
        {
            /** @brief 그림자를 켠 점광과 스폿이면 true 입니다(방향광은 씬이 하나 고릅니다). */
            static bool castsLocalShadow( const LightComponent& light )
            {
                if ( light.getLightType() == shaderslot::kLightTypePoint )
                    return static_cast<const PointLightComponent&>( light ).castsShadow();
                if ( light.getLightType() == shaderslot::kLightTypeSpot )
                    return static_cast<const SpotLightComponent&>( light ).castsShadow();
                return false;
            }

            /**
             * @brief 프로브 볼륨 하나를 라이트 원소(bindingslots.hlsli 8 절의 SW_LIGHT_TYPE_PROBE_VOLUME 배치)로 만들고, 그 프로브들의 SH9 원소를 붙입니다.
             * @details 경계 구는 상자의 외접구다 — 라이트 그리드가 점광과 같은 식으로 클러스터에 넣는다. 수와 번호는 실수로 싣는다(2^24 안이면 정확하다).
             */
            static GPULight makeProbeVolumeElement( const LightProbeGrid& grid, vector<float4>& inoutListProbeElement )
            {
                const uint32   probeBase = static_cast<uint32>( inoutListProbeElement.size() ) / shaderslot::kLightProbeFloat4Count;
                const GPULight element   = makeVolumeElementFor( grid._layout, probeBase );
                for ( const SH9Color& probe : grid._listProbe )
                {
                    float4 arrElement[SH9Color::kGPUFloat4Count];
                    probe.makeGPUElement( arrElement );
                    inoutListProbeElement.insert( inoutListProbeElement.end(), arrElement, arrElement + SH9Color::kGPUFloat4Count );
                }
                return element;
            }

            /**
             * @brief 실시간 갱신 볼륨의 라이트 원소를 만들고, 프로브 버퍼에 자리(프로브마다 0 으로 채운 SH9 원소)를 잡습니다. 값은 GPU 의 프로브 갱신(`probeupdate.hlsl`)이 프레임마다 덮어쓴다.
             * @details 소비 셰이더는 베이크 볼륨과 같은 원소를 읽는다 — 다른 길이 없다. 자리 번호(`_params.w`)가 갱신 컴퓨트의 쓰기 위치이기도 하다.
             */
            static GPULight makeRealtimeProbeVolumeElement( const LightProbeGridLayout& layout, vector<float4>& inoutListProbeElement )
            {
                const uint32   probeBase = static_cast<uint32>( inoutListProbeElement.size() ) / shaderslot::kLightProbeFloat4Count;
                const GPULight element   = makeVolumeElementFor( layout, probeBase );
                inoutListProbeElement.resize( inoutListProbeElement.size() + static_cast<size_t>( layout.getProbeCount() ) * shaderslot::kLightProbeFloat4Count, float4{} );
                return element;
            }

            /**
             * @brief 실시간 볼륨 하나를 라이트 목록 · 프로브 자리 · 갱신 요청 목록에 넣습니다. 복셀 격자 캐시가 낡았으면(상자 · 칸 크기 · 프리미티브 집합이 바뀜) 여기서 다시 만든다.
             */
            static void appendRealtimeVolume( const GameObjectManager& manager, const LightProbeVolumeComponent& volume, vector<GPULight>& inoutList, vector<float4>& inoutListProbeElement,
                                              vector<RealtimeProbeRequest>& inoutListRequest )
            {
                const PrimitiveRegistry&         primitives = manager.getPrimitiveRegistry();
                const uint64                     generation = primitives.getSetGeneration();
                shared_ptr<const SceneVoxelGrid> pGrid      = volume.findRealtimeVoxelGrid( generation );
                if ( pGrid == nullptr )
                {
                    pGrid = make_shared<SceneVoxelGrid>( SceneVoxelizer::makeGrid( primitives, volume.computeRealtimeVoxelBounds(), volume.getVoxelSize() ) );
                    volume.setRealtimeVoxelGrid( pGrid, generation );
                }
                if ( pGrid->isEmpty() )
                    return; // 상자가 유효하지 않다 — 갱신할 장면이 없다

                const LightProbeGridLayout layout = volume.computeLayout();
                RealtimeProbeRequest       request{};
                request._volumeElement = makeRealtimeProbeVolumeElement( layout, inoutListProbeElement );
                request._volumeID      = volume.getComponentID();
                request._probeCount    = layout.getProbeCount();
                // 광선 수 · 주기는 스윕 스위치가 0 보다 크면 그 값이다(같은 씬으로 값만 바꿔 잰다).
                request._rayCount     = gv_realtimeProbeRayCount > 0 ? static_cast<uint32>( gv_realtimeProbeRayCount ) : volume.getRealtimeRayCount();
                request._updatePeriod = gv_realtimeProbeUpdatePeriod > 0 ? static_cast<uint32>( gv_realtimeProbeUpdatePeriod ) : volume.getRealtimeUpdatePeriod();
                request._blend        = volume.getRealtimeBlend();
                request._bounceAlbedo = volume.getBounceAlbedo();
                request._pVoxelGrid   = std::move( pGrid );
                inoutList.push_back( request._volumeElement );
                inoutListRequest.push_back( std::move( request ) );
            }

            /** @brief 격자 모양과 프로브 시작 번호로 볼륨 라이트 원소를 만듭니다(베이크와 실시간이 같은 원소를 쓴다). */
            static GPULight makeVolumeElementFor( const LightProbeGridLayout& layout, uint32 probeBase )
            {
                const float3 extent{ layout._spacing._x * static_cast<float32>( layout._countX - 1u ), layout._spacing._y * static_cast<float32>( layout._countY - 1u ),
                                     layout._spacing._z * static_cast<float32>( layout._countZ - 1u ) };
                const float3 center = layout._origin + extent * 0.5f;
                GPULight     element{};
                element._positionRadius = float4{ center._x, center._y, center._z, extent.getLength() * 0.5f };
                element._colorIntensity = float4{ layout._origin._x, layout._origin._y, layout._origin._z, 0.0f };
                element._directionType  = float4{ layout._spacing._x, layout._spacing._y, layout._spacing._z, static_cast<float32>( shaderslot::kLightTypeProbeVolume ) };
                element._params         = float4{ static_cast<float32>( layout._countX ), static_cast<float32>( layout._countY ), static_cast<float32>( layout._countZ ),
                                          static_cast<float32>( probeBase ) };
                return element;
            }

            /**
             * @brief 반사 프로브 하나를 라이트 원소(bindingslots.hlsli 8 절의 SW_LIGHT_TYPE_REFLECTION_PROBE 배치)로 만들고, 그 옥타헤드럴 텍셀을 프로브 버퍼(t13)에 붙입니다.
             * @details 텍셀 하나가 float4 하나(rgb, a 는 0)다 — SH9 원소와 같은 버퍼라 텍스처 슬롯이 없는 에뮬 백엔드도 같은 길로 읽는다. 경계 구는 상자의 외접구다.
             */
            static GPULight makeReflectionProbeElement( const ReflectionProbeComponent& probe, const ReflectionProbeAsset& data, vector<float4>& inoutListProbeElement )
            {
                const float3 center     = probe.getWorldPosition();
                const float3 halfExtent = probe.getExtent() * 0.5f;
                const uint32 firstTexel = static_cast<uint32>( inoutListProbeElement.size() );
                GPULight     element{};
                element._positionRadius = float4{ center._x, center._y, center._z, halfExtent.getLength() };
                element._colorIntensity = float4{ halfExtent._x, halfExtent._y, halfExtent._z, probe.getBlendDistance() };
                element._directionType  = float4{ data._capturePosition._x, data._capturePosition._y, data._capturePosition._z, static_cast<float32>( shaderslot::kLightTypeReflectionProbe ) };
                element._params         = float4{ static_cast<float32>( data._resolution ), static_cast<float32>( data._levelCount ), static_cast<float32>( firstTexel ), 0.0f };
                inoutListProbeElement.reserve( inoutListProbeElement.size() + data._listTexel.size() );
                for ( const float3& texel : data._listTexel )
                {
                    inoutListProbeElement.push_back( float4{ texel._x, texel._y, texel._z, 0.0f } );
                }
                return element;
            }
        };
        static_assert( shaderslot::kLightProbeFloat4Count == SH9Color::kGPUFloat4Count, "셰이더의 프로브 원소 수와 SH9Color 의 GPU 원소 수가 같아야 한다" );
    } // namespace

    void collectSceneLights( const Scene* pScene, vector<GPULight>& outList, vector<uint32>* pOutListShadowLightIndex, vector<float4>* pOutListProbeElement,
                             vector<GPULightCookie>* pOutListCookie, vector<RealtimeProbeRequest>* pOutListRealtimeProbe )
    {
        outList.clear();
        if ( pOutListRealtimeProbe != nullptr )
            pOutListRealtimeProbe->clear();
        if ( pOutListCookie != nullptr )
            pOutListCookie->clear();
        if ( pOutListProbeElement != nullptr )
            pOutListProbeElement->clear();
        if ( pOutListShadowLightIndex != nullptr )
            pOutListShadowLightIndex->clear();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;

        const ComponentRegistry& registry = pScene->getObjectManager()->getComponentRegistry();
        // 그림자 플래그는 그림자 행렬을 만드는 쪽(Engine, FrameRenderer)과 **같은 빛**에 붙어야 한다. 고르는 규칙은 씬의 한 함수다.
        const DirectionalLightComponent* pShadowLight = pScene->findShadowCastingDirectionalLight();

        // 종류 순서로 돈다. 방향광이 0 이라 앞에 온다(상한을 넘으면 뒤에서부터 잘린다). 원소의 필드는 빛이 스스로 쓴다
        // (`LightComponent::writeGPULight`). 여기는 종류를 모른다. 그림자 플래그만 씬이 고른 빛에 켠다.
        for ( uint32 lightType = 0; lightType < shaderslot::kLightTypeCount; ++lightType )
        {
            for ( const LightComponent* pLight : registry.getAll<LightComponent>( lightType ) )
            {
                if ( pLight == nullptr || pLight->isActive() == false )
                    continue;

                GPULight light{};
                pLight->writeGPULight( light );
                if ( pLight == pShadowLight )
                    light._params._x = 1.0f;
                // 그림자를 드리우는 점광과 스폿은 번호만 적는다. 아틀라스 슬롯(`_params.x`, `_params.w`)은 슬롯 수를 아는 쪽이 나눠 준다(ShadowSetupUtil).
                if ( pOutListShadowLightIndex != nullptr && GPULightBufferInternal::castsLocalShadow( *pLight ) )
                    pOutListShadowLightIndex->push_back( static_cast<uint32>( outList.size() ) );
                const hashed_string cookiePath = pLight->getCookieTexturePath();
                if ( pOutListCookie != nullptr && cookiePath.empty() == false )
                    pOutListCookie->push_back( GPULightCookie{ static_cast<uint32>( outList.size() ), cookiePath } );
                outList.push_back( light );
                // 빛 원소 바로 뒤에 붙는 원소(자유 모양 2D 빛의 변) — 셰이더가 그 빛의 개수 칸만큼 뒤를 읽는다.
                pLight->appendGPUTrailingElements( outList );
            }
        }

        // 파티클 라이트(점광, 그림자 없음)는 라이트 컴포넌트 뒤 · 2D 가림막 앞에 붙는다. 그리드는 종류를 묻지 않고 같은 목록에서 만든다(LightGrid).
        for ( const VFXComponent* pVFX : registry.getAll<VFXComponent>() )
        {
            if ( pVFX != nullptr && pVFX->isPendingDestroy() == false )
                pVFX->appendGPULights( outList );
        }

        // 라이트 프로브 볼륨은 3D 라이트 · 파티클 라이트 뒤에 붙는다. 쓸 수 있는 베이크가 없는 볼륨(베이크하지 않음 · 모양이 다름)은 넣지 않는다 —
        // 그곳은 볼륨이 없는 곳과 같다(이유는 볼륨이 읽을 때 남긴다).
        if ( pOutListProbeElement != nullptr )
        {
            for ( const LightProbeVolumeComponent* pVolume : registry.getAll<LightProbeVolumeComponent>() )
            {
                if ( pVolume == nullptr || pVolume->isActive() == false )
                    continue;
                if ( pVolume->isRealtime() )
                {
                    // 실시간 볼륨은 베이크를 보지 않는다. 요청 목록을 받지 않는 경로(미리보기)는 갱신을 돌리지 못하므로 넣지 않는다 — 그곳은 볼륨이 없는 곳과 같다.
                    if ( pOutListRealtimeProbe != nullptr )
                        GPULightBufferInternal::appendRealtimeVolume( *pScene->getObjectManager(), *pVolume, outList, *pOutListProbeElement, *pOutListRealtimeProbe );
                    continue;
                }
                const LightProbeGrid* pGrid = pVolume->findProbeData();
                if ( pGrid != nullptr )
                    outList.push_back( GPULightBufferInternal::makeProbeVolumeElement( *pGrid, *pOutListProbeElement ) );
            }
            // 반사 프로브는 볼륨 뒤에 붙는다. 쓸 수 있는 베이크가 없는 프로브(베이크하지 않음 · 옮긴 뒤 다시 베이크하지 않음)는 넣지 않는다.
            for ( const ReflectionProbeComponent* pProbe : registry.getAll<ReflectionProbeComponent>() )
            {
                const ReflectionProbeAsset* pData = ( pProbe != nullptr && pProbe->isActive() ) ? pProbe->findProbeData() : nullptr;
                if ( pData != nullptr )
                    outList.push_back( GPULightBufferInternal::makeReflectionProbeElement( *pProbe, *pData, *pOutListProbeElement ) );
            }
        }

        // 2D 그림자 가림막 세그먼트는 빛 **뒤에** 붙는다. 상한을 넘으면 뒤에서부터 잘리므로 빛보다 가림막이 먼저 빠진다(lighting2d.hlsli).
        for ( const ShadowCaster2DComponent* pCaster : registry.getAll<ShadowCaster2DComponent>() )
        {
            if ( pCaster != nullptr && pCaster->isActive() )
                pCaster->appendGPUShadowSegments( outList );
        }
    }

    uint32 GPULightBuffer::selectGPULightSlotCount( LightGridProducer producer, uint32 requestedSlotCount )
    {
        if ( producer != LightGridProducer::GPU )
            return 0;
        return MathUtil::min( requestedSlotCount, kMaxGPULight );
    }

    void GPULightBuffer::populate( const vector<GPULight>& listLight, const vector<GPULight>& listShadowTileElement, const vector<float4>& listProbeElement, bool bProbeWritable )
    {
        _bProbeWritable = bProbeWritable ? SW_TRUE : SW_FALSE;
        // 상한(GPU 구간을 뺀 몫)은 GPU 구간 수를 아는 update 가 맞춘다.
        _listLightScratch.assign( listLight.begin(), listLight.end() );
        _listTileScratch.assign( listShadowTileElement.begin(), listShadowTileElement.end() );
        _listProbeScratch.assign( listProbeElement.begin(), listProbeElement.end() );
    }

    void GPULightBuffer::update( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj, LightGridProducer producer, uint32 requestedGPULightSlotCount )
    {
        _listGridBase.assign( listGridViewProj.size(), invalid_index::kUint32 );
        _gpuGridViewCount  = 0;
        _gridProducer      = LightGridProducer::None;
        _probeElementCount = 0;

        // CPU 라이트와 GPU 구간이 프레임 상한을 나눠 쓴다. 넘치면 CPU 라이트를 뒤에서부터 자른다(GPU 구간은 방출기가 이미 상한 안에서 요청했다).
        _gpuLightCount = selectGPULightSlotCount( producer, requestedGPULightSlotCount );
        if ( requestedGPULightSlotCount > 0 && _gpuLightCount == 0 )
            reportGPULightRefused( requestedGPULightSlotCount );
        const uint32 cpuCount = limitCPULightCount();
        _cpuCount             = cpuCount;
        _count                = cpuCount + _gpuLightCount;
        if ( pDevice == nullptr || _count == 0 )
        {
            // 버퍼는 그대로 둔다. 라이트가 잠깐 0 이 되는 프레임마다 만들고 지우면 그것이 비용이다.
            // 셰이더는 g_SwLightCount 가 0 이면 라이트를 돌지 않는다(앰비언트만).
            return;
        }

        if ( uploadLightList( pDevice, cpuCount ) == false )
            return;

        uploadProbeElements( pDevice );

        if ( listGridViewProj.empty() )
            return;
        switch ( producer )
        {
            case LightGridProducer::None:
            {
                break;
            }
            case LightGridProducer::CPU:
            {
                updateCPUGrid( pDevice, listGridViewProj );
                break;
            }
            case LightGridProducer::GPU:
            {
                updateGPUGrid( pDevice, listGridViewProj );
                break;
            }
        }
    }

    uint32 GPULightBuffer::limitCPULightCount()
    {
        const uint32 cpuLimit = shaderslot::kMaxFrameLight - _gpuLightCount;
        uint32       cpuCount = static_cast<uint32>( _listLightScratch.size() );
        if ( cpuCount > cpuLimit )
        {
            if ( _bWarnedOverflow == SW_FALSE )
            {
                _bWarnedOverflow = SW_TRUE;
                SW_LOG_WARNING( "The light count exceeds the limit (%#, excluding %# GPU slots) - truncating from the front.", shaderslot::kMaxFrameLight, _gpuLightCount );
            }
            cpuCount = cpuLimit;
        }
        return cpuCount;
    }

    bool GPULightBuffer::uploadLightList( IRHIDevice* pDevice, uint32 cpuCount )
    {
        // 업로드 목록: [CPU 라이트][GPU 구간 — 죽은 라이트][그림자 타일]. 점광과 스폿 그림자 타일 원소는 구간 뒤에 붙는다 — 셰이더는 g_SwLightCount(= 둘의 합) + 라이트의
        // params.w 로 찾는다. 죽은 라이트는 반경 0 의 점광이라 그리드에 들지 않는다(lightgrid.hlsl classifyLight) — 컴퓨트가 이번 프레임에 쓰지 못한 슬롯이 낡은 값으로 남지 않는다.
        _listUploadScratch.assign( _listLightScratch.begin(), _listLightScratch.begin() + cpuCount );
        GPULight deadLight{};
        deadLight._directionType = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypePoint ) };
        for ( uint32 gpuSlot = 0; gpuSlot < _gpuLightCount; ++gpuSlot )
        {
            _listUploadScratch.push_back( deadLight );
        }
        _listUploadScratch.insert( _listUploadScratch.end(), _listTileScratch.begin(), _listTileScratch.end() );

        const GPULight* pUpload    = _listUploadScratch.data();
        const uint32    totalCount = static_cast<uint32>( _listUploadScratch.size() );
        const bool      bNeedsUAV  = _gpuLightCount > 0;
        const uint32    stride     = static_cast<uint32>( sizeof( GPULight ) );
        // UAV 없이 잡혀 있던 버퍼는 용량이 충분해도 다시 잡는다(ensureCapacity 는 용량만 본다). 백엔드가 UAV 를 거절한 버퍼는 다시 잡아도 같으니 한 번만 해 본다.
        const bool bLacksUAV = bNeedsUAV && _buffer.isValid() && _buffer._uav == kInvalidDescriptorIndex;
        if ( bLacksUAV && _bUAVDenied == SW_FALSE )
            _buffer.release( pDevice );
        if ( _buffer.ensureCapacity( pDevice, stride, totalCount, bNeedsUAV ? kLightBufferUsageWithUAV : kLightBufferUsage, true, bNeedsUAV, pUpload ) == false )
        {
            _count         = 0;
            _cpuCount      = 0;
            _gpuLightCount = 0;
            return false;
        }
        const bool bUAVDeniedNow = bNeedsUAV && _buffer._uav == kInvalidDescriptorIndex;
        if ( bUAVDeniedNow && _bUAVDenied == SW_FALSE )
        {
            _bUAVDenied = SW_TRUE;
            SW_LOG_ERROR( "Light buffer has no UAV - GPU particle lights stay dead" );
        }
        _buffer.upload( pDevice, pUpload, totalCount * stride );
        return true;
    }

    void GPULightBuffer::uploadProbeElements( IRHIDevice* pDevice )
    {
        // 라이트 프로브 SH9(t13). 볼륨 원소가 라이트 목록에 있으면 그 프로브들이 여기 있다 — 못 올리면 볼륨 원소가 빈 버퍼를 가리키므로 오류로 남긴다.
        if ( _listProbeScratch.empty() )
            return;

        const uint32 probeElementCount = static_cast<uint32>( _listProbeScratch.size() );
        const uint32 probeStride       = static_cast<uint32>( sizeof( float4 ) );
        // 실시간 볼륨이 있으면 프로브 갱신 컴퓨트가 이 버퍼를 쓴다 — UAV 가 있는 버퍼로 잡는다(UAV 없이 잡혀 있던 버퍼는 다시 잡는다. 백엔드가 거절한 버퍼는 한 번만 다시 해 본다).
        const bool bWritable      = _bProbeWritable == SW_TRUE;
        const bool bLacksProbeUAV = bWritable && _probeBuffer.isValid() && _probeBuffer._uav == kInvalidDescriptorIndex;
        if ( bLacksProbeUAV && _bProbeUAVDenied == SW_FALSE )
            _probeBuffer.release( pDevice );
        if ( _probeBuffer.ensureCapacity( pDevice, probeStride, probeElementCount, bWritable ? kLightBufferUsageWithUAV : kLightBufferUsage, true, bWritable, _listProbeScratch.data() ) )
        {
            _probeBuffer.upload( pDevice, _listProbeScratch.data(), probeElementCount * probeStride );
            _probeElementCount      = probeElementCount;
            const bool bNewlyDenied = bWritable && _probeBuffer._uav == kInvalidDescriptorIndex;
            if ( bNewlyDenied && _bProbeUAVDenied == SW_FALSE )
            {
                _bProbeUAVDenied = SW_TRUE;
                SW_LOG_ERROR( "Light probe buffer has no UAV - realtime probe volumes stay black" );
            }
        }
        else
        {
            SW_LOG_ERROR( "Light probe buffer (%# elements) could not be allocated - probe volumes give no indirect light this frame", probeElementCount );
        }
    }

    void GPULightBuffer::updateCPUGrid( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj )
    {
        // 뷰마다 그리드 하나 — 셰이더는 PassCB g_SwLightGridBase(지금 뷰의 첫 단어)로 찾는다.
        _listGridWordScratch.clear();
        if ( _listGrid.size() < listGridViewProj.size() )
            _listGrid.resize( listGridViewProj.size() );
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( listGridViewProj.size() ); ++viewIndex )
        {
            LightGrid& grid = _listGrid[viewIndex];
            if ( grid.populate( listGridViewProj[viewIndex], _listUploadScratch.data(), _cpuCount ) == false )
            {
                reportGridFailure( "has no usable projection (near/far plane)", viewIndex );
                continue;
            }
            _listGridBase[viewIndex] = static_cast<uint32>( _listGridWordScratch.size() );
            _listGridWordScratch.insert( _listGridWordScratch.end(), grid.getWords().begin(), grid.getWords().end() );
        }
        if ( _listGridWordScratch.empty() )
            return;

        constexpr RHIBufferUsage kUsage    = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        const uint32             wordCount = static_cast<uint32>( _listGridWordScratch.size() );
        if ( _cpuGridBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), wordCount, kUsage, true, false, _listGridWordScratch.data() ) == false )
        {
            reportGridFailure( "could not allocate the CPU grid buffer", 0 );
            _listGridBase.assign( listGridViewProj.size(), invalid_index::kUint32 );
            return;
        }
        _cpuGridBuffer.upload( pDevice, _listGridWordScratch.data(), wordCount * static_cast<uint32>( sizeof( uint32 ) ) );
        _gridProducer = LightGridProducer::CPU;
    }

    void GPULightBuffer::updateGPUGrid( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj )
    {
        namespace grid = shaderslot::lightgrid;
        // 뷰 정보(bindingslots.hlsli 10 절): 행 넷 · (가까운 면 w, 조각 배율, 원근, 0) · asuint(라이트 수, 그리드 첫 단어, 0, 0). 그룹 번호가 곧 이 목록의 번호다.
        // 뷰-투영이 깨진 뷰는 목록에서 뺀다 — 그 뷰는 그리드 없이 그린다.
        _listGridViewScratch.clear();
        uint32 gridViewCount = 0;
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( listGridViewProj.size() ); ++viewIndex )
        {
            LightGridSlicing slicing{};
            const float4x4&  viewProj = listGridViewProj[viewIndex];
            if ( LightGrid::computeSlicing( viewProj, slicing ) == false )
            {
                reportGridFailure( "has no usable projection (near/far plane)", viewIndex );
                continue;
            }
            const uint32 gridBase = gridViewCount * grid::kGPUWordsPerView;
            const uint32 arrCount[4]{ _count, gridBase, 0u, 0u };
            float4       counts{};
            Memory::copy( &counts, arrCount, sizeof( counts ) );
            _listGridViewScratch.push_back( float4{ viewProj._11, viewProj._12, viewProj._13, viewProj._14 } );
            _listGridViewScratch.push_back( float4{ viewProj._21, viewProj._22, viewProj._23, viewProj._24 } );
            _listGridViewScratch.push_back( float4{ viewProj._31, viewProj._32, viewProj._33, viewProj._34 } );
            _listGridViewScratch.push_back( float4{ viewProj._41, viewProj._42, viewProj._43, viewProj._44 } );
            _listGridViewScratch.push_back( float4{ slicing._nearDepth, slicing._sliceScale, slicing._bPerspective == SW_TRUE ? 1.0f : 0.0f, 0.0f } );
            _listGridViewScratch.push_back( counts );
            _listGridBase[viewIndex] = gridBase;
            ++gridViewCount;
        }
        static_assert( grid::kGPUViewFloat4Count == 6, "뷰 정보의 float4 수가 위의 push_back 수와 같아야 한다" );
        if ( gridViewCount == 0 )
            return;

        constexpr RHIBufferUsage kViewUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        constexpr RHIBufferUsage kGridUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
        const uint32             viewCount  = static_cast<uint32>( _listGridViewScratch.size() );
        const bool               bViewReady = _gpuGridViewBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( float4 ) ), viewCount, kViewUsage, true, false, _listGridViewScratch.data() );
        const bool               bGridReady = _gpuGridBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), gridViewCount * grid::kGPUWordsPerView, kGridUsage, true, true, nullptr ) &&
                                _gpuGridBuffer._uav != kInvalidDescriptorIndex;
        if ( bViewReady == false || bGridReady == false )
        {
            reportGridFailure( "could not allocate the GPU grid buffers (UAV)", 0 );
            _listGridBase.assign( listGridViewProj.size(), invalid_index::kUint32 );
            return;
        }
        _gpuGridViewBuffer.upload( pDevice, _listGridViewScratch.data(), viewCount * static_cast<uint32>( sizeof( float4 ) ) );
        _gpuGridViewCount = gridViewCount;
        _gridProducer     = LightGridProducer::GPU;
    }

    void GPULightBuffer::reportGridFailure( const utf8* pReason, uint32 viewIndex )
    {
        if ( _bWarnedGrid == SW_TRUE )
            return;
        _bWarnedGrid = SW_TRUE;
        SW_LOG_ERROR( "Light grid for view %# %# - that view walks every light", viewIndex, pReason );
    }

    void GPULightBuffer::reportGPULightRefused( uint32 requestedSlotCount )
    {
        if ( _bWarnedGPULightRefused == SW_TRUE )
            return;
        _bWarnedGPULightRefused = SW_TRUE;
        // 대신 생산자를 GPU 로 바꾸지 않는다 — 생산자는 데이터와 빌드(gv_lightCulling)가 정했다. 요청한 방출기의 빛만 그리지 않는다.
        SW_LOG_ERROR( "GPU particle lights (%# slots) need the GPU light grid producer (gv_lightCulling=GPU) - the lights are not drawn", requestedSlotCount );
    }

    const RHIStructuredBufferSlot* GPULightBuffer::findProbeBuffer() const
    {
        const bool bReady = _probeElementCount > 0 && _probeBuffer._buffer != 0 && _probeBuffer._srv != kInvalidDescriptorIndex;
        return bReady ? &_probeBuffer : nullptr;
    }

    const RHIStructuredBufferSlot* GPULightBuffer::findGridBuffer() const
    {
        switch ( _gridProducer )
        {
            case LightGridProducer::None:
                return nullptr;
            case LightGridProducer::CPU:
                return &_cpuGridBuffer;
            case LightGridProducer::GPU:
                return &_gpuGridBuffer;
        }
        return nullptr;
    }

    void GPULightBuffer::release( IRHIDevice* pDevice )
    {
        _buffer.release( pDevice );
        _cpuGridBuffer.release( pDevice );
        _gpuGridBuffer.release( pDevice );
        _gpuGridViewBuffer.release( pDevice );
        _probeBuffer.release( pDevice );
        _probeElementCount = 0;
        _count             = 0;
        _cpuCount          = 0;
        _gpuLightCount     = 0;
        _bUAVDenied        = SW_FALSE;
        _gpuGridViewCount  = 0;
        _gridProducer      = LightGridProducer::None;
        _listGridBase.clear();
    }
} // namespace sw
