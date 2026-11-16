/**
 * @file CinematicCameraComponent.h
 * @brief 시퀀스 플레이어의 카메라 신호를 카메라 디렉터와 카메라 매니저로 옮기는 컴포넌트를 정의합니다.
 * @details 엔진의 시퀀서는 게임 프레임워크의 카메라(프리셋, 블렌드 스택, 흔들림)를 모른다. 그래서 시퀀서는 신호만 내고(`SequenceCameraSignal`),
 *          이 컴포넌트가 들어서 디렉터를 움직인다. 언리얼 Sequencer 의 Camera Cut 트랙이 PlayerCameraManager 에 뷰 타깃을 넣는 것에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Engine/Animation/Graph/BlendCurve.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct SequenceCameraSignal;

    class CameraComponent;
    class CameraDirectorComponent;
    class GameObject;

    /**
     * @class CinematicCameraComponent
     * @brief 같은 오브젝트의 `SequencePlayerComponent` 가 내는 카메라 신호를 받아 카메라를 움직입니다.
     * @details 신호마다 하는 일은 다음과 같다.
     *          - 컷 바뀜: 컷 대상 카메라의 디렉터가 항목의 프리셋을 켠다. 다른 카메라에서 넘어오면 그 화면에서 프리셋의 블렌드로 섞는다(언리얼 Camera Cut 의 Can Blend).
     *          - 시네마틱 시작: 시퀀스의 Blend In 시간이 있으면 게임 카메라 화면에서 첫 컷으로 섞어 들어간다.
     *          - 시네마틱 끝: Blend Out 시간이 있으면 마지막 컷 화면에서 게임 카메라(디렉터 또는 카메라 매니저)로 섞어 나온다.
     *          - 대기 뷰: 로컬 플레이어의 카메라 매니저에 "곧 볼 시점" 을 등록한다. 듣는 쪽은 그 매니저의 대기 뷰 설명에 있다.
     *          - 흔들림: 대상 카메라의 디렉터(없으면 카메라 매니저)에 구간 길이만큼의 충격을 더한다.
     *          섞을 디렉터나 매니저가 없는 카메라는 끊어 바꾼다.
     *          시네마틱 시작부터 끝까지 게임 인스턴스의 세이브 막기 문(`SaveLock`)에 막기를 든다 — 시퀀서가 움직인 위치 · 외형이 세이브에 박히지 않게 한다.
     */
    REFLECT( Category = "Camera", DisplayName = "Cinematic Camera",
             Tooltip = "Routes the sequence player's camera signals (cut presets, game and cinematic blends, pending views, shakes) to camera directors and managers" )
    class SW_GF_API CinematicCameraComponent : public Component
    {
    public:
        REFLECT_BODY();

        CinematicCameraComponent();
        virtual ~CinematicCameraComponent() override = default;

        /** @brief 같은 오브젝트의 시퀀스 플레이어에 신호 델리게이트를 등록합니다. */
        void onBeginPlay() override;
        /** @brief 신호 델리게이트를 뗍니다. */
        void onEndPlay() override;

        /** @brief 신호 하나를 처리합니다. 시퀀스 플레이어가 부르고, 테스트는 직접 부릅니다. */
        void handleCameraSignal( const SequenceCameraSignal& signal );
        /** @brief 컷이 화면을 덮고 있으면 true 입니다(시네마틱 시작과 끝 사이). */
        bool isCinematicActive() const { return _currentCutCamera.isValid(); }
        /** @brief 지금 화면을 덮는 컷 카메라 오브젝트입니다. 없으면 빈 핸들입니다. */
        const GameObjectHandle& getCurrentCutCamera() const { return _currentCutCamera; }
        /** @brief 대기 뷰 · 끝 블렌드를 받는 지역 플레이어 번호입니다. */
        uint32 getLocalPlayerIndex() const { return _localPlayerIndex; }

    private:
        /** @brief 신호가 싣고 온 대상(항목의 바인딩이 묶인 오브젝트)입니다. 묶이지 않았거나 지워졌으면 nullptr 입니다. */
        GameObject*              findSignalTarget( const SequenceCameraSignal& signal ) const;
        CameraDirectorComponent* findDirector( const GameObject* pObject ) const;
        CameraComponent*         findGameCamera() const;
        void                     applyCut( const SequenceCameraSignal& signal );
        void                     beginCinematic( const SequenceCameraSignal& signal );
        void                     endCinematic( const SequenceCameraSignal& signal );
        void                     forwardPendingView( const SequenceCameraSignal& signal );
        void                     applyShake( const SequenceCameraSignal& signal );
        /** @brief 시네마틱 블렌드 곡선(`_cinematicBlendCurve`)에 시간을 붙입니다. @p seconds 는 0 보다 커야 한다(호출부가 0 이하를 먼저 거른다). */
        BlendCurveDef makeCinematicBlend( float32 seconds ) const;

    private:
        PROPERTY( Category = "Cinematic", DisplayName = "Blend Curve", Tooltip = "Curve of the game to cinematic blends; the sequence gives the duration" )
        BlendCurve _cinematicBlendCurve;
        PROPERTY( Category = "Cinematic", DisplayName = "Local Player", Tooltip = "Local player whose camera manager receives pending views" )
        uint32 _localPlayerIndex;
        PROPERTY( Category = "Cinematic", DisplayName = "Shake Frequency", Min = 0.0, Tooltip = "Phase speed of sequencer shakes", Units = "rad/s" )
        float32 _shakeFrequency;

        GameObjectHandle _currentCutCamera;   ///< 지금 화면을 덮는 컷 카메라
        Subscription     _signalSubscription; ///< 시퀀스 플레이어의 카메라 신호 구독
        Subscription     _saveBlock;          ///< 시네마틱 동안 든 세이브 막기(끝 · 플레이 끝에서 놓는다)
    };
} // namespace sw
