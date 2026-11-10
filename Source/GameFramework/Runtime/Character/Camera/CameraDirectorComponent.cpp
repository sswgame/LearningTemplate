#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Camera/CameraCollisionProbe.h"
#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Character/Camera/CameraSystem.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"

namespace sw
{
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
            /** @brief 포토 모드 입력 맵(`engine/input/photomode.input.xml`)의 액션 이름입니다. 맵 파일과 이 표가 계약이다. */
            static constexpr const utf8* kPhotoMoveAction     = "Photo.Move";
            static constexpr const utf8* kPhotoRiseAction     = "Photo.Rise";
            static constexpr const utf8* kPhotoLookAction     = "Photo.Look";
            static constexpr const utf8* kPhotoRollAction     = "Photo.Roll";
            static constexpr const utf8* kPhotoZoomAction     = "Photo.Zoom";
            static constexpr const utf8* kPhotoExposureAction = "Photo.Exposure";

            /** @brief 맵에 포토 모드 액션이 모두 있으면 true 입니다. 없으면 빠진 이름마다 오류를 남긴다(맵에 없는 액션 질의는 로그 없이 0 이다). */
            [[nodiscard]] static bool hasPhotoActions( const InputMap& inputMap, string_view mapPath )
            {
                bool bComplete = true;
                for ( const utf8* pAction : { kPhotoMoveAction, kPhotoRiseAction, kPhotoLookAction, kPhotoRollAction, kPhotoZoomAction, kPhotoExposureAction } )
                {
                    if ( inputMap.getActionHandle( hashed_string( pAction ) ).isValid() )
                        continue;
                    SW_LOG_ERROR_ID( "CameraDirectorComponent.PhotoModeInputMap", "Photo mode input map '%#' has no action '%#'", mapPath, pAction );
                    bComplete = false;
                }
                return bComplete;
            }

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

        /** @brief 켠 프리셋과 블렌드 스택을 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다). */
        const StatefulComponentRegistrar<CameraDirectorComponent> s_stateRegistration{ "CameraDirectorComponent" };
    } // namespace
} // namespace sw

namespace sw
{
    CameraDirectorComponent::CameraDirectorComponent()
        : _catalogPath{}
        , _initialPreset{}
        , _target{}
        , _listGroupTarget{}
        , _focusPoint{ 0.0f, 0.0f, 0.0f }
        , _cycleAction{}
        , _panAction{ CommonInputActions::kCameraPan }
        , _rotateAction{ CommonInputActions::kCameraRotate }
        , _lookAction{ CommonInputActions::kCameraLook }
        , _lookHoldAction{ CommonInputActions::kCameraLookHold }
        , _zoomAction{ CommonInputActions::kCameraZoom }
        , _photoMoveSpeed{ PhotoModeSettings{}._moveSpeed }
        , _photoLookSensitivity{ PhotoModeSettings{}._lookSensitivity }
        , _photoZoomStep{ PhotoModeSettings{}._zoomStep }
        , _photoMaxExposureOffset{ PhotoModeSettings{}._maxExposureOffset }
        , _photoHiddenUILayers{ EnumUtil::setFlag( EnumUtil::setFlag( UILayerMask::HUD, UILayerMask::GameMenu ), UILayerMask::Overlay ) }
        , _bReadInput{ true }
        , _overrideBlend{}
        , _fieldOfViewOverride{ 0.0f }
        , _focusDistanceOverride{ 0.0f }
        , _exposureOffset{ 0.0f }
        , _overrideDef{}
        , _presetBeforeOverride{}
        , _bViewOverride{ false }
        , _photoMode{}
        , _photoInputMap{}
        , _bPhotoModeCut{ false }
        , _photoAppliedHiddenLayers{ UILayerMask::None }
        , _pCatalog{ make_shared<CameraPresetCatalog>() }
        , _director{}
        , _pendingStateBytes{}
        , _pExternalProbe{ nullptr }
        , _inputCheckedPresetID{}
        , _pendingDeltaTime{ 0.0f }
        , _bResolvePending{ false }
    {
        setCanEverTick( true );
    }

