#include "pch.h"

#include "Engine/Vfx/VfxComponent.h"

#include "Core/Common/HashUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    VfxComponent::VfxComponent()
        : _effectPath{}
        , _listSimulation{}
        , _listPublished{}
        , _seed{ 0 }
        , _bPlayOnBegin{ true }
        , _bPlaying{ false }
    {
        setCanEverTick( true );
    }

    VfxComponent::~VfxComponent() = default;

    void VfxComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<VfxComponent>( this );
    }

    void VfxComponent::onUnregister( GameObjectManager& manager )
    {
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
        bool bAllFinished = true;
        for ( VfxCPUSimulation& simulation : _listSimulation )
        {
            simulation.update( deltaTime );
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

    void VfxComponent::loadEffect()
    {
        _listSimulation.clear();
        _listPublished.clear();
        _bPlaying = false;
        if ( _effectPath.empty() )
            return;
        VfxEffectDef effect;
        if ( effect.loadFromResource( _effectPath ) == false )
            return; // 효과 데이터가 무엇이 틀렸는지 오류를 남겼다 — 방출기 없이 아무것도 그리지 않는다
        _listSimulation.reserve( effect._listEmitter.size() );
        for ( uint32 emitterIndex = 0; emitterIndex < static_cast<uint32>( effect._listEmitter.size() ); ++emitterIndex )
        {
            _listSimulation.emplace_back( effect._listEmitter[emitterIndex], HashUtil::combine( _seed, emitterIndex ) );
        }
        _listPublished.resize( _listSimulation.size() );
    }

    void VfxComponent::publishParticles()
    {
        for ( size_t emitterIndex = 0; emitterIndex < _listSimulation.size(); ++emitterIndex )
        {
            const VfxCPUSimulation& simulation = _listSimulation[emitterIndex];
            if ( simulation.getAliveCount() == 0 )
            {
                _listPublished[emitterIndex].reset();
                continue;
            }
            // 새 배열이다 — 지난 배열은 렌더 스레드가 아직 읽고 있을 수 있다.
            shared_ptr<vector<GPUParticle>> pListParticle = make_shared<vector<GPUParticle>>();
            simulation.writeParticles( *pListParticle );
            _listPublished[emitterIndex] = std::move( pListParticle );
        }
    }
} // namespace sw
