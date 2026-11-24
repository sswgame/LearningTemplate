#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Circuit/GimmickCircuitComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/LightComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/GimmickDamageUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"
#include "GameFramework/Runtime/World/Spline/SplineComponent.h"

namespace sw
{
    namespace
    {
        struct GimmickCircuitComponentInternal
        {
            static constexpr float32 kFarDistance = 1.0e9f;

            static float3 toRadians( const float3& degrees ) { return degrees * MathUtil::kDegreeToRadian; }

            /** @brief 무버는 대상(없으면 회로 주인)의 스플라인에서 @p distance 위치로 옮깁니다. `faceForward` 면 진행 방향으로 돌립니다. */
            static void applyMover( const GimmickCircuit& circuit, int32 node, GameObject& target, const GameObject* pOwner, SceneComponent* pScene,
                                    const GimmickRestPose& pose, float32 distance )
            {
                const SplineComponent* pSpline = target.getComponent<SplineComponent>();
                if ( pSpline == nullptr && pOwner != nullptr )
                    pSpline = pOwner->getComponent<SplineComponent>();
                const bool bSplineUnavailable = pScene == nullptr || pSpline == nullptr || pSpline->getWorldPath().isValid() == false;
                if ( bSplineUnavailable )
                    return;
                const SplineSample sample = pSpline->getWorldPath().sampleAtDistance( distance );
                pScene->setWorldPosition( sample._position );
                if ( circuit.readParamNumber( node, "faceForward" ).value_or( 0.0f ) != 0.0f )
                    pScene->setLocalRotation( float3{ pose._rotation._x, MathUtil::atan2( sample._tangent._x, sample._tangent._z ), pose._rotation._z } );
            }

            /** @brief 피해 틱 출력이 켜진 스텝마다(`recordPulse`) 대상 센서 안의 오브젝트마다 피해를 줍니다. */
            static void applyHazard( const GimmickCircuit& circuit, int32 node, GameObjectManager& manager, GameObject& target )
            {
                const GimmickSensorComponent* pSensor = target.getComponent<GimmickSensorComponent>();
                if ( pSensor == nullptr )
                    return;
                const float32 damage = circuit.readParamNumber( node, "damage" ).value_or( 0.0f );
                for ( const GameObjectHandle& handle : pSensor->getOccupants() )
                {
                    GameObject* pVictim = manager.resolveGameObject( handle );
                    if ( pVictim != nullptr )
                        GimmickDamageUtil::applyDamage( *pVictim, &target, hashed_string( "Hazard" ), damage );
                }
            }

            /** @brief 라이트 세기를 바꿉니다. 컴포넌트 상태를 바꾸므로 틱 뒤로 미룹니다. */
            static void applyLight( const GimmickCircuit& circuit, int32 node, GameObjectManager& manager, GameObject& target, const GimmickRestPose& pose )
            {
                LightComponent* pLight = target.getComponent<LightComponent>();
                if ( pLight == nullptr )
                    return;
                const float32 intensity = circuit.getOutput( node, hashed_string( "Lit" ) ) ? pose._intensity * circuit.readParamNumber( node, "intensity" ).value_or( 1.0f ) : 0.0f;
                if ( pLight->getIntensity() == intensity )
                    return;
                const TComponentHandle<LightComponent> light{ pLight };
                GameObjectManager*                     pManager = &manager;
                manager.executeOrDeferPostTick( [pManager, light, intensity]()
                {
                    LightComponent* pResolved = pManager->resolveComponent( light );
                    if ( pResolved != nullptr )
                        pResolved->setIntensity( intensity );
                } );
            }

            /** @brief 대상을 켜고 끕니다. 회로 주인은 끄지 않습니다. 활성 상태 변경은 틱 뒤로 미룹니다. */
            static void applyEnable( const GimmickCircuit& circuit, int32 node, GameObjectManager& manager, GameObject& target, const GameObject* pOwner )
            {
                const bool bEnabled = circuit.getOutput( node, hashed_string( "Enabled" ) );
                if ( target.isActive() == bEnabled || &target == pOwner )
                    return;
                const GameObjectHandle object   = target.getHandle();
                GameObjectManager*     pManager = &manager;
                manager.executeOrDeferPostTick( [pManager, object, bEnabled]()
                {
                    GameObject* pResolved = pManager->resolveGameObject( object );
                    if ( pResolved != nullptr )
                        pResolved->setActive( bEnabled );
                } );
            }