    CameraDirectorComponent::~CameraDirectorComponent() = default;

    void CameraDirectorComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<CameraDirectorComponent>( this );
        (void)CameraSystem::ensureFor( manager );
    }

    void CameraDirectorComponent::onUnregister( GameObjectManager& manager )
    {
        // 켠 채 사라지면 정지 요청이 남아 게임이 영영 멈춘다. 내려가는 중이라 카메라에는 쓰지 않고 요청만 푼다.
        if ( _photoMode.isActive() )
        {
            _photoMode.end();
            _photoInputMap.reset();
            game::getSubsystem<GameTimeScale>().removePauseRequest();
            releasePhotoModeUILayers();
        }
        manager.getComponentRegistry().remove<CameraDirectorComponent>( this );
        CameraSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void CameraDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 대상이 앞 그룹에서 움직인 뒤 입력을 읽고, 포즈는 틱 뒤에 쓴다. 뷰 타깃으로 읽는 매니저(PostUpdate)보다 앞 그룹이다.
        setTickGroup( TickGroup::PostPhysics );
        if ( _catalogPath.empty() == false )
        {
            // 프리셋 표는 공유 캐시의 한 벌이다 — 같은 표를 쓰는 디렉터 · 에디터가 같은 객체를 읽고, 다시 읽기가 제자리로 닿는다.
            shared_ptr<const CameraPresetCatalog> pLoaded = SharedAssetCache<CameraPresetCatalog>::acquire( _catalogPath );
            if ( pLoaded == nullptr )
                SW_LOG_WARNING_ID( "CameraDirectorComponent.CameraPresetsDidNot", "Camera presets '%#' did not load", _catalogPath.c_str() );
            else
                _pCatalog = std::move( pLoaded );
        }
        if ( _pCatalog->getAll().empty() )
            return;
        // 저장한 상태(핫 리로드, 세이브)가 있으면 시작 프리셋 대신 그 스택에서 이어 간다.
        if ( applyPendingState() == false )
        {
            const hashed_string startID = resolveStartPreset();
            if ( _director.activatePreset( *_pCatalog, startID ) == false )
                SW_LOG_ERROR_ID( "Camera.UnknownPreset", "Camera preset '%#' is not in '%#'", startID.c_str(), _catalogPath.c_str() );
        }
        updateCamera( 0.0f );
    }

    void CameraDirectorComponent::setViewOverride( const float3& position, const float3& euler, float32 fieldOfViewY, float32 farPlane )
    {
        _overrideDef._id                 = hashed_string( CameraDirectorComponentInternal::kOverridePresetID );
        _overrideDef._view._mode         = CameraPresetMode::Fixed;
        _overrideDef._view._aim          = CameraAimMode::Angles;
        _overrideDef._view._offset       = position;
        _overrideDef._view._pitch        = euler._x;
        _overrideDef._view._yaw          = euler._y;
        _overrideDef._lens._fieldOfViewY = fieldOfViewY;
        _overrideDef._lens._nearPlane    = CameraDirectorComponentInternal::kOverrideNearPlane;
        SW_ASSERT( farPlane >= CameraDirectorComponentInternal::kOverrideNearPlane * 2.0f ); // 호출부 속성(`Far Plane`)의 범위가 지킨다. 올려 맞추지 않는다
        _overrideDef._lens._farPlane      = farPlane;
        _overrideDef._lens._bOrthographic = SW_FALSE;
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
        if ( _director.activatePreset( *_pCatalog, _presetBeforeOverride, _overrideBlend ) == false )
            SW_LOG_WARNING_ID( "CameraDirectorComponent.CameraPresetReturnAfter", "Camera preset '%#' to return to after the view override is gone", _presetBeforeOverride.c_str() );
    }

    bool CameraDirectorComponent::enterPhotoMode()
    {
        if ( _photoMode.isActive() || _director.hasPose() == false )
            return false;
        // 조작 맵은 엔진 데이터다. 읽지 못하면 움직이지 못하는 포토 모드를 켜지 않는다.
        const EngineDefaultAssets* pDefaultAssets = game::findSubsystem<EngineDefaultAssets>();
        if ( pDefaultAssets == nullptr )
        {
            SW_LOG_ERROR_ID( "CameraDirectorComponent.PhotoModeNeedsEngine", "Photo mode needs the engine default assets for its input map - not entered" );
            return false;
        }
        const string_view    mapPath       = pDefaultAssets->_photoModeInputMap.get();
        unique_ptr<InputMap> photoInputMap = make_unique<InputMap>();
        if ( photoInputMap->loadFromAsset( AssetID{ mapPath } ) == false )
        {
            SW_LOG_ERROR_ID( "CameraDirectorComponent.PhotoModeInputMap2", "Photo mode input map '%#' could not be read - not entered", mapPath );
            return false;
        }
        if ( CameraDirectorComponentInternal::hasPhotoActions( *photoInputMap, mapPath ) == false )
        {
            SW_LOG_ERROR_ID( "CameraDirectorComponent.PhotoModeInputMap3", "Photo mode input map '%#' lacks the photo actions listed above - not entered", mapPath );
            return false;
        }
        photoInputMap->setInputManager( game::findSubsystem<InputManager>() );
        _photoInputMap = std::move( photoInputMap );
        _photoMode.begin( _director.getPose() );
        game::getSubsystem<GameTimeScale>().addPauseRequest();
        // UI 는 이 컴포넌트가 모른다 — 숨길 레이어는 속성(데이터)이 정하고, 숨기는 일은 레이어 숨김 창구(`IUILayerVisibility`, 구현은 UI 시스템)가 한다. 창구가 없으면(전용 서버) 가릴 화면도 없다.
        IUILayerVisibility* pLayerVisibility = game::findSubsystem<IUILayerVisibility>();
        if ( pLayerVisibility != nullptr && _photoHiddenUILayers != UILayerMask::None )
        {
            pLayerVisibility->addLayerHideRequest( _photoHiddenUILayers );
            _photoAppliedHiddenLayers = _photoHiddenUILayers;
        }
        _bPhotoModeCut = true;
        updateCamera( 0.0f );
        return true;
    }

    void CameraDirectorComponent::exitPhotoMode()
    {
        if ( _photoMode.isActive() == false )
            return;
        _photoMode.end();
        _photoInputMap.reset();
        game::getSubsystem<GameTimeScale>().removePauseRequest();
        releasePhotoModeUILayers();
        _bPhotoModeCut = true;
        updateCamera( 0.0f );
    }

    void CameraDirectorComponent::releasePhotoModeUILayers()
    {
        if ( _photoAppliedHiddenLayers == UILayerMask::None )
            return;
        IUILayerVisibility* pLayerVisibility = game::findSubsystem<IUILayerVisibility>();
        if ( pLayerVisibility != nullptr )
            pLayerVisibility->removeLayerHideRequest( _photoAppliedHiddenLayers );
        _photoAppliedHiddenLayers = UILayerMask::None;
    }

    void CameraDirectorComponent::setFocusPoint( const float3& point )
    {
        _focusPoint                         = point;
        _director.getModeState()._panOffset = float3{ 0.0f, 0.0f, 0.0f };
    }

    void CameraDirectorComponent::startWithPreset( const hashed_string& id )
    {
        _initialPreset = id;
        if ( hasBegunPlay() == false )
            return;
        BlendCurveDef cut;
        cut._curve    = BlendCurve::Cut;
        cut._duration = 0.0f;
        if ( _director.activatePreset( *_pCatalog, id, cut ) == false )
            SW_LOG_WARNING_ID( "CameraDirectorComponent.CameraPresetNot", "Camera preset '%#' is not in '%#'", id.c_str(), _catalogPath.c_str() );
    }

    bool CameraDirectorComponent::findGroundPoint( const float2& mouseNormalized, float32 groundHeight, float3& outPoint ) const
    {
        if ( _director.hasPose() == false )
            return false;
        const GameObject*      pOwner      = getOwner();
        const CameraComponent* pCamera     = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        const float32          aspectRatio = pCamera != nullptr ? CameraPose::computeAspectRatio( *pCamera ) : CameraTarget::kDefaultAspectRatio;
        const CameraPose&      pose        = _director.getPose();
        const Ray              ray         = computeCameraScreenRay( pose, aspectRatio, mouseNormalized );
        float32                distance    = 0.0f;
        if ( Ray::intersectHorizontalPlane( ray, groundHeight, pose._farPlane * CameraDirectorComponentInternal::kGroundRayFarPlaneScale, distance ) == false )
            return false;
        outPoint = ray._origin + ray._direction * distance;
        return true;
    }

    void CameraDirectorComponent::serializeState( StateIO& io )
    {
        if ( io.isReading() && io.check( _pCatalog != nullptr ) == false )
            return; // 카탈로그 전에는 프리셋을 찾을 수 없다 — 부르는 쪽(`applyPendingState`)은 시작 뒤에만 읽는다
        _director.serializeState( io, io.isReading() ? _pCatalog.get() : nullptr );
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
        const bool bRead = StateIO::readInPlace( *this, archive ); // 디렉터 본문이 지역 변수에 읽어 끝까지 맞으면 바꾼다
        _pendingStateBytes.clear();
        if ( bRead == false )
            SW_LOG_WARNING_ID( "CameraDirectorComponent.CameraDirectorStateDid", "Camera director state of '%#' did not read - starting fresh", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
        return bRead;
    }

    hashed_string CameraDirectorComponent::resolveStartPreset() const
    {
        return _initialPreset.empty() ? _pCatalog->getAll().front()._id : _initialPreset;
    }

    void CameraDirectorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        updateCamera( deltaTime );
    }

    bool CameraDirectorComponent::activatePreset( const hashed_string& id )
    {
        return _director.activatePreset( *_pCatalog, id );
    }

    bool CameraDirectorComponent::activatePreset( const hashed_string& id, const BlendCurveDef& blend )
    {
        return _director.activatePreset( *_pCatalog, id, blend );
    }

    hashed_string CameraDirectorComponent::activateNextPreset()
    {
        const vector<CameraPresetDef>& listPreset = _pCatalog->getAll();
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
        (void)_director.activatePreset( *_pCatalog, nextID );
        return nextID;
    }

    void CameraDirectorComponent::updateCamera( float32 deltaTime )
    {
        if ( _director.hasActivePreset() == false )
            return;
        // 포토 모드는 게임이 멈춰도 움직이므로 실제 경과로 돌고, 게임 카메라에는 입력을 주지 않는다.
        if ( _photoMode.isActive() )
            gatherPhotoModeInput( game::getSubsystem<GameTimeScale>().getUnscaledDeltaTime() );
        else
            gatherInput( deltaTime );
        // 틱 중이면 카메라 시스템이 늦은 틱 뒤에 푼다 — 대상의 이번 프레임 자리는 틱이 끝나야 적용된다. 여러 번 불려도 시간은 모아 한 번 쓴다.
        _pendingDeltaTime += MathUtil::max( 0.0f, deltaTime );
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pManager->isStructuralMutationFrozen() )
        {
            _bResolvePending = true;
            return;
        }
        resolveCamera();
    }

    void CameraDirectorComponent::resolvePendingCamera()
    {
        if ( _bResolvePending )
            resolveCamera();
    }

    void CameraDirectorComponent::gatherInput( float32 deltaTime )
    {
        const InputManager* pInput = _bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput == nullptr )
            return;
        const GameInputView inputMap( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() ); // 메뉴가 먹은 입력과 모달 아래의 입력은 카메라가 보지 않는다
        if ( _cycleAction.empty() == false && inputMap.wasActionTriggered( _cycleAction ) )
        {
            [[maybe_unused]] const hashed_string nextID = activateNextPreset(); // 로그는 Shipping 에서 빠진다
            SW_LOG_INFO_ID( "CameraDirectorComponent.CameraPreset", "Camera preset -> '%#'", nextID.c_str() );
        }
        const CameraInputDef& inputDef = _director.getActivePreset()._input;
        if ( _inputCheckedPresetID.isEqual( _director.getActivePresetID(), NameCase::CaseSensitive ) == false )
            warnMissingPresetActions( pInput->getInputMap(), inputDef );
        CameraModeInput input;
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

    void CameraDirectorComponent::warnMissingPresetActions( const InputMap& inputMap, const CameraInputDef& inputDef )
    {
        _inputCheckedPresetID                  = _director.getActivePresetID();
        const hashed_string* arrNeededAction[] = {
            inputDef._lookSensitivity > 0.0f ? &_lookAction : nullptr,
            inputDef._lookSensitivity > 0.0f && inputDef._bLookWhileHeld ? &_lookHoldAction : nullptr,
            inputDef._zoomStep > 0.0f ? &_zoomAction : nullptr,
            inputDef._panSpeed > 0.0f ? &_panAction : nullptr,
            inputDef._rotateStep != 0.0f ? &_rotateAction : nullptr,
        };
        for ( const hashed_string* pAction : arrNeededAction )
        {
            const bool bMissing = pAction != nullptr && pAction->empty() == false && inputMap.getActionHandle( *pAction ).isValid() == false;
            if ( bMissing )
                SW_LOG_WARNING_ID( "Camera.PresetActionNotInInputMap", "Camera preset '%#' reads action '%#', which the InputMap does not have - that control does nothing", _inputCheckedPresetID.c_str(),
                                   pAction->c_str() );
        }
    }

    void CameraDirectorComponent::gatherPhotoModeInput( float32 unscaledDeltaTime )
    {
        using Internal      = CameraDirectorComponentInternal;
        const bool bNoInput = _bReadInput == false || _photoInputMap == nullptr;
        if ( bNoInput || game::findSubsystem<InputManager>() == nullptr )
            return;
        _photoInputMap->update( unscaledDeltaTime );
        const GameInputView inputMap( *_photoInputMap, game::findSubsystem<IInputCaptureQuery>() ); // 메뉴가 먹은 입력과 모달 아래의 입력은 보지 않는다
        const hashed_string zoomAction( Internal::kPhotoZoomAction );
        const hashed_string exposureAction( Internal::kPhotoExposureAction );
        PhotoModeInput      input;
        input._move = inputMap.getVector2D( hashed_string( Internal::kPhotoMoveAction ) );
        input._rise = inputMap.getAxis1D( hashed_string( Internal::kPhotoRiseAction ) );
        input._look = inputMap.getVector2D( hashed_string( Internal::kPhotoLookAction ) );
        input._roll = inputMap.getAxis1D( hashed_string( Internal::kPhotoRollAction ) );
        if ( inputMap.wasActionTriggered( zoomAction ) ) // 휠은 굴린 프레임, 패드 버튼 축은 누름 · 반복마다 한 칸
            input._zoomNotches = inputMap.getAxis1D( zoomAction );
        if ( inputMap.wasActionTriggered( exposureAction ) )
        {
            const float32 exposureInput = inputMap.getAxis1D( exposureAction );
            input._exposureSteps        = exposureInput > 0.0f ? 1.0f : ( exposureInput < 0.0f ? -1.0f : 0.0f );
        }
        // 조절 값은 PROPERTY 가 기준이다 — 에디터에서 바꾼 값이 다음 프레임부터 바로 듣도록 매 프레임 옮긴다.
        PhotoModeSettings settings  = _photoMode.getSettings();
        settings._moveSpeed         = _photoMoveSpeed;
        settings._lookSensitivity   = _photoLookSensitivity;
        settings._zoomStep          = _photoZoomStep;
        settings._maxExposureOffset = _photoMaxExposureOffset;
        _photoMode.setSettings( settings );
        _photoMode.applyInput( input, unscaledDeltaTime );
    }

    void CameraDirectorComponent::resolveCamera()
    {
        const float32 deltaTime     = _pendingDeltaTime;
        _pendingDeltaTime           = 0.0f;
        _bResolvePending            = false;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        CameraComponent*   pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;

        // 암 충돌: 따로 준 질의가 없으면 씬의 물리(3D · 2D, 2D 콜라이더 포함)에 쓸어 본다(대상 자신의 바디는 모두 뺀다 — 피벗이 그 안에 있다).
        const CameraTarget target = computeTarget();
        _director.setUserSettings( CameraUserSettings::makeFromGlobalVariables() );
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
        // 시퀀서 값 트랙(프로퍼티 트랙 `CameraDirectorComponent._fieldOfViewOverride` 등)이 키로 모는 값을 프리셋 포즈 위에 적용한다.
        // 시퀀서는 PostUpdate 틱에서 쓰고 이 계산은 틱 뒤라 같은 프레임에 보인다.
        CameraPose pose = _director.getPose();
        if ( _fieldOfViewOverride > 0.0f )
            pose._fieldOfViewY = _fieldOfViewOverride;
        if ( _focusDistanceOverride > 0.0f )
            pose._depthOfField._focusDistance = _focusDistanceOverride;
        pose._postProcess._exposureCompensation += _exposureOffset;
        // 포토 모드는 마지막에 덮는다 — 디렉터 상태는 그대로라서 끄면 이 줄만 빠진다.
        pose = _photoMode.makePose( pose );
        if ( pCamera == nullptr )
            return;
        CameraPose::applyToCamera( *pCamera, pose );
        const bool bDirectorCut = _director.consumeCut();
        if ( bDirectorCut || _bPhotoModeCut )
            pCamera->markCut();
        _bPhotoModeCut = false;
    }

    CameraTarget CameraDirectorComponent::computeTarget() const
    {
        CameraTarget target;
        target._focus                     = _focusPoint; // 대상 오브젝트가 없을 때의 초점(직교 시점 게임)
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const CameraComponent*   pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera != nullptr )
            target._aspectRatio = CameraPose::computeAspectRatio( *pCamera );
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

