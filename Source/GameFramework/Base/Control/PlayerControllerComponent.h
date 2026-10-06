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

#include "Engine/Animation/BlendCurve.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Control/ControllerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class PlayerControllerComponent
     * @brief 로컬 플레이어 하나 — 입력 맵의 액션을 폰의 스키마 이름대로 읽어 의도를 만듭니다(언리얼 `APlayerController` + Enhanced Input).
     *        빙의하면 폰의 입력 레이어를 넣고, 같은 플레이어의 카메라 매니저 뷰 타깃을 폰으로 옮기고(`_viewBlend`), `PossessionChangedEvent` 를 냅니다.
     * @details 시선(폰의 `Look Action`, 2D)은 감도를 곱해 조종 회전에 더하고 피치를 폰의 상한에서 자릅니다(화면 아래(+y)로 끌면 아래를 본다).
     *          폰 스키마에 있는데 입력 맵에 없는 버튼 · 아날로그는 처음 쥘 때 한 번 경고합니다 — 조용히 안 눌리면 안 된다.
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

    protected:
        /** @brief 입력 레이어를 넣고, 뷰 타깃을 옮기고, 빙의 이벤트를 냅니다. */
        void onPossessed( PawnComponent& pawn ) override;
        /** @brief 넣었던 입력 레이어를 뺍니다. 갈아타는 중이 아니면 놓은 이벤트를 냅니다. */
        void onUnpossessed( PawnComponent& pawn ) override;

    private:
        /** @brief 폰 스키마의 이름 중 입력 맵에 없는 것을 경고합니다(폰마다 한 번). */
        void warnMissingActions( const PawnComponent& pawn, const InputManager& input );
        /** @brief "game" 채널에 빙의 이벤트를 냅니다. */
        void sendPossessionChanged( const GameObjectHandle& pawnObject );

    private:
        PROPERTY( Category = "Player", DisplayName = "View Blend", Tooltip = "Blend used when the view target follows possession" )
        BlendCurveSpec _viewBlend;
        PROPERTY( Category = "Player", DisplayName = "Player Index", Min = 0, Max = 3, Tooltip = "Local player this controller serves" )
        uint32 _playerIndex;
        PROPERTY( Category = "Player", DisplayName = "Look Sensitivity", Min = 0.0, Tooltip = "Radians per unit of the pawn's look action (mouse pixels, stick)" )
        float32 _lookSensitivity;
        PROPERTY( Category = "Player", DisplayName = "Manage View Target", Tooltip = "Move this player's camera manager view target to the possessed pawn" )
        bool _bManageViewTarget;

        hashed_string    _pushedLayer;  ///< 빙의 때 넣은 레이어(뺄 때 이름으로)
        GameObjectHandle _previousPawn; ///< 마지막으로 놓은 폰의 오브젝트(이벤트의 앞 폰)
        ComponentHandle  _warnedPawn;   ///< 빠진 액션을 이미 경고한 폰
    };
} // namespace sw
