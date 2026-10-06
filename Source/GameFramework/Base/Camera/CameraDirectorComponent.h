/**
 * @file CameraDirectorComponent.h
 * @brief 같은 오브젝트의 `CameraComponent` 를 데이터 프리셋(모드 · 입력 · 제약 · 프레이밍 · 충돌 · 흔들림) · 블렌드로 움직이는 컴포넌트입니다
 *        (Cinemachine 가상 카메라 + Brain 의 블렌드 · 언리얼 카메라 모드의 자리).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Camera/CameraBlend.h"
#include "GameFramework/Base/Camera/CameraDirector.h"
#include "GameFramework/Base/Camera/CameraPose.h"
#include "GameFramework/Base/Camera/CameraPreset.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ICameraCollisionProbe;

    /**
     * @class CameraDirectorComponent
     * @brief 프리셋 카탈로그(`<CameraPresets>` XML)를 읽고, 켠 프리셋을 대상에 맞춰 풀어 블렌드 · 감쇠 · 흔들림을 거쳐 같은 오브젝트의 카메라에 씁니다.
     * @details **틱 뒤에 씁니다.** `TickGroup::PostPhysics` 에서 입력만 읽고 포즈 계산 · 쓰기는 틱 뒤 게임 스레드로 미룹니다
     *          (`executeOrDeferPostTick`) — 틱 중의 트랜스폼 쓰기는 틱이 끝나야 적용되므로, 틱 안에서 대상을 읽으면 한 프레임 늦은 자리를 따라간다
     *          (1인칭 프리셋이면 손에 든 총이 시점보다 한 프레임 앞서 흔들린다). 틱 밖에서 부르면 바로 씁니다. 그룹이 PostUpdate 보다 앞이라
     *          이 카메라를 뷰 타깃으로 읽는 카메라 매니저(PostUpdate 에서 미룸)보다 먼저 돈다.
     *
     *          대상은 오브젝트 하나(`_target`) 또는 묶음(`_listGroupTarget` — 프레이밍의 그룹 맞추기)이고 초점 = 월드 자리, 요 · 피치 = 그 오브젝트가 보는 쪽입니다.
     *          입력(`<Input>` 이 있는 프리셋)은 `InputManager` 에서 읽고, 암 충돌(`<Collision>`)은 따로 준 질의(`setCollisionProbe`)가 없으면 매니저의
     *          `PhysicsWorld` 바디에 쓸어 봅니다(대상 자신은 뺀다). 입력 맵 액션 `_cycleAction` 이 발동하면 카탈로그 순서로 다음 프리셋을 켭니다.
     *          블렌드 없는 전환(컷)은 카메라에 컷 표시를 넣어 렌더러가 TAA 기록을 버리게 합니다. 켠 프리셋 · 블렌드 진행은 저장하지 않습니다.
     */
    REFLECT( Category = "Camera", DisplayName = "Camera Director", Tooltip = "Drives the camera on this object from data presets with blends, damping, framing and shake" )
    class SW_GF_API CameraDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        CameraDirectorComponent();
        virtual ~CameraDirectorComponent() override = default;

        /** @brief 씬의 카메라 디렉터 목록(`ComponentRegistry`)에 듭니다 — "이 오브젝트를 따라가는 디렉터" 를 씬 전체를 훑지 않고 찾습니다. */
        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 프리셋을 카탈로그의 블렌드로 켭니다. 없는 id 면 false 입니다. */
        [[nodiscard]] bool activatePreset( const hashed_string& id );
        /** @brief 프리셋을 @p blend 로 켭니다(블렌드 표를 무시한다). */
        [[nodiscard]] bool activatePreset( const hashed_string& id, const BlendCurveSpec& blend );
        /** @brief 카탈로그 순서로 다음 프리셋을 켭니다(마지막 다음은 처음). 켠 id 를 돌려줍니다. 프리셋이 없으면 빈 id 입니다. */
        hashed_string activateNextPreset();

        /** @brief 카메라가 따라갈 오브젝트입니다. 비우면 초점은 원점 · 요 · 피치 0 입니다. */
        void                    setTarget( const GameObjectHandle& target ) { _target = target; }
        const GameObjectHandle& getTarget() const { return _target; }
        /** @brief 다음 프리셋으로 돌리는 입력 맵 액션입니다(비우면 키만). */
        void setCycleAction( const hashed_string& action ) { _cycleAction = action; }
        /** @brief 함께 담을 오브젝트 묶음입니다(프레이밍의 그룹 맞추기). 비어 있지 않으면 대상(`_target`) 대신 묶음을 따라간다. */
        void setGroupTargets( const vector<GameObjectHandle>& listTarget ) { _listGroupTarget = listTarget; }
        /** @brief 암 충돌 질의를 정합니다(부르는 쪽이 수명을 쥔다). nullptr 이면 매니저의 `PhysicsWorld` 를 쓴다. */
        void setCollisionProbe( const ICameraCollisionProbe* pProbe ) { _pExternalProbe = pProbe; }
        /** @brief 입력을 읽을지입니다(자동 플레이 · 시험은 끈다). */
        void setInputEnabled( bool bEnabled ) { _bReadInput = bEnabled; }
        /** @brief 충격을 더합니다(원점 · 모양) — 폭발 · 착지. */
        void addImpulse( const CameraImpulseDef& def, const float3& origin ) { _director.addImpulse( def, origin ); }

        const hashed_string& getActivePresetId() const { return _director.getActivePresetId(); }
        bool                 isBlending() const { return _director.isBlending(); }
        const CameraPose&    getPose() const { return _director.getPose(); }
        /** @brief 켠 프리셋의 모드 상태입니다(입력이 돌린 각 · 줌 · 팬 · 암 길이 — 시험 · 진단). */
        const CameraModeState& getModeState() const { return _director.getModeState(); }
        /** @brief 카탈로그입니다. 코드로 프리셋을 더하거나 시험이 XML 글을 읽을 때 씁니다(경로 PROPERTY 가 비어 있으면 시작 때 다시 읽지 않는다). */
        CameraPresetCatalog&       getCatalog() { return _catalog; }
        const CameraPresetCatalog& getCatalog() const { return _catalog; }

        /** @brief 시간을 흘려 포즈를 구하고 같은 오브젝트의 카메라에 씁니다(틱 안이면 틱 뒤로 미루고, 틱 밖이면 바로 보인다). */
        void updateCamera( float32 deltaTime );

    private:
        CameraTarget computeTarget() const;
        /** @brief 이번 프레임 입력을 `InputManager` 에서 모읍니다(켠 프리셋이 받는 것만 쓰인다). */
        void gatherInput( float32 deltaTime );
        /** @brief 미뤄 둔 포즈 계산 · 쓰기입니다(틱 뒤 게임 스레드). */
        void resolveCamera();
        /** @brief 시작 프리셋입니다 — `-gv_cameraPreset` 이 카탈로그에 있으면(게임 시점 카메라만) 그것, 아니면 `_initialPreset`, 아니면 첫 프리셋. */
        hashed_string resolveStartPreset() const;

    private:
        PROPERTY( Category = "Camera", DisplayName = "Presets", AssetPath, Tooltip = "Camera preset XML (<CameraPresets>)" )
        string _catalogPath;
        PROPERTY( Category = "Camera", DisplayName = "Initial Preset", Tooltip = "Preset active at begin play; empty uses the first preset" )
        hashed_string _initialPreset;
        PROPERTY( Category = "Camera", DisplayName = "Target", Tooltip = "Object the presets follow (focus = its world position, yaw / pitch = its facing)" )
        GameObjectHandle _target;
        PROPERTY( Category = "Camera", DisplayName = "Group Targets", Tooltip = "Objects framed together (group framing); empty follows Target" )
        vector<GameObjectHandle> _listGroupTarget;
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
        PROPERTY( Category = "Camera", DisplayName = "Read Input", Tooltip = "Feed the look, zoom, pan and rotate actions to presets with an <Input> section" )
        bool _bReadInput;

        CameraPresetCatalog          _catalog;
        CameraDirector               _director;
        const ICameraCollisionProbe* _pExternalProbe;
        float32                      _pendingDeltaTime; ///< 틱이 모은 시간 — 미룬 계산이 쓴다
    };
} // namespace sw
