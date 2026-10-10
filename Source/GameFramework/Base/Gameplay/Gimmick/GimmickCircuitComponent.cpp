#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/LightComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickDamageUtil.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"
#include "GameFramework/Base/World/Query/WorldQuery.h"
#include "GameFramework/Base/World/Spline/SplineComponent.h"

namespace sw
{
    SW_LOG_CALLER( "GimmickCircuitComponent" );

    namespace
    {
        struct GimmickCircuitComponentInternal
        {
            static constexpr float32 kFarDistance = 1.0e9f;

            static float3 readVector( const GimmickCircuit& circuit, int32 node, const utf8* pName )
            {
                const GimmickParamValue* pValue = circuit.findParam( node, hashed_string( pName ) );
                return pValue != nullptr ? float3{ pValue->_vector._x, pValue->_vector._y, pValue->_vector._z } : float3{};
            }

            static float32 readNumber( const GimmickCircuit& circuit, int32 node, const utf8* pName, float32 fallback )
            {
                const GimmickParamValue* pValue = circuit.findParam( node, hashed_string( pName ) );
                return pValue != nullptr ? pValue->_vector._x : fallback;
            }

            static const string* readText( const GimmickCircuit& circuit, int32 node, const utf8* pName )
            {
                const GimmickParamValue* pValue = circuit.findParam( node, hashed_string( pName ) );
                return pValue != nullptr ? &pValue->_text : nullptr;
            }

            static float3 toRadians( const float3& degrees ) { return degrees * MathUtil::kDegreeToRadian; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GimmickCircuitComponent::GimmickCircuitComponent()
        : _listNode{}
        , _listNodeTarget{}
        , _listWire{}
        , _listRestPose{}
        , _stateBytes{}
        , _stepTime{ GimmickCircuitDef::kDefaultStepTime }
        , _circuit{}
        , _listBinding{}
        , _listError{}
        , _checkpointBytes{}
    {
    }

    GimmickCircuitComponent::Binding GimmickCircuitComponent::findBinding( const hashed_string& kind )
    {
        struct NameBinding
        {
            const utf8* _pName;
            Binding     _binding;
        };
        static constexpr NameBinding kArrBinding[] = {
            {       "Volume",        Binding::Volume},
            {"PressurePlate", Binding::PressurePlate},
            {        "Laser",         Binding::Laser},
            {    "Proximity",     Binding::Proximity},
            {       "Damage",        Binding::Damage},
            {  "Interaction",   Binding::Interaction},
            {       "Signal",        Binding::Signal},
            {         "Door",          Binding::Door},
            {        "Mover",         Binding::Mover},
            {     "Elevator",      Binding::Elevator},
            {      "Rotator",       Binding::Rotator},
            {      "Spawner",       Binding::Spawner},
            {       "Hazard",        Binding::Hazard},
            {        "Light",         Binding::Light},
            {        "Sound",         Binding::Sound},
            {       "Enable",        Binding::Enable},
        };
        for ( const NameBinding& entry : kArrBinding )
        {
            if ( kind == hashed_string( entry._pName ) )
                return entry._binding;
        }
        return Binding::None;
    }

    void GimmickCircuitComponent::addNode( const hashed_string& id, const hashed_string& kind, string_view params, GameObjectHandle target )
    {
        GimmickNodeDesc node;
        node._id     = id;
        node._kind   = kind;
        node._params = string( params );
        _listNode.push_back( node );
        _listNodeTarget.resize( _listNode.size() );
        _listNodeTarget.back() = target;
    }

    void GimmickCircuitComponent::addWire( string_view from, string_view to, bool bInvert )
    {
        GimmickWireDesc wire;
        wire._from    = string( from );
        wire._to      = string( to );
        wire._bInvert = bInvert;
        _listWire.push_back( wire );
    }

    void GimmickCircuitComponent::clearCircuit()
    {
        _listNode.clear();
        _listNodeTarget.clear();
        _listWire.clear();
        _listRestPose.clear();
        _stateBytes.clear();
        _circuit.clear();
    }

    void GimmickCircuitComponent::makeDef( GimmickCircuitDef& outDef, vector<string>& outListError ) const
    {
        outDef.clear();
        const GameObject* pOwner = getOwner();
        outDef.setSourceName( pOwner != nullptr ? string( pOwner->getName().c_str() ) + ".GimmickCircuit" : string( "GimmickCircuit" ) );
        outDef.setStepTime( _stepTime );
        for ( const GimmickNodeDesc& desc : _listNode )
        {
            GimmickNodeDef& node = outDef.addNode( desc._id, desc._kind );
            // "name=value; name=value" — 값에는 공백이 들어갈 수 있다("0 3 0").
            size_t start = 0;
            while ( start < desc._params.size() )
            {
                size_t end = desc._params.find( ';', start );
                if ( end == string::npos )
                    end = desc._params.size();
                const string_view pair = StringUtil::trim( string_view( desc._params ).substr( start, end - start ) );
                start                  = end + 1;
                if ( pair.empty() )
                    continue;
                const size_t equal = pair.find( '=' );
                if ( equal == string_view::npos || equal == 0 )
                {
                    outListError.push_back( outDef.getSourceName() + ": node '" + desc._id.c_str() + "' has a parameter without name=value: '" + string( pair ) + "'" );
                    continue;
                }
                node.setParam( hashed_string( string( StringUtil::trim( pair.substr( 0, equal ) ) ) ), StringUtil::trim( pair.substr( equal + 1 ) ) );
            }
        }
        for ( const GimmickWireDesc& wire : _listWire )
        {
            if ( outDef.addWire( wire._from, wire._to, wire._bInvert ) == false )
                outListError.push_back( outDef.getSourceName() + ": wire '" + wire._from + "' -> '" + wire._to + "' needs node.port on both ends" );
        }
    }

    bool GimmickCircuitComponent::rebuild()
    {
        _listError.clear();
        GimmickCircuitDef def;
        makeDef( def, _listError );
        const bool bParsed = _listError.empty();
        const bool bBuilt  = _circuit.build( def, GimmickNodeRegistry::getDefault(), _listError ) && bParsed;
        if ( bBuilt == false )
            _circuit.clear();
        for ( const string& error : _listError )
        {
            SW_LOG_ERROR( "%#", error );
        }
        _listBinding.clear();
        return bBuilt;
    }

    void GimmickCircuitComponent::onPostLoad()
    {
        Component::onPostLoad();
        if ( rebuild() == false || _stateBytes.empty() )
            return;
        if ( _circuit.loadState( _stateBytes ) == false )
        {
            SW_LOG_WARNING( "Saved gimmick state does not match circuit '%#' - starting from the initial state", getOwner() != nullptr ? getOwner()->getName().c_str() : "" );
            _stateBytes.clear();
        }
    }

    void GimmickCircuitComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        (void)rebuild();
    }

