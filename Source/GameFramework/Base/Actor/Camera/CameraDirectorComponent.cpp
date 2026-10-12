#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Camera/CameraCollisionProbe.h"
#include "GameFramework/Base/Actor/Camera/CameraPoseUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"

namespace sw
{
    SW_LOG_CALLER( "CameraDirectorComponent" );

    /**
     * @brief `-gv_cameraPreset=<id>` — 시작 프리셋을 이것으로 바꿉니다(그 id 가 카탈로그에 있는 디렉터만, 캡처 카메라는 빼고).
     * @details 프리셋마다 스크린샷을 찍으려면(`-gv_screenshot`) 키를 누르지 않고 시점을 골라야 한다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( sw::string, gv_cameraPreset, "", "카메라 디렉터의 시작 프리셋 id (캡처 카메라 제외, 비우면 데이터대로)" );

    namespace
    {
        struct CameraDirectorComponentInternal
        {
            /** @brief 덮어쓴 시점 프리셋의 id 입니다. 카탈로그에 없으므로 저장하면 고정 포즈로 돌아온다. */
            static constexpr const utf8* kOverridePresetID = "__viewOverride";
            /** @brief 덮어쓴 원근 시점의 가까운 면(m)입니다. 탑승 카메라가 바로 앞 레일을 자르지 않게 짧게 둔다. */
            static constexpr float32 kOverrideNearPlane = 0.1f;
            /** @brief 땅 고르기 광선의 길이를 먼 면의 몇 배까지 볼지입니다. 직교 시점은 카메라가 땅에서 멀리 떨어져 있어 먼 면보다 길게 본다. */
            static constexpr float32 kGroundRayFarPlaneScale = 3.0f;

            /** @brief 오브젝트의 월드 자리와 보는 쪽(요 · 피치)입니다. 씬 컴포넌트가 없으면 false 입니다. */
            [[nodiscard]] static bool readFacing( const GameObject& object, float3& outPosition, float32& outYaw, float32& outPitch )
            {
                const SceneComponent* pScene = object.getComponent<SceneComponent>();
                if ( pScene == nullptr )
                    return false;
                const float4x4 world = pScene->getWorldMatrix();
                outPosition          = world.getTranslation();
                float3 forward       = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
                if ( forward.getLengthSquared() <= MathUtil::kEpsilon )
                    return true;
                forward.normalize();
                outYaw   = MathUtil::atan2( forward._x, forward._z );
                outPitch = -MathUtil::asin( MathUtil::clamp( forward._y, -1.0f, 1.0f ) );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CameraDirectorComponent::CameraDirectorComponent()
        : _catalogPath{}
        , _initialPreset{}
        , _target{}
        , _listGroupTarget{}
        , _cycleAction{}
        , _panAction{ "Camera.Pan" }
        , _rotateAction{ "Camera.Rotate" }
        , _lookAction{ "Camera.Look" }
        , _lookHoldAction{ "Camera.LookHold" }
        , _zoomAction{ "Camera.Zoom" }
        , _bReadInput{ true }
        , _overrideBlend{}
        , _overrideDef{}
        , _presetBeforeOverride{}
        , _bViewOverride{ false }
        , _catalog{}
        , _director{}
        , _pendingStateBytes{}
        , _pExternalProbe{ nullptr }
        , _pendingDeltaTime{ 0.0f }
    {
        setCanEverTick( true );
    }

    void CameraDirectorComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<CameraDirectorComponent>( this );
    }

