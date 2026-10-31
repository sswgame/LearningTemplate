#include "pch.h"

#include "Engine/VFX/VFXComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Physics/Component/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"

/**
 * @brief 이펙트 입자 생성 배율입니다. 사용자 설정 `graphics.effectsQuality` 선택지의 `targetValue` 가 넣는다(`engine.settings.xml`).
 * @details VFX 가 UserSettings 보다 아래 층이라 이 변수는 읽는 쪽인 여기에 둔다(설정은 이름으로 넣는다). 효과를 읽을 때 정하므로 바꾸면 다음에 읽는 효과부터 따른다.
 */
SW_GLOBAL_VARIABLE( float32, gv_effectsSpawnScale, 1.0f, "이펙트 입자 생성 배율 (사용자 설정 graphics.effectsQuality 의 targetValue)" );

namespace sw
{
    SW_LOG_CALLER( "VFXComponent" );

#if !defined( SW_SHIPPING )
    /**
     * @brief CPU · GPU 둘 다 되는 방출기의 시뮬레이션 대상입니다. 기본값은 Shipping 과 같은 GPU 다.
     * @details 학습용으로 두 방식을 나란히 보게 둔다. 한 곳만 되는 방출기(물리 충돌은 CPU, 깊이 충돌은 GPU)는
     *          이 값과 무관하게 그곳이다(`VFXEmitterDef::computeSupportedTargets`). Shipping 은 변수를 두지 않고 GPU 로 고정한다. 효과를 읽을 때 정하므로 바꾼 뒤 읽은 효과부터 따른다.
     */
    SW_TEST_GLOBAL_VARIABLE( VFXSimulationTarget, gv_vfxSimulation, VFXSimulationTarget::GPU, "둘 다 되는 방출기의 시뮬레이션 (CPU / GPU — 모듈이 한 곳만 허락하면 그곳)" );
#endif

    namespace
    {
        /**
         * @brief 물리 씬(3D)을 방출기 로컬 좌표로 감싼 충돌 질의입니다. 틱 하나 동안만 산다(방출기 행렬을 그 틱의 값으로 든다).
         * @details 점은 행렬의 역으로, 법선은 행렬의 전치로 되돌린다(행벡터 규약에서 법선은 역전치로 옮기므로 그 역은 전치다).
         */
        class VFXPhysicsCollisionScene final : public IVFXCollisionScene
        {
        public:
            VFXPhysicsCollisionScene( const IPhysicsQueryScene3D& scene, const float4x4& world )
                : _scene{ scene }
                , _world{ world }
                , _inverseWorld{ world.invert() }
                , _normalToLocal{ world.transpose() }
            {
            }

            bool castSegment( const float3& from, const float3& to, uint32 layerMask, VFXCollisionHit& outHit ) const override
            {
                const float3  worldFrom = float3::transform( from, _world );
                const float3  worldTo   = float3::transform( to, _world );
                const float3  segment   = worldTo - worldFrom;
                const float32 length    = segment.getLength();
                if ( length <= MathUtil::kEpsilon )
                    return false;
                PhysicsQueryFilter filter;
                filter._layerMask = layerMask;
                PhysicsCastHit3D hit;
                if ( _scene.raycast( worldFrom, segment * ( 1.0f / length ), length, filter, hit ) == false )
                    return false;
                outHit._point  = float3::transform( hit._point, _inverseWorld );
                outHit._normal = float3::transformVector( hit._normal, _normalToLocal ).normalize();
                return true;
            }

        private:
            const IPhysicsQueryScene3D& _scene;
            float4x4                    _world;
            float4x4                    _inverseWorld;
            float4x4                    _normalToLocal;
        };

        struct VFXComponentInternal
        {
            /** @brief 메시 원점에서 가장 먼 바운드 모서리까지의 거리입니다(크기 1 기준 항목 바운드). */
            static float32 computeOriginRadius( const Mesh& mesh )
            {
                const float3& boundsMin = mesh.getLocalBoundsMin();
                const float3& boundsMax = mesh.getLocalBoundsMax();
                const float3  farCorner{ MathUtil::max( MathUtil::abs( boundsMin._x ), MathUtil::abs( boundsMax._x ) ),
                                        MathUtil::max( MathUtil::abs( boundsMin._y ), MathUtil::abs( boundsMax._y ) ),
                                        MathUtil::max( MathUtil::abs( boundsMin._z ), MathUtil::abs( boundsMax._z ) ) };
                return farCorner.getLength();
            }

