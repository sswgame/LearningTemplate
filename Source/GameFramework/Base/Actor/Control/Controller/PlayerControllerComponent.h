/**
 * @file PlayerControllerComponent.h
 * @brief 로컬 플레이어 조종자 — 입력 → 매핑(InputMap) → 의도. 매핑 층을 읽는 **유일한** 조종자입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Graph/BlendCurve.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Control/Controller/ControllerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class InputMap;
    class UISystem;
} // namespace sw

namespace sw
{
    /**
     * @class PlayerControllerComponent
     * @brief 로컬 플레이어 하나 — 입력 맵의 액션을 폰의 스키마 이름대로 읽어 의도를 만듭니다(언리얼 `APlayerController` + Enhanced Input).
     *        빙의하면 폰의 입력 레이어를 넣고, 같은 플레이어의 카메라 매니저 뷰 타깃을 폰으로 옮기고(`_viewBlend`), `PossessionChangedEvent` 를 냅니다.
     * @details 시선(폰의 `Look Action`, 2D)은 감도를 곱해 조종 회전에 더하고 피치를 폰의 상한에서 자릅니다(화면 아래(+y)로 끌면 아래를 본다).
     *          폰 스키마에 있는데 입력 맵에 없는 버튼 · 아날로그는 처음 쥘 때 한 번 경고합니다 — 조용히 안 눌리면 안 된다.
     *
     *          **마우스 잠금**: 폰이 바라면(`PawnComponent::wantsMouseLock` — 1인칭) 쥘 때 커서를 창 가운데에 잠그고 숨기며, 잠금 토글 액션(`_mouseLockAction`,
     *          기본 `ToggleMouseLock` — Esc)이 풀고 다시 겁니다. 놓거나 지워질 때 건 것을 풉니다. 그런 폰의 시선은 잠금이 실제로 걸린 동안만 쌓습니다
     *          (`InputManager::isMouseLockActive` — 포커스 밖 · Alt · 개발 콘솔 · 클릭 전에는 풀린 커서를 움직여도 화면이 돌지 않는다). 배타 가상 입력은 OS 포인터를
     *          쥐지 않으므로 잠금 요청만 봅니다. 모두 조종 시스템 단계(게임 스레드)에서 바꿉니다.
     *
     *          **UI 와의 경계 — 이 조종자가 그 한 자리입니다**: 런타임 UI 가 먹은 입력(`UISystem::isActionConsumed` — 메뉴에서 누른 패드 A, 위젯이 받은 클릭)은
     *          뗄 때까지 의도에 넣지 않고, 모달 · 로딩 화면이 떠 있으면(`isGameInputBlocked`) 의도가 0 입니다(조종 회전은 남는다). 활성 화면이 커서를 바라면
     *          (`wantsCursor`) 마우스 잠금을 쉬고 시선을 쌓지 않습니다 — 화면이 닫히면 요청해 둔 잠금으로 돌아갑니다.
     */
    REFLECT( Category = "Control", DisplayName = "Player Controller", Tooltip = "Local player: InputMap actions -> control intent; moves the view target and input layer with possession" )
    class SW_GF_API PlayerControllerComponent : public ControllerComponent
    {
    public:
        REFLECT_BODY();

        PlayerControllerComponent();
        ~PlayerControllerComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        void produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent ) override;

        uint32  getPlayerIndex() const { return _playerIndex; }
        void    setPlayerIndex( uint32 playerIndex ) { _playerIndex = playerIndex; }
        float32 getLookSensitivity() const { return _lookSensitivity; }
        void    setLookSensitivity( float32 sensitivity ) { _lookSensitivity = sensitivity; }
        /** @brief 쥔 폰을 위해 마우스 잠금을 걸어 두려는지입니다(토글 액션이 뒤집는다). 폰이 잠금을 바라지 않으면 false 입니다. */
        bool isMouseLockRequested() const { return _bMouseLockRequested == SW_TRUE; }

    protected:
        /** @brief 입력 레이어를 넣고, 뷰 타깃을 옮기고, 빙의 이벤트를 냅니다. */
        void onPossessed( PawnComponent& pawn ) override;
        /** @brief 넣었던 입력 레이어를 뺍니다. 갈아타는 중이 아니면 놓은 이벤트를 냅니다. */
        void onUnpossessed( PawnComponent& pawn ) override;

    private:
        /** @brief 행동 @p action 이 눌려 있고 UI 가 먹은 입력이 아니면 true 입니다. */
        static bool isActionDownForGame( const InputMap& inputMap, const UISystem* pUISystem, const hashed_string& action );
        /** @brief 행동 @p action 이 이번 틱 발동했고 UI 가 먹은 입력이 아니면 true 입니다. */
        static bool wasActionTriggeredForGame( const InputMap& inputMap, const UISystem* pUISystem, const hashed_string& action );
        /** @brief 폰 스키마의 이름 중 입력 맵에 없는 것을 경고합니다(폰마다 한 번). */
        void warnMissingActions( const PawnComponent& pawn, const InputManager& input );
        /** @brief "game" 채널에 빙의 이벤트를 냅니다. */
        void sendPossessionChanged( const GameObjectHandle& pawnObject );
        /** @brief 잠금 · 커서 숨김을 @p bLocked 로 맞춥니다. 이 조종자가 건 것만 풉니다. */
        void applyMouseLock( InputManager& input, bool bLocked );
        /** @brief 이 매니저의 플레이어 입력입니다 — 조종 시스템이 없으면(씬을 비우는 중) 게임 서비스의 것입니다. */
        static InputManager* findInputManager( const GameObjectManager& manager );

    private:
        PROPERTY( Category = "Player", DisplayName = "View Blend", Tooltip = "Blend used when the view target follows possession" )
        BlendCurveDef _viewBlend;
        PROPERTY( Category = "Player", DisplayName = "Player Index", Min = 0, Max = 3, Tooltip = "Local player this controller serves" )
        uint32 _playerIndex;
        PROPERTY( Category = "Player", DisplayName = "Look Sensitivity", Min = 0.0, Tooltip = "Radians per unit of the pawn's look action (mouse pixels, stick)" )
        float32 _lookSensitivity;
        PROPERTY( Category = "Player", DisplayName = "Manage View Target", Tooltip = "Move this player's camera manager view target to the possessed pawn" )
        bool _bManageViewTarget;
        PROPERTY( Category = "Player", DisplayName = "Mouse Lock Action", Tooltip = "InputMap action that releases and re-locks the cursor of a pawn that wants mouse lock (Esc)" )
        hashed_string _mouseLockAction;

        hashed_string          _pushedLayer;             ///< 빙의 때 넣은 레이어(뺄 때 이름으로)
        GameObjectHandle       _previousPawn;            ///< 마지막으로 놓은 폰의 오브젝트(이벤트의 앞 폰)
        ComponentHandle        _warnedPawn;              ///< 빠진 액션을 이미 경고한 폰
        uint8                  _bMouseLockRequested : 1; ///< 쥔 폰을 위해 잠금을 걸어 두려 한다(토글 액션이 뒤집는다)
        uint8                  _bMouseLockApplied   : 1; ///< 이 조종자가 입력 관리자에 잠금을 걸었다(놓을 때 푼다)
        [[maybe_unused]] uint8 _reserved            : 6;
    };
} // namespace sw