            /** @brief 스폰 출력이 켜진 스텝마다(`recordPulse`) 프리팹을 대상 위치(오프셋 더함)에 스폰합니다. 스폰은 틱 뒤로 미룹니다. */
            static void applySpawner( const GimmickCircuit& circuit, int32 node, GameObjectManager& manager, const SceneComponent* pScene )
            {
                const string* pPrefab      = circuit.readParamText( node, "prefab" );
                const bool    bCannotSpawn = pPrefab == nullptr || pPrefab->empty() || pScene == nullptr;
                if ( bCannotSpawn )
                    return;
                const string       prefab   = *pPrefab;
                const float3       position = pScene->getWorldPosition() + circuit.readParamVector( node, "offset" );
                GameObjectManager* pManager = &manager;
                manager.executeOrDeferPostTick( [pManager, prefab, position]()
                {
                    AssetManager*   pAssetManager = game::findSubsystem<AssetManager>();
                    GameObject*     pSpawned      = pAssetManager != nullptr ? pAssetManager->getAssetCache<PrefabCache>().spawn( pManager, prefab ) : nullptr;
                    SceneComponent* pSpawnedScene = pSpawned != nullptr ? pSpawned->getPrimarySceneComponent() : nullptr;
                    if ( pSpawnedScene != nullptr )
                        pSpawnedScene->setWorldPosition( position );
                } );
            }

            /** @brief 재생 출력이 켜진 스텝마다(`recordPulse`) 소리를 냅니다. 재생은 틱 뒤로 미룹니다. */
            static void applySound( const GimmickCircuit& circuit, int32 node, GameObjectManager& manager )
            {
                const string* pSound           = circuit.readParamText( node, "sound" );
                const bool    bCannotPlaySound = pSound == nullptr || pSound->empty();
                if ( bCannotPlaySound )
                    return;
                const string sound = *pSound;
                manager.executeOrDeferPostTick( [sound]()
                { (void)GameSound::play( AssetID{ sound } ); } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GimmickCircuitComponent::GimmickCircuitComponent()
        : _listNode{}
        , _listNodeTarget{}
        , _listWire{}
        , _listComment{}
        , _listRestPose{}
        , _state{}
        , _stepTime{ constant::time::kDefaultFixedStepSeconds }
        , _circuit{}
        , _listBinding{}
        , _listNodeTag{}
        , _listTagged{}
        , _listPulsePort{}
        , _listPendingPulse{}
        , _listSensorRead{}
        , _listError{}
        , _checkpoint{}
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
        _state      = GimmickCircuitState{};
        _checkpoint = GimmickCircuitState{};
        _circuit.clear();
    }

    void GimmickCircuitComponent::makeDef( GimmickCircuitDef& outDef, vector<string>& outListError ) const
    {
        outDef.clear();
        const GameObject*                        pOwner = getOwner();
        StringBuilder<constant::buffer::kMax256> sourceName;
        if ( pOwner != nullptr )
            sourceName.appendFormat( "%#.GimmickCircuit", pOwner->getName().c_str() );
        else
            sourceName.append( "GimmickCircuit" );
        outDef.setSourceName( sourceName.view() );
        outDef.setStepTime( _stepTime );
        for ( const GimmickNodeDesc& desc : _listNode )
        {
            GimmickNodeDef& node = outDef.addNode( desc._id, desc._kind );
            // "name=value; name=value" 형식이다. 값에는 공백이 들어갈 수 있다("0 3 0").
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
                string_view name;
                string_view value;
                if ( StringUtil::trySplitKeyValue( pair, name, value ) == false )
                {
                    StringBuilder<constant::buffer::kMax512> message;
                    message.appendFormat( "%#: node '%#' has a parameter without name=value: '%#'", outDef.getSourceName(), desc._id.c_str(), pair );
                    outListError.push_back( string( message.view() ) );
                    continue;
                }
                node.setParam( hashed_string( name ), value );
            }
        }
        for ( const GimmickWireDesc& wire : _listWire )
        {
            if ( outDef.addWire( wire._from, wire._to, wire._bInvert ) == false )
            {
                StringBuilder<constant::buffer::kMax512> message;
                message.appendFormat( "%#: wire '%#' -> '%#' needs node.port on both ends", outDef.getSourceName(), wire._from, wire._to );
                outListError.push_back( string( message.view() ) );
            }
        }
    }

