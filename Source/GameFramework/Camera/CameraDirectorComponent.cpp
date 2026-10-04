#include "pch.h"

#include "GameFramework/Camera/CameraDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Camera/CameraCollisionProbe.h"
#include "GameFramework/Camera/CameraPoseUtil.h"
#include "GameFramework/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "CameraDirectorComponent" );

    /**
     * @brief `-gv_cameraPreset=<id>` — 시작 프리셋을 이것으로 바꿉니다(그 id 가 카탈로그에 있는 디렉터만, 캡처 카메라는 빼고).
     * @details 프리셋마다 스크린샷을 찍으려면(`-gv_screenshot`) 키를 누르지 않고 시점을 골라야 한다.
     */
    SW_TEST_GLOBAL_VARIABLE_STRING( gv_cameraPreset, "", "카메라 디렉터의 시작 프리셋 id (캡처 카메라 제외, 비우면 데이터대로)", SW_KEEP_IN_SHIPPING );

    namespace
    {
        struct CameraDirectorComponentInternal
        {
            /** @brief 오브젝트의 월드 자리와 보는 쪽(요 · 피치)입니다. 씬 컴포넌트가 없으면 false 입니다. */
            [[nodiscard]] static bool readFacing( const GameObject& object, float3& outPosition, float32& outYaw, float32& outPitch )
            {
                const SceneComponent* pScene = object.getComponent<SceneComponent>();
                if ( pScene == nullptr )
                    return false;
                const float4x4 world = pScene->getWorldMatrix();
                outPosition          = world.getTranslation();
                float3 forward       = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
                if ( forward.getLengthSquared() <= MathUtil::Epsilon )
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
        , _cyclePresetKey{ Key::Unknown }
        , _cycleAction{}
        , _bReadInput{ true }
        , _catalog{}
        , _director{}
        , _pExternalProbe{ nullptr }
        , _pendingDeltaTime{ 0.0f }
    {
        setCanEverTick( true );
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
        const hashed_string startId = resolveStartPreset();
        if ( _director.activatePreset( _catalog, startId ) == false )
            SW_LOG_WARNING( "Camera preset '%#' is not in '%#'", startId.c_str(), _catalogPath );
        updateCamera( 0.0f );
    }

    hashed_string CameraDirectorComponent::resolveStartPreset() const
    {
        const GameObject*      pOwner   = getOwner();
        const CameraComponent* pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        const bool             bCapture = pCamera != nullptr && pCamera->getRole() == CameraRole::Capture;
        if ( gv_cameraPreset.empty() == false && bCapture == false )
        {
            const hashed_string overrideId( string_view{ gv_cameraPreset.c_str(), gv_cameraPreset.size() } );
            if ( _catalog.findPreset( overrideId ) != nullptr )
                return overrideId;
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

    bool CameraDirectorComponent::activatePreset( const hashed_string& id, const BlendCurveSpec& blend )
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
            if ( listPreset[index]._id == _director.getActivePresetId() )
            {
                nextIndex = ( index + 1 ) % listPreset.size();
                break;
            }
        }
        const hashed_string nextId = listPreset[nextIndex]._id;
        (void)_director.activatePreset( _catalog, nextId );
        return nextId;
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
        pManager->executeOrDeferPostTick( SW_DELEGATE_METHOD( GameObjectManager::PostTickDelegate, &CameraDirectorComponent::resolveCamera, this ) );
    }

    void CameraDirectorComponent::gatherInput( float32 deltaTime )
    {
        const InputManager* pInput = _bReadInput ? game::getService<InputManager>() : nullptr;
        if ( pInput == nullptr )
            return;
        const bool bCycleKey    = _cyclePresetKey != Key::Unknown && pInput->wasKeyPressed( _cyclePresetKey );
        const bool bCycleAction = _cycleAction.empty() == false && pInput->getInputMap().wasActionTriggered( _cycleAction );
        if ( bCycleKey || bCycleAction )
        {
            const hashed_string nextId = activateNextPreset();
            SW_LOG_INFO( "Camera preset -> '%#'", nextId.c_str() );
        }
        const CameraInputDef& inputDef = _director.getActivePreset()._input;
        CameraModeInput       input;
        if ( inputDef._lookSensitivity > 0.0f )
        {
            const int2 mouseDelta = pInput->getMouseDelta();
            input._lookDelta      = float2{ static_cast<float32>( mouseDelta._x ), static_cast<float32>( mouseDelta._y ) };
            input._bLookHeld      = pInput->isMouseButtonDown( MouseButton::Right ) ? SW_TRUE : SW_FALSE;
        }
        if ( inputDef._zoomStep > 0.0f )
            input._zoomNotches = pInput->getMouseWheel();
        if ( inputDef._panSpeed > 0.0f )
        {
            if ( pInput->isKeyDown( Key::Up ) || pInput->isKeyDown( Key::W ) )
                input._pan._x += 1.0f;
            if ( pInput->isKeyDown( Key::Down ) || pInput->isKeyDown( Key::S ) )
                input._pan._x -= 1.0f;
            if ( pInput->isKeyDown( Key::Right ) || pInput->isKeyDown( Key::D ) )
                input._pan._y += 1.0f;
            if ( pInput->isKeyDown( Key::Left ) || pInput->isKeyDown( Key::A ) )
                input._pan._y -= 1.0f;
        }
        if ( inputDef._rotateStep != 0.0f )
        {
            if ( pInput->wasKeyPressed( Key::E ) )
                ++input._rotateSteps;
            if ( pInput->wasKeyPressed( Key::Q ) )
                --input._rotateSteps;
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
            const SceneCameraProbe physicsProbe( *pManager, pTarget != nullptr ? pTarget->getObjectId() : 0 );
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
                return makeGroupCameraTarget( listPoint.data(), static_cast<uint32>( listPoint.size() ) );
        }
        const GameObject* pObject = pManager->resolveGameObject( _target );
        if ( pObject != nullptr )
            (void)CameraDirectorComponentInternal::readFacing( *pObject, target._focus, target._yaw, target._pitch );
        return target;
    }
} // namespace sw
