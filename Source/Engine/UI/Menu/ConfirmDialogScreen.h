/**
 * @file ConfirmDialogScreen.h
 * @brief 공통 확인 대화 상자입니다. 제목 · 본문 · [확인] · [취소] 를 보이고 고른 답을 델리게이트로 한 번 알립니다(엔진 문서 `EngineDefaultAssets::_uiConfirmDialog`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    /** @brief 확인 대화 상자의 답입니다. */
    enum class ConfirmDialogResult : uint8
    {
        Accepted = 0, ///< [확인]
        Cancelled     ///< [취소] · 뒤로 가기(`UI.Back`, Esc, 패드 B)
    };

    /**
     * @struct ConfirmDialogDesc
     * @brief 대화 상자에 보일 글입니다. 넷 모두 필수이며 현지화 키 · 메시지(`LocalizedMessage`) · 원문입니다(`TextWidget::setText` 와 같은 규칙 — 바인딩이 문화권으로 해석한다).
     */
    struct ConfirmDialogDesc
    {
        string _title{};
        string _message{};
        string _acceptText{};
        string _cancelText{};
    };
} // namespace sw

namespace sw
{
    /** @class ConfirmDialogViewModel @brief 대화 상자 문서가 `{bind:_title}` · `_message` · `_acceptText` · `_cancelText` 로 읽는 값입니다. */
    REFLECT()
    class SW_API ConfirmDialogViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        ConfirmDialogViewModel();
        ~ConfirmDialogViewModel() override;

        const TypeInfo* getTypeInfo() const override;
        void            apply( const ConfirmDialogDesc& desc );

    private:
        PROPERTY( DisplayName = "Title" )
        string _title;
        PROPERTY( DisplayName = "Message" )
        string _message;
        PROPERTY( DisplayName = "Accept Text" )
        string _acceptText;
        PROPERTY( DisplayName = "Cancel Text" )
        string _cancelText;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ConfirmDialogScreen
     * @brief 모달 확인 대화 상자입니다(언리얼 CommonUI 의 `UCommonGameDialog` · Lyra 의 `ULyraConfirmationScreen`, 유니티 · Godot 의 `ConfirmationDialog`).
     * @details 키트 · 게임이 저마다 "정말 끝낼까요?" 창을 다시 만들지 않게 엔진에 하나 둔다. 문서는 명령 `Accept` · `Cancel` 단추를 가진다.
     *          답은 정확히 한 번 알린다 — 같은 프레임에 두 단추를 눌러도, 닫기 애니메이션 동안 다시 눌러도 두 번째는 버린다(구매 확인의 중복 누름).
     *          덮였던 화면의 포커스는 닫힐 때 화면 스택이 돌려준다(`UIScreen::_lastFocused`). 델리게이트는 닫기를 요청한 뒤에 부르므로 그 안에서 새 화면을 열어도 된다.
     */
    class SW_API ConfirmDialogScreen : public UIScreen
    {
    public:
        using ResultDelegate = Delegate<void( ConfirmDialogResult result )>;

        static constexpr const utf8* kAcceptCommand = "Accept";
        static constexpr const utf8* kCancelCommand = "Cancel";

        ConfirmDialogScreen( const UIScreenDef& desc, unique_ptr<Widget> root );
        ~ConfirmDialogScreen() override;

        /**
         * @brief 엔진 확인 대화 상자 문서(`UISystem::getEngineDocuments()._confirmDialog`)로 창을 엽니다.
         * @details 글이 하나라도 비었거나 문서가 없으면 오류를 남기고 무효 핸들입니다(빈 단추를 띄우지 않는다). @p onResult 는 묶여 있어야 합니다.
         */
        [[nodiscard]] static UIScreenHandle open( UISystem& ui, const ConfirmDialogDesc& desc, const ResultDelegate& onResult );

        /** @brief 이 창에 글 @p desc 와 답을 받을 @p onResult 를 겁니다(`open` 이 문서 화면을 만든 뒤 부른다). 이미 답을 냈으면 아무것도 하지 않습니다. */
        void present( const ConfirmDialogDesc& desc, const ResultDelegate& onResult );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        /** @brief 뒤로 가기는 취소입니다. */
        bool onBack() override;

        /** @brief 답을 이미 냈는지입니다(닫기 애니메이션 동안 true). */
        bool hasFinished() const { return _bFinished == SW_TRUE; }

    private:
        /** @brief 처음 한 번만 닫고 답을 알립니다. */
        void finish( ConfirmDialogResult result );

    private:
        ConfirmDialogViewModel _viewModel;
        ResultDelegate         _onResult;
        uint8                  _bFinished;
    };
} // namespace sw
