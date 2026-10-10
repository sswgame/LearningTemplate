/**
 * @file UIBindingSet.h
 * @brief 화면 하나의 바인딩 집합입니다 — 문서의 바인딩 식을 풀어 위젯 칸과 소스(뷰모델 필드)를 잇고, 바뀐 것만 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Binding/UIBindingConverterRegistry.h"
#include "Engine/UI/Binding/UIBindingExpression.h"
#include "Engine/UI/Binding/UIBindingValue.h"
#include "Engine/UI/Document/UIBindingDesc.h"

namespace sw
{
    struct UserSettingEvent;

    class LocalizationManager;
    class UIScreen;
    class UIViewModel;
    class UserSettingsManager;
    class Widget;

    /** @struct UIBindingContext @brief 바인딩 단계가 묻는 것 — 현지화(형식 바인딩) · 변환기 표 · 사용자 설정(설정 바인딩)입니다. */
    struct UIBindingContext
    {
        const LocalizationManager*        _pLocalization{ nullptr };      ///< 형식 바인딩의 메시지 패턴(없으면 값 글 그대로)
        const UIBindingConverterRegistry* _pConverters{ nullptr };        ///< 변환기 표(없으면 변환기를 쓴 바인딩은 오류)
        UserSettingsManager*              _pSettings{ nullptr };          ///< 설정 바인딩의 소스(없으면 설정 바인딩은 오류)
        bool                              _bTextRevisionChanged{ false }; ///< 이번 프레임 글 판이 바뀌었다 — 형식 바인딩을 다시 쓴다
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIBindingSet
     * @brief 화면(`UIScreen`) 하나의 바인딩입니다(UE5 MVVM 의 바인딩 목록 · 유니티 런타임 바인딩). 문서의 식(`UIBindingDesc`)을 풀어 걸고(`bind`),
     *        바인딩 단계(`update`)마다 바뀐 소스에 묶인 위젯 칸만 리플렉션으로 씁니다.
     * @details **소스**: `{bind:필드}` 는 뷰모델이 그 필드를 알렸을 때만(필드 통지), `{poll:필드}` 는 매 프레임 값을 견줘(개발 편의 — 비용 카운터 `UI.PollBindings`).
     *          위젯 칸에 쓰면 그 위젯의 `onBoundPropertyChanged` 가 칸에 맞는 무효화를 합니다(글 · 크기 칸은 레이아웃, 색 칸은 그리기만).
     *          **타입 검사**는 걸 때 합니다 — 소스와 칸의 갈래가 맞지 않고 변환기도 없으면 오류(로그 + `getErrors`)이고 그 바인딩은 걸리지 않습니다(조용히 0 이 되지 않게).
     *          **양방향**: 사용자가 위젯 값을 바꾸면(`Widget::notifyValueEdited`) 소스에 되쓰고 그 필드를 알립니다. 같은 바인딩이 그 알림으로 위젯에 다시 쓰지 않습니다.
     *          뷰모델은 게임이 소유합니다 — 먼저 지워지면 그 바인딩들을 놓습니다(`onViewModelDestroyed`).
     *
     *          **설정 바인딩**(`{setting:id}` — 기본 양방향): 값은 `getValue`(보류 우선), 칸이 `_value` 이고 위젯에 `_minValue` · `_maxValue` · `_step` 이
     *          있으면 설정 정의의 범위를, 칸이 `_selectedIndex` 이고 위젯에 `_listOption` 이 있으면 선택지(`collectOptions` 의 글 키)를 채우고,
     *          `isSettingEnabled` 를 위젯 사용 가능으로 둡니다. 사용자 입력은 `setPendingValue` 계열로 넣고 다음 단계에서 다시 읽습니다(고쳐 받은 값 ·
     *          거절은 위젯이 설정 값으로 돌아간다). 다른 곳의 변경(되돌리기 · 기본값 · 확인 카운트다운의 자동 되돌림)은 변경 통보로 받고, 리스너는
     *          이 집합(= 화면)이 지워질 때 뗍니다. 게임 스레드만.
     */
    class SW_API UIBindingSet
    {
    public:
        explicit UIBindingSet( UIScreen& screen );
        ~UIBindingSet();
        UIBindingSet( const UIBindingSet& )            = delete;
        UIBindingSet& operator=( const UIBindingSet& ) = delete;

        /** @brief 뷰모델을 바꿉니다(nullptr 이면 뗀다). 다음 `update` 가 다시 걸고 모든 칸을 처음부터 씁니다. */
        void         setViewModel( UIViewModel* pViewModel );
        UIViewModel* getViewModel() const { return _pViewModel; }
        /** @brief 다음 `update` 가 바인딩을 다시 걸게 합니다(식이 더해졌거나 변환기 표가 바뀌었다). */
        void markRebind() { _bBound = false; }

        /**
         * @brief 바인딩 단계 — 걸리지 않았으면 걸고 모든 칸을 쓰고, 걸려 있으면 바뀐 소스의 칸만 씁니다.
         * @param listBinding 화면의 바인딩 식(`UIScreen::getBindings`).
         */
        void update( const vector<UIBindingDesc>& listBinding, const UIBindingContext& context );
        /** @brief 사용자 입력이 위젯 @p widget 의 칸 @p propertyName 을 바꿨다 — 양방향 바인딩이면 소스에 되씁니다. */
        void onWidgetValueEdited( Widget& widget, const hashed_string& propertyName );
        /** @brief 뷰모델 @p viewModel 이 지워진다 — 놓고, 바인딩은 다음 `setViewModel` 까지 쉽니다. */
        void onViewModelDestroyed( UIViewModel& viewModel );
        /** @brief 사용자 설정이 바뀌었다(변경 통보) — 다음 `update` 가 설정 바인딩을 다시 읽습니다. */
        void onSettingEvent( const UserSettingEvent& event );

        /** @brief 걸린 바인딩 수입니다(오류로 빠진 것 제외). */
        uint32 getBindingCount() const { return static_cast<uint32>( _listBinding.size() ); }
        /** @brief 마지막으로 걸 때의 오류(파일 · 줄 · 식 · 이유, 영어)입니다. */
        const vector<string>& getErrors() const { return _listError; }
        /** @brief 위젯 칸에 실제로 쓴 횟수입니다(같은 값이면 세지 않는다 — 시험 · 진단). */
        uint32 getWriteCount() const { return _writeCount; }
        /** @brief 이번 `update` 가 견준 폴링 바인딩 수입니다(프로파일 카운터 UI.PollBindings). */
        uint32 getPolledCount() const { return _polledCount; }

    private:
        /** @struct ActiveBinding @brief 걸린 바인딩 하나 — 풀어 둔 식 · 위젯 칸 경로 · 소스 경로입니다. */
        struct ActiveBinding
        {
            UIBindingExpression _expression;
            UIPropertyPath      _target;       ///< 위젯 기준 칸
            UIPropertyPath      _source;       ///< 뷰모델 기준 필드
            UIPropertyPath      _rangeMin;     ///< 설정 바인딩: 위젯의 `_minValue`(없으면 무효)
            UIPropertyPath      _rangeMax;     ///< 설정 바인딩: 위젯의 `_maxValue`
            UIPropertyPath      _rangeStep;    ///< 설정 바인딩: 위젯의 `_step`
            UIPropertyPath      _optionList;   ///< 설정 바인딩: 위젯의 `_listOption`(글 목록)
            UIBindingConverter  _converter;    ///< 변환기(이름이 비면 없음)
            string              _lastPollText; ///< 폴링: 지난 프레임에 본 값(글 표기)
            hashed_string       _sourceField;  ///< 뷰모델의 맨 위 필드 이름(알림 단위) · 설정 바인딩이면 설정 id
            uint64              _skipSerial;   ///< 양방향: 이 바인딩이 되쓰며 낸 알림 번호 — 그 알림으로는 위젯에 다시 쓰지 않는다
            WidgetId            _widget;
            bool                _bPollKnown; ///< 폴링: `_lastPollText` 가 유효하다
            bool                _bDirty;     ///< 설정: 되쓴 뒤 다시 읽어야 한다(고쳐 받음 · 거절)
        };

        /** @brief 식을 모두 풀어 겁니다. 오류는 `_listError` 와 로그에 남깁니다. */
        void bind( const vector<UIBindingDesc>& listBinding, const UIBindingContext& context );
        /** @brief 식 하나를 풀어 겁니다. 걸 수 없으면 false 이고 @p outError 에 이유가 있습니다. 뷰모델이 없어 쉬는 식은 true · 걸지 않음입니다. */
        [[nodiscard]] bool bindOne( const UIBindingDesc& desc, const UIBindingContext& context, string& outError );
        /** @brief 설정 바인딩의 소스 갈래를 정하고 범위 · 선택지 칸을 찾습니다. 설정이 없으면 false 입니다. */
        [[nodiscard]] bool bindSetting( ActiveBinding& binding, const Widget& widget, const UIBindingContext& context, UIBindingValueKind& outSourceKind,
                                        string& outError ) const;
        /** @brief 설정 바인딩의 값을 읽고 범위 · 선택지 · 사용 가능을 위젯에 씁니다(값 칸 앞에 — 범위가 값을 묶는다). */
        UIBindingValue readSetting( const ActiveBinding& binding, Widget& widget, const UIBindingContext& context );
        /** @brief 사용자 입력을 설정의 보류 값으로 넣습니다. */
        void writeSetting( ActiveBinding& binding, const Widget& widget );
        /** @brief 사용자 설정 변경 통보를 겁니다(다른 서비스면 옛 것을 뗀다). */
        void registerSettingsListener( UserSettingsManager* pSettings );
        /** @brief 바인딩 하나의 소스 값을 읽어(변환기 · 형식 적용) 위젯 칸에 씁니다. 칸이 바뀌었으면 true 입니다. */
        [[nodiscard]] bool apply( ActiveBinding& binding, const UIBindingContext& context );
        /** @brief 소스 값에 변환기 · 형식을 얹습니다. 변환기가 실패하면 false 입니다. */
        [[nodiscard]] bool transform( const ActiveBinding& binding, const UIBindingContext& context, UIBindingValue& inoutValue ) const;
        /** @brief 뷰모델의 관찰자 등록을 뗍니다. */
        void releaseViewModel();

    private:
        vector<ActiveBinding> _listBinding;
        vector<string>        _listError;
        UIScreen*             _pScreen;
        UIViewModel*          _pViewModel;
        UserSettingsManager*  _pSettings;        ///< 변경 통보를 건 설정 서비스(설정 바인딩이 없으면 nullptr)
        DelegateHandle        _settingsListener; ///< 그 통보의 핸들
        uint64                _seenSerial;       ///< 마지막 `update` 가 본 뷰모델 알림 번호
        uint32                _writeCount;
        uint32                _polledCount;
        bool                  _bBound;           ///< 식을 풀어 걸었다(뷰모델 · 식이 바뀌면 다시)
        bool                  _bApplying;        ///< 위젯 칸에 쓰는 중 — 그 사이의 사용자 입력 알림은 되쓰지 않는다
        bool                  _bSettingsChanged; ///< 설정 변경 통보를 받았다 — 설정 바인딩을 모두 다시 읽는다
    };
} // namespace sw