namespace sw
{
    namespace
    {
        /**
         * @brief 개발 명령 `camera.preset <id>` — 활성 씬의 카메라 디렉터(캡처 카메라 제외) 중 그 프리셋이 카탈로그에 있는 것을 그 프리셋으로 자릅니다(`startWithPreset`).
         * @details 프리셋마다 스크린샷을 찍으려면(시나리오 골든) 키를 누르지 않고 시점을 골라야 한다. 예전에는 테스트용 변수(`-gv_cameraPreset`)를 시작 때 컴포넌트가 읽었다 —
         *          묻는 쪽이 부르는 명령으로 바꿔 런타임 시작 경로에서 개발 분기를 뺐다(배포본에는 등록되지 않는다). 받은 디렉터가 하나도 없으면 오류다 — 틀린 화면으로 골든을 뜨지 않는다.
         */
        struct CameraPresetDevCommandInternal
        {
            static bool runPreset( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                Scene* const        pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                GameObjectManager*  pManager      = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "camera.preset: no active scene";
                    return true;
                }
                const hashed_string id( listArgument[0] );
                uint32              applied = 0;
                for ( CameraDirectorComponent* pDirector : pManager->getComponentRegistry().getAll<CameraDirectorComponent>() )
                {
                    const GameObject*      pOwner   = pDirector->getOwner();
                    const CameraComponent* pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
                    const bool             bCapture = pCamera != nullptr && pCamera->getRole() == CameraRole::Capture;
                    if ( bCapture || pDirector->getCatalog().find( id ) == nullptr )
                        continue;
                    pDirector->startWithPreset( id );
                    ++applied;
                }
                if ( applied == 0 )
                    SW_LOG_ERROR_ID( "Camera.UnknownPreset", "camera.preset '%#': no camera director in the active scene has that preset", id.c_str() );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "camera.preset '%#': %# director(s)", id.c_str(), applied );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( CameraPreset, "camera.preset", "camera.preset <id>", "Cut every camera director of the active scene (capture cameras excluded) that has this preset to it",
                    &CameraPresetDevCommandInternal::runPreset );
} // namespace sw
