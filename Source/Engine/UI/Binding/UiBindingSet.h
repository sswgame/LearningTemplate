/**
 * @file UiBindingSet.h
 * @brief 화면 하나의 바인딩 집합입니다 — 문서의 바인딩 식을 풀어 위젯 칸과 소스(뷰모델 필드)를 잇고, 바뀐 것만 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Binding/UiBindingConverterRegistry.h"
#include "Engine/UI/Binding/UiBindingExpression.h"
#include "Engine/UI/Binding/UiBindingValue.h"
#include "Engine/UI/Core/WidgetTypes.h"
#include "Engine/UI/Document/UiBindingDesc.h"

namespace sw
{
    class LocalizationManager;
    class UiScreen;
    class UiViewModel;
    class Widget;

    /** @struct UiBindingContext @brief 바인딩 단계가 묻는 것 — 현지화(형식 바인딩) · 변환기 표입니다. */
    struct UiBindingContext
    {
        const LocalizationManager*        _pLocalization{ nullptr }; ///< 형식 바인딩의 메시지 패턴(없으면 값 글 그대로)
        const UiBindingConverterRegistry* _pConverters{ nullptr };   ///< 변환기 표(없으면 변환기를 쓴 바인딩은 오류)
    };
} // namespace sw

namespace sw
{
    /**
     * @class UiBindingSet
     * @brief 화면(`UiScreen`) 하나의 바인딩입니다(UE5 MVVM 의 바인딩 목록 · 유니티 런타임 바인딩). 문서의 식(`UiBindingDesc`)을 풀어 걸고(`bind`),
     *        바인딩 단계(`update`)마다 바뀐 소스에 묶인 위젯 칸만 리플렉션으로 씁니다.
     * @details **소스**: `{bind:필드}` 는 뷰모델이 그 필드를 알렸을 때만(필드 통지), `{poll:필드}` 는 매 프레임 값을 견줘(개발 편의 — 비용 카운터 `Ui.PollBindings`).
     *          위젯 칸에 쓰면 그 위젯의 `onBoundPropertyChanged` 가 칸에 맞는 무효화를 합니다(글 · 크기 칸은 레이아웃, 색 칸은 그리기만).
     *          **타입 검사**는 걸 때 합니다 — 소스와 칸의 갈래가 맞지 않고 변환기도 없으면 오류(로그 + `getErrors`)이고 그 바인딩은 걸리지 않습니다(조용히 0 이 되지 않게).
     *          **양방향**: 사용자가 위젯 값을 바꾸면(`Widget::notifyValueEdited`) 소스에 되쓰고 그 필드를 알립니다. 같은 바인딩이 그 알림으로 위젯에 다시 쓰지 않습니다.
     *          뷰모델은 게임이 소유합니다 — 먼저 지워지면 그 바인딩들을 놓습니다(`onViewModelDestroyed`). 게임 스레드만.
     */
    class SW_API UiBindingSet
    {
    public:
        explicit UiBindingSet( UiScreen& screen );
        ~UiBindingSet();
        UiBindingSet( const UiBindingSet& )            = delete;
        UiBindingSet& operator=( const UiBindingSet& ) = delete;

        /** @brief 뷰모델을 바꿉니다(nullptr 이면 뗀다). 다음 `update` 가 다시 걸고 모든 칸을 처음부터 씁니다. */
        void         setViewModel( UiViewModel* pViewModel );
        UiViewModel* getViewModel() const { return _pViewModel; }
        /** @brief 다음 `update` 가 바인딩을 다시 걸게 합니다(식이 더해졌거나 변환기 표가 바뀌었다). */
        void markRebind() { _bBound = false; }

