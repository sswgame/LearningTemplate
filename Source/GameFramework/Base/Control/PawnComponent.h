/**
 * @file PawnComponent.h
 * @brief 조종 대상(폰) — 조종자가 채운 이번 틱의 의도를 들고, 버튼 · 아날로그 이름표(스키마)와 플레이어가 빙의했을 때의 입력 레이어를 정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 조종자 없이 플레이를 시작한 폰을 누가 쥐는가입니다. */
    ENUM()
    enum class PawnAutoPossess : uint8
    {
        None = 0, ///< 아무도 — 의도 0(멈춤 · 대기). 탑승자가 내린 탈것 · 관전 대상
        Player0,  ///< 로컬 플레이어 0 의 조종자(없으면 조종 시스템이 하나 세운다)
        Ai,       ///< `_aiControllerPrefab` 을 세워 빙의시킨다(비었으면 기본 AI 조종자 — 서 있다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class PawnComponent
     * @brief 폰 오브젝트에 하나 붙습니다. 같은 오브젝트의 이동 · 무기 · 탑승 컴포넌트는 **이것의 의도만** 읽습니다(InputMap · InputManager 를 읽지 않는다).
     * @details 의도는 조종 시스템(`ControlSystem`, 씬 프레임 단계 `FrameSystems`)이 틱 전에 게임 스레드에서 씁니다 — 틱(병렬) 동안에는 읽기만 합니다.
     *          조종자가 없으면 이동 · 버튼 · 아날로그가 0 이고 조종 회전은 마지막 값을 지킵니다.
     *          언리얼 `APawn` 의 자리입니다(그쪽은 폰이 입력 컴포넌트에 바인딩하지만, 여기는 조종자가 의도 구조체 하나를 채운다 — 플레이어와 NPC 의 이동 코드가 하나).
     */
    REFLECT( Category = "Control", DisplayName = "Pawn", Tooltip = "Something a controller can possess; movement and actions read only its control intent" )
    class SW_GF_API PawnComponent : public Component
    {
    public:
        REFLECT_BODY();

        PawnComponent();
        ~PawnComponent() override = default;

        /** @brief 등록부에 들고 조종 시스템을 매니저에 붙입니다(없으면). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 조종자와의 끈을 끊고 등록부에서 빠집니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 이번 틱의 의도입니다. */
        const ControlIntent& getIntent() const { return _intent; }
        /** @brief 버튼 이름의 자리입니다(스키마 순서). 없으면 −1 입니다. 소비자는 시작할 때 한 번 풀어 둡니다. */
        int32 findButton( const hashed_string& name ) const;
        /** @brief 아날로그 이름의 자리입니다. 없으면 −1 입니다. */
        int32 findAnalog( const hashed_string& name ) const;
        bool  isButtonDown( int32 buttonIndex ) const { return _intent.isDown( buttonIndex ); }
        bool  wasButtonTriggered( int32 buttonIndex ) const { return _intent.wasTriggered( buttonIndex ); }

        /** @brief 지금 쥔 조종자 컴포넌트입니다(없으면 무효). */
        const ComponentHandle& getController() const { return _controller; }
        bool                   isPossessed() const { return _controller.isValid(); }
        /** @brief 조종 회전에 더할 것(반동 · 피격 흔들림)을 쌓습니다 — 조종자가 다음 틱의 조종 회전에 넣습니다. 틱 안 여러 컴포넌트가 불러도 된다(원자적 합). */
        void addControlRotationOffset( float32 deltaYaw, float32 deltaPitch );

        const vector<hashed_string>& getButtonNames() const { return _listButton; }
        void                         setButtonNames( const vector<hashed_string>& listButton ) { _listButton = listButton; }
        const vector<hashed_string>& getAnalogNames() const { return _listAnalog; }
        void                         setAnalogNames( const vector<hashed_string>& listAnalog ) { _listAnalog = listAnalog; }
        const hashed_string&         getMoveAction() const { return _moveAction; }
        const hashed_string&         getLookAction() const { return _lookAction; }
        const hashed_string&         getUpAction() const { return _upAction; }
        const hashed_string&         getInputLayer() const { return _inputLayer; }
        void                         setInputLayer( const hashed_string& layer ) { _inputLayer = layer; }
        float32                      getMaxPitch() const { return _maxPitch; }
        PawnAutoPossess              getAutoPossess() const { return _autoPossess; }
        void                         setAutoPossess( PawnAutoPossess autoPossess ) { _autoPossess = autoPossess; }
        const string&                getAiControllerPrefab() const { return _aiControllerPrefab; }
        /** @brief 이 폰의 의도를 내는 네트워크 연결입니다(0 = 로컬). 빙의를 따라갑니다 — 탈것은 운전석 조종자의 연결입니다. */
        uint32 getInputPeer() const { return _inputPeer; }

    private:
        friend class ControlSystem;
        friend class ControllerComponent;

        void applyIntent( const ControlIntent& intent ) { _intent = intent; }
        /** @brief 조종자 없음 — 이동 · 버튼 · 아날로그 0, 조종 회전은 그대로입니다. */
        void clearMotion();
        /** @brief 쌓인 조종 회전 오프셋(요 · 피치)을 꺼내고 0 으로 돌립니다. */
        float2 consumeControlRotationOffset();

    private:
        PROPERTY( Category = "Control", DisplayName = "Buttons", Tooltip = "Intent button names in bit order (an InputMap action of the same name drives each for a player)" )
        vector<hashed_string> _listButton;
        PROPERTY( Category = "Control", DisplayName = "Analogs", Tooltip = "Intent analog names (an InputMap 1D action of the same name drives each for a player)" )
        vector<hashed_string> _listAnalog;
        PROPERTY( Category = "Control", DisplayName = "Move Action", Tooltip = "InputMap 2D action a player controller reads as the move axes (empty: none)" )
        hashed_string _moveAction;
        PROPERTY( Category = "Control", DisplayName = "Look Action", Tooltip = "InputMap 2D action a player controller adds to the control rotation (empty: none)" )
        hashed_string _lookAction;
        PROPERTY( Category = "Control", DisplayName = "Up Action", Tooltip = "InputMap 1D action for up and down (swim, fly); empty = none" )
        hashed_string _upAction;
        PROPERTY( Category = "Control", DisplayName = "Input Layer", Tooltip = "InputMap layer pushed while a player possesses this pawn (OnFoot, Horse, Car); empty = none" )
        hashed_string _inputLayer;
        PROPERTY( Category = "Control", DisplayName = "AI Controller Prefab", AssetPath, AssetType = "Prefab", Tooltip = "Spawned to possess this pawn when Auto Possess is Ai (empty: a plain AI controller)" )
        string _aiControllerPrefab;
        PROPERTY( Category = "Control", DisplayName = "Max Pitch", Min = 0.0, Max = 1.5707963, Tooltip = "Largest look up or down angle of the control rotation", Units = rad )
        float32 _maxPitch;
        PROPERTY( Category = "Control", DisplayName = "Auto Possess", Tooltip = "Who takes this pawn when play starts without a controller" )
        PawnAutoPossess _autoPossess;

        ControlIntent          _intent;
        ComponentHandle        _controller;           ///< 쥔 조종자 컴포넌트
        atomic<int32>          _yawOffsetUnits;       ///< 쌓인 요 오프셋(`kRotationOffsetUnitsPerRadian` 단위 고정소수점)
        atomic<int32>          _pitchOffsetUnits;     ///< 쌓인 피치 오프셋
        uint32                 _inputPeer;            ///< 의도를 내는 연결(0 = 로컬)
        uint8                  _bAutoPossessDone : 1; ///< 자동 빙의를 한 번 시도했다(실패해도 다시 하지 않는다)
        [[maybe_unused]] uint8 _reserved         : 7;
    };
} // namespace sw