            /**
             * @brief GPU 메시 입자가 쓸 수 있는 머티리얼이면 true 입니다 — 불투명만(머티리얼이 없으면 씬 기본 머티리얼이라 불투명).
             * @details 투명 배치는 CPU 의 행렬로 정렬되고 뷰마다 꼬리 순서를 다시 만든다 — GPU 가 쓴 행렬은 그 정렬에 들어가지 않는다(`MeshInstanceWriter`).
             */
            [[nodiscard]] static bool isGPUMeshBlendSupported( const Material* pMaterial )
            {
                return pMaterial == nullptr || pMaterial->getBlendMode() == RHIBlendMode::Opaque;
            }

            /** @brief 메시 id 의 로드입니다(`.mesh` 경로는 메시 캐시가 워커에서 읽고, 내장 도형은 메모리에서 바로 만들므로 빈 핸들이다 — `MeshComponent` 와 같은 규칙). */
            static MeshHandle requestMesh( const string& meshID )
            {
                return MeshAssetFormat::isMeshAssetPath( meshID ) ? MeshCache::request( meshID ) : MeshHandle{};
            }

            /**
             * @brief 방출기 @p def 의 시뮬레이션 대상입니다. 모듈이 한 곳만 허락하면(`computeSupportedTargets`) 데이터가 정한 그곳이고,
             *        둘 다 허락할 때만 빌드가 고릅니다(Shipping 은 GPU, 개발 빌드는 `gv_vfxSimulation`).
             * @details `gv_vfxSimulation` 은 둘 다 되는 방출기의 비교 스위치라 한 곳만 되는 방출기에는 묻지 않는다. 허락한 곳이 하나도 없는 방출기는 검증이 로드를 막았다.
             */
            [[nodiscard]] static VFXSimulationTarget selectSimulationTarget( const VFXEmitterDef& def )
            {
                const VFXSimulationTargetSet supported = def.computeSupportedTargets();
                SW_ASSERT( supported.isEmpty() == false );
                if ( supported.has( VFXSimulationTarget::GPU ) == false )
                    return VFXSimulationTarget::CPU;
                if ( supported.has( VFXSimulationTarget::CPU ) == false )
                    return VFXSimulationTarget::GPU;
#if defined( SW_SHIPPING )
                return VFXSimulationTarget::GPU;
#else
                return gv_vfxSimulation;
#endif
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VFXComponent::VFXComponent()
        : _effectPath{}
        , _listSimulation{}
        , _listPublished{}
        , _listMeshEmitter{}
        , _listMeshLoad{}
        , _pPrimitiveRegistry{ nullptr }
        , _seed{ 0 }
        , _bPlayOnBegin{ true }
        , _bPlaying{ false }
    {
        setCanEverTick( true );
    }

    VFXComponent::~VFXComponent()
    {
        releaseMeshEmitters();
    }

    void VFXComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<VFXComponent>( this );
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        rebuildMeshEmitters();
    }

    void VFXComponent::onUnregister( GameObjectManager& manager )
    {
        releaseMeshEmitters();
        _listMeshLoad.clear();
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->removeAssetWaiter( this );
        _pPrimitiveRegistry = nullptr;
        manager.getComponentRegistry().remove<VFXComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void VFXComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        if ( _bPlayOnBegin )
            play();
    }

