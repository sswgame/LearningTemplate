#include "pch.h"

#include "Engine/Vfx/VfxComponent.h"

#include "Core/Common/HashUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/MeshInstanceBatch.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Physics/IPhysicsScene.h"

/**
 * @brief 이펙트 입자 생성 배율입니다. 사용자 설정 `graphics.effectsQuality` 선택지의 `targetValue` 가 넣는다(`engine.settings.xml`).
 * @details Vfx 가 UserSettings 보다 아래 층이라 이 변수는 읽는 쪽인 여기에 둔다(설정은 이름으로 넣는다). 효과를 읽을 때 정하므로 바꾸면 다음에 읽는 효과부터 따른다.
 */
SW_GLOBAL_VARIABLE( float32, gv_effectsSpawnScale, 1.0f, "이펙트 입자 생성 배율 (사용자 설정 graphics.effectsQuality 의 targetValue)" );

namespace sw
{
    SW_LOG_CALLER( "VfxComponent" );

    namespace
    {
        /**
         * @brief 물리 씬(3D)을 방출기 로컬 좌표로 감싼 충돌 질의입니다. 틱 하나 동안만 산다(방출기 행렬을 그 틱의 값으로 든다).
         * @details 점은 행렬의 역으로, 법선은 행렬의 전치로 되돌린다(행벡터 규약에서 법선은 역전치로 옮기므로 그 역은 전치다).
         */
        class VfxPhysicsCollisionScene final : public IVfxCollisionScene
        {
        public:
            VfxPhysicsCollisionScene( const IPhysicsQueryScene3D& scene, const float4x4& world )
                : _scene{ scene }
                , _world{ world }
                , _inverseWorld{ world.invert() }
                , _normalToLocal{ world.transpose() }
            {
            }

            bool castSegment( const float3& from, const float3& to, uint32 layerMask, VfxCollisionHit& outHit ) const override
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

        struct VfxComponentInternal
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

            /** @brief 메시 id 를 메시로 해석합니다(`.mesh` 경로는 메시 캐시, 아니면 내장 도형 — `MeshComponent` 와 같은 규칙). */
            static shared_ptr<Mesh> acquireMesh( const string& meshID )
            {
                return MeshAssetFormat::isMeshAssetPath( meshID ) ? MeshCache::acquire( meshID ) : MeshUtil::acquirePrimitive( meshID );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VfxComponent::VfxComponent()
        : _effectPath{}
        , _listSimulation{}
        , _listPublished{}
        , _listMeshEmitter{}
        , _pPrimitiveRegistry{ nullptr }
        , _seed{ 0 }
        , _bPlayOnBegin{ true }
        , _bPlaying{ false }
    {
        setCanEverTick( true );
    }

    VfxComponent::~VfxComponent()
    {
        releaseMeshEmitters();
    }

    void VfxComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<VfxComponent>( this );
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        rebuildMeshEmitters();
    }

    void VfxComponent::onUnregister( GameObjectManager& manager )
    {
        releaseMeshEmitters();
        _pPrimitiveRegistry = nullptr;
        manager.getComponentRegistry().remove<VfxComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void VfxComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        if ( _bPlayOnBegin )
            play();
    }

    void VfxComponent::onTick( float32 deltaTime )
    {
        SceneComponent::onTick( deltaTime );
        if ( _bPlaying == false )
            return;

        // 충돌하는 방출기가 있고 물리 씬(3D)이 있을 때만 질의를 만든다. 물리 씬이 없는 씬에서는 충돌 없이 돈다(닿을 것이 없다).
        bool bAnyCollision = false;
        for ( const VfxCPUSimulation& simulation : _listSimulation )
        {
            bAnyCollision = bAnyCollision || simulation.getDef()._collision._mode != VfxCollisionMode::None;
        }
        const GameObject*                    pOwner        = getOwner();
        const GameObjectManager*             pManager      = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const IPhysicsQueryScene3D*          pPhysicsScene = ( bAnyCollision && pManager != nullptr ) ? pManager->getSubsystem<ScenePhysics>().findScene3D() : nullptr;
        unique_ptr<VfxPhysicsCollisionScene> pCollision;
        if ( pPhysicsScene != nullptr )
            pCollision = sw::make_unique<VfxPhysicsCollisionScene>( *pPhysicsScene, getWorldMatrix() );

        bool bAllFinished = true;
        for ( VfxCPUSimulation& simulation : _listSimulation )
        {
            simulation.update( deltaTime, pCollision.get() );
            bAllFinished = bAllFinished && simulation.isFinished();
        }
        _bPlaying = bAllFinished == false;
        publishParticles();
    }

    void VfxComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        loadEffect();
    }

    void VfxComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        static const hashed_string s_effectPathName( "_effectPath" );
        static const hashed_string s_seedName( "_seed" );
        if ( propertyName == s_effectPathName || propertyName == s_seedName )
            loadEffect();
    }

    void VfxComponent::play()
    {
        for ( VfxCPUSimulation& simulation : _listSimulation )
        {
            simulation.restart();
        }
        _bPlaying = _listSimulation.empty() == false;
        publishParticles();
    }

    void VfxComponent::stop()
    {
        for ( VfxCPUSimulation& simulation : _listSimulation )
        {
            simulation.restart();
        }
        _bPlaying = false;
        publishParticles();
    }

    void VfxComponent::setEffectPath( string_view path )
    {
        _effectPath = string( path );
        loadEffect();
    }

