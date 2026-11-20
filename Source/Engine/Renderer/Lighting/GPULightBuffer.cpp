#include "pch.h"

#include "Engine/Renderer/Lighting/GPULightBuffer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/Frustum.h"
#include "Core/Memory/Memory.h"

#include "Engine/Environment/Sky/SkyLight.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Lighting/SceneVoxelizer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Visual/Component/DirectionalLightComponent.h"
#include "Engine/Visual/Component/LightProbeVolumeComponent.h"
#include "Engine/Visual/Component/PointLightComponent.h"
#include "Engine/Visual/Component/ReflectionProbeComponent.h"
#include "Engine/Visual/Component/SpotLightComponent.h"

namespace sw
{
    /**
     * @brief `-gv_realtimeProbeRayCount=N`: 실시간 프로브 볼륨의 프로브당 광선 수를 N 으로 덮어씁니다(0 이면 볼륨의 `_realtimeRayCount`) — 광선 수별 GPU 시간(RealtimeProbeUpdateSweep)을 한 씬으로 잽니다.
     * @details 프로브 수는 볼륨의 `_spacing` 이 정해서 씬 파일로 가른다(`game/playground/maps/realtimegi_dense` · `realtimegi_sparse`).
     *          GI 품질 · 비용 설정이라 배포본에도 둔다 — 언리얼 Lumen 의 광선 수 · 갱신 cvar(`r.Lumen.*`, 확장성 묶음 sg.GlobalIlluminationQuality 가 고른다)와 같은 자리다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_realtimeProbeRayCount, 0, "실시간 프로브 볼륨의 프로브당 광선 수 덮어쓰기(0 이면 볼륨의 값)" );
    /** @brief `-gv_realtimeProbeUpdatePeriod=N`: 실시간 프로브 볼륨의 갱신 주기(프레임)를 N 으로 덮어씁니다(0 이면 볼륨의 `_realtimeUpdatePeriod`). 광선 수와 같은 GI 비용 설정이다. */
    SW_GLOBAL_VARIABLE( int32, gv_realtimeProbeUpdatePeriod, 0, "실시간 프로브 볼륨의 갱신 주기(프레임) 덮어쓰기(0 이면 볼륨의 값)" );

    namespace
    {
        constexpr RHIBufferUsage kLightBufferUsage        = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
        constexpr RHIBufferUsage kLightBufferUsageWithUAV = kLightBufferUsage | RHIBufferUsage::UnorderedAccess;

        struct GPULightBufferInternal
        {
            /// @brief 빛 고르기(`cullLightsToLimit`)의 카메라 깊이 하한 — 반경의 이 비율이다. 카메라가 빛 구 안이면 값이 끝없이 커지지 않는다(그림자 칸 우선순위와 같은 식).
            static constexpr float32 kCullMinDepthFraction = 0.1f;

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
             * @brief 프로브 볼륨 하나를 라이트 원소(binding_slots.hlsli 8 절의 SW_LIGHT_TYPE_PROBE_VOLUME 배치)로 만들고, 그 프로브들의 SH9 원소 자리를 잡습니다.
             * @details 경계 구는 상자의 외접구다 — 라이트 그리드가 점광과 같은 식으로 클러스터에 넣는다. 수와 번호는 실수로 싣는다(2^24 안이면 정확하다).
             */
            static GPULight makeProbeVolumeElement( const LightProbeVolumeComponent& volume, const LightProbeGrid& grid, LightProbeElementBuilder& inoutProbeElement )
            {
                const uint32 probeBase = inoutProbeElement.appendBakedVolume( volume.getComponentID(), volume.getProbeDataRevision(), grid );
                return makeVolumeElementFor( grid._layout, probeBase );
            }

            /**
             * @brief 실시간 갱신 볼륨의 라이트 원소를 만들고, 프로브 버퍼에 자리(프로브마다 0 으로 채운 SH9 원소와 가시성 모먼트)를 잡습니다. 값은 GPU 의 프로브 갱신(`probe_update.hlsl`)이 프레임마다 덮어쓴다.
             * @details 소비 셰이더는 베이크 볼륨과 같은 원소를 읽는다 — 다른 길이 없다. 자리 번호(`_params.w`)가 갱신 컴퓨트의 쓰기 위치이기도 하다.
             *          모먼트 자리(`_extra.x`, 있음 `_extra.y` = 1)는 실시간 볼륨만 있다 — 소비 셰이더가 체비쇼프 가시성으로 벽 너머 프로브를 줄인다(binding_slots.hlsli 8 절).
             */
            static GPULight makeRealtimeProbeVolumeElement( const LightProbeGridLayout& layout, LightProbeElementBuilder& inoutProbeElement )
            {
                const RealtimeProbeSlot slot    = inoutProbeElement.appendRealtimeVolume( layout.getProbeCount() );
                GPULight                element = makeVolumeElementFor( layout, slot._probeBase );
                element._extra                  = float4{ static_cast<float32>( slot._visibilityBase ), 1.0f, 0.0f, 0.0f };
                return element;
            }

