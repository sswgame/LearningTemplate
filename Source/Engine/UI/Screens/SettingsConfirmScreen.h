/**
 * @file SettingsConfirmScreen.h
 * @brief 설정 확인 카운트다운 창입니다 — "유지할까요? 0:15" (화면 방식 · 해상도처럼 `confirmSeconds` 가 있는 설정을 적용한 뒤).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Binding/UiViewModel.h"
#include "Engine/UI/Screen/UiScreen.h"

namespace sw
{
    class UserSettingsManager;

    /** @class SettingsConfirmViewModel @brief 확인 창이 보이는 값 — 남은 초입니다(`{bind:_secondsLeft, converter=Seconds}`). */
    REFLECT()
    class SW_API SettingsConfirmViewModel : public UiViewModel
    {
    public:
        REFLECT_BODY();

        SettingsConfirmViewModel();
        ~SettingsConfirmViewModel() override;

        const TypeInfo* getTypeInfo() const override;
        void            setSecondsLeft( float32 secondsLeft );

    private:
        PROPERTY( DisplayName = "Seconds Left", Units = s )
        float32 _secondsLeft;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SettingsConfirmScreen
     * @brief 확인 대기(`UserSettingsManager::isAwaitingConfirm`) 동안 뜨는 모달입니다. [유지] = `confirmChanges`, [되돌리기] · 뒤로 = 지금 되돌림.
     * @details 시간이 다 되면 매니저가 스스로 되돌리고(호스트가 프레임마다 `update`), 이 창은 확인 대기가 끝난 것을 보고 스스로 닫습니다 — 창이 시간을 세지 않습니다.
     */
    class SW_API SettingsConfirmScreen : public UiScreen
    {
    public:
        SettingsConfirmScreen( const UiScreenDesc& desc, unique_ptr<Widget> root );
        ~SettingsConfirmScreen() override;

        /** @brief 문서 @p documentPath 로 창을 열고 설정 @p settings 의 카운트다운을 붙입니다. 열지 못하면 무효 핸들입니다. */
        static UiScreenHandle open( UiSystem& ui, UserSettingsManager& settings, string_view documentPath );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        bool onBack() override;
        void onTick( float32 deltaSeconds ) override;

    private:
        /** @brief 확인 대기의 남은 시간을 지금 다 써서 되돌립니다(매니저의 시간이 다 된 것과 같은 길). */
        void revertNow();

    private:
        SettingsConfirmViewModel _viewModel;
        UserSettingsManager*     _pSettings; ///< 카운트다운을 든 설정(창보다 오래 산다 — 엔진 서비스)
    };
} // namespace sw