    void CameraDirectorComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<CameraDirectorComponent>( this );
        Component::onUnregister( manager );
    }

    void CameraDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 대상이 앞 그룹에서 움직인 뒤 입력을 읽고, 포즈는 틱 뒤에 쓴다. 뷰 타깃으로 읽는 매니저(PostUpdate)보다 앞 그룹이다.
        setTickGroup( TickGroup::PostPhysics );
        if ( _catalogPath.empty() == false )
        {
            _catalog.clear();
            if ( _catalog.loadFromResource( _catalogPath ) == false )
                SW_LOG_WARNING( "Camera presets '%#' did not load", _catalogPath );
        }
        if ( _catalog.getPresets().empty() )
            return;
        // 저장한 상태(핫 리로드, 세이브)가 있으면 시작 프리셋 대신 그 스택에서 이어 간다.
        if ( applyPendingState() == false )
        {
            const hashed_string startID = resolveStartPreset();
            if ( _director.activatePreset( _catalog, startID ) == false )
                SW_LOG_WARNING( "Camera preset '%#' is not in '%#'", startID.c_str(), _catalogPath );
        }
        updateCamera( 0.0f );
    }

    void CameraDirectorComponent::setViewOverride( const float3& position, const float3& euler, float32 fieldOfViewY, float32 farPlane )
    {
        _overrideDef._id                  = hashed_string( CameraDirectorComponentInternal::kOverridePresetID );
        _overrideDef._view._mode          = CameraPresetMode::Fixed;
        _overrideDef._view._aim           = CameraAimMode::Angles;
        _overrideDef._view._offset        = position;
        _overrideDef._view._pitch         = euler._x;
        _overrideDef._view._yaw           = euler._y;
        _overrideDef._lens._fieldOfViewY  = fieldOfViewY;
        _overrideDef._lens._nearPlane     = CameraDirectorComponentInternal::kOverrideNearPlane;
        _overrideDef._lens._farPlane      = MathUtil::max( farPlane, CameraDirectorComponentInternal::kOverrideNearPlane * 2.0f );
        _overrideDef._lens._bOrthographic = false;
        // 덮어쓰는 동안에는 값만 바꾼다 — 매 프레임 블렌드를 새로 시작하면 영영 도착하지 못한다.
        if ( _bViewOverride )
        {
            _director.refreshActivePreset( _overrideDef );
            return;
        }
        _presetBeforeOverride = _director.getActivePresetID();
        _director.activatePreset( _overrideDef, _overrideBlend );
        _bViewOverride = true;
    }

    void CameraDirectorComponent::clearViewOverride()
    {
        if ( _bViewOverride == false )
            return;
        _bViewOverride = false;
        if ( _director.activatePreset( _catalog, _presetBeforeOverride, _overrideBlend ) == false )
            SW_LOG_WARNING( "Camera preset '%#' to return to after the view override is gone", _presetBeforeOverride.c_str() );
    }

    bool CameraDirectorComponent::findGroundPoint( const float2& mouseNormalized, float32 groundHeight, float3& outPoint ) const
    {
        if ( _director.hasPose() == false )
            return false;
        const GameObject*      pOwner      = getOwner();
        const CameraComponent* pCamera     = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        const float32          aspectRatio = pCamera != nullptr ? CameraPoseUtil::computeAspectRatio( *pCamera ) : CameraTarget::kDefaultAspectRatio;
        const CameraPose&      pose        = _director.getPose();
        const GameRay          ray         = computeCameraScreenRay( pose, aspectRatio, mouseNormalized );
        float32                distance    = 0.0f;
        if ( RayMath::intersectHorizontalPlane( ray, groundHeight, pose._farPlane * CameraDirectorComponentInternal::kGroundRayFarPlaneScale, distance ) == false )
            return false;
        outPoint = ray._origin + ray._direction * distance;
        return true;
    }

    void CameraDirectorComponent::writeState( Archive& outArchive ) const
    {
        _director.writeState( outArchive );
    }

    void CameraDirectorComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( hasBegunPlay() && applyPendingState() )
            updateCamera( 0.0f );
    }

    bool CameraDirectorComponent::applyPendingState()
    {
        if ( _pendingStateBytes.empty() )
            return false;
        Archive    archive( _pendingStateBytes.data(), static_cast<uint64>( _pendingStateBytes.size() ) );
        const bool bRead = _director.readState( archive, _catalog );
        _pendingStateBytes.clear();
        if ( bRead == false )
            SW_LOG_WARNING( "Camera director state of '%#' did not read - starting fresh", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
        return bRead;
    }

    hashed_string CameraDirectorComponent::resolveStartPreset() const
    {
        const GameObject*      pOwner   = getOwner();
        const CameraComponent* pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        const bool             bCapture = pCamera != nullptr && pCamera->getRole() == CameraRole::Capture;
        if ( gv_cameraPreset.empty() == false && bCapture == false )
        {
            const hashed_string overrideID( string_view{ gv_cameraPreset.c_str(), gv_cameraPreset.size() } );
            if ( _catalog.findPreset( overrideID ) != nullptr )
                return overrideID;
        }
        return _initialPreset.empty() ? _catalog.getPresets().front()._id : _initialPreset;
    }

    void CameraDirectorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        updateCamera( deltaTime );
    }

    bool CameraDirectorComponent::activatePreset( const hashed_string& id )
    {
        return _director.activatePreset( _catalog, id );
    }

    bool CameraDirectorComponent::activatePreset( const hashed_string& id, const BlendCurveDef& blend )
    {
        return _director.activatePreset( _catalog, id, blend );
    }

    hashed_string CameraDirectorComponent::activateNextPreset()
    {
        const vector<CameraPresetDef>& listPreset = _catalog.getPresets();
        if ( listPreset.empty() )
            return hashed_string{};
        size_t nextIndex = 0;
        for ( size_t index = 0; index < listPreset.size(); ++index )
        {
            if ( listPreset[index]._id == _director.getActivePresetID() )
            {
                nextIndex = ( index + 1 ) % listPreset.size();
                break;
            }
        }
        const hashed_string nextID = listPreset[nextIndex]._id;
        (void)_director.activatePreset( _catalog, nextID );
        return nextID;
    }

    void CameraDirectorComponent::updateCamera( float32 deltaTime )
    {
        if ( _director.hasActivePreset() == false )
            return;
        gatherInput( deltaTime );
        // 틱 중이면 틱 뒤로 미룬다 — 대상의 이번 프레임 자리는 틱이 끝나야 적용된다. 여러 번 불려도 시간은 모아 한 번 쓴다.
        _pendingDeltaTime += MathUtil::max( 0.0f, deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
        {
            resolveCamera();
            return;
        }
        pManager->executeOrDeferPostTickOnComponent<CameraDirectorComponent>( getHandle(), []( CameraDirectorComponent& component )
        { component.resolveCamera(); } );
    }

    void CameraDirectorComponent::gatherInput( float32 deltaTime )
    {
        const InputManager* pInput = _bReadInput ? game::getService<InputManager>() : nullptr;
        if ( pInput == nullptr )
            return;
        const InputMap& inputMap = pInput->getInputMap();
        if ( _cycleAction.empty() == false && inputMap.wasActionTriggered( _cycleAction ) )
        {
            [[maybe_unused]] const hashed_string nextID = activateNextPreset(); // 로그는 Shipping 에서 빠진다
            SW_LOG_INFO( "Camera preset -> '%#'", nextID.c_str() );
        }
        const CameraInputDef& inputDef = _director.getActivePreset()._input;
        CameraModeInput       input;
        // 장치는 입력 맵이 정한다(게임의 `Camera.Look` · `Camera.LookHold` · `Camera.Zoom` — 맵에 없으면 그 조작은 0).
        if ( inputDef._lookSensitivity > 0.0f )
        {
            input._lookDelta = inputMap.getVector2D( _lookAction );
            input._bLookHeld = inputMap.isActionDown( _lookHoldAction ) ? SW_TRUE : SW_FALSE;
        }
        if ( inputDef._zoomStep > 0.0f && inputMap.wasActionTriggered( _zoomAction ) ) // 휠은 굴린 프레임, 패드 버튼 축은 누름 · 반복마다 한 칸
            input._zoomNotches = inputMap.getAxis1D( _zoomAction );
        if ( inputDef._panSpeed > 0.0f )
        {
            // 모드 입력의 팬은 (앞, 오른쪽) — 액션의 (x 오른쪽, y 앞)을 바꿔 넣는다.
            const float2 panInput = inputMap.getVector2D( _panAction );
            input._pan            = float2{ panInput._y, panInput._x };
        }
        if ( inputDef._rotateStep != 0.0f && inputMap.wasActionTriggered( _rotateAction ) )
        {
            const float32 rotateInput = inputMap.getAxis1D( _rotateAction );
            input._rotateSteps += rotateInput > 0.0f ? 1 : ( rotateInput < 0.0f ? -1 : 0 );
        }
        _director.applyInput( input, deltaTime );
    }

    void CameraDirectorComponent::resolveCamera()
    {
        const float32 deltaTime     = _pendingDeltaTime;
        _pendingDeltaTime           = 0.0f;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        CameraComponent*   pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;

        // 암 충돌: 따로 준 질의가 없으면 씬의 강체 물리 · 겹침 월드에 쓸어 본다(대상 자신의 바디는 모두 뺀다 — 피벗이 그 안에 있다).
        const CameraTarget target = computeTarget();
        if ( _pExternalProbe == nullptr && pManager != nullptr )
        {
            const GameObject*      pTarget = pManager->resolveGameObject( _target );
            const SceneCameraProbe physicsProbe( *pManager, pTarget != nullptr ? pTarget->getObjectID() : 0 );
            _director.setCollisionProbe( &physicsProbe );
            (void)_director.step( deltaTime, target );
        }
        else
        {
            _director.setCollisionProbe( _pExternalProbe );
            (void)_director.step( deltaTime, target );
        }
        _director.setCollisionProbe( nullptr );
        const CameraPose& pose = _director.getPose();
        if ( pCamera == nullptr )
            return;
        CameraPoseUtil::applyToCamera( *pCamera, pose );
        if ( _director.consumeCut() )
            pCamera->markCut();
    }

    CameraTarget CameraDirectorComponent::computeTarget() const
    {
        CameraTarget             target;
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const CameraComponent*   pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera != nullptr )
            target._aspectRatio = CameraPoseUtil::computeAspectRatio( *pCamera );
        if ( pManager == nullptr )
            return target;
        if ( _listGroupTarget.empty() == false )
        {
            vector<float3> listPoint;
            listPoint.reserve( _listGroupTarget.size() );
            for ( const GameObjectHandle& handle : _listGroupTarget )
            {
                const GameObject* pObject = pManager->resolveGameObject( handle );
                float3            position{};
                float32           yaw   = 0.0f;
                float32           pitch = 0.0f;
                if ( pObject != nullptr && CameraDirectorComponentInternal::readFacing( *pObject, position, yaw, pitch ) )
                    listPoint.push_back( position );
            }
            if ( listPoint.empty() == false )
            {
                target = makeGroupCameraTarget( listPoint.data(), static_cast<uint32>( listPoint.size() ) );
                return target;
            }
        }
        const GameObject* pObject = pManager->resolveGameObject( _target );
        if ( pObject != nullptr )
            // 씬 컴포넌트가 없으면 target 의 기본 값을 그대로 쓴다
            (void)CameraDirectorComponentInternal::readFacing( *pObject, target._focus, target._yaw, target._pitch );
        return target;
    }
} // namespace sw