            /**
             * @brief 프리미티브 집합이 바뀐 뒤의 복셀 격자입니다 — 그 사이 등록 · 해제된 메시 칸만 다시 칠하고(`SceneVoxelizer::updateGrid`), 기록이 모자라면 다시 만듭니다.
             * @details 가리개가 아니거나 격자 밖인 칸의 변화(움직이는 투사체 스폰 · 소멸, 반투명, 먼 곳)는 격자를 그대로 둔다 — 같은 포인터라 GPU 로 다시 올리지도 않는다.
             *          칠한 칸이 바뀌면 사본에 칠해 새 포인터로 넣는다(지난 격자는 렌더 스레드가 아직 들 수 있다).
             */
            static shared_ptr<const SceneVoxelGrid> refreshVoxelGrid( shared_ptr<const SceneVoxelGrid> pCached, const PrimitiveRegistry& primitives, Material* pSceneDefault,
                                                                      const LightProbeVolumeComponent& volume, uint64 cacheGeneration, uint64 generation )
            {
                vector<uint32> listChangedSlot;
                if ( primitives.collectMeshSetChanges( cacheGeneration, listChangedSlot ) == false )
                {
                    shared_ptr<const SceneVoxelGrid> pRebuilt = make_shared<SceneVoxelGrid>( SceneVoxelizer::makeGrid( primitives, pSceneDefault, volume.computeRealtimeVoxelBounds(), volume.getVoxelSize() ) );
                    volume.setRealtimeVoxelGrid( pRebuilt, generation );
                    return pRebuilt;
                }
                SceneVoxelGrid updated = *pCached;
                if ( SceneVoxelizer::updateGrid( updated, primitives, pSceneDefault, listChangedSlot ) )
                    pCached = make_shared<SceneVoxelGrid>( std::move( updated ) );
                volume.setRealtimeVoxelGrid( pCached, generation );
                return pCached;
            }

            /**
             * @brief 실시간 볼륨 하나를 라이트 목록 · 프로브 자리 · 갱신 요청 목록에 넣습니다. 복셀 격자 캐시가 낡았으면(상자 · 칸 크기 · 프리미티브 집합이 바뀜) 여기서 다시 만든다.
             */
            static void appendRealtimeVolume( const Scene& scene, const GameObjectManager& manager, const LightProbeVolumeComponent& volume, vector<GPULight>& inoutList, LightProbeElementBuilder& inoutProbeElement,
                                              vector<RealtimeProbeRequest>& inoutListRequest )
            {
                const PrimitiveRegistry&         primitives      = manager.getPrimitiveRegistry();
                const uint64                     generation      = primitives.getSetGeneration();
                uint64                           cacheGeneration = 0;
                shared_ptr<const SceneVoxelGrid> pGrid           = volume.findRealtimeVoxelGrid( cacheGeneration );
                if ( pGrid == nullptr )
                {
                    pGrid = make_shared<SceneVoxelGrid>( SceneVoxelizer::makeGrid( primitives, scene.getMaterial(), volume.computeRealtimeVoxelBounds(), volume.getVoxelSize() ) );
                    volume.setRealtimeVoxelGrid( pGrid, generation );
                }
                else if ( cacheGeneration != generation )
                    pGrid = refreshVoxelGrid( std::move( pGrid ), primitives, scene.getMaterial(), volume, cacheGeneration, generation );
                if ( pGrid->isEmpty() )
                    return; // 상자가 유효하지 않다 — 갱신할 장면이 없다

                const LightProbeGridLayout layout = volume.computeLayout();
                RealtimeProbeRequest       request{};
                request._volumeElement = makeRealtimeProbeVolumeElement( layout, inoutProbeElement );
                // 하늘 가림 세기 — 소비 셰이더가 프로브의 하늘 보임(probe_update.hlsl 이 원소 6 의 w 에 쓴다)으로 반사 앰비언트를 가린다(binding_slots.hlsli 8 절 원소 배치).
                request._volumeElement._colorIntensity._w = volume.getRealtimeSkyOcclusion();
                request._volumeID                         = volume.getComponentID();
                request._probeCount                       = layout.getProbeCount();
                // 광선 수 · 주기는 스윕 스위치가 0 보다 크면 그 값이다(같은 씬으로 값만 바꿔 잰다).
                request._rayCount          = gv_realtimeProbeRayCount > 0 ? static_cast<uint32>( gv_realtimeProbeRayCount ) : volume.getRealtimeRayCount();
                request._updatePeriod      = gv_realtimeProbeUpdatePeriod > 0 ? static_cast<uint32>( gv_realtimeProbeUpdatePeriod ) : volume.getRealtimeUpdatePeriod();
                request._blend             = volume.getRealtimeBlend();
                request._bounceAlbedo      = volume.getBounceAlbedo();
                request._emissiveIntensity = volume.getRealtimeEmissiveIntensity();
                request._pVoxelGrid        = std::move( pGrid );
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
             * @brief 반사 프로브 하나를 라이트 원소(binding_slots.hlsli 8 절의 SW_LIGHT_TYPE_REFLECTION_PROBE 배치)로 만들고, 그 옥타헤드럴 텍셀을 프로브 버퍼(t13)에 붙입니다.
             * @details 텍셀 하나가 float4 하나(rgb, a 는 0)다 — SH9 원소와 같은 버퍼라 텍스처 슬롯이 없는 에뮬 백엔드도 같은 길로 읽는다. 경계 구는 상자의 외접구다.
             */
            static GPULight makeReflectionProbeElement( const ReflectionProbeComponent& probe, const ReflectionProbeAsset& data, LightProbeElementBuilder& inoutProbeElement )
            {
                const float3 center     = probe.getWorldPosition();
                const float3 halfExtent = probe.getExtent() * 0.5f;
                const uint32 firstTexel = inoutProbeElement.appendReflectionProbe( probe.getComponentID(), probe.getProbeDataRevision(), data );
                GPULight     element{};
                element._positionRadius = float4{ center._x, center._y, center._z, halfExtent.getLength() };
                element._colorIntensity = float4{ halfExtent._x, halfExtent._y, halfExtent._z, probe.getBlendDistance() };
                element._directionType  = float4{ data._capturePosition._x, data._capturePosition._y, data._capturePosition._z, static_cast<float32>( shaderslot::kLightTypeReflectionProbe ) };
                element._params         = float4{ static_cast<float32>( data._resolution ), static_cast<float32>( data._levelCount ), static_cast<float32>( firstTexel ), 0.0f };
                return element;
            }
        };
        static_assert( shaderslot::kLightProbeFloat4Count == SH9Color::kGPUFloat4Count, "셰이더의 프로브 원소 수와 SH9Color 의 GPU 원소 수가 같아야 한다" );
    } // namespace

