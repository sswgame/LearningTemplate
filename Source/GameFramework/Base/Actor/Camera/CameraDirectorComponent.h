/**
 * @file CameraDirectorComponent.h
 * @brief 같은 오브젝트의 `CameraComponent` 를 데이터 프리셋과 블렌드로 움직이는 컴포넌트를 정의합니다.
 * @details 프리셋은 모드, 입력, 제약, 프레이밍, 충돌, 흔들림을 담는다.
 *          Cinemachine 의 가상 카메라와 Brain 의 블렌드, 언리얼의 카메라 모드에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Camera/CameraBlend.h"
#include "GameFramework/Base/Actor/Camera/CameraDirector.h"
#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/Base/Actor/Camera/CameraPreset.h"
#include "GameFramework/Base/Actor/Camera/PhotoModeCamera.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class ICameraCollisionProbe;
    class InputMap;

    /**
     * @class CameraDirectorComponent
     * @brief 프리셋 카탈로그(`<CameraPresets>` XML)를 읽고, 켠 프리셋을 대상에 맞춰 계산하고 블렌드, 감쇠, 흔들림을 거쳐 같은 오브젝트의 카메라에 씁니다.
     * @details **카메라에는 틱 뒤에 쓴다.** `TickGroup::PostPhysics` 에서는 입력만 읽고, 포즈 계산과 쓰기는 틱 뒤 게임 스레드로 미룬다(`executeOrDeferPostTick`).
     *          틱 중의 트랜스폼 쓰기는 틱이 끝나야 적용되므로, 틱 안에서 대상을 읽으면 한 프레임 늦은 위치를 따라간다.
     *          1인칭 프리셋이면 손에 든 총이 시점보다 한 프레임 앞서 흔들린다.
     *          틱 밖에서 부르면 바로 쓴다.
     *          이 그룹은 PostUpdate 보다 앞이라서, 이 카메라를 뷰 타깃으로 읽는 카메라 매니저(PostUpdate 에서 미룸)보다 먼저 돈다.
     *
     *          대상은 오브젝트 하나(`_target`)이거나 그룹(`_listGroupTarget`, 프레이밍의 그룹 맞추기)이다. 초점은 월드 위치이고, 요와 피치는 그 오브젝트가 보는 쪽이다.
     *          입력(`<Input>` 이 있는 프리셋)은 `InputManager` 에서 읽는다.
     *          암 충돌(`<Collision>`)은 따로 준 질의(`setCollisionProbe`)가 없으면 씬의 물리(`SceneCameraProbe`)에 쓸어 본다. 대상 자신은 뺀다.
     *          입력 맵 액션 `_cycleAction` 이 발동하면 카탈로그 순서로 다음 프리셋을 켠다.
     *          블렌드 없는 전환(컷)은 카메라에 컷 표시를 넣어서 렌더러가 TAA 기록을 버리게 한다.
     *          켠 프리셋과 블렌드 스택(진행, 입력이 돌린 각과 줌)은 `ComponentStateStore` 계약(`writeState` / `restoreState`)으로 저장한다.
     *          이 타입은 정의한 .cpp 가 `StatefulComponentRegistrar` 로 등록해 모든 게임의 스냅샷에 실린다. 게임 인스턴스가 따로 등록하지 않는다.
     */
    REFLECT( Category = "Camera", DisplayName = "Camera Director", Tooltip = "Drives the camera on this object from data presets with blends, damping, framing and shake" )
    class SW_GF_API CameraDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        CameraDirectorComponent();
        virtual ~CameraDirectorComponent() override;

        /** @brief 씬의 카메라 디렉터 목록(`ComponentRegistry`)에 등록합니다. "이 오브젝트를 따라가는 디렉터"를 씬 전체를 훑지 않고 찾을 수 있습니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 프리셋을 카탈로그의 블렌드로 켭니다. 없는 id 면 false 입니다. */
        [[nodiscard]] bool activatePreset( const hashed_string& id );
        /** @brief 프리셋을 @p blend 로 켭니다. 블렌드 표는 무시합니다. */
        [[nodiscard]] bool activatePreset( const hashed_string& id, const BlendCurveDef& blend );
        /** @brief 카탈로그 순서로 다음 프리셋을 켭니다. 마지막 다음은 처음이며, 켠 id 를 반환합니다. 프리셋이 없으면 빈 id 입니다. */
        hashed_string activateNextPreset();
        /** @brief 화면이 지금 @p from 이라고 보고 켠 프리셋으로 @p blend 를 따라 섞습니다(`CameraDirector::blendFromPose`). 시네마틱에서 게임 카메라로 돌아올 때 씁니다. */
        void blendFromPose( const CameraPose& from, const BlendCurveDef& blend ) { _director.blendFromPose( from, blend ); }

        /**
         * @brief 다음 갱신부터 이 원근 시점(`Fixed`)을 씁니다. @p euler 는 피치, 요, 롤(라디안)이며 롤은 쓰지 않습니다.
         * @details 처음 덮어쓸 때 `_overrideBlend` 로 섞어 들어가고, 덮어쓰는 동안 다시 부르면 값만 바꾼다(탑승 시점은 매 프레임 움직인다).
         *          게임 디렉터가 코스터 탑승이나 짧은 컷에 쓴다. 언리얼의 `SetViewTargetWithBlend` 로 임시 시점을 넣는 것과 같다.
         */
        void setViewOverride( const float3& position, const float3& euler, float32 fieldOfViewY, float32 farPlane );
        /** @brief 덮어쓴 시점을 풀고, 덮어쓰기 전에 켜 있던 프리셋으로 `_overrideBlend` 를 따라 돌아갑니다. */
        void clearViewOverride();
        bool isViewOverridden() const { return _bViewOverride; }
        /**
         * @brief 포토 모드를 켭니다. 게임 시간을 멈추고(`GameTimeScale::addPauseRequest`), 지금 포즈에서 시작하는 자유 카메라를 디렉터 포즈 위에 덮습니다.
         * @details 디렉터 상태(프리셋, 블렌드, 덮어쓴 시점)는 건드리지 않고 마지막 쓰기만 바꾼다. 그래서 끌 때 되돌릴 것은 정지 요청 하나뿐이다.
         *          `setViewOverride` 는 탑승 카메라가 매 프레임 부르는 원근 전용 함수라서 쓰지 않는다. 겹치면 끌 때 탑승 시점을 지운다.
         *          조작은 게임 입력 맵이 아니라 엔진의 포토 모드 입력 맵(`EngineDefaultAssets::_photoModeInputMap`)에서 읽는다. 그래서 게임마다 맵에 액션을 더하지 않아도 되고,
         *          게임 키와 겹쳐도 게임이 멈춰 있어 부딪히지 않는다(언리얼 Enhanced Input 이 포토 모드 동안 전용 매핑 컨텍스트를 추가하는 것과 같다).
         *          아직 포즈가 없거나(프리셋 없음) 이미 켜 있거나 그 맵을 읽지 못하면(오류 로그) false 입니다.
         */
        [[nodiscard]] bool enterPhotoMode();
        /** @brief 포토 모드를 끕니다. 정지 요청을 풀고 디렉터 포즈로 컷합니다. 꺼 있으면 아무 일도 하지 않습니다. */
        void exitPhotoMode();
        bool isInPhotoMode() const { return _photoMode.isActive(); }
        /** @brief 포토 모드 자유 카메라입니다. 조절 값을 바꾸거나 테스트가 읽습니다. */
        PhotoModeCamera&       getPhotoMode() { return _photoMode; }
        const PhotoModeCamera& getPhotoMode() const { return _photoMode; }
        /** @brief 덮어쓰기에 들어가고 나오는 블렌드입니다. */
        void setOverrideBlend( const BlendCurveDef& blend ) { _overrideBlend = blend; }
        /**
         * @brief 화면 점 아래의 땅(y 가 @p groundHeight) 점을 구합니다. 지금 낸 포즈(블렌드 중이면 섞인 포즈)와 카메라 화면 비율로 광선을 쏩니다.
         * @details 디렉터는 PostPhysics 틱 뒤에 포즈를 쓰므로, 앞 그룹(PrePhysics)에서 부르면 지난 프레임의 시점이다. 땅과 만나지 않으면 false 입니다.
         */
        [[nodiscard]] bool findGroundPoint( const float2& mouseNormalized, float32 groundHeight, float3& outPoint ) const;

        /** @brief `ComponentStateStore` 계약입니다. 켠 프리셋과 블렌드 스택을 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `ComponentStateStore` 계약입니다. 시작 전이면 보관했다가 카탈로그를 읽은 뒤 적용하고, 시작 뒤면 바로 적용합니다. 읽지 못하면 알리고 지금 상태를 둡니다. */
        void restoreState( vector<uint8>&& bytes );

        /** @brief 카메라가 따라갈 오브젝트입니다. 비우면 초점은 `_focusPoint` 이고 요와 피치는 0 입니다. */
        void                    setTarget( const GameObjectHandle& target ) { _target = target; }
        const GameObjectHandle& getTarget() const { return _target; }
        /**
         * @brief 대상 오브젝트가 없을 때의 초점(월드)을 정하고, 이동 키가 옮긴 팬을 비웁니다. 그러면 화면 가운데가 @p point 가 됩니다.
         * @details 직교 시점 게임의 디렉터가 앞 그룹(PrePhysics)에서 부른다(선택한 유닛으로 가기, 농부 따라가기). 디렉터는 PostPhysics 틱 뒤에 읽는다.
         */
        void          setFocusPoint( const float3& point );
        const float3& getFocusPoint() const { return _focusPoint; }
        /** @brief 초점을 미는 입력 맵 액션(2D 벡터)을 바꿉니다. 같은 게임에서 모드마다 다른 키로 밀 때 씁니다(관전은 WASD 도 받는다). */
        void setPanAction( const hashed_string& action ) { _panAction = action; }
        /**
         * @brief 시작 프리셋을 @p id 로 정합니다. 이미 시작했으면 그 프리셋으로 끊어 바꿉니다(컷).
         * @details 게임 디렉터가 `onGameStarted` 에서 부른다. 그래서 오브젝트의 시작 순서가 어느 쪽이어도 같은 프리셋에서 시작한다.
         *          시작 전에는 카탈로그가 없어 id 를 대어 보지 못하고, 시작할 때 없는 id 면 알린다(`onBeginPlay`).
         */
        void startWithPreset( const hashed_string& id );
        /** @brief 다음 프리셋으로 돌리는 입력 맵 액션입니다. 비우면 키로만 돌립니다. */
        void setCycleAction( const hashed_string& action ) { _cycleAction = action; }
        /** @brief 화면에 함께 담을 오브젝트 그룹입니다. 프레이밍의 그룹 맞추기에 쓰며, 비어 있지 않으면 대상(`_target`) 대신 그룹을 따라갑니다. */
        void setGroupTargets( const vector<GameObjectHandle>& listTarget ) { _listGroupTarget = listTarget; }
        /** @brief 암 충돌 질의를 정합니다. 수명은 부르는 쪽이 쥐며, nullptr 이면 씬의 물리(`SceneCameraProbe`)를 씁니다. */
        void setCollisionProbe( const ICameraCollisionProbe* pProbe ) { _pExternalProbe = pProbe; }
        /** @brief 입력을 읽을지 정합니다. 자동 플레이와 테스트는 끕니다. */
        void setInputEnabled( bool bEnabled ) { _bReadInput = bEnabled; }
        /** @brief 충격을 더합니다. 원점과 모양을 받으며, 폭발이나 착지에 씁니다. */
        void addImpulse( const CameraImpulseDef& def, const float3& origin ) { _director.addImpulse( def, origin ); }
        /** @brief 시야각 킥(+, 달리기나 부스트)이나 줌 펀치(-, 사격이나 착탄)를 더합니다. */
        void addFieldOfViewKick( const CameraFieldOfViewKickDef& def ) { _director.addFieldOfViewKick( def ); }

        const hashed_string& getActivePresetID() const { return _director.getActivePresetID(); }
        bool                 isBlending() const { return _director.isBlending(); }
        const CameraPose&    getPose() const { return _director.getPose(); }
        /** @brief 켠 프리셋의 모드 상태입니다. 입력이 돌린 각, 줌, 팬, 암 길이를 담으며 테스트와 진단이 씁니다. */
        const CameraModeState& getModeState() const { return _director.getModeState(); }
        /** @brief 프리셋 카탈로그입니다. 코드로 프리셋을 더하거나 테스트가 XML 문자열을 읽을 때 씁니다. 경로 PROPERTY 가 비어 있으면 시작할 때 다시 읽지 않습니다. */
        CameraPresetCatalog&       getCatalog() { return _catalog; }
        const CameraPresetCatalog& getCatalog() const { return _catalog; }

        /** @brief 시간을 흘려 포즈를 구하고 같은 오브젝트의 카메라에 씁니다. 틱 안이면 틱 뒤로 미루고, 틱 밖이면 바로 보입니다. */
        void updateCamera( float32 deltaTime );

    private:
        CameraTarget computeTarget() const;
        /** @brief 이번 프레임 입력을 `InputManager` 에서 모읍니다. 켠 프리셋이 받는 것만 쓰입니다. */
        void gatherInput( float32 deltaTime );
        /**
         * @brief 켠 프리셋이 받는 조작(둘러보기, 줌, 팬, 회전)의 액션이 입력 맵에 없으면 프리셋마다 한 번 알립니다.
         * @details 맵에 없는 액션 질의는 로그 없이 0 이다. 그래서 액션 이름을 바꾸면 그 프리셋의 조작이 조용히 멈춘다. 받지 않는 조작의 액션은 없어도 된다.
         */
        void warnMissingPresetActions( const InputMap& inputMap, const CameraInputDef& inputDef );
        /** @brief 포토 모드 입력 맵(`_photoInputMap`)을 갱신하고 그 액션(`Photo.Move` 등)을 모아 자유 카메라에 넣습니다. */
        void gatherPhotoModeInput( float32 unscaledDeltaTime );
        /** @brief 미뤄 둔 포즈 계산과 쓰기를 합니다. 틱 뒤 게임 스레드에서 돕니다. */
        void resolveCamera();
        /** @brief 시작 프리셋을 고릅니다. `-gv_cameraPreset` 이 카탈로그에 있으면(게임 시점 카메라만) 그것을 쓰고, 아니면 `_initialPreset`, 그것도 아니면 첫 프리셋을 씁니다. */
        hashed_string resolveStartPreset() const;
        /** @brief 보관해 둔 상태 바이트를 디렉터에 적용합니다. 적용했으면 true 입니다. */
        [[nodiscard]] bool applyPendingState();

    private:
        PROPERTY( Category = "Camera", DisplayName = "Presets", AssetPath, Tooltip = "Camera preset XML (<CameraPresets>)" )
        string _catalogPath;
        PROPERTY( Category = "Camera", DisplayName = "Initial Preset", Tooltip = "Preset active at begin play; empty uses the first preset" )
        hashed_string _initialPreset;
        PROPERTY( Category = "Camera", DisplayName = "Target", Tooltip = "Object the presets follow (focus = its world position, yaw / pitch = its facing)" )
        GameObjectHandle _target;
        PROPERTY( Category = "Camera", DisplayName = "Group Targets", Tooltip = "Objects framed together (group framing); empty follows Target" )
        vector<GameObjectHandle> _listGroupTarget;
        PROPERTY( Category = "Camera", DisplayName = "Focus Point", Tooltip = "World focus used when there is no Target (top-down rigs: the panned ground point)", Units = m )
        float3 _focusPoint;
        PROPERTY( Category = "Camera", DisplayName = "Cycle Preset Action", Tooltip = "InputMap action that switches to the next preset in catalog order (empty: none)" )
        hashed_string _cycleAction;
        PROPERTY( Category = "Camera", DisplayName = "Pan Action", Tooltip = "InputMap action (2D vector) that pans presets with a pan speed; missing from the map: no pan" )
        hashed_string _panAction;
        PROPERTY( Category = "Camera", DisplayName = "Rotate Action", Tooltip = "InputMap action (1D axis, Pressed) whose sign steps presets with a rotate step" )
        hashed_string _rotateAction;
        PROPERTY( Category = "Camera", DisplayName = "Look Action", Tooltip = "InputMap action (2D movement, e.g. mouse delta) that orbits presets with a look sensitivity" )
        hashed_string _lookAction;
        PROPERTY( Category = "Camera", DisplayName = "Look Hold Action", Tooltip = "InputMap action held to orbit presets that look only while held" )
        hashed_string _lookHoldAction;
        PROPERTY( Category = "Camera", DisplayName = "Zoom Action", Tooltip = "InputMap action (1D axis, wheel notches; up is closer) that zooms presets with a zoom step" )
        hashed_string _zoomAction;
        PROPERTY( Category = "Photo Mode", DisplayName = "Move Speed", Min = 0.0, Tooltip = "Photo mode camera move speed in a perspective view (m/s)" )
        float32 _photoMoveSpeed;
        PROPERTY( Category = "Photo Mode", DisplayName = "Look Sensitivity", Min = 0.0, Tooltip = "Photo mode look sensitivity (rad per input unit)" )
        float32 _photoLookSensitivity;
        PROPERTY( Category = "Photo Mode", DisplayName = "Zoom Step", Min = 0.01, Max = 1.0,
                  Tooltip = "Scale per wheel notch applied to the field of view or ortho height (below 1 zooms in)" )
        float32 _photoZoomStep;
        PROPERTY( Category = "Photo Mode", DisplayName = "Max Exposure Offset", Min = 0.0, Tooltip = "Photo mode exposure compensation limit (+/- EV)", Meta = "Units=EV" )
        float32 _photoMaxExposureOffset;
        PROPERTY( Category = "Camera", DisplayName = "Read Input", Tooltip = "Feed the look, zoom, pan and rotate actions to presets with an <Input> section" )
        bool _bReadInput;
        PROPERTY( Category = "Camera", DisplayName = "Override Blend", Tooltip = "Blend into and out of a view override (ride camera)" )
        BlendCurveDef _overrideBlend;
        PROPERTY( Category = "Sequencer", DisplayName = "Field Of View Override", Min = 0.0, Max = 3.14,
                  Tooltip = "Vertical field of view a sequencer value track keys on top of the preset; 0 keeps the preset's", Units = rad )
        float32 _fieldOfViewOverride;
        PROPERTY( Category = "Sequencer", DisplayName = "Focus Distance Override", Min = 0.0,
                  Tooltip = "Focus distance a sequencer value track keys on top of the preset; 0 keeps the preset's", Units = m )
        float32 _focusDistanceOverride;
        PROPERTY( Category = "Sequencer", DisplayName = "Exposure Offset", Tooltip = "Exposure compensation a sequencer value track adds to the preset's", Meta = "Units=EV" )
        float32 _exposureOffset;

        CameraPresetDef _overrideDef;          ///< 덮어쓴 시점의 고정 프리셋
        hashed_string   _presetBeforeOverride; ///< 덮어쓰기를 풀 때 돌아갈 프리셋
        bool            _bViewOverride;

        PhotoModeCamera      _photoMode;
        unique_ptr<InputMap> _photoInputMap; ///< 포토 모드 동안만 있는 엔진 포토 모드 입력 맵(`enterPhotoMode` 가 읽고 `exitPhotoMode` 가 버린다)
        bool                 _bPhotoModeCut; ///< 포토 모드를 켜거나 끈 뒤 첫 쓰기에 컷 표시를 넣는다(TAA 기록을 버린다)

        CameraPresetCatalog          _catalog;
        CameraDirector               _director;
        vector<uint8>                _pendingStateBytes; ///< 시작 전에 받은 상태(`restoreState`). 카탈로그를 읽은 뒤 적용한다
        const ICameraCollisionProbe* _pExternalProbe;
        hashed_string                _inputCheckedPresetID; ///< 액션을 입력 맵에 대어 본 마지막 프리셋(`warnMissingPresetActions`)
        float32                      _pendingDeltaTime;     ///< 틱이 모은 시간. 미룬 계산이 쓴다
    };
} // namespace sw