    void VfxComponent::setSeed( uint32 seed )
    {
        _seed = seed;
        loadEffect();
    }

    const VfxEmitterDef& VfxComponent::getEmitterDef( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listSimulation.size() );
        return _listSimulation[emitterIndex].getDef();
    }

    const shared_ptr<const vector<GPUParticle>>& VfxComponent::getEmitterParticles( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listPublished.size() );
        return _listPublished[emitterIndex];
    }

    uint32 VfxComponent::getAliveParticleCount() const
    {
        uint32 aliveCount = 0;
        for ( const VfxCPUSimulation& simulation : _listSimulation )
        {
            aliveCount += simulation.getAliveCount();
        }
        return aliveCount;
    }

    const MeshInstanceBatch* VfxComponent::findEmitterMeshBatch( uint32 emitterIndex ) const
    {
        SW_ASSERT( emitterIndex < _listSimulation.size() );
        return emitterIndex < _listMeshEmitter.size() ? _listMeshEmitter[emitterIndex]._batch.get() : nullptr;
    }

    void VfxComponent::appendGPULights( vector<GPULight>& outList ) const
    {
        if ( isActive() == false )
            return;
        const float4x4 world     = getWorldMatrix();
        const float32  sizeScale = world.getMaximumAxisScale();
        for ( size_t emitterIndex = 0; emitterIndex < _listSimulation.size(); ++emitterIndex )
        {
            const VfxLightDef&                           lightDef      = _listSimulation[emitterIndex].getDef()._light;
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

    void VfxComponent::loadEffect()
    {
        releaseMeshEmitters();
        _listSimulation.clear();
        _listPublished.clear();
        _bPlaying = false;
        if ( _effectPath.empty() )
            return;
        VfxEffectDef effect;
        if ( effect.loadFromResource( _effectPath ) == false )
            return; // 효과 데이터가 무엇이 틀렸는지 오류를 남겼다 — 방출기 없이 아무것도 그리지 않는다
        _listSimulation.reserve( effect._listEmitter.size() );
        // 이펙트 품질(사용자 설정 graphics.effectsQuality → 선택지의 targetValue)은 효과를 읽을 때 정한다 — 바꾸면 다음에 읽는 효과부터 따른다.
        const float32 spawnScale = MathUtil::max( 0.0f, static_cast<float32>( gv_effectsSpawnScale ) );
        for ( uint32 emitterIndex = 0; emitterIndex < static_cast<uint32>( effect._listEmitter.size() ); ++emitterIndex )
        {
            _listSimulation.emplace_back( effect._listEmitter[emitterIndex], HashUtil::combine( _seed, emitterIndex ), spawnScale );
        }
        _listPublished.resize( _listSimulation.size() );
        rebuildMeshEmitters();
    }

    void VfxComponent::rebuildMeshEmitters()
    {
        releaseMeshEmitters();
        if ( _pPrimitiveRegistry == nullptr )
            return;
        _listMeshEmitter.resize( _listSimulation.size() );
        for ( uint32 emitterIndex = 0; emitterIndex < static_cast<uint32>( _listSimulation.size() ); ++emitterIndex )
        {
            const VfxEmitterDef& def = _listSimulation[emitterIndex].getDef();
            if ( def._renderMode != VfxRenderMode::Mesh )
                continue;
            shared_ptr<Mesh> mesh = VfxComponentInternal::acquireMesh( def._mesh._meshID );
            if ( mesh == nullptr )
            {
                // 대신 그릴 메시를 고르지 않는다 — 이 방출기는 그리지 않고(시뮬레이션은 돈다) 원인을 남긴다.
                SW_LOG_ERROR( "Vfx '%#': emitter '%#' mesh '%#' could not be acquired - the emitter is not drawn", _effectPath, def._name, def._mesh._meshID );
                continue;
            }
            MeshEmitter& meshEmitter = _listMeshEmitter[emitterIndex];
            meshEmitter._material    = sw::make_unique<EnvironmentMaterial>();
            if ( def._mesh._materialPath.empty() == false )
                (void)meshEmitter._material->acquire( def._mesh._materialPath ); // 못 잡으면 씬 기본 머티리얼(경고는 안에서)
            meshEmitter._meshRadius = VfxComponentInternal::computeOriginRadius( *mesh );
            meshEmitter._batch      = sw::make_unique<MeshInstanceBatch>( std::move( mesh ), meshEmitter._material->getMaterial(), meshEmitter._material->getInstance(), def._maxParticles );
            meshEmitter._batch->setOwnerComponent( this );
            writeMeshEntries( emitterIndex, _listPublished[emitterIndex].get() );
            _pPrimitiveRegistry->addInstanceBatch( meshEmitter._batch.get() );
        }
    }

    void VfxComponent::releaseMeshEmitters()
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

    void VfxComponent::writeMeshEntries( uint32 emitterIndex, const vector<GPUParticle>* pListParticle )
    {
        MeshInstanceBatch* pBatch = emitterIndex < _listMeshEmitter.size() ? _listMeshEmitter[emitterIndex]._batch.get() : nullptr;
        if ( pBatch == nullptr )
            return;
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

    void VfxComponent::publishParticles()
    {
        for ( uint32 emitterIndex = 0; emitterIndex < static_cast<uint32>( _listSimulation.size() ); ++emitterIndex )
        {
            const VfxCPUSimulation& simulation = _listSimulation[emitterIndex];
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