        /**
         * @brief 바인딩 단계 — 걸리지 않았으면 걸고 모든 칸을 쓰고, 걸려 있으면 바뀐 소스의 칸만 씁니다.
         * @param listBinding 화면의 바인딩 식(`UiScreen::getBindings`).
         */
        void update( const vector<UiBindingDesc>& listBinding, const UiBindingContext& context );
        /** @brief 사용자 입력이 위젯 @p widget 의 칸 @p propertyName 을 바꿨다 — 양방향 바인딩이면 소스에 되씁니다. */
        void onWidgetValueEdited( Widget& widget, const hashed_string& propertyName );
        /** @brief 뷰모델 @p viewModel 이 지워진다 — 놓고, 바인딩은 다음 `setViewModel` 까지 쉽니다. */
        void onViewModelDestroyed( UiViewModel& viewModel );

        /** @brief 걸린 바인딩 수입니다(오류로 빠진 것 제외). */
        uint32 getBindingCount() const { return static_cast<uint32>( _listBinding.size() ); }
        /** @brief 마지막으로 걸 때의 오류(파일 · 줄 · 식 · 이유, 영어)입니다. */
        const vector<string>& getErrors() const { return _listError; }
        /** @brief 위젯 칸에 실제로 쓴 횟수입니다(같은 값이면 세지 않는다 — 시험 · 진단). */
        uint32 getWriteCount() const { return _writeCount; }
        /** @brief 이번 `update` 가 견준 폴링 바인딩 수입니다(프로파일 카운터 Ui.PollBindings). */
        uint32 getPolledCount() const { return _polledCount; }

    private:
        /** @struct ActiveBinding @brief 걸린 바인딩 하나 — 풀어 둔 식 · 위젯 칸 경로 · 소스 경로입니다. */
        struct ActiveBinding
        {
            UiBindingExpression _expression;
            UiPropertyPath      _target;       ///< 위젯 기준 칸
            UiPropertyPath      _source;       ///< 뷰모델 기준 필드
            UiBindingConverter  _converter;    ///< 변환기(이름이 비면 없음)
            string              _lastPollText; ///< 폴링: 지난 프레임에 본 값(글 표기)
            hashed_string       _sourceField;  ///< 뷰모델의 맨 위 필드 이름(알림 단위)
            uint64              _skipSerial;   ///< 양방향: 이 바인딩이 되쓰며 낸 알림 번호 — 그 알림으로는 위젯에 다시 쓰지 않는다
            WidgetId            _widget;
            bool                _bPollKnown; ///< 폴링: `_lastPollText` 가 유효하다
        };

        /** @brief 식을 모두 풀어 겁니다. 오류는 `_listError` 와 로그에 남깁니다. */
        void bind( const vector<UiBindingDesc>& listBinding, const UiBindingContext& context );
        /** @brief 식 하나를 풀어 겁니다. 걸 수 없으면 false 이고 @p outError 에 이유가 있습니다. 뷰모델이 없어 쉬는 식은 true · 걸지 않음입니다. */
        [[nodiscard]] bool bindOne( const UiBindingDesc& desc, const UiBindingContext& context, string& outError );
        /** @brief 바인딩 하나의 소스 값을 읽어(변환기 · 형식 적용) 위젯 칸에 씁니다. 칸이 바뀌었으면 true 입니다. */
        [[nodiscard]] bool apply( ActiveBinding& binding, const UiBindingContext& context );
        /** @brief 소스 값에 변환기 · 형식을 얹습니다. 변환기가 실패하면 false 입니다. */
        [[nodiscard]] bool transform( const ActiveBinding& binding, const UiBindingContext& context, UiBindingValue& inoutValue ) const;
        /** @brief 뷰모델의 관찰자 등록을 뗍니다. */
        void releaseViewModel();

    private:
        vector<ActiveBinding> _listBinding;
        vector<string>        _listError;
        UiScreen*             _pScreen;
        UiViewModel*          _pViewModel;
        uint64                _seenSerial; ///< 마지막 `update` 가 본 뷰모델 알림 번호
        uint32                _writeCount;
        uint32                _polledCount;
        bool                  _bBound;    ///< 식을 풀어 걸었다(뷰모델 · 식이 바뀌면 다시)
        bool                  _bApplying; ///< 위젯 칸에 쓰는 중 — 그 사이의 사용자 입력 알림은 되쓰지 않는다
    };
} // namespace sw
