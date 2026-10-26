/**
 * @file UIBindingSet.h
 * @brief 화면 하나의 바인딩 집합입니다. 문서의 바인딩 식을 해석해 위젯 필드와 소스(뷰모델 필드)를 연결하고, 바뀐 것만 씁니다.
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

    /** @struct UIBindingContext @brief 바인딩 단계가 필요로 하는 것입니다. 현지화(형식 바인딩), 변환기 레지스트리, 사용자 설정(설정 바인딩)이 있습니다. */
    struct UIBindingContext
    {
        const LocalizationManager*        _pLocalization{ nullptr };      ///< 형식 바인딩의 메시지 패턴입니다(없으면 값 텍스트를 그대로 씁니다).
        const UIBindingConverterRegistry* _pConverters{ nullptr };        ///< 변환기 레지스트리입니다(없으면 변환기를 쓴 바인딩은 오류입니다).
        UserSettingsManager*              _pSettings{ nullptr };          ///< 설정 바인딩의 소스입니다(없으면 설정 바인딩은 오류입니다).
        bool                              _bTextRevisionChanged{ false }; ///< 이번 프레임에 텍스트 버전이 바뀌었는지 나타냅니다. 바뀌었으면 형식 바인딩을 다시 씁니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIBindingSet
     * @brief 화면(`UIScreen`) 하나의 바인딩입니다(UE5 MVVM 의 바인딩 목록, 유니티 런타임 바인딩에 해당).
     * @details 문서의 식(`UIBindingDesc`)을 해석해 연결하고(`bind`), 바인딩 단계(`update`)마다 바뀐 소스에 묶인 위젯 필드만 리플렉션으로 쓴다.
     *
     *          **소스**: `{bind:필드}` 는 뷰모델이 그 필드를 알렸을 때만(필드 통지) 쓰고, `{poll:필드}` 는 매 프레임 값을 비교한다(개발 편의용이며 비용 카운터는 `UI.PollBindings`).
     *          위젯 필드에 쓰면 그 위젯의 `onBoundPropertyChanged` 가 필드에 맞는 무효화를 한다(텍스트와 크기 필드는 레이아웃, 색 필드는 그리기만).
     *          **타입 검사**는 연결할 때 한다. 소스와 필드의 종류가 맞지 않고 변환기도 없으면 오류(로그와 `getErrors`)이며 그 바인딩은 연결하지 않는다(조용히 0 이 되지 않게 하기 위해서다).
     *          **양방향**: 사용자가 위젯 값을 바꾸면(`Widget::notifyValueEdited`) 소스에 되쓰고 그 필드를 알린다. 같은 바인딩이 그 알림으로 위젯에 다시 쓰지는 않는다.
     *          뷰모델은 게임이 소유한다. 뷰모델이 먼저 지워지면 그 바인딩들을 해제한다(`onViewModelDestroyed`).
     *
     *          **설정 바인딩**(`{setting:id}`, 기본 양방향): 값은 `getValue`(보류 값 우선)로 읽는다. 필드가 `_value` 이고 위젯에 `_minValue`, `_maxValue`, `_step` 이
     *          있으면 설정 정의의 범위를 채우고, 필드가 `_selectedIndex` 이고 위젯에 `_listOption` 이 있으면 선택지(`collectOptions` 의 텍스트 키)를 채운다.
     *          `isSettingEnabled` 는 위젯의 사용 가능 여부로 쓴다. 사용자 입력은 `setPendingValue` 계열로 넣고 다음 단계에서 다시 읽는다(보정된 값이나
     *          거절된 값은 위젯이 설정 값으로 돌아간다). 다른 곳의 변경(되돌리기, 기본값, 확인 카운트다운의 자동 되돌림)은 변경 통보로 받고, 리스너는
     *          이 집합(곧 화면)이 지워질 때 해제한다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIBindingSet
    {
    public:
        explicit UIBindingSet( UIScreen& screen );
        ~UIBindingSet();
        UIBindingSet( const UIBindingSet& )            = delete;
        UIBindingSet& operator=( const UIBindingSet& ) = delete;

        /** @brief 뷰모델을 바꿉니다(nullptr 이면 해제). 다음 `update` 가 다시 연결하고 모든 필드를 처음부터 씁니다. */
        void         setViewModel( UIViewModel* pViewModel );
        UIViewModel* getViewModel() const { return _pViewModel; }
        /** @brief 다음 `update` 가 바인딩을 다시 연결하게 합니다(식이 추가됐거나 변환기 레지스트리가 바뀌었을 때). */
        void markRebind() { _bBound = false; }

        /**
         * @brief 바인딩 단계입니다. 연결되지 않았으면 연결하고 모든 필드를 쓰며, 연결되어 있으면 바뀐 소스의 필드만 씁니다.
         * @param listBinding 화면의 바인딩 식입니다(`UIScreen::getBindings`).
         */
        void update( const vector<UIBindingDesc>& listBinding, const UIBindingContext& context );
        /** @brief 사용자 입력이 위젯 @p widget 의 필드 @p propertyName 을 바꿨을 때 부릅니다. 양방향 바인딩이면 소스에 되씁니다. */
        void onWidgetValueEdited( Widget& widget, const hashed_string& propertyName );
        /** @brief 뷰모델 @p viewModel 이 지워질 때 부릅니다. 바인딩을 해제하고, 다음 `setViewModel` 까지 쉽니다. */
        void onViewModelDestroyed( UIViewModel& viewModel );
        /** @brief 사용자 설정이 바뀌었을 때(변경 통보) 부릅니다. 다음 `update` 가 설정 바인딩을 다시 읽습니다. */
        void onSettingEvent( const UserSettingEvent& event );

        /** @brief 연결된 바인딩 수입니다(오류로 빠진 것은 제외). */
        uint32 getBindingCount() const { return static_cast<uint32>( _listBinding.size() ); }
        /** @brief 마지막으로 연결할 때의 오류입니다(파일, 줄, 식, 이유이며 영어). */
        const vector<string>& getErrors() const { return _listError; }
        /** @brief 위젯 필드에 실제로 쓴 횟수입니다(같은 값이면 세지 않으며, 테스트와 진단에 씁니다). */
        uint32 getWriteCount() const { return _writeCount; }
        /** @brief 이번 `update` 가 비교한 폴링 바인딩 수입니다(프로파일 카운터 UI.PollBindings). */
        uint32 getPolledCount() const { return _polledCount; }

    private:
        /** @struct ActiveBinding @brief 연결된 바인딩 하나입니다. 해석해 둔 식, 위젯 필드 경로, 소스 경로를 가집니다. */
        struct ActiveBinding
        {
            UIBindingExpression _expression;
            UIPropertyPath      _target;       ///< 위젯 기준 필드입니다.
            UIPropertyPath      _source;       ///< 뷰모델 기준 필드입니다.
            UIPropertyPath      _rangeMin;     ///< 설정 바인딩에서 위젯의 `_minValue` 입니다(없으면 무효).
            UIPropertyPath      _rangeMax;     ///< 설정 바인딩에서 위젯의 `_maxValue` 입니다.
            UIPropertyPath      _rangeStep;    ///< 설정 바인딩에서 위젯의 `_step` 입니다.
            UIPropertyPath      _optionList;   ///< 설정 바인딩에서 위젯의 `_listOption`(텍스트 목록)입니다.
            UIBindingConverter  _converter;    ///< 변환기입니다(이름이 비면 없음).
            string              _lastPollText; ///< 폴링에서 지난 프레임에 본 값입니다(텍스트 표기).
            hashed_string       _sourceField;  ///< 뷰모델의 맨 위 필드 이름입니다(알림 단위). 설정 바인딩이면 설정 id 입니다.
            uint64              _skipSerial;   ///< 양방향에서 이 바인딩이 되쓰며 낸 알림 번호입니다. 그 알림으로는 위젯에 다시 쓰지 않습니다.
            WidgetID            _widget;
            bool                _bPollKnown; ///< 폴링에서 `_lastPollText` 가 유효한지 나타냅니다.
            bool                _bDirty;     ///< 설정에서 되쓴 뒤 다시 읽어야 하는지 나타냅니다(보정되거나 거절된 경우).
        };

        /** @brief 식을 모두 해석해 연결합니다. 오류는 `_listError` 와 로그에 남깁니다. */
        void bind( const vector<UIBindingDesc>& listBinding, const UIBindingContext& context );
        /** @brief 식 하나를 해석해 연결합니다. 연결할 수 없으면 false 이고 @p outError 에 이유가 있습니다. 뷰모델이 없어 쉬는 식은 true 이며 연결하지 않습니다. */
        [[nodiscard]] bool bindOne( const UIBindingDesc& desc, const UIBindingContext& context, string& outError );
        /** @brief 설정 바인딩의 소스 종류를 정하고 범위와 선택지 필드를 찾습니다. 설정이 없으면 false 입니다. */
        [[nodiscard]] bool bindSetting( ActiveBinding& binding, const Widget& widget, const UIBindingContext& context, UIBindingValueKind& outSourceKind,
                                        string& outError ) const;
        /** @brief 설정 바인딩의 값을 읽고 범위, 선택지, 사용 가능 여부를 위젯에 씁니다(범위가 값을 제한하므로 값 필드보다 먼저 씁니다). */
        UIBindingValue readSetting( const ActiveBinding& binding, Widget& widget, const UIBindingContext& context );
        /** @brief 사용자 입력을 설정의 보류 값으로 넣습니다. */
        void writeSetting( ActiveBinding& binding, const Widget& widget );
        /** @brief 사용자 설정 변경 통보를 등록합니다(다른 서비스면 기존 구독을 풉니다). */
        void registerSettingsListener( UserSettingsManager* pSettings );
        /** @brief 바인딩 하나의 소스 값을 읽어(변환기와 형식 적용) 위젯 필드에 씁니다. 필드가 바뀌었으면 true 입니다. */
        [[nodiscard]] bool apply( ActiveBinding& binding, const UIBindingContext& context );
        /** @brief 소스 값에 변환기와 형식을 적용합니다. 변환기가 실패하면 false 입니다. */
        [[nodiscard]] bool transform( const ActiveBinding& binding, const UIBindingContext& context, UIBindingValue& inoutValue ) const;
        /** @brief 뷰모델의 관찰자 등록을 해제합니다. */
        void releaseViewModel();

    private:
        vector<ActiveBinding> _listBinding;
        vector<string>        _listError;
        UIScreen*             _pScreen;
        UIViewModel*          _pViewModel;
        Subscription          _viewModelSubscription; ///< 뷰모델의 관찰자 등록입니다. 뷰모델이 지워지면 알림(`onViewModelDestroyed`)이 풉니다
        UserSettingsManager*  _pSettings;             ///< 변경 통보를 등록한 설정 서비스입니다(설정 바인딩이 없으면 nullptr).
        Subscription          _settingsSubscription;  ///< 그 통보의 구독입니다.
        uint64                _seenSerial;            ///< 마지막 `update` 가 본 뷰모델 알림 번호입니다.
        uint32                _writeCount;
        uint32                _polledCount;
        bool                  _bBound;           ///< 식을 해석해 연결했는지 나타냅니다(뷰모델이나 식이 바뀌면 다시 연결합니다).
        bool                  _bApplying;        ///< 위젯 필드에 쓰는 중인지 나타냅니다. 그 사이의 사용자 입력 알림은 되쓰지 않습니다.
        bool                  _bSettingsChanged; ///< 설정 변경 통보를 받았는지 나타냅니다. 받았으면 설정 바인딩을 모두 다시 읽습니다.
    };
} // namespace sw
