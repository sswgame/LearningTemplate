/**
 * @file KeyRebindScreen.h
 * @brief 키 바인딩 창입니다 — "누를 키를 누르세요" 로 다음 원시 입력을 받아 키 바인딩 설정의 보류 값으로 넣습니다(겹치면 바꾸기를 묻는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Screen/UiScreen.h"

namespace sw
{
    struct InputSlot;

    class UserSettingsManager;

    /**
     * @class KeyRebindScreen
     * @brief 키 바인딩 설정 하나의 키를 받는 모달입니다(Lyra 의 키 바인딩 창 · 유니티 Input System 의 `PerformInteractiveRebinding`).
     * @details **받기**: 행동이 아니라 원시 입력이다 — 열린 다음 프레임부터 `InputManager::findFirstPressedSlot` 의 첫 슬롯(키보드 · 마우스 · 패드). 받는 동안은
     *          UI 행동을 끈다(`wantsUiActions` — 누른 키가 메뉴를 움직이지 않게). Esc 는 떼면 취소, 1 초 누르고 있으면 Esc 를 바인딩한다. 받은 슬롯은 UI 가
     *          먹는다(뗄 때까지 게임이 보지 못한다).
     *          **겹침**: `findBindingConflict` 가 같은 범위의 다른 설정을 찾으면 "이미 쓰입니다 — 바꾸기 · 취소" 로 바뀌고(이때는 UI 행동을 받는다), 바꾸기는
     *          `setPendingBinding( Swap )`. 스키마 밖 액션과 겹치면 바꿀 상대가 없어 취소만 남는다. 겹치지 않으면 바로 보류 값으로 넣고 닫는다.
     */
    class SW_API KeyRebindScreen : public UiScreen
    {
    public:
        /** @brief Esc 를 이만큼 누르고 있으면 Esc 를 바인딩합니다(그 전에 떼면 취소). */
        static constexpr float32 kEscapeHoldSeconds = 1.0f;

        KeyRebindScreen( const UiScreenDesc& desc, unique_ptr<Widget> root );
        ~KeyRebindScreen() override;

        /** @brief 문서 @p documentPath 로 창을 열어 설정 @p settingId 의 키를 받습니다. 열지 못하면 무효 핸들입니다. */
        static UiScreenHandle open( UiSystem& ui, UserSettingsManager& settings, const hashed_string& settingId, string_view documentPath );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        bool onBack() override;
        void onTick( float32 deltaSeconds ) override;
        /** @brief 키를 받는 동안은 UI 행동을 끕니다(겹침을 묻는 동안은 받는다). */
        bool wantsUiActions() const override { return _bListening == false; }

        /** @brief 키를 기다리는 중인가입니다(겹침을 묻는 중이면 false). */
        bool                 isListening() const { return _bListening; }
        const hashed_string& getSettingId() const { return _settingId; }

    private:
        /** @brief 받은 슬롯을 먹고, 겹치면 묻는 상태로, 아니면 보류 값으로 넣고 닫습니다. */
        void capture( const InputSlot& slot );
        /** @brief 겹침을 묻는 상태로 바꿉니다 — 상대 이름 · 바꾸기 단추(상대가 설정일 때만) · 포커스. */
        void showConflict( const hashed_string& otherSettingId, const hashed_string& otherAction );

    private:
        string               _capturedSlotText; ///< 받은 슬롯의 글(겹침을 물을 때 바꾸기에 쓴다)
        hashed_string        _settingId;
        UserSettingsManager* _pSettings;         ///< 보류 값을 넣을 설정(창보다 오래 산다 — 엔진 서비스)
        uint32               _openFrame;         ///< 연 프레임의 입력 번호(`InputManager::getBeginFrameCount`) — 연 입력을 받지 않게
        float32              _escapeHeldSeconds; ///< Esc 를 누르고 있는 시간(0 = 누르지 않음)
        bool                 _bListening;
        bool                 _bEscapeHeld;
    };
} // namespace sw