    void LightProbeElementBuilder::begin()
    {
        _listKeyScratch.clear();
        _listSourceScratch.clear();
        _elementCountScratch = 0;
        _skyLight            = float4{};
    }

    uint32 LightProbeElementBuilder::appendBakedVolume( uint64 componentID, uint32 revision, const LightProbeGrid& grid )
    {
        const uint32 probeBase = _elementCountScratch / shaderslot::kLightProbeFloat4Count;
        _listKeyScratch.push_back( componentID );
        _listKeyScratch.push_back( revision );
        _listSourceScratch.push_back( Source{ &grid, nullptr, nullptr, 0 } );
        _elementCountScratch += static_cast<uint32>( grid._listProbe.size() ) * SH9Color::kGPUFloat4Count;
        return probeBase;
    }

    uint32 LightProbeElementBuilder::computeRealtimeElementCount( uint32 probeCount )
    {
        const uint32 momentCount = probeCount * shaderslot::kProbeVisibilityFloat4Count;
        const uint32 roundedUp   = ( momentCount + shaderslot::kLightProbeFloat4Count - 1u ) / shaderslot::kLightProbeFloat4Count * shaderslot::kLightProbeFloat4Count;
        return probeCount * shaderslot::kLightProbeFloat4Count + roundedUp;
    }

    RealtimeProbeSlot LightProbeElementBuilder::appendRealtimeVolume( uint32 probeCount )
    {
        RealtimeProbeSlot slot;
        slot._probeBase      = _elementCountScratch / shaderslot::kLightProbeFloat4Count;
        slot._visibilityBase = _elementCountScratch + probeCount * shaderslot::kLightProbeFloat4Count;
        // 0 자리의 내용은 수만 정한다. 컴포넌트 번호 0 은 쓰이지 않는 번호라 베이크 원본과 섞이지 않는다.
        _listKeyScratch.push_back( 0 );
        _listKeyScratch.push_back( probeCount );
        _listSourceScratch.push_back( Source{ nullptr, nullptr, nullptr, probeCount } );
        _elementCountScratch += computeRealtimeElementCount( probeCount );
        return slot;
    }

    uint32 LightProbeElementBuilder::appendReflectionProbe( uint64 componentID, uint32 revision, const ReflectionProbeAsset& data )
    {
        const uint32 firstTexel = _elementCountScratch;
        _listKeyScratch.push_back( componentID );
        _listKeyScratch.push_back( revision );
        _listSourceScratch.push_back( Source{ nullptr, &data, nullptr, 0 } );
        _elementCountScratch += static_cast<uint32>( data._listTexel.size() );
        return firstTexel;
    }

    void LightProbeElementBuilder::appendSkyLight( const SkyLightData& data )
    {
        const ReflectionProbeAsset& reflection = data._reflection;
        _skyLight                              = float4{ static_cast<float32>( reflection._resolution ), static_cast<float32>( reflection._levelCount ), static_cast<float32>( _elementCountScratch ), 0.0f };
        // 컴포넌트 번호가 될 수 없는 값이 스카이 라이트의 열쇠다 — 다른 원본과 섞이지 않는다.
        const uint64 skyLightKey = constant::invalid_index::kUint64;
        _listKeyScratch.push_back( skyLightKey );
        _listKeyScratch.push_back( data._revision );
        _listSourceScratch.push_back( Source{ nullptr, nullptr, &data, 0 } );
        _elementCountScratch += static_cast<uint32>( reflection._listTexel.size() ) + SH9Color::kGPUFloat4Count;
    }

    void LightProbeElementBuilder::finish()
    {
        if ( _listSourceScratch.empty() )
        {
            _pElements.reset();
            _listKey.clear();
            return;
        }
        if ( _pElements != nullptr && _listKeyScratch == _listKey )
            return;
        rebuildElements();
        _listKey.swap( _listKeyScratch );
    }

    void LightProbeElementBuilder::rebuildElements()
    {
        // 렌더 스레드가 지난 묶음을 아직 들고 있을 수 있어 새로 만든다(제자리에 쓰지 않는다). 열쇠가 바뀐 프레임에만 온다.
        auto pElements = make_shared<vector<float4>>();
        pElements->reserve( _elementCountScratch );
        for ( const Source& source : _listSourceScratch )
        {
            if ( source._pGrid != nullptr )
            {
                for ( const SH9Color& probe : source._pGrid->_listProbe )
                {
                    float4 arrElement[SH9Color::kGPUFloat4Count];
                    probe.makeGPUElement( arrElement );
                    pElements->insert( pElements->end(), arrElement, arrElement + SH9Color::kGPUFloat4Count );
                }
            }
            else if ( source._pReflection != nullptr )
            {
                for ( const float3& texel : source._pReflection->_listTexel )
                {
                    pElements->push_back( float4{ texel._x, texel._y, texel._z, 0.0f } );
                }
            }
            else if ( source._pSkyLight != nullptr )
            {
                // 텍셀(반사 프로브와 같은 차례) 뒤에 SH9 원소 — 셰이더 SkyLight 가 첫 원소 + 한 변² × 단계 수에서 SH9 를 읽는다.
                for ( const float3& texel : source._pSkyLight->_reflection._listTexel )
                {
                    pElements->push_back( float4{ texel._x, texel._y, texel._z, 0.0f } );
                }
                float4 arrElement[SH9Color::kGPUFloat4Count];
                source._pSkyLight->_ambient.makeGPUElement( arrElement );
                pElements->insert( pElements->end(), arrElement, arrElement + SH9Color::kGPUFloat4Count );
            }
            else
            {
                pElements->resize( pElements->size() + computeRealtimeElementCount( source._realtimeProbeCount ), float4{} ); // SH9 0 자리 + 모먼트 0 자리(모름)
            }
        }
        _pElements = std::move( pElements );
    }

