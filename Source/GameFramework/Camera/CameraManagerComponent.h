/**
 * @file CameraManagerComponent.h
 * @brief 로컬 플레이어 하나의 "보는 카메라(뷰 타깃)" 를 블렌드로 바꾸는 컴포넌트입니다(언리얼 PlayerCameraManager `SetViewTargetWithBlend` ·
 *        Cinemachine Brain). 씬의 카메라는 용도(`CameraRole` — 플레이어 시점 · 보조 · 캡처)로 등록부(`CameraRegistry`)에 있습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"

#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Camera/CameraBlend.h"
#include "GameFramework/Camera/CameraPose.h"
#include "GameFramework/Camera/CameraShake.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CameraShakeRequest;

    class GameObjectManager;

    /**
     * @class CameraManagerComponent
     * @brief 같은 오브젝트의 카메라(이 플레이어가 실제로 그리는 카메라)를 뷰 타깃 카메라의 포즈 · 렌즈로 두고, 뷰 타깃을 바꾸면 **지금 화면에서**
     *        새 타깃으로 블렌드합니다(`CameraPoseBlender` — 블렌드 도중 다시 바꾸면 그 순간의 섞인 포즈에서 이어 간다). 충격(`addImpulse`)은 마지막에 얹습니다.
     * @details 뷰 타깃은 다른 오브젝트의 `CameraComponent` 입니다 — 그 카메라는 자기 컴포넌트(디렉터 · 리그 · 1인칭)가 움직이고, 매니저는 읽기만 합니다.
     *          그래서 타깃 카메라는 보통 보조(`CameraRole::Custom`)라 직접 뽑히지 않고, 매니저의 카메라가 플레이어 시점(우선순위가 가장 높은
     *          `Game`)입니다. 틱(`PostUpdate`)에서 일을 틱 뒤로 미뤄, 같은 프레임에 타깃을 움직인 디렉터(`PostPhysics` 에서 미룸) 다음에 읽는다.
     *          블렌드 없는 전환 · 타깃 카메라의 컷은 이 카메라에 컷 표시를 넣습니다.
     */
    REFLECT( Category = "Camera", DisplayName = "Camera Manager", Tooltip = "Per-player view target with blends (SetViewTargetWithBlend)" )
    class SW_GF_API CameraManagerComponent : public Component
    {
    public:
        REFLECT_BODY();

        CameraManagerComponent();
        virtual ~CameraManagerComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 뷰 타깃을 @p blend 로 바꿉니다. 같은 타깃이면 아무것도 하지 않습니다. */
        void setViewTarget( const GameObjectHandle& target, const BlendCurveSpec& blend );
        /** @brief 뷰 타깃을 기본 블렌드로 바꿉니다. */
        void                    setViewTarget( const GameObjectHandle& target ) { setViewTarget( target, _defaultBlend ); }
        const GameObjectHandle& getViewTarget() const { return _viewTarget; }
        /** @brief 역할이 @p role 인 켜진 카메라 중 지금 타깃 다음 것(등록 순서, 자기 카메라 제외)으로 바꿉니다. 바꾼 타깃을 돌려줍니다. */
        GameObjectHandle cycleViewTarget( CameraRole role );
        /** @brief 충격을 더합니다(원점 · 모양). 듣는 자리는 이 카메라입니다. */
        void addImpulse( const CameraImpulseDef& def, const float3& origin ) { _impulseListener.addImpulse( def, origin ); }

        bool              isBlending() const { return _blender.isBlending(); }
        const CameraPose& getPose() const { return _blender.getPose(); }
        uint32            getLocalPlayerIndex() const { return _localPlayerIndex; }

        /** @brief 매니저의 켜진 카메라 중 역할이 @p role 인 것들입니다(등록 순서). 용도별 목록의 창구입니다. */
        static void findCamerasByRole( const GameObjectManager& manager, CameraRole role, vector<CameraComponent*>& outListCamera );
        /** @brief 로컬 플레이어 @p playerIndex 의 매니저입니다. 없으면 nullptr 입니다. */
        static CameraManagerComponent* findForPlayer( const GameObjectManager& manager, uint32 playerIndex );

        /** @brief 시간을 흘려 이번 포즈를 쓰도록 합니다(틱 안이면 틱 뒤로 미루고, 틱 밖이면 바로 쓴다). */
        void updateCamera( float32 deltaTime );

    private:
        /** @brief 미뤄 둔 블렌드 · 쓰기입니다(틱 뒤 게임 스레드). */
        void resolveCamera();
        /** @brief 핸들의 오브젝트에 있는 카메라입니다. 없거나 자기 오브젝트면 nullptr 입니다. */
        CameraComponent* findTargetCamera( const GameObjectHandle& target ) const;
        /** @brief 애니메이션 알림(`CameraShake` 처리기)이 카메라 충격을 요청했습니다 — 충격으로 더합니다. */
        void onCameraShakeRequested( const CameraShakeRequest& request );

    private:
        PROPERTY( Category = "Camera", DisplayName = "View Target", Tooltip = "Object whose camera this player looks through" )
        GameObjectHandle _viewTarget;
        PROPERTY( Category = "Camera", DisplayName = "Default Blend", Tooltip = "Blend used by setViewTarget without an explicit blend" )
        BlendCurveSpec _defaultBlend;
        PROPERTY( Category = "Camera", DisplayName = "Local Player", Tooltip = "Local player index this manager serves" )
        uint32 _localPlayerIndex;
        PROPERTY( Category = "Camera", DisplayName = "Cycle Key", Tooltip = "Key that cycles the view target through cameras of Cycle Role (Unknown: none)" )
        Key _cycleKey;
        PROPERTY( Category = "Camera", DisplayName = "Cycle Role", Tooltip = "Role of the cameras the cycle key steps through" )
        CameraRole _cycleRole;

        GameObjectHandle      _previousTarget; ///< 살아 있는 나가는 쪽(블렌드가 없던 때 바꿨을 때)
        CameraPoseBlender     _blender;
        CameraImpulseListener _impulseListener;
        DelegateHandle        _shakeSubscription; ///< 알림의 카메라 흔들림 요청 구독(플레이 동안)
        float32               _pendingDeltaTime;
    };
} // namespace sw
