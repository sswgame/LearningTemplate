#include "pch.h"

#include "Engine/UI/Binding/UIBindingSet.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    namespace
    {
        struct UIBindingSetInternal
        {
            /** @brief 형식 바인딩이 메시지 패턴에 값을 넣는 인자 이름입니다(`{value}`). */
            static constexpr utf8 kFormatArgumentName[] = "value";
            /** @brief 형식 키가 테이블에 없을 때의 패턴입니다(값 그대로). */
            static constexpr utf8 kFormatFallbackPattern[] = "{value}";

            /** @brief 값을 형식 인자로 담습니다. */
            static TextArgumentList makeFormatArguments( const UIBindingValue& value )
            {
                TextArgumentList arguments;
                if ( value._kind == UIBindingValueKind::Number && value._bInteger )
                    arguments.addInteger( kFormatArgumentName, static_cast<int64>( value._number ) );
                else if ( value._kind == UIBindingValueKind::Number )
                    arguments.addDecimal( kFormatArgumentName, value._number );
                else
                    arguments.addText( kFormatArgumentName, value.toText() );
                return arguments;
            }

            /** @brief 설정 바인딩이 범위와 선택지를 채우는 위젯 필드 이름입니다(슬라이더와 콤보의 PROPERTY 이며, 이름이 같은 위젯이면 모두 해당합니다). */
            static constexpr utf8 kValueProperty[]         = "_value";
            static constexpr utf8 kMinValueProperty[]      = "_minValue";
            static constexpr utf8 kMaxValueProperty[]      = "_maxValue";
            static constexpr utf8 kStepProperty[]          = "_step";
            static constexpr utf8 kSelectedIndexProperty[] = "_selectedIndex";
            static constexpr utf8 kOptionListProperty[]    = "_listOption";

            /** @brief 위젯 타입에 그 필드가 있으면 경로를 해석합니다(없으면 무효 경로이며 오류가 아닙니다). */
            static UIPropertyPath findOptionalPath( const TypeInfo& type, const utf8* pName )
            {
                UIPropertyPath path{};
                if ( UIBindingValue::resolvePath( type, pName, path ).has_value() == false )
                    path = UIPropertyPath{};
                return path;
            }

            /** @brief 범위 필드 하나(있으면)에 설정 정의의 숫자를 쓰고, 바뀌었으면 위젯에 알립니다. */
            static void writeRange( const UIPropertyPath& path, Widget& widget, float64 number, bool bInteger )
            {
                if ( path.isValid() && UIBindingValue::writeValue( path, &widget, UIBindingValue::makeNumber( number, bInteger ) ) )
                    widget.onBoundPropertyChanged( path.getRootProperty() );
            }

            /** @brief 선택지에 보일 텍스트입니다(텍스트 키가 있으면 키, 없으면 값이며, 텍스트 위젯이 키로 해석합니다). */
            static const string& getOptionText( const UserSettingOption& option ) { return option._textKey.empty() ? option._value : option._textKey; }

            /** @brief 소스 종류와 필드 종류를 연결할 수 있는지 판정합니다. 같은 Other 타입끼리 연결하거나, 종류 변환이 가능해야 합니다. */
            static bool canBind( UIBindingValueKind sourceKind, const hashed_string& sourceTypeName, const UIPropertyPath& target )
            {
                if ( sourceKind == UIBindingValueKind::Other && target._kind == UIBindingValueKind::Other )
                    return sourceTypeName == target.getLeafTypeName();
                return UIBindingValue::canConvert( sourceKind, target._kind );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIBindingSet::UIBindingSet( UIScreen& screen )
        : _listBinding{}
        , _listError{}
        , _pScreen{ &screen }
        , _pViewModel{ nullptr }
        , _viewModelSubscription{}
        , _pSettings{ nullptr }
        , _settingsSubscription{}
        , _seenSerial{ 0 }
        , _writeCount{ 0 }
        , _polledCount{ 0 }
        , _bBound{ SW_FALSE }
        , _bApplying{ SW_FALSE }
        , _bSettingsChanged{ SW_FALSE }
    {
    }

    UIBindingSet::~UIBindingSet()
    {
        releaseViewModel();
        registerSettingsListener( nullptr );
    }

    void UIBindingSet::registerSettingsListener( UserSettingsManager* pSettings )
    {
        if ( _pSettings == pSettings )
            return;
        _settingsSubscription.reset();
        _pSettings = pSettings;
        if ( _pSettings != nullptr )
            _settingsSubscription = _pSettings->registerEventListener( SW_DELEGATE_METHOD( UserSettingEventListener, &UIBindingSet::onSettingEvent, this ) );
    }

    void UIBindingSet::onSettingEvent( const UserSettingEvent& event )
    {
        (void)event; // 어느 설정이든 상관없이 처리한다. 사용 가능 여부(`enabledWhen`)는 다른 설정에 의존하므로 설정 바인딩을 모두 다시 읽는다
        _bSettingsChanged = SW_TRUE;
    }

    void UIBindingSet::setViewModel( UIViewModel* pViewModel )
    {
        if ( _pViewModel == pViewModel )
            return;
        releaseViewModel();
        _pViewModel = pViewModel;
        if ( _pViewModel != nullptr )
            _viewModelSubscription = _pViewModel->registerObserver( *this );
        _bBound = SW_FALSE;
    }

    void UIBindingSet::releaseViewModel()
    {
        _viewModelSubscription.reset();
        _pViewModel = nullptr;
    }

    void UIBindingSet::onViewModelDestroyed( UIViewModel& viewModel )
    {
        if ( _pViewModel != &viewModel )
            return;
        releaseViewModel();
        _bBound = SW_FALSE;
    }

    void UIBindingSet::update( const vector<UIBindingDef>& listBinding, const UIBindingContext& context )
    {
        _polledCount = 0;
        if ( _bBound == false )
        {
            bind( listBinding, context );
            for ( ActiveBinding& binding : _listBinding )
            {
                (void)apply( binding, context ); // 필드가 바뀌었는지는 쓴 횟수(`_writeCount`)로 센다
            }
            return;
        }
        const uint64 viewModelSerial = _pViewModel != nullptr ? _pViewModel->getChangeSerial() : 0;
        const bool   bViewModelMoved = viewModelSerial != _seenSerial;
        for ( ActiveBinding& binding : _listBinding )
        {
            switch ( binding._expression._source )
            {
                case UIBindingSource::ViewModel:
                {
                    const bool bReformat = context._bTextRevisionChanged && binding._expression._format.empty() == false;
                    if ( bViewModelMoved == false && bReformat == false )
                        break;
                    const uint64 fieldSerial = _pViewModel->findFieldChangeSerial( binding._sourceField );
                    if ( bReformat || ( fieldSerial > _seenSerial && fieldSerial != binding._skipSerial ) )
                        (void)apply( binding, context ); // 필드가 바뀌었는지는 쓴 횟수(`_writeCount`)로 센다
                    break;
                }
                case UIBindingSource::Poll:
                {
                    ++_polledCount;
                    const UIBindingValue polled         = UIBindingValue::readValue( binding._source, _pViewModel ); // 값으로 비교한다 — 대부분 프레임은 그대로이고 글은 쓸 때만 필요하다
                    const bool           bReformat      = context._bTextRevisionChanged && binding._expression._format.empty() == false;
                    const bool           bPollUnchanged = binding._bPollKnown && binding._lastPollValue.isSameValue( polled ) && bReformat == false;
                    if ( bPollUnchanged )
                        break;
                    (void)apply( binding, context ); // 필드가 바뀌었는지는 쓴 횟수(`_writeCount`)로 센다
                    break;
                }
                case UIBindingSource::Setting:
                {
                    const bool bReformat   = context._bTextRevisionChanged && binding._expression._format.empty() == false;
                    const bool bNeedsApply = _bSettingsChanged || binding._bDirty || bReformat;
                    if ( bNeedsApply )
                        (void)apply( binding, context ); // 필드가 바뀌었는지는 쓴 횟수(`_writeCount`)로 센다
                    break;
                }
            }
        }
        _seenSerial       = viewModelSerial;
        _bSettingsChanged = SW_FALSE;
    }

    void UIBindingSet::bind( const vector<UIBindingDef>& listBinding, const UIBindingContext& context )
    {
        _listBinding.clear();
        _listError.clear();
        _seenSerial       = _pViewModel != nullptr ? _pViewModel->getChangeSerial() : 0;
        _bSettingsChanged = SW_FALSE;
        for ( const UIBindingDef& desc : listBinding )
        {
            const expected<void> bound = bindOne( desc, context );
            if ( bound.has_value() )
                continue;
            const string&                            documentPath = _pScreen->getDocumentPath();
            string                                   message      = documentPath.empty() ? string( "(code)" ) : documentPath;
            StringBuilder<constant::buffer::kMax256> messagePart;
            messagePart.appendFormat( ":%#: binding '%#' on %#: %# '%#'", desc._sourceLine, desc._expression, desc._propertyPath, bound.error().message(), bound.error().detail() );
            message += messagePart.c_str();
            SW_LOG_ERROR_ID( "UIBindingSet.BindingInvalid", "[UI] %#", message.c_str() );
            _listError.push_back( std::move( message ) );
        }
        bool bUsesSettings = false;
        for ( const ActiveBinding& binding : _listBinding )
        {
            bUsesSettings = bUsesSettings || binding._expression._source == UIBindingSource::Setting;
        }
        registerSettingsListener( bUsesSettings ? context._pSettings : nullptr );
        _bBound = SW_TRUE;
    }

    expected<void> UIBindingSet::bindOne( const UIBindingDef& desc, const UIBindingContext& context )
    {
        ActiveBinding binding{};
        binding._widget     = desc._widget;
        binding._skipSerial = 0;
        binding._bPollKnown = false;
        binding._bDirty     = SW_FALSE;
        if ( expected<void> parsed = UIBindingExpression::parse( desc._expression, binding._expression ); parsed.has_value() == false )
            return parsed;
        const Widget* pWidget = _pScreen->getTree().findWidgetByID( desc._widget );
        if ( pWidget == nullptr || pWidget->getTypeInfo() == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "the widget is not in the screen" } );
        if ( expected<void> resolved = UIBindingValue::resolvePath( *pWidget->getTypeInfo(), desc._propertyPath, binding._target ); resolved.has_value() == false )
            return resolved;
        UIBindingValueKind sourceKind     = UIBindingValueKind::None;
        hashed_string      sourceTypeName = {};
        if ( binding._expression._source == UIBindingSource::Setting )
        {
            if ( expected<void> setting = bindSetting( binding, *pWidget, context, sourceKind ); setting.has_value() == false )
                return setting;
        }
        else
        {
            // 뷰모델 소스는 뷰모델이 없으면 쉰다(오류가 아니며, `setViewModel` 이 다시 연결한다).
            if ( _pViewModel == nullptr || _pViewModel->getTypeInfo() == nullptr )
                return {};
            if ( expected<void> resolved = UIBindingValue::resolvePath( *_pViewModel->getTypeInfo(), binding._expression._path, binding._source ); resolved.has_value() == false )
                return resolved;
            binding._sourceField = binding._source.getRootProperty()._name;
            sourceKind           = binding._source._kind;
            sourceTypeName       = binding._source.getLeafTypeName();
        }

        if ( binding._expression._converter.empty() == false )
        {
            const UIBindingConverter* pConverter = context._pConverters != nullptr ? context._pConverters->findConverter( binding._expression._converter ) : nullptr;
            if ( pConverter == nullptr )
                return unexpected( Error{ ErrorKind::NotFound, "unknown converter", binding._expression._converter.c_str() } );
            if ( UIBindingValue::canConvert( sourceKind, pConverter->_inputKind ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the converter does not take the source kind", pConverter->_name.c_str() }
                                       .appendDetail( " takes " )
                                       .appendDetail( UIBindingValue::getKindName( pConverter->_inputKind ) )
                                       .appendDetail( ", the source is " )
                                       .appendDetail( UIBindingValue::getKindName( sourceKind ) ) );
            binding._converter = *pConverter;
            sourceKind         = pConverter->_outputKind;
        }
        if ( binding._expression._format.empty() == false )
            sourceKind = UIBindingValueKind::Text;
        if ( UIBindingSetInternal::canBind( sourceKind, sourceTypeName, binding._target ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "cannot bind the source to the field without a converter", UIBindingValue::getKindName( sourceKind ) }
                                   .appendDetail( " '" )
                                   .appendDetail( sourceTypeName.c_str() )
                                   .appendDetail( "' to " )
                                   .appendDetail( UIBindingValue::getKindName( binding._target._kind ) )
                                   .appendDetail( " '" )
                                   .appendDetail( binding._target.getLeafTypeName().c_str() )
                                   .appendDetail( "'" ) );
        if ( binding._expression._mode == UIBindingMode::TwoWay )
        {
            const bool bTransformed  = binding._converter._pConvert != nullptr || binding._expression._format.empty() == false;
            const bool bConvertsBack = binding._expression._source == UIBindingSource::Setting
                                         ? UIBindingValue::canConvert( binding._target._kind, sourceKind )
                                         : UIBindingSetInternal::canBind( binding._target._kind, binding._target.getLeafTypeName(), binding._source );
            if ( bTransformed || bConvertsBack == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "a TwoWay binding needs a field the widget value converts back to (no converter or format)" } );
        }
        if ( build::kShipping && binding._expression._source == UIBindingSource::Poll )
            SW_LOG_WARNING_ID( "UIBindingSet.PollBindingShippedBuild", "[UI] %#:%#: poll binding '%#' in a shipped build - use {bind:} with field notification", _pScreen->getDocumentPath().c_str(),
                               desc._sourceLine, desc._expression.c_str() );
        _listBinding.push_back( std::move( binding ) );
        return {};
    }

    bool UIBindingSet::transform( const ActiveBinding& binding, const UIBindingContext& context, UIBindingValue& inoutValue ) const
    {
        if ( binding._converter._pConvert != nullptr )
        {
            UIBindingValue converted{};
            if ( binding._converter._pConvert( inoutValue, converted ) == false )
                return false;
            inoutValue = std::move( converted );
        }
        if ( binding._expression._format.empty() == false )
        {
            const TextArgumentList arguments = UIBindingSetInternal::makeFormatArguments( inoutValue );
            if ( context._pLocalization != nullptr )
                inoutValue = UIBindingValue::makeText( context._pLocalization->getFormattedString( binding._expression._format, arguments,
                                                                                                   UIBindingSetInternal::kFormatFallbackPattern ) );
            else
                inoutValue = UIBindingValue::makeText( inoutValue.toText() );
        }
        return true;
    }

    expected<void> UIBindingSet::bindSetting( ActiveBinding& binding, const Widget& widget, const UIBindingContext& context, UIBindingValueKind& outSourceKind ) const
    {
        if ( context._pSettings == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "no user settings service" } );
        binding._sourceField       = hashed_string( binding._expression._path );
        const UserSettingDef* pDef = context._pSettings->findSetting( binding._sourceField );
        if ( pDef == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "unknown setting", binding._expression._path } );
        const hashed_string& leafName = binding._target.getLeafProperty()._name;
        const TypeInfo&      type     = *widget.getTypeInfo();
        switch ( pDef->_type )
        {
            case UserSettingType::Bool:
            {
                outSourceKind = UIBindingValueKind::Bool;
                break;
            }
            case UserSettingType::Int:
            case UserSettingType::Float:
            {
                outSourceKind = UIBindingValueKind::Number;
                if ( leafName == hashed_string( UIBindingSetInternal::kValueProperty ) )
                {
                    binding._rangeMin  = UIBindingSetInternal::findOptionalPath( type, UIBindingSetInternal::kMinValueProperty );
                    binding._rangeMax  = UIBindingSetInternal::findOptionalPath( type, UIBindingSetInternal::kMaxValueProperty );
                    binding._rangeStep = UIBindingSetInternal::findOptionalPath( type, UIBindingSetInternal::kStepProperty );
                }
                break;
            }
            case UserSettingType::Enum:
            {
                // 콤보에서 고른 항목이면 선택지 순번을, 아니면 보일 텍스트(텍스트 키)를 쓴다.
                const bool bIndex = leafName == hashed_string( UIBindingSetInternal::kSelectedIndexProperty ) &&
                                    binding._target._kind == UIBindingValueKind::Number;
                outSourceKind = bIndex ? UIBindingValueKind::Number : UIBindingValueKind::Text;
                if ( bIndex )
                    binding._optionList = UIBindingSetInternal::findOptionalPath( type, UIBindingSetInternal::kOptionListProperty );
                break;
            }
            case UserSettingType::KeyBinding:
            case UserSettingType::String:
            {
                outSourceKind = UIBindingValueKind::Text;
                break;
            }
        }
        return {};
    }

    UIBindingValue UIBindingSet::readSetting( const ActiveBinding& binding, Widget& widget, const UIBindingContext& context )
    {
        UserSettingsManager*  pSettings = context._pSettings;
        const UserSettingDef* pDef      = pSettings != nullptr ? pSettings->findSetting( binding._sourceField ) : nullptr;
        if ( pDef == nullptr )
            return {};
        widget.setEnabled( pSettings->isSettingEnabled( binding._sourceField ) );
        const string_view text = pSettings->getValue( binding._sourceField );
        switch ( pDef->_type )
        {
            case UserSettingType::Bool:
            {
                bool bValue = false;
                (void)StringUtil::tryParseBool( text, bValue ); // 값은 정규화되어 있다(true, false)
                return UIBindingValue::makeBool( bValue );
            }
            case UserSettingType::Int:
            case UserSettingType::Float:
            {
                // 범위를 값보다 먼저 쓴다. 위젯이 값을 범위로 제한하기 때문이다.
                const bool bInteger = pDef->_type == UserSettingType::Int;
                UIBindingSetInternal::writeRange( binding._rangeMin, widget, pDef->_minValue, bInteger );
                UIBindingSetInternal::writeRange( binding._rangeMax, widget, pDef->_maxValue, bInteger );
                UIBindingSetInternal::writeRange( binding._rangeStep, widget, pDef->_step, bInteger );
                float64 number = 0.0;
                (void)StringUtil::parseDouble( text, number ); // 값은 정규화되어 있다(가장 짧은 왕복 표기)
                return UIBindingValue::makeNumber( number, bInteger );
            }
            case UserSettingType::Enum:
            {
                vector<UserSettingOption> listOption;
                pSettings->collectOptions( binding._sourceField, listOption );
                if ( binding._target._kind != UIBindingValueKind::Number )
                {
                    for ( const UserSettingOption& option : listOption )
                    {
                        if ( option._value == text )
                            return UIBindingValue::makeText( UIBindingSetInternal::getOptionText( option ) );
                    }
                    return UIBindingValue::makeText( text );
                }
                // 선택지 목록(텍스트 키)을 고른 항목보다 먼저 쓴다.
                // 원소 타입은 캐스트할 C++ 타입의 내장 자리(`ReflectBuiltinOf<string>`)로 묻는다 — 이름 리터럴을 두지 않는다.
                const bool bTextOptionList = binding._optionList.isValid() &&
                                             ReflectedValueUtil::findBuiltinIndex( binding._optionList.getLeafProperty()._elementTypeName ) == ReflectBuiltinOf<string>::kIndex;
                if ( bTextOptionList )
                {
                    vector<string>* pList = binding._optionList.getLeafProperty().getValuePtr<vector<string>>( binding._optionList.findLeafOwner( &widget ) );
                    vector<string>  listText;
                    for ( const UserSettingOption& option : listOption )
                    {
                        listText.push_back( UIBindingSetInternal::getOptionText( option ) );
                    }
                    if ( pList != nullptr && *pList != listText )
                    {
                        *pList = std::move( listText );
                        widget.onBoundPropertyChanged( binding._optionList.getRootProperty() );
                    }
                }
                for ( uint32 index = 0; index < static_cast<uint32>( listOption.size() ); ++index )
                {
                    if ( listOption[index]._value == text )
                        return UIBindingValue::makeNumber( index, true );
                }
                return UIBindingValue::makeNumber( static_cast<float64>( constant::invalid_index::kUint32 ), true );
            }
            case UserSettingType::KeyBinding:
            case UserSettingType::String:
            {
                return UIBindingValue::makeText( text );
            }
        }
        return {};
    }

    void UIBindingSet::writeSetting( ActiveBinding& binding, const Widget& widget )
    {
        const UserSettingDef* pDef = _pSettings != nullptr ? _pSettings->findSetting( binding._sourceField ) : nullptr;
        if ( pDef == nullptr )
            return;
        const UIBindingValue value = UIBindingValue::readValue( binding._target, &widget );
        // 결과는 보지 않는다. 보정되었든(Clamped) 거절됐든(Rejected, Disabled) 다음 단계가 설정 값을 다시 읽어 위젯에 쓴다.
        binding._bDirty = SW_TRUE;
        switch ( pDef->_type )
        {
            case UserSettingType::Bool:
            {
                (void)_pSettings->setPendingBoolValue( binding._sourceField, value.toBool() );
                break;
            }
            case UserSettingType::Int:
            {
                (void)_pSettings->setPendingIntValue( binding._sourceField, static_cast<int32>( MathUtil::round( value.toNumber() ) ) );
                break;
            }
            case UserSettingType::Float:
            {
                (void)_pSettings->setPendingFloatValue( binding._sourceField, static_cast<float32>( value.toNumber() ) );
                break;
            }
            case UserSettingType::Enum:
            {
                if ( value._kind != UIBindingValueKind::Number )
                {
                    (void)_pSettings->setPendingValue( binding._sourceField, value.toText() );
                    break;
                }
                vector<UserSettingOption> listOption;
                _pSettings->collectOptions( binding._sourceField, listOption );
                const float64 index = value.toNumber();
                if ( 0.0 <= index && index < static_cast<float64>( listOption.size() ) )
                    (void)_pSettings->setPendingValue( binding._sourceField, listOption[static_cast<uint32>( index )]._value );
                break;
            }
            case UserSettingType::KeyBinding:
            case UserSettingType::String:
            {
                (void)_pSettings->setPendingValue( binding._sourceField, value.toText() );
                break;
            }
        }
    }

    bool UIBindingSet::apply( ActiveBinding& binding, const UIBindingContext& context )
    {
        Widget*    pWidget  = _pScreen->getTree().findWidgetByID( binding._widget );
        const bool bSetting = binding._expression._source == UIBindingSource::Setting;
        if ( pWidget == nullptr || ( bSetting == false && _pViewModel == nullptr ) )
            return false;
        _bApplying           = SW_TRUE; // 범위, 선택지, 사용 가능 여부도 쓴다
        binding._bDirty      = SW_FALSE;
        UIBindingValue value = bSetting ? readSetting( binding, *pWidget, context ) : UIBindingValue::readValue( binding._source, _pViewModel );
        _bApplying           = SW_FALSE;
        if ( binding._expression._source == UIBindingSource::Poll )
        {
            binding._lastPollValue = value;
            binding._bPollKnown    = true;
        }
        if ( transform( binding, context, value ) == false )
            return false;
        if ( value._kind == UIBindingValueKind::Other && binding._target._kind == UIBindingValueKind::Text )
            value._kind = UIBindingValueKind::Text;
        _bApplying          = SW_TRUE;
        const bool bChanged = UIBindingValue::writeValue( binding._target, pWidget, value );
        if ( bChanged )
        {
            ++_writeCount;
            pWidget->onBoundPropertyChanged( binding._target.getRootProperty() );
        }
        _bApplying = SW_FALSE;
        return bChanged;
    }

    void UIBindingSet::onWidgetValueEdited( Widget& widget, const hashed_string& propertyName )
    {
        if ( _bApplying )
            return;
        for ( ActiveBinding& binding : _listBinding )
        {
            const bool bMatches = binding._widget == widget.getID() && binding._expression._mode == UIBindingMode::TwoWay &&
                                  binding._target.isValid() && binding._target.getRootProperty()._name == propertyName; // 낡은 경로(타입 재등록)는 맞추지 않는다
            if ( bMatches == false )
                continue;
            if ( binding._expression._source == UIBindingSource::Setting )
            {
                writeSetting( binding, widget );
                continue;
            }
            if ( _pViewModel == nullptr )
                continue;
            if ( UIBindingValue::copyValue( binding._target, &widget, binding._source, _pViewModel ) == false )
                continue;
            _pViewModel->notifyFieldChanged( binding._sourceField );
            binding._skipSerial = _pViewModel->getChangeSerial();
        }
    }
} // namespace sw