    void VFXComponent::onTick( float32 deltaTime )
    {
        SceneComponent::onTick( deltaTime );
        if ( _bPlaying == false )
            return;

        // 충돌하는 방출기가 있고 물리 씬(3D)이 있을 때만 질의를 만든다. 물리 씬이 없는 씬에서는 충돌 없이 돈다(닿을 것이 없다).
        // 충돌은 CPU 전용 모듈이라 충돌하는 방출기는 늘 CPU 시뮬레이션이다.
        bool bAnyCollision = false;
        for ( const EmitterSimulation& simulation : _listSimulation )
        {
            const VFXCPUSimulation* pCPUSimulation = std::get_if<VFXCPUSimulation>( &simulation );
            bAnyCollision                          = bAnyCollision || ( pCPUSimulation != nullptr && pCPUSimulation->getDef()._collision._mode != VFXCollisionMode::None );
        }
        const GameObject*                    pOwner        = getOwner();
        const GameObjectManager*             pManager      = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const IPhysicsQueryScene3D*          pPhysicsScene = ( bAnyCollision && pManager != nullptr ) ? pManager->getSubsystem<ScenePhysics>().findScene3D() : nullptr;
        unique_ptr<VFXPhysicsCollisionScene> pCollision;
        if ( pPhysicsScene != nullptr )
            pCollision = sw::make_unique<VFXPhysicsCollisionScene>( *pPhysicsScene, getWorldMatrix() );

        bool bAllFinished = true;
        for ( EmitterSimulation& simulation : _listSimulation )
        {
            VFXCPUSimulation* pCPUSimulation = std::get_if<VFXCPUSimulation>( &simulation );
            VFXGPUEmitter*    pGPUEmitter    = std::get_if<VFXGPUEmitter>( &simulation );
            if ( pCPUSimulation != nullptr )
            {
                pCPUSimulation->update( deltaTime, pCollision.get() );
                bAllFinished = bAllFinished && pCPUSimulation->isFinished();
            }
            else
            {
                pGPUEmitter->update( deltaTime );
                bAllFinished = bAllFinished && pGPUEmitter->isFinished();
            }
        }
        _bPlaying = bAllFinished == false;
        publishParticles();
    }

    void VFXComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        loadEffect();
    }

    void VFXComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        static const hashed_string s_effectPathName( "_effectPath" );
        static const hashed_string s_seedName( "_seed" );
        if ( propertyName == s_effectPathName || propertyName == s_seedName )
            loadEffect();
    }

    void VFXComponent::play()
    {
        restartSimulations();
        _bPlaying = _listSimulation.empty() == false;
        publishParticles();
    }

    void VFXComponent::stop()
    {
        restartSimulations();
        _bPlaying = false;
        publishParticles();
    }

    void VFXComponent::restartSimulations()
    {
        for ( EmitterSimulation& simulation : _listSimulation )
        {
            VFXCPUSimulation* pCPUSimulation = std::get_if<VFXCPUSimulation>( &simulation );
            if ( pCPUSimulation != nullptr )
                pCPUSimulation->restart();
            else
                std::get<VFXGPUEmitter>( simulation ).restart();
        }
    }

    void VFXComponent::setEffectPath( string_view path )
    {
        _effectPath = string( path );
        loadEffect();
    }

    void VFXComponent::setSeed( uint32 seed )
    {
        _seed = seed;
        loadEffect();
    }