    void GimmickCircuitComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        if ( _circuit.isBuilt() == false && rebuild() && _stateBytes.empty() == false )
            (void)_circuit.loadState( _stateBytes ); // 모양이 다른 저장이면 false — 회로는 처음 상태로 시작한다
        bindNodes();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && _circuit.isBuilt() )
            applyActuators( *pManager );
    }

    GameObject* GimmickCircuitComponent::resolveTarget( GameObjectManager& manager, int32 node ) const
    {
        const size_t index = static_cast<size_t>( node );
        if ( index < _listNodeTarget.size() && _listNodeTarget[index].isValid() )
            return manager.resolveGameObject( _listNodeTarget[index] );
        return getOwner();
    }

    void GimmickCircuitComponent::bindNodes()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        _listBinding.assign( static_cast<size_t>( _circuit.getNodeCount() ), Binding::None );
        _listRestPose.resize( static_cast<size_t>( _circuit.getNodeCount() ) );
        if ( pManager == nullptr )
            return;
        for ( int32 node = 0; node < _circuit.getNodeCount(); ++node )
        {
            const Binding binding                     = findBinding( _circuit.getKind( node )._name );
            _listBinding[static_cast<size_t>( node )] = binding;
            GameObject* pTarget                       = resolveTarget( *pManager, node );
            if ( pTarget == nullptr )
            {
                if ( binding != Binding::None )
                    SW_LOG_ERROR( "Gimmick node '%#' points at an object that is not loaded", _circuit.getNodeID( node ).c_str() );
                continue;
            }
            const bool bNeedsSensor = binding == Binding::Volume || binding == Binding::PressurePlate || binding == Binding::Damage || binding == Binding::Interaction ||
                                      binding == Binding::Signal || binding == Binding::Hazard;
            if ( bNeedsSensor && pTarget->getComponent<GimmickSensorComponent>() == nullptr )
            {
                const string error = string( "Gimmick node '" ) + _circuit.getNodeID( node ).c_str() + "' (" + _circuit.getKind( node )._name.c_str() + ") needs a GimmickSensorComponent on '" +
                                     pTarget->getName().c_str() + "'";
                SW_LOG_ERROR( "%#", error );
                _listError.push_back( error );
            }
            if ( binding == Binding::Mover )
            {
                const SplineComponent* pSpline = pTarget->getComponent<SplineComponent>();
                if ( pSpline == nullptr && pOwner != nullptr )
                    pSpline = pOwner->getComponent<SplineComponent>();
                if ( pSpline != nullptr && pSpline->getWorldPath().isValid() )
                    _circuit.setPathLength( node, pSpline->getWorldPath().getLength() );
            }
            GimmickRestPose&      pose   = _listRestPose[static_cast<size_t>( node )];
            const SceneComponent* pScene = pTarget->getPrimarySceneComponent();
            if ( pose._bCaptured == false && pScene != nullptr )
            {
                pose._position               = pScene->getLocalPosition();
                pose._rotation               = pScene->getLocalRotation();
                pose._bCaptured              = true;
                const LightComponent* pLight = pTarget->getComponent<LightComponent>();
                if ( pLight != nullptr )
                    pose._intensity = pLight->getIntensity();
            }
        }
    }

    float32 GimmickCircuitComponent::computeProximity( GameObjectManager& manager, int32 node, const GameObject& target ) const
    {
        const string*         pTag   = GimmickCircuitComponentInternal::readText( _circuit, node, "tag" );
        const SceneComponent* pScene = target.getPrimarySceneComponent();
        if ( pTag == nullptr || pTag->empty() || pScene == nullptr )
            return GimmickCircuitComponentInternal::kFarDistance;
        const TagID  tag      = TagID::request( *pTag );
        const bool   bPlanar  = GimmickCircuitComponentInternal::readNumber( _circuit, node, "planar", 0.0f ) != 0.0f;
        const float3 origin   = pScene->getWorldPosition();
        float32      nearest  = GimmickCircuitComponentInternal::kFarDistance;
        const uint64 targetID = target.getObjectID();
        manager.forEachGameObject( [&]( GameObject* pObject )
        {
            const SceneComponent* pOther = pObject != nullptr ? pObject->getPrimarySceneComponent() : nullptr;
            if ( pOther == nullptr || pObject->getObjectID() == targetID || pObject->isActiveInHierarchy() == false || pObject->hasTag( tag ) == false )
                return;
            float3 delta = pOther->getWorldPosition() - origin;
            if ( bPlanar )
                delta._z = 0.0f;
            nearest = MathUtil::min( nearest, delta.getLength() );
        } );
        return nearest;
    }

    float32 GimmickCircuitComponent::computeLaserBlocked( GameObjectManager& manager, int32 node, const GameObject& target ) const
    {
        const SceneComponent* pScene = target.getPrimarySceneComponent();
        if ( pScene == nullptr )
            return 0.0f;
        const float4x4 world     = pScene->getWorldMatrix();
        const float3   origin    = pScene->getWorldPosition();
        const float3   direction = float3::transformVector( GimmickCircuitComponentInternal::readVector( _circuit, node, "direction" ), world ).normalize();
        const float32  range     = GimmickCircuitComponentInternal::readNumber( _circuit, node, "range", 10.0f );
        WorldRayHit    hit;
        return WorldQuery::raycast( manager, origin, origin + direction * range, target.getObjectID(), hit ) ? 1.0f : 0.0f;
    }

    void GimmickCircuitComponent::pullSensors( GameObjectManager& manager )
    {
        for ( int32 node = 0; node < _circuit.getNodeCount() && static_cast<size_t>( node ) < _listBinding.size(); ++node )
        {
            const Binding binding = _listBinding[static_cast<size_t>( node )];
            if ( binding == Binding::None )
                continue;
            GameObject* pTarget = resolveTarget( manager, node );
            if ( pTarget == nullptr )
                continue;
            GimmickSensorComponent* pSensor = pTarget->getComponent<GimmickSensorComponent>();
            switch ( binding )
            {
                case Binding::Volume:
                {
                    if ( pSensor != nullptr )
                        _circuit.setSensorValue( node, static_cast<float32>( pSensor->getOccupantCount() ) );
                    break;
                }
                case Binding::PressurePlate:
                {
                    if ( pSensor != nullptr )
                        _circuit.setSensorValue( node, pSensor->computeOccupantWeight() );
                    break;
                }
                case Binding::Damage:
                {
                    if ( pSensor != nullptr )
                        _circuit.addSensorImpulse( node, pSensor->consumeDamage() );
                    break;
                }
                case Binding::Interaction:
                {
                    if ( pSensor != nullptr )
                        _circuit.addSensorImpulse( node, pSensor->consumeUses() );
                    break;
                }
                case Binding::Signal:
                {
                    if ( pSensor != nullptr )
                        _circuit.setSensorValue( node, pSensor->getSignal() );
                    break;
                }
                case Binding::Laser:
                {
                    _circuit.setSensorValue( node, computeLaserBlocked( manager, node, *pTarget ) );
                    break;
                }
                case Binding::Proximity:
                {
                    _circuit.setSensorValue( node, computeProximity( manager, node, *pTarget ) );
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
    }

    void GimmickCircuitComponent::applyActuators( GameObjectManager& manager )
    {
        for ( int32 node = 0; node < _circuit.getNodeCount() && static_cast<size_t>( node ) < _listBinding.size(); ++node )
        {
            applyActuator( manager, node, _listBinding[static_cast<size_t>( node )] );
        }
    }

    void GimmickCircuitComponent::applyActuator( GameObjectManager& manager, int32 node, Binding binding )
    {
        using Internal      = GimmickCircuitComponentInternal;
        GameObject* pTarget = resolveTarget( manager, node );
        if ( pTarget == nullptr )
            return;
        SceneComponent*        pScene = pTarget->getPrimarySceneComponent();
        const GimmickRestPose& pose   = _listRestPose[static_cast<size_t>( node )];
        const float32          value  = _circuit.getActuatorValue( node );
        switch ( binding )
        {
            case Binding::Door:
            {
                if ( pScene == nullptr )
                    break;
                pScene->setLocalPosition( pose._position + Internal::readVector( _circuit, node, "openOffset" ) * value );
                pScene->setLocalRotation( pose._rotation + Internal::toRadians( Internal::readVector( _circuit, node, "openRotation" ) ) * value );
                break;
            }
            case Binding::Elevator:
            {
                if ( pScene != nullptr )
                    pScene->setLocalPosition( pose._position + Internal::readVector( _circuit, node, "axis" ) * value );
                break;
            }
            case Binding::Rotator:
            {
                if ( pScene != nullptr )
                    pScene->setLocalRotation( pose._rotation + Internal::toRadians( Internal::readVector( _circuit, node, "axis" ) * value ) );
                break;
            }
            case Binding::Mover:
            {
                const SplineComponent* pSpline = pTarget->getComponent<SplineComponent>();
                if ( pSpline == nullptr && getOwner() != nullptr )
                    pSpline = getOwner()->getComponent<SplineComponent>();
                if ( pScene == nullptr || pSpline == nullptr || pSpline->getWorldPath().isValid() == false )
                    break;
                const SplineSample sample = pSpline->getWorldPath().sampleAtDistance( value );
                pScene->setWorldPosition( sample._position );
                if ( Internal::readNumber( _circuit, node, "faceForward", 0.0f ) != 0.0f )
                    pScene->setLocalRotation( float3{ pose._rotation._x, MathUtil::atan2( sample._tangent._x, sample._tangent._z ), pose._rotation._z } );
                break;
            }
            case Binding::Hazard:
            {
                const GimmickSensorComponent* pSensor = pTarget->getComponent<GimmickSensorComponent>();
                if ( pSensor == nullptr || _circuit.getOutput( node, hashed_string( "OnDamageTick" ) ) == false )
                    break;
                const float32 damage = Internal::readNumber( _circuit, node, "damage", 0.0f );
                for ( const GameObjectHandle& handle : pSensor->getOccupants() )
                {
                    GameObject* pVictim = manager.resolveGameObject( handle );
                    if ( pVictim != nullptr )
                        GimmickDamageUtil::applyDamage( *pVictim, pTarget, hashed_string( "Hazard" ), damage );
                }
                break;
            }
            case Binding::Light:
            {
                LightComponent* pLight = pTarget->getComponent<LightComponent>();
                if ( pLight == nullptr )
                    break;
                const float32 intensity = _circuit.getOutput( node, hashed_string( "Lit" ) ) ? pose._intensity * Internal::readNumber( _circuit, node, "intensity", 1.0f ) : 0.0f;
                if ( pLight->getIntensity() != intensity )
                {
                    const ComponentHandle light    = pLight->getHandle();
                    GameObjectManager*    pManager = &manager;
                    manager.executeOrDeferPostTick( [pManager, light, intensity]()
                    {
                        LightComponent* pResolved = static_cast<LightComponent*>( pManager->resolveComponent( light ) );
                        if ( pResolved != nullptr )
                            pResolved->setIntensity( intensity );
                    } );
                }
                break;
            }
            case Binding::Enable:
            {
                const bool bEnabled = _circuit.getOutput( node, hashed_string( "Enabled" ) );
                if ( pTarget->isActive() == bEnabled || pTarget == getOwner() )
                    break;
                const GameObjectHandle object   = pTarget->getHandle();
                GameObjectManager*     pManager = &manager;
                manager.executeOrDeferPostTick( [pManager, object, bEnabled]()
                {
                    GameObject* pResolved = pManager->resolveGameObject( object );
                    if ( pResolved != nullptr )
                        pResolved->setActive( bEnabled );
                } );
                break;
            }
            case Binding::Spawner:
            {
                const string* pPrefab = Internal::readText( _circuit, node, "prefab" );
                if ( _circuit.getOutput( node, hashed_string( "OnSpawn" ) ) == false || pPrefab == nullptr || pPrefab->empty() || pScene == nullptr )
                    break;
                const string       prefab   = *pPrefab;
                const float3       position = pScene->getWorldPosition() + Internal::readVector( _circuit, node, "offset" );
                GameObjectManager* pManager = &manager;
                manager.executeOrDeferPostTick( [pManager, prefab, position]()
                {
                    AssetManager*   pAssetManager = game::getService<AssetManager>();
                    GameObject*     pSpawned      = pAssetManager != nullptr ? pAssetManager->getPrefabCache().spawn( pManager, prefab ) : nullptr;
                    SceneComponent* pSpawnedScene = pSpawned != nullptr ? pSpawned->getPrimarySceneComponent() : nullptr;
                    if ( pSpawnedScene != nullptr )
                        pSpawnedScene->setWorldPosition( position );
                } );
                break;
            }
            case Binding::Sound:
            {
                const string* pSound = Internal::readText( _circuit, node, "sound" );
                if ( _circuit.getOutput( node, hashed_string( "OnPlay" ) ) == false || pSound == nullptr || pSound->empty() )
                    break;
                const string sound = *pSound;
                manager.executeOrDeferPostTick( [sound]()
                { (void)GameSound::play( sound ); } );
                break;
            }
            default:
            {
                break;
            }
        }
    }

    void GimmickCircuitComponent::storeState() { _circuit.saveState( _stateBytes ); }

    void GimmickCircuitComponent::stepOnce()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _circuit.isBuilt() == false )
            return;
        if ( _listBinding.size() != static_cast<size_t>( _circuit.getNodeCount() ) )
            bindNodes();
        pullSensors( *pManager );
        _circuit.step();
        applyActuators( *pManager );
        storeState();
    }

    void GimmickCircuitComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || _circuit.isBuilt() == false )
            return;
        const int32 stepCount = _circuit.consumeTime( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            pullSensors( *pManager );
            _circuit.step();
            applyActuators( *pManager );
        }
        if ( stepCount > 0 )
            storeState();
    }

    void GimmickCircuitComponent::captureCheckpoint() { _circuit.saveState( _checkpointBytes ); }

    void GimmickCircuitComponent::restoreCheckpoint()
    {
        if ( _checkpointBytes.empty() || _circuit.loadState( _checkpointBytes ) == false )
            _circuit.resetToInitial();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            applyActuators( *pManager );
        storeState();
    }

    void GimmickCircuitComponent::resetCircuit()
    {
        _checkpointBytes.clear();
        restoreCheckpoint();
    }
} // namespace sw