    void collectSceneLights( const Scene* pScene, vector<GPULight>& outList, vector<LocalShadowLight>* pOutListShadowLight, LightProbeElementBuilder* pOutProbeElement,
                             vector<GPULightCookie>* pOutListCookie, vector<RealtimeProbeRequest>* pOutListRealtimeProbe, const SkyLightData* pSkyLight )
    {
        outList.clear();
        if ( pOutListRealtimeProbe != nullptr )
            pOutListRealtimeProbe->clear();
        if ( pOutListCookie != nullptr )
            pOutListCookie->clear();
        if ( pOutProbeElement != nullptr )
            pOutProbeElement->begin();
        if ( pOutListShadowLight != nullptr )
            pOutListShadowLight->clear();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            if ( pOutProbeElement != nullptr )
                pOutProbeElement->finish();
            return;
        }

        const ComponentRegistry& registry = pScene->getObjectManager()->getComponentRegistry();
        // 그림자 플래그는 그림자 행렬을 만드는 쪽(Engine, FrameRenderer)과 **같은 빛**에 붙어야 한다. 고르는 규칙은 씬의 한 함수다.
        const DirectionalLightComponent* pShadowLight = DirectionalLightComponent::findShadowCasting( pScene->getObjectManager() );

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
                // 그림자를 드리우는 점광과 스폿은 번호와 정체만 적는다. 아틀라스 슬롯(`_params.x`, `_params.w`)은 슬롯 수를 아는 쪽이 나눠 준다(FrameShadowData).
                if ( pOutListShadowLight != nullptr && GPULightBufferInternal::castsLocalShadow( *pLight ) )
                    pOutListShadowLight->push_back( LocalShadowLight{ static_cast<uint32>( outList.size() ), pLight->getComponentID() } );
                const hashed_string cookiePath = pLight->getCookieTexturePath();
                if ( pOutListCookie != nullptr && cookiePath.empty() == false )
                    pOutListCookie->push_back( GPULightCookie{ static_cast<uint32>( outList.size() ), cookiePath } );
                outList.push_back( light );
                // 빛 원소 바로 뒤에 붙는 원소(자유 모양 2D 빛의 변) — 셰이더가 그 빛의 개수 칸만큼 뒤를 읽는다.
                pLight->appendGPUTrailingElements( outList );
            }
        }

        // 파티클 라이트(점광, 그림자 없음)는 라이트 컴포넌트 뒤 · 2D 가림막 앞에 붙는다. 그리드는 종류를 묻지 않고 같은 목록에서 만든다(LightGrid).
        pScene->getObjectManager()->collectServiceLights( SceneLightPhase::Particles, outList );

        // 라이트 프로브 볼륨은 3D 라이트 · 파티클 라이트 뒤에 붙는다. 쓸 수 있는 베이크가 없는 볼륨(베이크하지 않음 · 모양이 다름)은 넣지 않는다 —
        // 그곳은 볼륨이 없는 곳과 같다(이유는 볼륨이 읽을 때 남긴다).
        if ( pOutProbeElement != nullptr )
        {
            for ( const LightProbeVolumeComponent* pVolume : registry.getAll<LightProbeVolumeComponent>() )
            {
                if ( pVolume == nullptr || pVolume->isActive() == false )
                    continue;
                if ( pVolume->isRealtime() )
                {
                    // 실시간 볼륨은 베이크를 보지 않는다. 요청 목록을 받지 않는 경로(미리보기)는 갱신을 돌리지 못하므로 넣지 않는다 — 그곳은 볼륨이 없는 곳과 같다.
                    if ( pOutListRealtimeProbe != nullptr )
                        GPULightBufferInternal::appendRealtimeVolume( *pScene, *pScene->getObjectManager(), *pVolume, outList, *pOutProbeElement, *pOutListRealtimeProbe );
                    continue;
                }
                const LightProbeGrid* pGrid = pVolume->findProbeData();
                if ( pGrid != nullptr )
                    outList.push_back( GPULightBufferInternal::makeProbeVolumeElement( *pVolume, *pGrid, *pOutProbeElement ) );
            }
            // 반사 프로브는 볼륨 뒤에 붙는다. 쓸 수 있는 베이크가 없는 프로브(베이크하지 않음 · 옮긴 뒤 다시 베이크하지 않음)는 넣지 않는다.
            for ( const ReflectionProbeComponent* pProbe : registry.getAll<ReflectionProbeComponent>() )
            {
                const ReflectionProbeAsset* pData = ( pProbe != nullptr && pProbe->isActive() ) ? pProbe->findProbeData() : nullptr;
                if ( pData != nullptr )
                    outList.push_back( GPULightBufferInternal::makeReflectionProbeElement( *pProbe, *pData, *pOutProbeElement ) );
            }
            // 스카이 라이트는 묶음 끝에 붙는다 — 라이트 원소가 아니라서 목록 · 그리드 · 상한 고르기에 들지 않는다.
            if ( pSkyLight != nullptr )
                pOutProbeElement->appendSkyLight( *pSkyLight );
            pOutProbeElement->finish();
        }

        // 2D 그림자 가림막 세그먼트는 빛 **뒤에** 붙는다. 상한을 넘으면 뒤에서부터 잘리므로 빛보다 가림막이 먼저 빠진다(lighting_2d.hlsli).
        pScene->getObjectManager()->collectServiceLights( SceneLightPhase::ShadowOccluders, outList );
    }

    uint32 GPULightBuffer::selectGPULightSlotCount( LightGridProducer producer, uint32 requestedSlotCount )
    {
        if ( producer != LightGridProducer::GPU )
            return 0;
        return MathUtil::min( requestedSlotCount, kMaxGPULight );
    }

    void GPULightBuffer::populate( const vector<GPULight>& listLight, const vector<GPULight>& listShadowTileElement, const shared_ptr<const vector<float4>>& pProbeElements, bool bProbeWritable )
    {
        _bProbeWritable = bProbeWritable ? SW_TRUE : SW_FALSE;
        // 상한(GPU 구간을 뺀 몫)은 GPU 구간 수를 아는 update 가 맞춘다.
        _listLightScratch.assign( listLight.begin(), listLight.end() );
        _listSourceToUploaded.clear();
        _listTileScratch.assign( listShadowTileElement.begin(), listShadowTileElement.end() );
        _pProbeElements = pProbeElements;
    }

    void GPULightBuffer::update( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj, LightGridProducer producer, uint32 requestedGPULightSlotCount )
    {
        _listGridBase.assign( listGridViewProj.size(), constant::invalid_index::kUint32 );
        _gpuGridViewCount  = 0;
        _gridProducer      = LightGridProducer::None;
        _probeElementCount = 0;

        // CPU 라이트와 GPU 구간이 프레임 상한을 나눠 쓴다. 넘치면 CPU 라이트를 뷰 컬링 · 중요도로 골라 줄인다(GPU 구간은 방출기가 이미 상한 안에서 요청했다).
        _gpuLightCount = selectGPULightSlotCount( producer, requestedGPULightSlotCount );
        if ( requestedGPULightSlotCount > 0 && _gpuLightCount == 0 )
            reportGPULightRefused( requestedGPULightSlotCount );
        const uint32 cpuCount = limitCPULightCount( listGridViewProj );
        _cpuCount             = cpuCount;
        _count                = cpuCount + _gpuLightCount;
        if ( pDevice == nullptr )
            return;
        // 프로브 버퍼는 라이트 수와 무관하게 올린다 — 스카이 라이트는 라이트 원소 없이 이 버퍼만 쓴다(빛이 하나도 없는 하늘 장면도 하늘빛을 받는다).
        uploadProbeElements( pDevice );
        if ( _count == 0 )
        {
            // 버퍼는 그대로 둔다. 라이트가 잠깐 0 이 되는 프레임마다 만들고 지우면 그것이 비용이다.
            // 셰이더는 g_SwLightCount 가 0 이면 라이트를 돌지 않는다(환경광만).
            return;
        }

        if ( uploadLightList( pDevice, cpuCount ) == false )
            return;

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

    uint32 GPULightBuffer::limitCPULightCount( const vector<float4x4>& listViewProj )
    {
        const uint32 cpuLimit = shaderslot::kMaxFrameLight - _gpuLightCount;
        // 이미 줄인 목록(같은 프레임에 update 를 다시 불렀다)은 넘지 않으므로 번호 표를 그대로 둔다 — 표는 populate 가 비운다.
        if ( _listLightScratch.size() <= cpuLimit )
            return static_cast<uint32>( _listLightScratch.size() );
        if ( _bWarnedOverflow == SW_FALSE )
        {
            _bWarnedOverflow = SW_TRUE;
            SW_LOG_WARNING_ID( "GPULightBuffer.LightCountExceedsLimit", "The light count %# exceeds the limit (%#, excluding %# GPU slots) - lights outside the views and the least important are dropped.",
                               static_cast<uint64>( _listLightScratch.size() ), shaderslot::kMaxFrameLight, _gpuLightCount );
        }
        return cullLightsToLimit( _listLightScratch, cpuLimit, listViewProj, _listSourceToUploaded );
    }

    uint32 GPULightBuffer::cullLightsToLimit( vector<GPULight>& inoutListLight, uint32 limit, const vector<float4x4>& listViewProj, vector<uint32>& outListSourceToKept )
    {
        outListSourceToKept.clear();
        const uint32 sourceCount = static_cast<uint32>( inoutListLight.size() );
        if ( sourceCount <= limit )
            return sourceCount;

        // 단위 하나 — 머리 원소와 뒤에 붙은 꼬리(자유 모양 2D 빛의 변)의 구간과 중요도. 중요도가 음수면 어느 뷰에도 닿지 않아 뺀다.
        struct LightUnit
        {
            float32 _importance{ 0.0f };
            uint32  _first{ 0 };
            uint32  _count{ 0 };
        };
        // 늘 남는 원소(방향광 · 전역 2D 빛)와 빛 다음 차례(2D 가림막)의 값. 빛의 중요도는 0 보다 크다.
        constexpr float32 kAlwaysKept   = 3.0e38f;
        constexpr float32 kAfterLights  = 0.0f;
        constexpr float32 kOutsideViews = -1.0f;
        vector<Frustum>   listFrustum;
        listFrustum.reserve( listViewProj.size() );
        for ( const float4x4& viewProj : listViewProj )
        {
            listFrustum.push_back( Frustum::fromViewProjection( viewProj ) );
        }

        vector<LightUnit> listUnit;
        for ( uint32 first = 0; first < sourceCount; )
        {
            uint32 end = first + 1u;
            while ( end < sourceCount && static_cast<uint32>( inoutListLight[end]._directionType._w + 0.5f ) == shaderslot::kLightTypeFreeform2DEdge )
                ++end;
            const GPULight& head      = inoutListLight[first];
            const uint32    lightType = static_cast<uint32>( head._directionType._w + 0.5f );
            float32         importance{ kAfterLights };
            if ( lightType == shaderslot::kLightTypeDirectional || lightType == shaderslot::kLightTypeGlobal2D )
            {
                importance = kAlwaysKept;
            }
            else if ( lightType != shaderslot::kLightTypeShadow2D && lightType != shaderslot::kLightTypeFreeform2DEdge )
            {
                const float3  center{ head._positionRadius._x, head._positionRadius._y, head._positionRadius._z };
                const float32 radius  = MathUtil::max( head._positionRadius._w, 0.0f );
                const bool    bProbe  = lightType == shaderslot::kLightTypeProbeVolume || lightType == shaderslot::kLightTypeReflectionProbe;
                const float32 power   = bProbe ? 1.0f : MathUtil::max( head._colorIntensity._w, 0.0f ) * MathUtil::max( head._colorIntensity._x, MathUtil::max( head._colorIntensity._y, head._colorIntensity._z ) );
                const float32 minimum = MathUtil::max( radius * GPULightBufferInternal::kCullMinDepthFraction, MathUtil::kEpsilon );
                // 뷰가 없으면 깊이 1 — 세기 × 반경이다. 있으면 닿는 뷰 가운데 가장 가까운 깊이(클립 w — 원근에서 뷰 깊이, 직교에서 1)로 화면 크기를 어림한다.
                float32 depth = listFrustum.empty() ? 1.0f : -1.0f;
                for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( listFrustum.size() ); ++viewIndex )
                {
                    if ( listFrustum[viewIndex].overlapsSphere( center, radius ) == false )
                        continue;
                    const float32 viewDepth = MathUtil::max( float4::transform( float4{ center._x, center._y, center._z, 1.0f }, listViewProj[viewIndex] )._w, minimum );
                    depth                   = depth < 0.0f ? viewDepth : MathUtil::min( depth, viewDepth );
                }
                // 세기가 0 인 빛도 가림막(0)보다 앞이 되도록 아주 작은 값을 더한다.
                importance = depth < 0.0f ? kOutsideViews : MathUtil::kEpsilon + power * radius / depth;
            }
            listUnit.push_back( LightUnit{ importance, first, end - first } );
            first = end;
        }

        // 중요도 순서로 자리가 남는 단위만 고른다(크기가 커서 안 들어가는 단위 뒤의 작은 단위는 아직 들어간다). 같은 값은 목록 순서다.
        vector<uint32> listOrder( listUnit.size() );
        for ( uint32 unitIndex = 0; unitIndex < static_cast<uint32>( listUnit.size() ); ++unitIndex )
        {
            listOrder[unitIndex] = unitIndex;
        }
        VectorUtil::stableSort( listOrder, [&listUnit]( uint32 first, uint32 second )
        { return listUnit[first]._importance > listUnit[second]._importance; } );
        vector<uint8> listKept( listUnit.size(), SW_FALSE );
        uint32        keptCount = 0;
        for ( const uint32 unitIndex : listOrder )
        {
            const LightUnit& unit = listUnit[unitIndex];
            if ( unit._importance < 0.0f || keptCount + unit._count > limit )
                continue;
            listKept[unitIndex] = SW_TRUE;
            keptCount += unit._count;
        }

        // 남은 단위를 원래 순서로 앞으로 당긴다(종류별 순서 — 빛 · 프로브 · 가림막 — 를 셰이더가 그대로 본다).
        outListSourceToKept.assign( sourceCount, constant::invalid_index::kUint32 );
        uint32 writeIndex = 0;
        for ( uint32 unitIndex = 0; unitIndex < static_cast<uint32>( listUnit.size() ); ++unitIndex )
        {
            if ( listKept[unitIndex] == SW_FALSE )
                continue;
            const LightUnit& unit = listUnit[unitIndex];
            for ( uint32 offset = 0; offset < unit._count; ++offset )
            {
                outListSourceToKept[unit._first + offset] = writeIndex;
                inoutListLight[writeIndex++]              = inoutListLight[unit._first + offset];
            }
        }
        inoutListLight.resize( writeIndex );
        return writeIndex;
    }

    uint32 GPULightBuffer::findUploadedLightIndex( uint32 sourceIndex ) const
    {
        // 표가 비었으면 이번 목록은 상한 안이다(또는 아직 update 전이다) — 번호가 그대로다.
        if ( _listSourceToUploaded.empty() )
            return sourceIndex;
        return sourceIndex < _listSourceToUploaded.size() ? _listSourceToUploaded[sourceIndex] : constant::invalid_index::kUint32;
    }

    bool GPULightBuffer::uploadLightList( IRHIDevice* pDevice, uint32 cpuCount )
    {
        // 업로드 목록: [CPU 라이트][GPU 구간 — 죽은 라이트][그림자 타일]. 점광과 스폿 그림자 타일 원소는 구간 뒤에 붙는다 — 셰이더는 g_SwLightCount(= 둘의 합) + 라이트의
        // params.w 로 찾는다. 죽은 라이트는 반경 0 의 점광이라 그리드에 들지 않는다(light_grid.hlsl classifyLight) — 컴퓨트가 이번 프레임에 쓰지 못한 슬롯이 낡은 값으로 남지 않는다.
        _listUploadScratch.assign( _listLightScratch.begin(), _listLightScratch.begin() + cpuCount );
        GPULight deadLight{};
        deadLight._directionType = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypePoint ) };
        for ( uint32 gpuSlot = 0; gpuSlot < _gpuLightCount; ++gpuSlot )
        {
            _listUploadScratch.push_back( deadLight );
        }
        _listUploadScratch.insert( _listUploadScratch.end(), _listTileScratch.begin(), _listTileScratch.end() );

        const uint32 totalCount = static_cast<uint32>( _listUploadScratch.size() );
        const bool   bNeedsUAV  = _gpuLightCount > 0;
        const uint32 stride     = static_cast<uint32>( sizeof( GPULight ) );
        // UAV 없이 잡혀 있던 버퍼는 용량이 충분해도 다시 잡는다(ensureCapacity 는 용량만 본다). 백엔드가 UAV 를 거절한 버퍼는 다시 잡아도 같으니 한 번만 해 본다.
        const bool bLacksUAV = bNeedsUAV && _buffer.isValid() && _buffer._uav == kInvalidDescriptorIndex;
        if ( bLacksUAV && _bUAVDenied == SW_FALSE )
            _buffer.release( pDevice );
        if ( _buffer.ensureCapacity( pDevice, stride, totalCount, bNeedsUAV ? kLightBufferUsageWithUAV : kLightBufferUsage, true, bNeedsUAV, nullptr ) == false )
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
            SW_LOG_ERROR_ID( "GPULightBuffer.LightBufferNoUAV", "Light buffer has no UAV - GPU particle lights stay dead" );
        }
        uploadChangedLights( pDevice, cpuCount );
        return true;
    }

    void GPULightBuffer::uploadChangedLights( IRHIDevice* pDevice, uint32 cpuCount )
    {
        // 바뀐 구간만 올린다(인스턴스 버퍼와 같다). 통째로 올리는 때: 버퍼가 새것이거나 구간 배치([CPU][GPU 구간][타일])가 바뀌었을 때 — 같은 칸이 다른 원소가 된다.
        // GPU 구간은 컴퓨트(파티클 라이트)가 프레임마다 덮어써서 지난 내용과 견줄 수 없다 — 죽은 라이트로 늘 다시 깐다(쓰지 못한 슬롯이 낡은 값으로 남지 않게).
        constexpr uint32 kStride          = static_cast<uint32>( sizeof( GPULight ) );
        constexpr uint32 kMergeGapLights  = 4; // 사이가 이만큼 이하인 두 구간은 한 조각으로 — 조각마다 복사 명령이 하나 더 나간다
        const uint32     totalCount       = static_cast<uint32>( _listUploadScratch.size() );
        const bool       bLayoutUnchanged = _buffer.isValid() && _uploadedLightBufferGeneration == _buffer._generation && _uploadedCPUCount == cpuCount && _uploadedGPULightCount == _gpuLightCount &&
                                      _listUploadedLight.size() == totalCount;
        [[maybe_unused]] uint32 uploadedBytes = 0;
        if ( bLayoutUnchanged == false )
        {
            _buffer.upload( pDevice, _listUploadScratch.data(), totalCount * kStride );
            uploadedBytes = totalCount * kStride;
        }
        else
        {
            RHIStructuredBuffer::collectChangedRegions( _listUploadedLight.data(), static_cast<uint32>( _listUploadedLight.size() ), _listUploadScratch.data(), totalCount, kStride,
                                                        kMergeGapLights, _listScratchRegion );
            // 죽은 라이트 칸은 두 배열에서 같은 값이라 위 비교가 고르지 않는다 — 겹치지 않는 조각으로 따로 더한다.
            if ( _gpuLightCount > 0 )
                _listScratchRegion.push_back( RHIBufferCopyRegion{ cpuCount * kStride, cpuCount * kStride, _gpuLightCount * kStride } );
            uploadedBytes = _buffer.uploadRegions( pDevice, _listUploadScratch.data(), _listScratchRegion );
        }
        SW_PROFILE_COUNT( "RT.Light.uploadBytes", uploadedBytes );
        _listUploadedLight.swap( _listUploadScratch );
        _uploadedLightBufferGeneration = _buffer._generation;
        _uploadedCPUCount              = cpuCount;
        _uploadedGPULightCount         = _gpuLightCount;
    }

    void GPULightBuffer::uploadProbeElements( IRHIDevice* pDevice )
    {
        // 라이트 프로브 SH9(t13). 볼륨 원소가 라이트 목록에 있으면 그 프로브들이 여기 있다 — 못 올리면 볼륨 원소가 빈 버퍼를 가리키므로 오류로 남긴다.
        if ( _pProbeElements == nullptr || _pProbeElements->empty() )
            return;

        const vector<float4>& listProbeElement  = *_pProbeElements;
        const uint32          probeElementCount = static_cast<uint32>( listProbeElement.size() );
        const uint32          probeStride       = static_cast<uint32>( sizeof( float4 ) );
        // 실시간 볼륨이 있으면 프로브 갱신 컴퓨트가 이 버퍼를 쓴다 — UAV 가 있는 버퍼로 잡는다(UAV 없이 잡혀 있던 버퍼는 다시 잡는다. 백엔드가 거절한 버퍼는 한 번만 다시 해 본다).
        const bool bWritable = _bProbeWritable == SW_TRUE;
        // 컴퓨트가 쓰지 않는 버퍼에 이미 같은 묶음이 들어 있으면 올리지 않는다(베이크 프로브만 있는 씬은 내용이 바뀔 때만 올린다).
        // 컴퓨트가 쓰는 프레임은 예전처럼 올린다 — 0 자리를 다시 까는 흐름을 바꾸지 않는다.
        if ( bWritable == false && _pUploadedProbeElements == _pProbeElements && _probeBuffer.isValid() && _uploadedProbeBufferGeneration == _probeBuffer._generation )
        {
            _probeElementCount = probeElementCount;
            return;
        }
        // 컴퓨트가 쓰지 않는 버퍼는 지난 묶음과 견주어 바뀐 구간만 올린다(같은 버퍼 · 같은 원소 수일 때). 컴퓨트가 쓰는 버퍼는 GPU 가 덮어써 견줄 수 없다.
        const bool bComparable = bWritable == false && _pUploadedProbeElements != nullptr && _pUploadedProbeElements->size() == listProbeElement.size() && _probeBuffer.isValid() &&
                                 _uploadedProbeBufferGeneration == _probeBuffer._generation;
        if ( bComparable )
        {
            constexpr uint32 kMergeGapProbeElements = 16; // SH9 프로브 하나가 원소 아홉이다 — 이웃 프로브 하나 정도의 사이는 붙인다
            RHIStructuredBuffer::collectChangedRegions( _pUploadedProbeElements->data(), probeElementCount, listProbeElement.data(), probeElementCount, probeStride,
                                                        kMergeGapProbeElements, _listScratchRegion );
            [[maybe_unused]] const uint32 uploadedBytes = _probeBuffer.uploadRegions( pDevice, listProbeElement.data(), _listScratchRegion );
            SW_PROFILE_COUNT( "RT.LightProbe.uploadBytes", uploadedBytes );
            _probeElementCount      = probeElementCount;
            _pUploadedProbeElements = _pProbeElements;
            return;
        }
        _pUploadedProbeElements.reset();
        SW_PROFILE_COUNT( "RT.LightProbe.uploadBytes", probeElementCount * probeStride );
        const bool bLacksProbeUAV = bWritable && _probeBuffer.isValid() && _probeBuffer._uav == kInvalidDescriptorIndex;
        if ( bLacksProbeUAV && _bProbeUAVDenied == SW_FALSE )
            _probeBuffer.release( pDevice );
        if ( _probeBuffer.ensureCapacity( pDevice, probeStride, probeElementCount, bWritable ? kLightBufferUsageWithUAV : kLightBufferUsage, true, bWritable, nullptr ) )
        {
            _probeBuffer.upload( pDevice, listProbeElement.data(), probeElementCount * probeStride );
            _probeElementCount = probeElementCount;
            if ( bWritable == false )
                _pUploadedProbeElements = _pProbeElements;
            _uploadedProbeBufferGeneration = _probeBuffer._generation;
            const bool bNewlyDenied        = bWritable && _probeBuffer._uav == kInvalidDescriptorIndex;
            if ( bNewlyDenied && _bProbeUAVDenied == SW_FALSE )
            {
                _bProbeUAVDenied = SW_TRUE;
                SW_LOG_ERROR_ID( "GPULightBuffer.LightProbeBufferNo", "Light probe buffer has no UAV - realtime probe volumes stay black" );
            }
        }
        else
        {
            SW_LOG_ERROR_ID( "GPULightBuffer.LightProbeBufferElements", "Light probe buffer (%# elements) could not be allocated - probe volumes give no indirect light this frame", probeElementCount );
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
            // 이번 프레임 라이트는 올린 뒤 _listUploadedLight 에 있다(uploadChangedLights 가 _listUploadScratch 와 바꾼다) — 사본을 읽으면 지난 프레임(첫 프레임은 빈 배열)이었다.
            SW_ASSERT( _listUploadedLight.size() >= _cpuCount );
            if ( grid.populate( listGridViewProj[viewIndex], _listUploadedLight.data(), _cpuCount ) == false )
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
        if ( _cpuGridBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), wordCount, kUsage, true, false, nullptr ) == false )
        {
            reportGridFailure( "could not allocate the CPU grid buffer", 0 );
            _listGridBase.assign( listGridViewProj.size(), constant::invalid_index::kUint32 );
            return;
        }
        _cpuGridBuffer.upload( pDevice, _listGridWordScratch.data(), wordCount * static_cast<uint32>( sizeof( uint32 ) ) );
        _gridProducer = LightGridProducer::CPU;
    }

    void GPULightBuffer::updateGPUGrid( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj )
    {
        namespace grid = shaderslot::lightgrid;
        // 뷰 정보(binding_slots.hlsli 10 절): 행 넷 · (가까운 면 w, 조각 배율, 원근, 0) · asuint(라이트 수, 그리드 첫 단어, 0, 0). 그룹 번호가 곧 이 목록의 번호다.
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
        const bool               bViewReady = _gpuGridViewBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( float4 ) ), viewCount, kViewUsage, true, false, nullptr );
        const bool               bGridReady = _gpuGridBuffer.ensureCapacity( pDevice, static_cast<uint32>( sizeof( uint32 ) ), gridViewCount * grid::kGPUWordsPerView, kGridUsage, true, true, nullptr ) &&
                                _gpuGridBuffer._uav != kInvalidDescriptorIndex;
        if ( bViewReady == false || bGridReady == false )
        {
            reportGridFailure( "could not allocate the GPU grid buffers (UAV)", 0 );
            _listGridBase.assign( listGridViewProj.size(), constant::invalid_index::kUint32 );
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
        SW_LOG_ERROR_ID( "GPULightBuffer.LightGridViewView", "Light grid for view %# %# - that view walks every light", viewIndex, pReason );
    }

    void GPULightBuffer::reportGPULightRefused( uint32 requestedSlotCount )
    {
        if ( _bWarnedGPULightRefused == SW_TRUE )
            return;
        _bWarnedGPULightRefused = SW_TRUE;
        // 대신 생산자를 GPU 로 바꾸지 않는다 — 생산자는 진단 정책(`RenderDiagnosticsPolicy::_lightGridProducer`)이 정했다. 요청한 방출기의 빛만 그리지 않는다.
        SW_LOG_ERROR_ID( "GPULightBuffer.GPUParticleLightsSlots", "GPU particle lights (%# slots) need the GPU light grid producer (light culling GPU) - the lights are not drawn", requestedSlotCount );
    }

    const RHIStructuredBuffer* GPULightBuffer::findProbeBuffer() const
    {
        const bool bReady = _probeElementCount > 0 && _probeBuffer._buffer != 0 && _probeBuffer._srv != kInvalidDescriptorIndex;
        return bReady ? &_probeBuffer : nullptr;
    }

    const RHIStructuredBuffer* GPULightBuffer::findGridBuffer() const
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
        _pUploadedProbeElements.reset();
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
