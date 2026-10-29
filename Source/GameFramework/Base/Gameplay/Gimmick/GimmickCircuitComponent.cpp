#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/Component/3D/LightComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/Prefab/PrefabAsset.h"

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

            [[nodiscard]] static std::optional<float32> readNumber( const GimmickCircuit& circuit, int32 node, const utf8* pName )
            {
                const GimmickParamValue* pValue = circuit.findParam( node, hashed_string( pName ) );
                return pValue != nullptr ? std::optional<float32>{ pValue->_vector._x } : std::nullopt; // 없을 때의 값은 호출부가 정한다
            }

            static const string* readText( const GimmickCircuit& circuit, int32 node, const utf8* pName )
            {
                const GimmickParamValue* pValue = circuit.findParam( node, hashed_string( pName ) );
                return pValue != nullptr ? &pValue->_text : nullptr;
            }

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
                if ( readNumber( circuit, node, "faceForward" ).value_or( 0.0f ) != 0.0f )
                    pScene->setLocalRotation( float3{ pose._rotation._x, MathUtil::atan2( sample._tangent._x, sample._tangent._z ), pose._rotation._z } );
            }

            /** @brief 피해 틱 출력이 켜진 틱에 대상 센서 안의 오브젝트마다 피해를 줍니다. */
            static void applyHazard( const GimmickCircuit& circuit, int32 node, GameObjectManager& manager, GameObject& target )
            {
                const GimmickSensorComponent* pSensor = target.getComponent<GimmickSensorComponent>();
                if ( pSensor == nullptr || circuit.getOutput( node, hashed_string( "OnDamageTick" ) ) == false )
                    return;
                const float32 damage = readNumber( circuit, node, "damage" ).value_or( 0.0f );
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
                const float32 intensity = circuit.getOutput( node, hashed_string( "Lit" ) ) ? pose._intensity * readNumber( circuit, node, "intensity" ).value_or( 1.0f ) : 0.0f;
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

            /** @brief 스폰 출력이 켜진 틱에 프리팹을 대상 위치(오프셋 더함)에 스폰합니다. 스폰은 틱 뒤로 미룹니다. */
            static void applySpawner( const GimmickCircuit& circuit, int32 node, GameObjectManager& manager, const SceneComponent* pScene )
            {
                const string* pPrefab      = readText( circuit, node, "prefab" );
                const bool    bCannotSpawn = circuit.getOutput( node, hashed_string( "OnSpawn" ) ) == false || pPrefab == nullptr || pPrefab->empty() || pScene == nullptr;
                if ( bCannotSpawn )
                    return;
                const string       prefab   = *pPrefab;
                const float3       position = pScene->getWorldPosition() + readVector( circuit, node, "offset" );
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

            /** @brief 재생 출력이 켜진 틱에 소리를 냅니다. 재생은 틱 뒤로 미룹니다. */
            static void applySound( const GimmickCircuit& circuit, int32 node, GameObjectManager& manager )
            {
                const string* pSound           = readText( circuit, node, "sound" );
                const bool    bCannotPlaySound = circuit.getOutput( node, hashed_string( "OnPlay" ) ) == false || pSound == nullptr || pSound->empty();
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
        , _listRestPose{}
        , _state{}
        , _stepTime{ GimmickCircuitDef::kDefaultStepTime }
        , _circuit{}
        , _listBinding{}
        , _listNodeTag{}
        , _listTagged{}
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
        const GameObject* pOwner = getOwner();
        outDef.setSourceName( pOwner != nullptr ? string( pOwner->getName().c_str() ) + ".GimmickCircuit" : string( "GimmickCircuit" ) );
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
                    outListError.push_back( outDef.getSourceName() + ": node '" + desc._id.c_str() + "' has a parameter without name=value: '" + string( pair ) + "'" );
                    continue;
                }
                node.setParam( hashed_string( string( name ) ), value );
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
        const bool bBuilt  = _circuit.populate( def, game::getSubsystem<GimmickNodeKindCatalog>().getRegistry(), _listError ) && bParsed;
        if ( bBuilt == false )
            _circuit.clear();
        for ( const string& error : _listError )
        {
            SW_LOG_ERROR( "%#", error );
        }
        _listBinding.clear();
        _listNodeTag.clear();
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
        setTickGroup( TickGroup::PrePhysics );
        const bool bRestoreState = _circuit.isBuilt() == false && rebuild() && _state.isEmpty() == false;
        if ( bRestoreState )
            applySavedState( _state );
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
        _listNodeTag.assign( static_cast<size_t>( _circuit.getNodeCount() ), TagContainer{} );
        _listRestPose.resize( static_cast<size_t>( _circuit.getNodeCount() ) );
        if ( pManager == nullptr )
            return;
        for ( int32 node = 0; node < _circuit.getNodeCount(); ++node )
        {
            const Binding binding                     = findBinding( _circuit.getKind( node )._name );
            _listBinding[static_cast<size_t>( node )] = binding;
            // 근접 노드의 태그는 여기서 한 번 TagID 로 바꾼다. 스텝마다 문자열을 intern 하지 않는다.
            const string* pTag = binding == Binding::Proximity ? GimmickCircuitComponentInternal::readText( _circuit, node, "tag" ) : nullptr;
            if ( pTag != nullptr && pTag->empty() == false )
                _listNodeTag[static_cast<size_t>( node )].addTag( TagID::request( *pTag ) );
            GameObject* pTarget = resolveTarget( *pManager, node );
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

    float32 GimmickCircuitComponent::computeProximity( GameObjectManager& manager, int32 node, const GameObject& target )
    {
        const size_t          index        = static_cast<size_t>( node );
        const SceneComponent* pScene       = target.getPrimarySceneComponent();
        const bool            bNoTagFilter = index >= _listNodeTag.size() || _listNodeTag[index].getTagCount() == 0 || pScene == nullptr;
        if ( bNoTagFilter )
            return GimmickCircuitComponentInternal::kFarDistance;
        const bool   bPlanar  = GimmickCircuitComponentInternal::readNumber( _circuit, node, "planar" ).value_or( 0.0f ) != 0.0f;
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

    float32 GimmickCircuitComponent::computeLaserBlocked( GameObjectManager& manager, int32 node, const GameObject& target ) const
    {
        const SceneComponent* pScene = target.getPrimarySceneComponent();
        if ( pScene == nullptr )
            return 0.0f;
        const float4x4 world     = pScene->getWorldMatrix();
        const float3   origin    = pScene->getWorldPosition();
        const float3   direction = float3::transformVector( GimmickCircuitComponentInternal::readVector( _circuit, node, "direction" ), world ).normalize();
        const float32  range     = GimmickCircuitComponentInternal::readNumber( _circuit, node, "range" ).value_or( 10.0f );
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
        // 바인딩마다 짧은 분기다. 몇 줄이 넘는 액추에이터는 내부 함수로 나눴다.
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
            const utf8* pNode = loaded._changedNode >= 0 ? _circuit.getNodeID( loaded._changedNode ).c_str() : "(duplicate node id in the saved state)";
            utf8        arrBuffer[constant::kMaxBuffer512]{};
            formatstring( arrBuffer, constant::kMaxBuffer512,
                          "Saved gimmick state of '%#' does not fit node '%#' (its kind or state version changed) - the circuit does not run until the saved state is cleared",
                          pName, pNode );
            SW_LOG_ERROR( "%#", arrBuffer );
            _listError.push_back( string( arrBuffer ) );
            _circuit.clear();
            return;
        }
        if ( loaded._match == GimmickStateMatch::NodeSetChanged )
            SW_LOG_WARNING( "Saved gimmick state of '%#' was written for a different node set - matching nodes continue, new nodes start from their initial state", pName );
        commitRestoredState();
    }

    void GimmickCircuitComponent::commitRestoredState()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            applyActuators( *pManager );
        storeState();
    }

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

    void GimmickCircuitComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<GimmickCircuitComponent>( this );
    }

    void GimmickCircuitComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<GimmickCircuitComponent>( this );
        Component::onUnregister( manager );
    }
} // namespace sw
