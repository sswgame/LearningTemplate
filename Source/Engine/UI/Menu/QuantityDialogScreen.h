/**
 * @file QuantityDialogScreen.h
 * @brief 수량 대화 상자입니다. 1 ~ 최대 사이의 수를 슬라이더(패드 좌우 · 마우스)로 고르고 [확인] · [취소] 로 답합니다(엔진 문서 `EngineDefaultAssets::_uiQuantityDialog`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Menu/ConfirmDialogScreen.h"
#include "Engine/UI/Screen/UIScreen.h"

namespace sw
{
    /** @class QuantityDialogViewModel @brief 수량 대화 상자 문서가 `{bind:_title}` · `_count` 로 읽는 값입니다. */
    REFLECT()
    class SW_API QuantityDialogViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        QuantityDialogViewModel();
        ~QuantityDialogViewModel() override;

        const TypeInfo* getTypeInfo() const override;
        void            setTitle( const string& title );
        void            setCount( int32 count );

    private:
        PROPERTY( DisplayName = "Title" )
        string _title;
        PROPERTY( DisplayName = "Count" )
        int32 _count;
    };
} // namespace sw

namespace sw
{
    /**
     * @class QuantityDialogScreen
     * @brief 모달 수량 고르기입니다(겹친 아이템 나누기 · 여러 개 사기 · 버리기 수 — 디아블로 · 와우의 스택 나누기 창, 유니티 · Godot 의 SpinBox 대화 상자).
     * @details 문서는 슬라이더 `Amount` 와 명령 `Accept` · `Cancel` 단추를 가진다. 답은 확인 대화 상자와 같이 정확히 한 번이며, 뒤로 가기는 취소다.
     */
    class SW_API QuantityDialogScreen : public UIScreen
    {
    public:
        /** @brief 답입니다. 확인이면 고른 수(1 ~ 최대)이고, 취소면 수는 0 입니다. */
        using ResultDelegate = Delegate<void( ConfirmDialogResult result, int32 count )>;

        static constexpr const utf8* kAmountName = "Amount";

        QuantityDialogScreen( const UIScreenDef& desc, unique_ptr<Widget> root );
        ~QuantityDialogScreen() override;

        /**
         * @brief 엔진 수량 대화 상자 문서로 창을 엽니다. 제목 @p title(현지화 키이거나 원문), 범위 1 ~ @p maxCount, 처음 값 @p initialCount 입니다.
         * @details 최대가 1 보다 작거나, 제목이 비었거나, 답을 받을 쪽이 없으면 오류를 남기고 무효 핸들입니다(고를 것이 없는 창을 띄우지 않는다).
         */
        [[nodiscard]] static UIScreenHandle open( UISystem& ui, const string& title, int32 maxCount, int32 initialCount, const ResultDelegate& onResult );

        /** @brief 글 · 범위 · 답을 받을 쪽을 겁니다(`open` 이 문서 화면을 만든 뒤 부른다). 이미 답했으면 아무것도 하지 않습니다. */
        void present( const string& title, int32 maxCount, int32 initialCount, const ResultDelegate& onResult );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        bool onBack() override;
        /** @brief 슬라이더 값을 보이는 수에 옮깁니다. */
        void onTick( float32 deltaSeconds ) override;

        /** @brief 지금 고른 수입니다(1 ~ 최대). */
        int32 getCount() const;

    private:
        void finish( ConfirmDialogResult result );

    private:
        QuantityDialogViewModel _viewModel;
        ResultDelegate          _onResult;
        int32                   _maxCount;
        int32                   _initialCount;
        uint8                   _bFinished;
    };
} // namespace sw
