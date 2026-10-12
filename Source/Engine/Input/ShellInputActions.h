/**
 * @file Engine/Input/ShellInputActions.h
 * @brief 셸 디버그 액션 맵(`EngineDefaultAssets::_shellInputMap`)을 만들고 프레임마다 돌립니다. `EngineLoop` 의 값 멤버입니다.
 * @details 이 클래스가 내주는 것은 **입력 사실**(그 액션이 이번 프레임에 발동했나)뿐입니다. 액션이 무엇을 뜻하는지(셰이더를 다시 굽는다,
 *          모듈을 다시 올린다)는 그 장치를 가진 쪽이 정합니다 — 셰이더 리로드 단축키는 `ShaderRecompiler` 를 소유한 `EngineLoop` 가 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class InputManager;
    class InputMap;

    /**
     * @class ShellInputActions
     * @brief 셸 디버그 InputMap 하나의 수명과 프레임 갱신입니다. 맵은 처음 `update` 할 때 만듭니다(그 전과 Shipping 에서는 없다).
     */
    class SW_API ShellInputActions
    {
    public:
        ShellInputActions();
        ~ShellInputActions();

        ShellInputActions( const ShellInputActions& )            = delete;
        ShellInputActions& operator=( const ShellInputActions& ) = delete;

        /**
         * @brief 맵을 (처음이면 만들고) @p inputManager 에 붙여 한 프레임 돌립니다. 타이틀 레이어는 끕니다 — 셸 단축키는 화면과 상관없이 받는다.
         */
        void update( InputManager& inputManager, float32 deltaTime );
        /** @brief 맵을 놓습니다. 맵이 입력 매니저를 가리키므로 입력 매니저를 없애기 전에 부릅니다(다음 `update` 가 다시 만든다). */
        void reset();

        /**
         * @brief 셸 디버그 액션 맵을 @p inputMapPath 의 InputMap 리소스에서 만듭니다.
         * @details 경로가 비었거나 읽지 못하면 오류를 남기고 **빈 맵**을 돌려줍니다. 손으로 적은 바인딩으로 바꿔 끼우지 않습니다 —
         *          그 내용은 리소스와 따로 낡고, 실패를 가립니다.
         */
        static unique_ptr<InputMap> createInputMap( string_view inputMapPath );

        /** @brief 해당 액션이 이번 프레임에 발동했는지 반환합니다. 맵이 아직 없으면 false 입니다. */
        bool wasActionTriggered( const hashed_string& actionName ) const;
        /** @brief 셸 InputMap 입니다(처음 `update` 전에는 nullptr). */
        InputMap* getInputMap() const { return _mapDebugAction.get(); }

    private:
        unique_ptr<InputMap> _mapDebugAction;
        bool                 _bBound;
    };
} // namespace sw