    const VFXEmitterDef& VFXComponent::getEmitterDef( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listSimulation.size() );
        const VFXCPUSimulation* pCPUSimulation = std::get_if<VFXCPUSimulation>( &_listSimulation[emitterIndex] );
        return pCPUSimulation != nullptr ? pCPUSimulation->getDef() : std::get<VFXGPUEmitter>( _listSimulation[emitterIndex] ).getDef();
    }

    VFXSimulationTarget VFXComponent::getEmitterSimulationTarget( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listSimulation.size() );
        return std::holds_alternative<VFXCPUSimulation>( _listSimulation[emitterIndex] ) ? VFXSimulationTarget::CPU : VFXSimulationTarget::GPU;
    }

    const shared_ptr<const vector<GPUParticle>>& VFXComponent::getEmitterParticles( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listPublished.size() );
        return _listPublished[emitterIndex];
    }

    const VFXGPUEmitterRequest* VFXComponent::findEmitterGPURequest( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listGPURequest.size() );
        return _listGPURequest[emitterIndex]._pDef != nullptr ? &_listGPURequest[emitterIndex] : nullptr;
    }

    uint32 VFXComponent::getCPUAliveParticleCount() const
    {
        uint32 aliveCount = 0;
        for ( const EmitterSimulation& simulation : _listSimulation )
        {
            const VFXCPUSimulation* pCPUSimulation = std::get_if<VFXCPUSimulation>( &simulation );
            aliveCount += pCPUSimulation != nullptr ? pCPUSimulation->getAliveCount() : 0u;
        }
        return aliveCount;
    }

    uint64 VFXComponent::getSpawnedParticleCount() const
    {
        uint64 spawnedCount = 0;
        for ( const EmitterSimulation& simulation : _listSimulation )
        {
            const VFXCPUSimulation* pCPUSimulation = std::get_if<VFXCPUSimulation>( &simulation );
            spawnedCount += pCPUSimulation != nullptr ? pCPUSimulation->getSpawnedCount() : std::get<VFXGPUEmitter>( simulation ).getSpawnedCount();
        }
        return spawnedCount;
    }

    const MeshInstanceBatch* VFXComponent::findEmitterMeshBatch( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listSimulation.size() );
        return emitterIndex < _listMeshEmitter.size() ? _listMeshEmitter[emitterIndex]._batch.get() : nullptr;
    }

    float32 VFXComponent::getEmitterMeshRadius( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listSimulation.size() );
        return emitterIndex < _listMeshEmitter.size() ? _listMeshEmitter[emitterIndex]._meshRadius : 0.0f;
    }

    void VFXComponent::appendGPULights( vector<GPULight>& outList ) const
    {
        if ( isActive() == false )
            return;
        const float4x4 world     = getWorldMatrix();
        const float32  sizeScale = world.getMaximumAxisScale();
        for ( size_t emitterIndex = 0; emitterIndex < _listSimulation.size(); ++emitterIndex )
        {
            const VFXLightDef&                           lightDef      = getEmitterDef( static_cast<uint32>( emitterIndex ) )._light;
            const shared_ptr<const vector<GPUParticle>>& pListParticle = _listPublished[emitterIndex];
            if ( lightDef._maxLights == 0 || pListParticle == nullptr )
                continue;
            const uint32 lightCount = MathUtil::min( lightDef._maxLights, static_cast<uint32>( pListParticle->size() ) );
            for ( uint32 particleIndex = 0; particleIndex < lightCount; ++particleIndex )
            {
                const GPUParticle& particle = ( *pListParticle )[particleIndex];
                const float3       position = float3::transform( float3( particle._positionSize._x, particle._positionSize._y, particle._positionSize._z ), world );
                GPULight&          light    = outList.emplace_back();
                light._positionRadius       = float4{ position._x, position._y, position._z, lightDef._radius * sizeScale };
                light._colorIntensity       = float4{ particle._color._x, particle._color._y, particle._color._z, lightDef._intensity * particle._color._w };
                light._directionType        = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( shaderslot::kLightTypePoint ) };
            }
        }
    }

    void VFXComponent::loadEffect()
    {
        releaseMeshEmitters();
        _listSimulation.clear();
        _listPublished.clear();
        _listGPURequest.clear();
        _bPlaying = false;
        if ( _effectPath.empty() )
            return;
        VFXEffectDef effect;
        if ( effect.loadFromAsset( AssetID{ _effectPath } ) == false )
            return; // 효과 데이터가 무엇이 틀렸는지 오류를 남겼다 — 방출기 없이 아무것도 그리지 않는다
        _listSimulation.reserve( effect._listEmitter.size() );
        // 이펙트 품질(사용자 설정 graphics.effectsQuality → 선택지의 targetValue)은 효과를 읽을 때 정한다 — 바꾸면 다음에 읽는 효과부터 따른다.
        const float32 spawnScale = MathUtil::max( 0.0f, static_cast<float32>( gv_effectsSpawnScale ) );
        for ( uint32 emitterIndex = 0; emitterIndex < static_cast<uint32>( effect._listEmitter.size() ); ++emitterIndex )
        {
            shared_ptr<const VFXEmitterDef> pDef = make_shared<const VFXEmitterDef>( std::move( effect._listEmitter[emitterIndex] ) );
            const uint64                    seed = HashUtil::combine( _seed, emitterIndex );
            if ( VFXComponentInternal::selectSimulationTarget( *pDef ) == VFXSimulationTarget::CPU )
                _listSimulation.emplace_back( std::in_place_type<VFXCPUSimulation>, std::move( pDef ), seed, spawnScale );
            else
                _listSimulation.emplace_back( std::in_place_type<VFXGPUEmitter>, std::move( pDef ), seed, spawnScale );
        }
        _listPublished.resize( _listSimulation.size() );
        _listGPURequest.resize( _listSimulation.size() );
        rebuildMeshEmitters();
    }

    void VFXComponent::rebuildMeshEmitters()
    {
        releaseMeshEmitters();
        if ( _pPrimitiveRegistry == nullptr )
            return;
        // 메시는 워커가 읽는다 — 게임 스레드가 디스크를 기다리지 않게 다 올 때까지 배치 없이 두고(시뮬레이션은 돈다), 씬 프레임의 도착 단계가 다시 만든다
        // (언리얼 나이아가라 메시 렌더러도 메시가 스트리밍되기 전에는 그리지 않는다). 항목 바운드가 메시 반지름을 쓰므로 메시 없이 미리 만들지 않는다.
        if ( requestEmitterMeshes() == false )
        {
            _pPrimitiveRegistry->addAssetWaiter( this, &VFXComponent::updateMeshArrival );
            return;
        }
        _listMeshEmitter.resize( _listSimulation.size() );
        for ( uint32 emitterIndex = 0; emitterIndex < static_cast<uint32>( _listSimulation.size() ); ++emitterIndex )
        {
            const VFXEmitterDef& def = getEmitterDef( emitterIndex );
            if ( def._renderMode != VFXRenderMode::Mesh )
                continue;
            const MeshHandle& load = _listMeshLoad[emitterIndex];
            shared_ptr<Mesh>  mesh = load.isValid() ? load.get() : MeshUtil::acquirePrimitive( def._mesh._meshID );
            if ( mesh == nullptr )
            {
                // 대신 그릴 메시를 고르지 않는다 — 이 방출기는 그리지 않고(시뮬레이션은 돈다) 원인을 남긴다.
                SW_LOG_ERROR( "VFX '%#': emitter '%#' mesh '%#' could not be acquired - the emitter is not drawn", _effectPath, def._name, def._mesh._meshID );
                continue;
            }
            MeshEmitter& meshEmitter = _listMeshEmitter[emitterIndex];
            meshEmitter._material    = sw::make_unique<EnvironmentMaterial>();
            if ( def._mesh._materialPath.empty() == false )
                (void)meshEmitter._material->acquire( def._mesh._materialPath ); // 못 잡으면 씬 기본 머티리얼(경고는 안에서)
            // GPU 메시 입자는 컴퓨트가 인스턴스 칸에 행렬을 쓴다 — 투명 정렬은 CPU 행렬로 하므로 불투명 머티리얼만 받는다(대신 CPU 로 돌리지 않는다).
            const bool bGPUWritten = getEmitterSimulationTarget( emitterIndex ) == VFXSimulationTarget::GPU;
            if ( bGPUWritten && VFXComponentInternal::isGPUMeshBlendSupported( meshEmitter._material->getMaterial() ) == false )
            {
                SW_LOG_ERROR( "VFX '%#': emitter '%#' simulates on the GPU but material '%#' is not opaque - GPU mesh particles need an opaque material; the emitter is not drawn",
                              _effectPath, def._name, def._mesh._materialPath );
                meshEmitter._material->release();
                meshEmitter._material.reset();
                continue;
            }
            meshEmitter._meshRadius = VFXComponentInternal::computeOriginRadius( *mesh );
            meshEmitter._batch      = sw::make_unique<MeshInstanceBatch>( std::move( mesh ), meshEmitter._material->getMaterial(), meshEmitter._material->getInstance(),
                                                                          def._maxParticles, bGPUWritten ? MeshInstanceWriter::GPU : MeshInstanceWriter::CPU );
            meshEmitter._batch->setOwnerComponent( this );
            if ( bGPUWritten == false )
                writeMeshEntries( emitterIndex, _listPublished[emitterIndex].get() );
            _pPrimitiveRegistry->addInstanceBatch( meshEmitter._batch.get() );
        }
    }

    bool VFXComponent::requestEmitterMeshes()
    {
        vector<MeshHandle> listLoad( _listSimulation.size() );
        bool               bAllArrived{ true };
        for ( uint32 emitterIndex = 0; emitterIndex < static_cast<uint32>( _listSimulation.size() ); ++emitterIndex )
        {
            const VFXEmitterDef& def = getEmitterDef( emitterIndex );
            if ( def._renderMode != VFXRenderMode::Mesh )
                continue;
            listLoad[emitterIndex] = VFXComponentInternal::requestMesh( def._mesh._meshID );
            bAllArrived            = bAllArrived && listLoad[emitterIndex].isLoading() == false;
        }
        _listMeshLoad = std::move( listLoad );
        return bAllArrived;
    }

    bool VFXComponent::updateMeshArrival( SceneComponent& component, float32 /*deltaSeconds*/ )
    {
        // 이 함수를 거는 것은 VFX 컴포넌트 자신뿐이다(`rebuildMeshEmitters`).
        VFXComponent& self = static_cast<VFXComponent&>( component );
        for ( const MeshHandle& load : self._listMeshLoad )
        {
            if ( load.isLoading() )
                return true;
        }
        self.rebuildMeshEmitters();
        for ( const MeshHandle& load : self._listMeshLoad )
        {
            if ( load.isLoading() )
                return true;
        }
        return false;
    }

    void VFXComponent::releaseMeshEmitters()
    {
        // 배치의 소멸자가 레지스트리에서 뺀다. 머티리얼은 배치보다 뒤에 놓는다(배치가 머티리얼을 빌린다).
        for ( MeshEmitter& meshEmitter : _listMeshEmitter )
        {
            meshEmitter._batch.reset();
            if ( meshEmitter._material != nullptr )
                meshEmitter._material->release();
            meshEmitter._material.reset();
        }
        _listMeshEmitter.clear();
    }

    void VFXComponent::writeMeshEntries( uint32 emitterIndex, const vector<GPUParticle>* pListParticle )
    {
        MeshInstanceBatch* pBatch = emitterIndex < _listMeshEmitter.size() ? _listMeshEmitter[emitterIndex]._batch.get() : nullptr;
        if ( pBatch == nullptr )
            return;
        SW_ASSERT( pBatch->getInstanceWriter() == MeshInstanceWriter::CPU ); // GPU 배치의 항목은 컴퓨트가 쓴다(`VFXGPUSimulation`)
        const float4x4 world      = getWorldMatrix();
        const float32  sizeScale  = world.getMaximumAxisScale();
        const float32  meshRadius = _listMeshEmitter[emitterIndex]._meshRadius;
        const uint32   aliveCount = pListParticle != nullptr ? static_cast<uint32>( pListParticle->size() ) : 0u;
        for ( uint32 entry = 0; entry < pBatch->getCount(); ++entry )
        {
            if ( entry >= aliveCount )
            {
                // 죽은 자리 — 크기 0 행렬(클래스 머리말의 shortcut).
                pBatch->setWorld( entry, float4x4::makeScale( 0.0f ) * world );
                pBatch->setBoundsRadius( entry, 0.0f );
                continue;
            }
            const GPUParticle& particle = ( *pListParticle )[entry];
            const float32      size     = particle._positionSize._w;
            const float3       local{ particle._positionSize._x, particle._positionSize._y, particle._positionSize._z };
            pBatch->setWorld( entry, float4x4::makeScale( size ) * float4x4::makeTranslation( local ) * world );
            pBatch->setBoundsRadius( entry, meshRadius * size * sizeScale );
            pBatch->setSprite( entry, GPUSpriteInstanceData::make( float4{ 0.0f, 0.0f, 1.0f, 1.0f }, particle._color ) );
        }
    }

    void VFXComponent::publishParticles()
    {
        for ( uint32 emitterIndex = 0; emitterIndex < static_cast<uint32>( _listSimulation.size() ); ++emitterIndex )
        {
            const VFXGPUEmitter* pGPUEmitter = std::get_if<VFXGPUEmitter>( &_listSimulation[emitterIndex] );
            if ( pGPUEmitter != nullptr )
            {
                // 입자는 렌더 스레드의 GPU 버퍼에 있다 — 재생 중일 때만 누적 단계 수를 싣는다(멈추면 렌더 스레드가 그 상태를 놓는다).
                _listGPURequest[emitterIndex] = _bPlaying ? pGPUEmitter->makeRequest() : VFXGPUEmitterRequest{};
                continue;
            }
            const VFXCPUSimulation& simulation = std::get<VFXCPUSimulation>( _listSimulation[emitterIndex] );
            if ( simulation.getAliveCount() == 0 )
            {
                _listPublished[emitterIndex].reset();
                writeMeshEntries( emitterIndex, nullptr );
                continue;
            }
            // 새 배열이다 — 지난 배열은 렌더 스레드가 아직 읽고 있을 수 있다.
            shared_ptr<vector<GPUParticle>> pListParticle = make_shared<vector<GPUParticle>>();
            simulation.writeParticles( *pListParticle );
            writeMeshEntries( emitterIndex, pListParticle.get() );
            _listPublished[emitterIndex] = std::move( pListParticle );
        }
    }
} // namespace sw