    bool GimmickCircuitComponent::rebuild()
    {
        _listError.clear();
        GimmickCircuitDef def;
        makeDef( def, _listError );
        const bool bParsed = _listError.empty();
        const bool bBuilt  = _circuit.populate( def, game::getSubsystem<GimmickNodeRegistrySubsystem>().getRegistry(), _listError ) && bParsed;
        if ( bBuilt == false )
            _circuit.clear();
        for ( const string& error : _listError )
        {
            SW_LOG_ERROR_ID( "GimmickCircuitComponent.CircuitInvalid", "%#", error );
        }
        _listBinding.clear();
        _listNodeTag.clear();
        _listPulsePort.clear();
        _listPendingPulse.clear();
        _listSensorRead.clear();
        return bBuilt;
    }

    void GimmickCircuitComponent::onPostLoad()
    {
        Component::onPostLoad();
        if ( rebuild() && _state.isEmpty() == false )
            applySavedState( _state );
    }

    void GimmickCircuitComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        (void)rebuild();
    }

    void GimmickCircuitComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const bool bRestoreState = _circuit.isBuilt() == false && rebuild() && _state.isEmpty() == false;
        if ( bRestoreState )
            applySavedState( _state );
        bindNodes();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr && _circuit.isBuilt() )
            applyActuators( *pManager );
    }

    const GameObject* GimmickCircuitComponent::resolveTarget( const GameObjectManager& manager, int32 node ) const
    {
        const size_t index = static_cast<size_t>( node );
        if ( index < _listNodeTarget.size() && _listNodeTarget[index].isValid() )
            return manager.resolveGameObject( _listNodeTarget[index] );
        return getOwner();
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
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        _listBinding.assign( static_cast<size_t>( _circuit.getNodeCount() ), Binding::None );
        _listNodeTag.assign( static_cast<size_t>( _circuit.getNodeCount() ), TagContainer{} );
        _listPulsePort.assign( static_cast<size_t>( _circuit.getNodeCount() ), hashed_string{} );
        _listRestPose.resize( static_cast<size_t>( _circuit.getNodeCount() ) );
        _listPlateWeight.assign( static_cast<size_t>( _circuit.getNodeCount() ), 0.0f );
        if ( pManager == nullptr )
            return;
        for ( int32 node = 0; node < _circuit.getNodeCount(); ++node )
        {
            const Binding binding                     = findBinding( _circuit.getKind( node )._name );
            _listBinding[static_cast<size_t>( node )] = binding;
            // 펄스 작동기는 켜진 스텝마다 한 번씩 돈다(`recordPulse`). 상태 작동기는 틱의 마지막 값만 적용한다.
            if ( binding == Binding::Hazard )
                _listPulsePort[static_cast<size_t>( node )] = hashed_string( "OnDamageTick" );
            else if ( binding == Binding::Spawner )
                _listPulsePort[static_cast<size_t>( node )] = hashed_string( "OnSpawn" );
            else if ( binding == Binding::Sound )
                _listPulsePort[static_cast<size_t>( node )] = hashed_string( "OnPlay" );
            // 근접 노드의 태그는 여기서 한 번 TagID 로 바꾼다. 스텝마다 문자열을 intern 하지 않는다.
            const string* pTag = binding == Binding::Proximity ? _circuit.readParamText( node, "tag" ) : nullptr;
            if ( pTag != nullptr && pTag->empty() == false )
                _listNodeTag[static_cast<size_t>( node )].addTag( TagID::request( *pTag ) );
            const GameObject* pTarget = resolveTarget( *pManager, node );
            if ( pTarget == nullptr )
            {
                if ( binding != Binding::None )
                    SW_LOG_ERROR_ID( "GimmickCircuitComponent.GimmickNodePointsObject", "Gimmick node '%#' points at an object that is not loaded", _circuit.getNodeID( node ).c_str() );
                continue;
            }
            const bool bNeedsSensor = binding == Binding::Volume || binding == Binding::PressurePlate || binding == Binding::Damage || binding == Binding::Interaction ||
                                      binding == Binding::Signal || binding == Binding::Hazard;
            if ( bNeedsSensor && pTarget->getComponent<GimmickSensorComponent>() == nullptr )
            {
                StringBuilder<constant::buffer::kMax512> error;
                error.appendFormat( "Gimmick node '%#' (%#) needs a GimmickSensorComponent on '%#'", _circuit.getNodeID( node ).c_str(), _circuit.getKind( node )._name.c_str(),
                                    pTarget->getName().c_str() );
                SW_LOG_ERROR_ID( "GimmickCircuitComponent.SensorMissing", "%#", error.view() );
                _listError.push_back( string( error.view() ) );
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
                pose._position               = pScene->getZeroOriginPosition();
                pose._rotation               = pScene->getLocalRotation();
                pose._bCaptured              = true;
                const LightComponent* pLight = pTarget->getComponent<LightComponent>();
                if ( pLight != nullptr )
                    pose._intensity = pLight->getIntensity();
            }
        }
        refreshPlateWeights( *pManager );
    }

    float32 GimmickCircuitComponent::computeProximity( const GameObjectManager& manager, int32 node, const GameObject& target )
    {
        const size_t          index        = static_cast<size_t>( node );
        const SceneComponent* pScene       = target.getPrimarySceneComponent();
        const bool            bNoTagFilter = index >= _listNodeTag.size() || _listNodeTag[index].getTagCount() == 0 || pScene == nullptr;
        if ( bNoTagFilter )
            return GimmickCircuitComponentInternal::kFarDistance;
        const bool   bPlanar  = _circuit.readParamNumber( node, "planar" ).value_or( 0.0f ) != 0.0f;
        const float3 origin   = pScene->getWorldPosition();
        float32      nearest  = GimmickCircuitComponentInternal::kFarDistance;
        const uint64 targetID = target.getObjectID();
        manager.getTagIndex().findObjectsWithTags( _listNodeTag[index], _listTagged );
        for ( const GameObject* pObject : _listTagged )
        {
            const SceneComponent* pOther         = pObject->getPrimarySceneComponent();
            const bool            bSkipCandidate = pOther == nullptr || pObject->getObjectID() == targetID || pObject->isActiveInHierarchy() == false;
            if ( bSkipCandidate )
                continue;
            float3 delta = pOther->getWorldPosition() - origin;
            if ( bPlanar )
                delta._z = 0.0f;
            nearest = MathUtil::min( nearest, delta.getLength() );
        }
        return nearest;
    }

    float32 GimmickCircuitComponent::computeLaserBlocked( const GameObjectManager& manager, int32 node, const GameObject& target ) const
    {
        const SceneComponent* pScene = target.getPrimarySceneComponent();
        if ( pScene == nullptr )
            return 0.0f;
        const float4x4 world     = pScene->getWorldMatrix();
        const float3   origin    = pScene->getWorldPosition();
        const float3   direction = float3::transformVector( _circuit.readParamVector( node, "direction" ), world ).normalize();
        const float32  range     = _circuit.readParamNumber( node, "range" ).value_or( 10.0f );
        WorldRayHit    hit;
        return WorldQuery::raycast( manager, origin, origin + direction * range, target.getObjectID(), hit ) ? 1.0f : 0.0f;
    }

    float32 GimmickCircuitComponent::readSensorImpulse( const GimmickSensorComponent& sensor, Binding binding, bool bFirstStep )
    {
        // 한 틱에 한 번만 읽는다 — 옛 소비처럼 둘째 스텝이나 같은 센서를 보는 둘째 노드는 0 을 받는다.
        // 센서는 남의 오브젝트일 수 있어 틱에서는 읽기만 하고, 읽은 몫을 비우는 것은 틱 뒤(`applyPendingActuators`)다.
        const TComponentHandle<GimmickSensorComponent> handle{ &sensor };
        if ( bFirstStep == false )
            return 0.0f;
        for ( const SensorRead& read : _listSensorRead )
        {
            if ( read._sensor == handle && read._binding == binding )
                return 0.0f;
        }
        SensorRead read;
        read._sensor  = handle;
        read._read    = binding == Binding::Damage ? sensor.peekDamage() : sensor.peekUses();
        read._binding = binding;
        _listSensorRead.push_back( read );
        return read._read._amount;
    }

    void GimmickCircuitComponent::pullSensors( const GameObjectManager& manager, bool bFirstStep )
    {
        for ( int32 node = 0; node < _circuit.getNodeCount() && static_cast<size_t>( node ) < _listBinding.size(); ++node )
        {
            const Binding binding = _listBinding[static_cast<size_t>( node )];
            if ( binding == Binding::None )
                continue;
            const GameObject* pTarget = resolveTarget( manager, node );
            if ( pTarget == nullptr )
                continue;
            // 센서는 남의 오브젝트지만 틱 중에 읽어도 되는 형식이다(`GimmickSensorComponent::kTickReadSafe`).
            const GimmickSensorComponent* pSensor = pTarget->getTickSafeComponent<GimmickSensorComponent>();
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
                    if ( pSensor != nullptr && static_cast<size_t>( node ) < _listPlateWeight.size() )
                        _circuit.setSensorValue( node, _listPlateWeight[static_cast<size_t>( node )] );
                    break;
                }
                case Binding::Damage:
                {
                    if ( pSensor != nullptr )
                        _circuit.addSensorImpulse( node, readSensorImpulse( *pSensor, binding, bFirstStep ) );
                    break;
                }
                case Binding::Interaction:
                {
                    if ( pSensor != nullptr )
                        _circuit.addSensorImpulse( node, readSensorImpulse( *pSensor, binding, bFirstStep ) );
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

    void GimmickCircuitComponent::refreshPlateWeights( const GameObjectManager& manager )
    {
        _listPlateWeight.resize( _listBinding.size(), 0.0f );
        for ( size_t node = 0; node < _listBinding.size(); ++node )
        {
            if ( _listBinding[node] != Binding::PressurePlate )
                continue;
            const GameObject*             pTarget = resolveTarget( manager, static_cast<int32>( node ) );
            const GimmickSensorComponent* pSensor = pTarget != nullptr ? pTarget->getComponent<GimmickSensorComponent>() : nullptr;
            _listPlateWeight[node]                = pSensor != nullptr ? pSensor->computeOccupantWeight() : 0.0f;
        }
    }

    void GimmickCircuitComponent::recordPulse()
    {
        _circuit.appendRaisedOutputs( _listPulsePort, _listPendingPulse );
    }

    void GimmickCircuitComponent::applyActuators( GameObjectManager& manager )
    {
        recordPulse();
        applyPendingActuators( manager );
    }

    void GimmickCircuitComponent::applyPendingActuators( GameObjectManager& manager )
    {
        for ( const SensorRead& read : _listSensorRead )
        {
            GimmickSensorComponent* pSensor = manager.resolveComponent( read._sensor );
            if ( pSensor == nullptr )
                continue;
            if ( read._binding == Binding::Damage )
                pSensor->acknowledgeDamage( read._read );
            else
                pSensor->acknowledgeUses( read._read );
        }
        _listSensorRead.clear();
        // 펄스는 켜진 (스텝, 노드) 순서대로 모두, 상태 작동기는 마지막 스텝의 값으로 한 번(앞 스텝의 자세는 같은 틱 안에서 덮였다).
        for ( const int32 node : _listPendingPulse )
        {
            applyActuator( manager, node, _listBinding[static_cast<size_t>( node )] );
        }
        _listPendingPulse.clear();
        for ( int32 node = 0; node < _circuit.getNodeCount() && static_cast<size_t>( node ) < _listBinding.size(); ++node )
        {
            if ( _listPulsePort[static_cast<size_t>( node )].empty() )
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
        // 바인딩마다 짧은 분기다. 몇 줄이 넘는 액추에이터는 내부 함수로 나눴다.
        switch ( binding )
        {
            case Binding::Door:
            {
                if ( pScene == nullptr )
                    break;
                pScene->setZeroOriginPosition( pose._position + _circuit.readParamVector( node, "openOffset" ) * value );
                pScene->setLocalRotation( pose._rotation + Internal::toRadians( _circuit.readParamVector( node, "openRotation" ) ) * value );
                break;
            }
            case Binding::Elevator:
            {
                if ( pScene != nullptr )
                    pScene->setZeroOriginPosition( pose._position + _circuit.readParamVector( node, "axis" ) * value );
                break;
            }
            case Binding::Rotator:
            {
                if ( pScene != nullptr )
                    pScene->setLocalRotation( pose._rotation + Internal::toRadians( _circuit.readParamVector( node, "axis" ) * value ) );
                break;
            }
            case Binding::Mover:
            {
                Internal::applyMover( _circuit, node, *pTarget, getOwner(), pScene, pose, value );
                break;
            }
            case Binding::Hazard:
            {
                Internal::applyHazard( _circuit, node, manager, *pTarget );
                break;
            }
            case Binding::Light:
            {
                Internal::applyLight( _circuit, node, manager, *pTarget, pose );
                break;
            }
            case Binding::Enable:
            {
                Internal::applyEnable( _circuit, node, manager, *pTarget, getOwner() );
                break;
            }
            case Binding::Spawner:
            {
                Internal::applySpawner( _circuit, node, manager, pScene );
                break;
            }
            case Binding::Sound:
            {
                Internal::applySound( _circuit, node, manager );
                break;
            }
            default:
            {
                break;
            }
        }
    }

    void GimmickCircuitComponent::storeState()
    {
        _circuit.saveState( _state );
    }

    void GimmickCircuitComponent::applySavedState( const GimmickCircuitState& state )
    {
        const GameObject*      pOwner    = getOwner();
        const utf8*            pName     = pOwner != nullptr ? pOwner->getName().c_str() : "";
        const GimmickStateLoad loaded    = _circuit.loadState( state, GimmickStateMatch::NodeSetChanged );
        const bool             bMismatch = loaded._match == GimmickStateMatch::Mismatch;
        if ( bMismatch )
        {
            // 같은 id 인데 종류나 상태 뜻이 바뀐 노드다. 슬롯의 뜻을 메울 수 없으므로 처음 상태로 바꾸지 않고 멈춘다.
            const utf8* pNode = loaded._changedNode.has_value() ? _circuit.getNodeID( *loaded._changedNode ).c_str() : "(duplicate node id in the saved state)";
            utf8        arrBuffer[constant::buffer::kMax512]{};
            formatstring( arrBuffer, constant::buffer::kMax512,
                          "Saved gimmick state of '%#' does not fit node '%#' (its kind or state version changed) - the circuit does not run until the saved state is cleared",
                          pName, pNode );
            SW_LOG_ERROR_ID( "GimmickCircuitComponent.SavedStateMismatch", "%#", arrBuffer );
            _listError.push_back( string( arrBuffer ) );
            _circuit.clear();
            return;
        }
        if ( loaded._match == GimmickStateMatch::NodeSetChanged )
            SW_LOG_WARNING_ID( "GimmickCircuitComponent.SavedGimmickStateWritten", "Saved gimmick state of '%#' was written for a different node set - matching nodes continue, new nodes start from their initial state", pName );
        commitRestoredState();
    }

    void GimmickCircuitComponent::commitRestoredState()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            applyActuators( *pManager );
        storeState();
    }

    void GimmickCircuitComponent::stepOnce()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager == nullptr || _circuit.isBuilt() == false )
            return;
        if ( _listBinding.size() != static_cast<size_t>( _circuit.getNodeCount() ) )
            bindNodes();
        refreshPlateWeights( *pManager );
        pullSensors( *pManager, true );
        _circuit.step();
        applyActuators( *pManager );
        storeState();
    }

    void GimmickCircuitComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _circuit.isBuilt() == false )
            return;
        // 압력판 무게 사본은 스텝이 없는 틱에도 매 틱 뒤에 받는다 — 스텝 간격이 프레임보다 길어도 사본이 한 프레임 넘게 묵지 않는다.
        if ( std::find( _listBinding.begin(), _listBinding.end(), Binding::PressurePlate ) != _listBinding.end() )
        {
            context.deferPostTickOnComponent( *this, []( GimmickCircuitComponent& self, GameObjectManager& sceneManager )
            { self.refreshPlateWeights( sceneManager ); } );
        }
        const int32 stepCount = _circuit.consumeTime( context.getDeltaTime() );
        if ( stepCount <= 0 )
            return;
        // 스텝은 회로 자기 상태만 돈다(센서는 읽기만). 작동기 쓰기(남의 오브젝트)는 마지막에 한 번 틱 뒤로 모은다 — 언리얼 Mass 의 Defer 자리.
        const GameObjectManager& manager = context.getManager();
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            pullSensors( manager, stepIndex == 0 );
            _circuit.step();
            recordPulse();
        }
        storeState();
        context.deferPostTickOnComponent( *this, []( GimmickCircuitComponent& self, GameObjectManager& sceneManager )
        { self.applyPendingActuators( sceneManager ); } );
    }

    void GimmickCircuitComponent::captureCheckpoint()
    {
        _circuit.saveState( _checkpoint );
    }

    void GimmickCircuitComponent::restoreCheckpoint()
    {
        if ( _circuit.isBuilt() == false )
            return;
        if ( _checkpoint.isEmpty() )
        {
            // 잡은 체크포인트가 없다는 뜻이다(`resetCircuit` 이 비운다). 실패를 덮는 길이 아니다.
            _circuit.resetToInitial();
            commitRestoredState();
            return;
        }
        applySavedState( _checkpoint );
    }

    void GimmickCircuitComponent::resetCircuit()
    {
        _checkpoint = GimmickCircuitState{};
        restoreCheckpoint();
    }

    bool GimmickCircuitComponent::restoreState( const GimmickCircuitState& state )
    {
        if ( _circuit.isBuilt() == false )
            return false;
        const GimmickStateLoad loaded = _circuit.loadState( state, GimmickStateMatch::Exact );
        if ( loaded._bApplied == false )
            return false;
        commitRestoredState();
        return true;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 런타임 기믹 디버거(HUD 섹션 `gimmick`)입니다. 대상은 `debug.target`, 없으면 활성 씬의 첫 회로입니다. */
        struct GimmickDebugSectionInternal
        {
            static void writeGimmick( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const       pManager = GameplayDebugTarget::findActiveObjectManager();
                const GimmickCircuitComponent* const pCircuit = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<GimmickCircuitComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                if ( pCircuit == nullptr )
                {
                    writer.addLine( "target", "none (no gimmick circuit in the active scene)" );
                    return;
                }
                writer.addLine( "target", pCircuit->getOwner() != nullptr ? string( pCircuit->getOwner()->getName().c_str() ) : string( "?" ) );
                for ( const string& error : pCircuit->getErrors() )
                {
                    writer.addLine( "error", error );
                }
                if ( pCircuit->getCircuit().isBuilt() == false )
                {
                    writer.addLine( "circuit", "not built (see the errors)" );
                    return;
                }
                pCircuit->getCircuit().writeDebugLines( writer, GameplayDebugTarget::kMaxLines );
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( gimmick, "gimmick", "Gimmick circuit", 220, DebugHUDSectionFlag::kNone, &GimmickDebugSectionInternal::writeGimmick );
} // namespace sw
