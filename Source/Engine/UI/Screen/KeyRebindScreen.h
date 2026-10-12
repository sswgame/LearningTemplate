/**
 * @file KeyRebindScreen.h
 * @brief 키 바인딩 창입니다. "누를 키를 누르세요" 를 보여 주고 다음 원시 입력을 받아 키 바인딩 설정의 보류 값으로 넣습니다(겹치면 바꾸기를 묻습니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    struct InputSlot;

    class UserSettingsManager;

    /**
     * @class KeyRebindScreen
     * @brief 키 바인딩 설정 하나의 키를 받는 모달입니다(Lyra 의 키 바인딩 창, 유니티 Input System 의 `PerformInteractiveRebinding` 에 해당).
     * @details **받기**: 액션이 아니라 원시 입력을 받는다. 열린 다음 프레임부터 `InputManager::findFirstPressedSlot` 의 첫 슬롯(키보드, 마우스, 패드)을 쓴다.
     *          받는 동안은 UI 액션을 끈다(`wantsUIActions`, 누른 키가 메뉴를 움직이지 않게 한다). Esc 는 떼면 취소하고, 1 초 누르고 있으면 Esc 를 바인딩한다.
     *          **패드 사용자의 취소.** 패드 B 도 바인딩할 수 있어야 해서 패드 단추에는 취소 입력을 두지 않는다. 대신 아무것도 받지 못한 채 `kListenTimeoutSeconds` 가 지나면 취소하고 닫는다
     *          (Esc 는 눌렀다 떼면 취소이므로 그 시간은 Esc 를 누르는 동안 세지 않는다).
     *          받은 슬롯은 UI 가 소비한다(뗄 때까지 게임이 보지 못한다).
     *          **겹침**: `findBindingConflict` 가 같은 범위의 다른 설정을 찾으면 "이미 쓰입니다. 바꾸기 또는 취소" 로 바뀌고(이때는 UI 액션을 받는다), 바꾸기는
     *          `setPendingBinding( Swap )` 을 부른다. 스키마 밖 액션과 겹치면 바꿀 상대가 없어 취소만 남는다. 겹치지 않으면 바로 보류 값으로 넣고 닫는다.
     */
    class SW_API KeyRebindScreen : public UIScreen
    {
    public:
        /** @brief Esc 를 이만큼 누르고 있으면 Esc 를 바인딩합니다(그 전에 떼면 취소). */
        static constexpr float32 kEscapeHoldSeconds = 1.0f;
        /** @brief 키를 기다리다 이 시간이 지나면 취소하고 닫습니다(패드 사용자도 빠져나올 길이 있어야 하고, 패드 B 는 바인딩할 수 있는 입력이라 취소 단추로 쓸 수 없습니다). */
        static constexpr float32 kListenTimeoutSeconds = 10.0f;

        KeyRebindScreen( const UIScreenDesc& desc, unique_ptr<Widget> root );
        ~KeyRebindScreen() override;

        /** @brief 문서 @p documentPath 로 창을 열어 설정 @p settingID 의 키를 받습니다. 열지 못하면 무효 핸들을 반환합니다. */
        static UIScreenHandle open( UISystem& ui, UserSettingsManager& settings, const hashed_string& settingID, string_view documentPath );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        bool onBack() override;
        void onTick( float32 deltaSeconds ) override;
        /** @brief 키를 받는 동안은 UI 액션을 끕니다(겹침을 묻는 동안은 받습니다). */
        bool wantsUIActions() const override { return _bListening == false; }

        /** @brief 키를 기다리는 중이면 true 입니다(겹침을 묻는 중이면 false). */
        bool                 isListening() const { return _bListening; }
        const hashed_string& getSettingID() const { return _settingID; }

    private:
        /** @brief 받은 슬롯을 소비하고, 겹치면 묻는 상태로 바꾸며, 아니면 보류 값으로 넣고 닫습니다. */
        void capture( const InputSlot& slot );
        /** @brief 겹침을 묻는 상태로 바꿉니다. 상대 이름, 바꾸기 버튼(상대가 설정일 때만), 포커스를 맞춥니다. */
        void showConflict( const hashed_string& otherSettingID, const hashed_string& otherAction );

    private:
        string               _capturedSlotText; ///< 받은 슬롯의 텍스트입니다(겹침을 물을 때 바꾸기에 씁니다).
        hashed_string        _settingID;
        UserSettingsManager* _pSettings;         ///< 보류 값을 넣을 설정입니다(창보다 오래 사는 엔진 서비스).
        uint32               _openFrame;         ///< 연 프레임의 입력 번호입니다(`InputManager::getBeginFrameCount`). 창을 연 입력을 받지 않게 하기 위한 값입니다.
        float32              _escapeHeldSeconds; ///< Esc 를 누르고 있는 시간입니다(0 이면 누르지 않음).
        float32              _listenSeconds;     ///< 키를 기다린 시간입니다(`kListenTimeoutSeconds` 에 닿으면 취소).
        bool                 _bListening;
        bool                 _bEscapeHeld;
    };
} // namespace sw
