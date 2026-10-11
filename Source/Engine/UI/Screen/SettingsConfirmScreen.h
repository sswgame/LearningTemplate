/**
 * @file SettingsConfirmScreen.h
 * @brief 설정 확인 카운트다운 창입니다. 화면 방식이나 해상도처럼 `confirmSeconds` 가 있는 설정을 적용한 뒤 "유지할까요? 0:15" 를 보여 줍니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Binding/UIViewModel.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    class UserSettingsManager;

    /** @class SettingsConfirmViewModel @brief 확인 창이 보여 주는 값입니다. 남은 초를 가집니다(`{bind:_secondsLeft, converter=Seconds}`). */
    REFLECT()
    class SW_API SettingsConfirmViewModel : public UIViewModel
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
     * @brief 확인 대기(`UserSettingsManager::isAwaitingConfirm`) 동안 뜨는 모달입니다. [유지]는 `confirmChanges` 를, [되돌리기]와 뒤로 가기는 지금 되돌리기를 부릅니다.
     * @details 시간이 다 되면 매니저가 스스로 되돌리고(호스트가 프레임마다 `update` 를 부른다), 이 창은 확인 대기가 끝난 것을 보고 스스로 닫는다. 창이 시간을 세지 않는다.
     */
    class SW_API SettingsConfirmScreen : public UIScreen
    {
    public:
        SettingsConfirmScreen( const UIScreenDesc& desc, unique_ptr<Widget> root );
        ~SettingsConfirmScreen() override;

        /** @brief 문서 @p documentPath 로 창을 열고 설정 @p settings 의 카운트다운을 붙입니다. 열지 못하면 무효 핸들입니다. */
        static UIScreenHandle open( UISystem& ui, UserSettingsManager& settings, string_view documentPath );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        bool onBack() override;
        void onTick( float32 deltaSeconds ) override;

    private:
        /** @brief 확인 대기의 남은 시간을 지금 다 써서 되돌립니다(매니저의 시간이 다 된 것과 같은 길). */
        void revertNow();

    private:
        SettingsConfirmViewModel _viewModel;
        UserSettingsManager*     _pSettings; ///< 카운트다운을 가진 설정입니다(창보다 오래 사는 엔진 서비스).
    };
} // namespace sw
