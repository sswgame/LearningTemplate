#include "pch.h"

#include "Engine/UI/Binding/UiBindingSet.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Binding/UiViewModel.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    SW_LOG_CALLER( "UiBindingSet" );

    namespace
    {
        struct UiBindingSetInternal
        {
            /** @brief 형식 바인딩이 메시지 패턴에 값을 넣는 인자 이름입니다(`{value}`). */
            static constexpr utf8 kFormatArgumentName[] = "value";
            /** @brief 형식 키가 표에 없을 때의 패턴입니다(값 그대로). */
            static constexpr utf8 kFormatFallbackPattern[] = "{value}";

            /** @brief 값을 형식 인자로 담습니다. */
            static TextArgumentList makeFormatArguments( const UiBindingValue& value )
            {
                TextArgumentList arguments;
                if ( value._kind == UiBindingValueKind::Number && value._bInteger )
                    arguments.addInteger( kFormatArgumentName, static_cast<int64>( value._number ) );
                else if ( value._kind == UiBindingValueKind::Number )
                    arguments.addDecimal( kFormatArgumentName, value._number );
                else
                    arguments.addText( kFormatArgumentName, value.toText() );
                return arguments;
            }

            /** @brief 설정 바인딩이 범위 · 선택지를 채우는 위젯 칸 이름입니다(슬라이더 · 콤보의 PROPERTY — 이름이 같은 위젯이면 모두). */
            static constexpr utf8 kValueProperty[]         = "_value";
            static constexpr utf8 kMinValueProperty[]      = "_minValue";
            static constexpr utf8 kMaxValueProperty[]      = "_maxValue";
            static constexpr utf8 kStepProperty[]          = "_step";
            static constexpr utf8 kSelectedIndexProperty[] = "_selectedIndex";
            static constexpr utf8 kOptionListProperty[]    = "_listOption";

            /** @brief 위젯 타입에 그 칸이 있으면 경로를 풉니다(없으면 무효 경로 — 오류 아님). */
            static UiPropertyPath findOptionalPath( const TypeInfo& type, const utf8* pName )
            {
                UiPropertyPath path{};
                string         ignored;
                if ( UiBindingValueUtil::resolvePath( type, pName, path, ignored ) == false )
                    return UiPropertyPath{};
                return path;
            }

            /** @brief 범위 칸 하나(있으면)에 설정 정의의 숫자를 쓰고, 바뀌었으면 위젯에 알립니다. */
            static void writeRange( const UiPropertyPath& path, Widget& widget, float64 number, bool bInteger )
            {
                if ( path.isValid() && UiBindingValueUtil::writeValue( path, &widget, UiBindingValue::makeNumber( number, bInteger ) ) )
                    widget.onBoundPropertyChanged( path.getRootProperty() );
            }

            /** @brief 선택지의 보일 글입니다(글 키, 없으면 값 — 글 위젯이 키로 푼다). */
            static const string& getOptionText( const UserSettingOption& option ) { return option._textKey.empty() ? option._value : option._textKey; }

            /** @brief 소스 갈래 · 칸 갈래가 묶일 수 있는가 — 같은 Other 타입끼리, 아니면 갈래 변환. */
            static bool canBind( UiBindingValueKind sourceKind, const hashed_string& sourceTypeName, const UiPropertyPath& target )
            {
                if ( sourceKind == UiBindingValueKind::Other && target._kind == UiBindingValueKind::Other )
                    return sourceTypeName == target.getLeafTypeName();
                return UiBindingValueUtil::canConvert( sourceKind, target._kind );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiBindingSet::UiBindingSet( UiScreen& screen )
        : _listBinding{}
        , _listError{}
        , _pScreen{ &screen }
        , _pViewModel{ nullptr }
        , _pSettings{ nullptr }
        , _settingsListener{}
        , _seenSerial{ 0 }
        , _writeCount{ 0 }
        , _polledCount{ 0 }
        , _bBound{ false }
        , _bApplying{ false }
        , _bSettingsChanged{ false }
    {
    }

    UiBindingSet::~UiBindingSet()
    {
        releaseViewModel();
        registerSettingsListener( nullptr );
    }

    void UiBindingSet::registerSettingsListener( UserSettingsManager* pSettings )
    {
        if ( _pSettings == pSettings )
            return;
        if ( _pSettings != nullptr && _settingsListener.isValid() )
            _pSettings->unregisterEventListener( _settingsListener );
        _pSettings        = pSettings;
        _settingsListener = {};
        if ( _pSettings != nullptr )
            _settingsListener = _pSettings->registerEventListener( SW_DELEGATE_METHOD( UserSettingEventListener, &UiBindingSet::onSettingEvent, this ) );
    }

    void UiBindingSet::onSettingEvent( const UserSettingEvent& event )
    {
        (void)event; // 어느 설정이든 — 사용 가능(`enabledWhen`)은 다른 설정에 기대므로 설정 바인딩을 모두 다시 읽는다
        _bSettingsChanged = true;
    }

    void UiBindingSet::setViewModel( UiViewModel* pViewModel )
    {
        if ( _pViewModel == pViewModel )
            return;
        releaseViewModel();
        _pViewModel = pViewModel;
        if ( _pViewModel != nullptr )
            _pViewModel->registerObserver( *this );
        _bBound = false;
    }

    void UiBindingSet::releaseViewModel()
    {
        if ( _pViewModel != nullptr )
            _pViewModel->unregisterObserver( *this );
        _pViewModel = nullptr;
    }

    void UiBindingSet::onViewModelDestroyed( UiViewModel& viewModel )
    {
        if ( _pViewModel != &viewModel )
            return;
        releaseViewModel();
        _bBound = false;
    }

    void UiBindingSet::update( const vector<UiBindingDesc>& listBinding, const UiBindingContext& context )
    {
        _polledCount = 0;
        if ( _bBound == false )
        {
            bind( listBinding, context );
            for ( ActiveBinding& binding : _listBinding )
            {
                (void)apply( binding, context ); // 칸이 바뀌었는지는 쓴 수(`_writeCount`)가 센다
            }
            return;
        }
        const uint64 viewModelSerial = _pViewModel != nullptr ? _pViewModel->getChangeSerial() : 0;
        const bool   bViewModelMoved = viewModelSerial != _seenSerial;
        for ( ActiveBinding& binding : _listBinding )
        {
            switch ( binding._expression._source )
            {
                case UiBindingSource::ViewModel:
                {
                    const bool bReformat = context._bTextRevisionChanged && binding._expression._format.empty() == false;
                    if ( bViewModelMoved == false && bReformat == false )
                        break;
                    const uint64 fieldSerial = _pViewModel->findFieldChangeSerial( binding._sourceField );
                    if ( bReformat || ( fieldSerial > _seenSerial && fieldSerial != binding._skipSerial ) )
                        (void)apply( binding, context ); // 칸이 바뀌었는지는 쓴 수(`_writeCount`)가 센다
                    break;
                }
                case UiBindingSource::Poll:
                {
                    ++_polledCount;
                    const string text      = UiBindingValueUtil::readValue( binding._source, _pViewModel ).toText();
                    const bool   bReformat = context._bTextRevisionChanged && binding._expression._format.empty() == false;
                    if ( binding._bPollKnown && binding._lastPollText == text && bReformat == false )
                        break;
                    (void)apply( binding, context ); // 칸이 바뀌었는지는 쓴 수(`_writeCount`)가 센다
                    break;
                }
                case UiBindingSource::Setting:
                {
                    const bool bReformat = context._bTextRevisionChanged && binding._expression._format.empty() == false;
                    if ( _bSettingsChanged || binding._bDirty || bReformat )
                        (void)apply( binding, context ); // 칸이 바뀌었는지는 쓴 수(`_writeCount`)가 센다
                    break;
                }
            }
        }
        _seenSerial       = viewModelSerial;
        _bSettingsChanged = false;
    }

    void UiBindingSet::bind( const vector<UiBindingDesc>& listBinding, const UiBindingContext& context )
    {
        _listBinding.clear();
        _listError.clear();
        _seenSerial       = _pViewModel != nullptr ? _pViewModel->getChangeSerial() : 0;
        _bSettingsChanged = false;
        for ( const UiBindingDesc& desc : listBinding )
        {
            string error;
            if ( bindOne( desc, context, error ) )
                continue;
            const string& documentPath = _pScreen->getDocumentPath();
            string        message      = documentPath.empty() ? string( "(code)" ) : documentPath;
            message += ":" + to_string( desc._sourceLine ) + ": binding '" + desc._expression + "' on " + desc._propertyPath + ": " + error;
            SW_LOG_ERROR( "[Ui] %#", message.c_str() );
            _listError.push_back( std::move( message ) );
        }
        bool bUsesSettings = false;
        for ( const ActiveBinding& binding : _listBinding )
        {
            bUsesSettings = bUsesSettings || binding._expression._source == UiBindingSource::Setting;
        }
        registerSettingsListener( bUsesSettings ? context._pSettings : nullptr );
        _bBound = true;
    }

    bool UiBindingSet::bindOne( const UiBindingDesc& desc, const UiBindingContext& context, string& outError )
    {
        ActiveBinding binding{};
        binding._widget     = desc._widget;
        binding._skipSerial = 0;
        binding._bPollKnown = false;
        binding._bDirty     = false;
        if ( UiBindingExpression::parse( desc._expression, binding._expression, outError ) == false )
            return false;
        const Widget* pWidget = _pScreen->getTree().findWidgetById( desc._widget );
        if ( pWidget == nullptr || pWidget->getTypeInfo() == nullptr )
        {
            outError = "the widget is not in the screen";
            return false;
        }
        if ( UiBindingValueUtil::resolvePath( *pWidget->getTypeInfo(), desc._propertyPath, binding._target, outError ) == false )
            return false;
        UiBindingValueKind sourceKind     = UiBindingValueKind::None;
        hashed_string      sourceTypeName = {};
        if ( binding._expression._source == UiBindingSource::Setting )
        {
            if ( bindSetting( binding, *pWidget, context, sourceKind, outError ) == false )
                return false;
        }
        else
        {
            // 뷰모델 소스 — 뷰모델이 없으면 쉰다(오류 아님 — `setViewModel` 이 다시 건다).
            if ( _pViewModel == nullptr || _pViewModel->getTypeInfo() == nullptr )
                return true;
            if ( UiBindingValueUtil::resolvePath( *_pViewModel->getTypeInfo(), binding._expression._path, binding._source, outError ) == false )
                return false;
            binding._sourceField = binding._source.getRootProperty()._name;
            sourceKind           = binding._source._kind;
            sourceTypeName       = binding._source.getLeafTypeName();
        }

        if ( binding._expression._converter.empty() == false )
        {
            const UiBindingConverter* pConverter = context._pConverters != nullptr ? context._pConverters->findConverter( binding._expression._converter ) : nullptr;
            if ( pConverter == nullptr )
            {
                outError = "unknown converter '" + string( binding._expression._converter.c_str() ) + "'";
                return false;
            }
            if ( UiBindingValueUtil::canConvert( sourceKind, pConverter->_inputKind ) == false )
            {
                outError = "converter '" + string( pConverter->_name.c_str() ) + "' takes " + UiBindingValueUtil::getKindName( pConverter->_inputKind ) + ", the source is " +
                           UiBindingValueUtil::getKindName( sourceKind );
                return false;
            }
            binding._converter = *pConverter;
            sourceKind         = pConverter->_outputKind;
        }
        if ( binding._expression._format.empty() == false )
            sourceKind = UiBindingValueKind::Text;
        if ( UiBindingSetInternal::canBind( sourceKind, sourceTypeName, binding._target ) == false )
        {
            outError = string( "cannot bind " ) + UiBindingValueUtil::getKindName( sourceKind ) + " '" + sourceTypeName.c_str() + "' to " +
                       UiBindingValueUtil::getKindName( binding._target._kind ) + " '" + binding._target.getLeafTypeName().c_str() + "' without a converter";
            return false;
        }
        if ( binding._expression._mode == UiBindingMode::TwoWay )
        {
            const bool bTransformed  = binding._converter._pConvert != nullptr || binding._expression._format.empty() == false;
            const bool bConvertsBack = binding._expression._source == UiBindingSource::Setting
                                         ? UiBindingValueUtil::canConvert( binding._target._kind, sourceKind )
                                         : UiBindingSetInternal::canBind( binding._target._kind, binding._target.getLeafTypeName(), binding._source );
            if ( bTransformed || bConvertsBack == false )
            {
                outError = "a TwoWay binding needs a field the widget value converts back to (no converter or format)";
                return false;
            }
        }
#if defined( SW_SHIPPING )
        if ( binding._expression._source == UiBindingSource::Poll )
            SW_LOG_WARNING( "[Ui] %#:%#: poll binding '%#' in a shipped build - use {bind:} with field notification", _pScreen->getDocumentPath().c_str(),
                            desc._sourceLine, desc._expression.c_str() );
#endif
        _listBinding.push_back( std::move( binding ) );
        return true;
    }

    bool UiBindingSet::transform( const ActiveBinding& binding, const UiBindingContext& context, UiBindingValue& inoutValue ) const
    {
        if ( binding._converter._pConvert != nullptr )
        {
            UiBindingValue converted{};
            if ( binding._converter._pConvert( inoutValue, converted ) == false )
                return false;
            inoutValue = std::move( converted );
        }
        if ( binding._expression._format.empty() == false )
        {
            const TextArgumentList arguments = UiBindingSetInternal::makeFormatArguments( inoutValue );
            if ( context._pLocalization != nullptr )
                inoutValue = UiBindingValue::makeText( context._pLocalization->getFormattedString( binding._expression._format, arguments,
                                                                                                   UiBindingSetInternal::kFormatFallbackPattern ) );
            else
                inoutValue = UiBindingValue::makeText( inoutValue.toText() );
        }
        return true;
    }

    bool UiBindingSet::bindSetting( ActiveBinding& binding, const Widget& widget, const UiBindingContext& context, UiBindingValueKind& outSourceKind,
                                    string& outError ) const
    {
        if ( context._pSettings == nullptr )
        {
            outError = "no user settings service";
            return false;
        }
        binding._sourceField       = hashed_string( binding._expression._path );
        const UserSettingDef* pDef = context._pSettings->findSetting( binding._sourceField );
        if ( pDef == nullptr )
        {
            outError = "unknown setting '" + binding._expression._path + "'";
            return false;
        }
        const hashed_string& leafName = binding._target.getLeafProperty()._name;
        const TypeInfo&      type     = *widget.getTypeInfo();
        switch ( pDef->_type )
        {
            case UserSettingType::Bool:
            {
                outSourceKind = UiBindingValueKind::Bool;
                break;
            }
            case UserSettingType::Int:
            case UserSettingType::Float:
            {
                outSourceKind = UiBindingValueKind::Number;
                if ( leafName == hashed_string( UiBindingSetInternal::kValueProperty ) )
                {
                    binding._rangeMin  = UiBindingSetInternal::findOptionalPath( type, UiBindingSetInternal::kMinValueProperty );
                    binding._rangeMax  = UiBindingSetInternal::findOptionalPath( type, UiBindingSetInternal::kMaxValueProperty );
                    binding._rangeStep = UiBindingSetInternal::findOptionalPath( type, UiBindingSetInternal::kStepProperty );
                }
                break;
            }
            case UserSettingType::Enum:
            {
                // 콤보의 고른 자리면 선택지 순번, 아니면 보일 글(글 키).
                const bool bIndex = leafName == hashed_string( UiBindingSetInternal::kSelectedIndexProperty ) &&
                                    binding._target._kind == UiBindingValueKind::Number;
                outSourceKind = bIndex ? UiBindingValueKind::Number : UiBindingValueKind::Text;
                if ( bIndex )
                    binding._optionList = UiBindingSetInternal::findOptionalPath( type, UiBindingSetInternal::kOptionListProperty );
                break;
            }
            case UserSettingType::KeyBinding:
            case UserSettingType::String:
            {
                outSourceKind = UiBindingValueKind::Text;
                break;
            }
        }
        return true;
    }

    UiBindingValue UiBindingSet::readSetting( const ActiveBinding& binding, Widget& widget, const UiBindingContext& context )
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
                (void)StringUtil::tryParseBool( text, bValue ); // 값은 정규화되어 있다(true · false)
                return UiBindingValue::makeBool( bValue );
            }
            case UserSettingType::Int:
            case UserSettingType::Float:
            {
                // 범위를 값보다 먼저 쓴다 — 위젯이 값을 범위로 묶는다.
                const bool bInteger = pDef->_type == UserSettingType::Int;
                UiBindingSetInternal::writeRange( binding._rangeMin, widget, pDef->_minValue, bInteger );
                UiBindingSetInternal::writeRange( binding._rangeMax, widget, pDef->_maxValue, bInteger );
                UiBindingSetInternal::writeRange( binding._rangeStep, widget, pDef->_step, bInteger );
                float64 number = 0.0;
                (void)StringUtil::parseDouble( text, number ); // 값은 정규화되어 있다(가장 짧은 왕복 표기)
                return UiBindingValue::makeNumber( number, bInteger );
            }
            case UserSettingType::Enum:
            {
                vector<UserSettingOption> listOption;
                pSettings->collectOptions( binding._sourceField, listOption );
                if ( binding._target._kind != UiBindingValueKind::Number )
                {
                    for ( const UserSettingOption& option : listOption )
                    {
                        if ( option._value == text )
                            return UiBindingValue::makeText( UiBindingSetInternal::getOptionText( option ) );
                    }
                    return UiBindingValue::makeText( text );
                }
                // 선택지 목록(글 키)을 고른 자리보다 먼저 쓴다.
                if ( binding._optionList.isValid() && binding._optionList.getLeafProperty()._elementTypeName == hashed_string( "string" ) )
                {
                    vector<string>* pList = binding._optionList.getLeafProperty().getValuePtr<vector<string>>( binding._optionList.findLeafOwner( &widget ) );
                    vector<string>  listText;
                    for ( const UserSettingOption& option : listOption )
                    {
                        listText.push_back( UiBindingSetInternal::getOptionText( option ) );
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
                        return UiBindingValue::makeNumber( index, true );
                }
                return UiBindingValue::makeNumber( static_cast<float64>( invalid_index::kUint32 ), true );
            }
            case UserSettingType::KeyBinding:
            case UserSettingType::String:
            {
                return UiBindingValue::makeText( text );
            }
        }
        return {};
    }

    void UiBindingSet::writeSetting( ActiveBinding& binding, const Widget& widget )
    {
        const UserSettingDef* pDef = _pSettings != nullptr ? _pSettings->findSetting( binding._sourceField ) : nullptr;
        if ( pDef == nullptr )
            return;
        const UiBindingValue value = UiBindingValueUtil::readValue( binding._target, &widget );
        // 결과는 보지 않는다 — 고쳐 받았든(Clamped) 거절됐든(Rejected · Disabled) 다음 단계가 설정 값을 다시 읽어 위젯에 쓴다.
        binding._bDirty = true;
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
                if ( value._kind != UiBindingValueKind::Number )
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

    bool UiBindingSet::apply( ActiveBinding& binding, const UiBindingContext& context )
    {
        Widget*    pWidget  = _pScreen->getTree().findWidgetById( binding._widget );
        const bool bSetting = binding._expression._source == UiBindingSource::Setting;
        if ( pWidget == nullptr || ( bSetting == false && _pViewModel == nullptr ) )
            return false;
        _bApplying           = true; // 범위 · 선택지 · 사용 가능도 쓴다
        binding._bDirty      = false;
        UiBindingValue value = bSetting ? readSetting( binding, *pWidget, context ) : UiBindingValueUtil::readValue( binding._source, _pViewModel );
        _bApplying           = false;
        if ( binding._expression._source == UiBindingSource::Poll )
        {
            binding._lastPollText = value.toText();
            binding._bPollKnown   = true;
        }
        if ( transform( binding, context, value ) == false )
            return false;
        if ( value._kind == UiBindingValueKind::Other && binding._target._kind == UiBindingValueKind::Text )
            value._kind = UiBindingValueKind::Text;
        _bApplying          = true;
        const bool bChanged = UiBindingValueUtil::writeValue( binding._target, pWidget, value );
        if ( bChanged )
        {
            ++_writeCount;
            pWidget->onBoundPropertyChanged( binding._target.getRootProperty() );
        }
        _bApplying = false;
        return bChanged;
    }

    void UiBindingSet::onWidgetValueEdited( Widget& widget, const hashed_string& propertyName )
    {
        if ( _bApplying )
            return;
        for ( ActiveBinding& binding : _listBinding )
        {
            const bool bMatches = binding._widget == widget.getId() && binding._expression._mode == UiBindingMode::TwoWay &&
                                  binding._target.getRootProperty()._name == propertyName;
            if ( bMatches == false )
                continue;
            if ( binding._expression._source == UiBindingSource::Setting )
            {
                writeSetting( binding, widget );
                continue;
            }
            if ( _pViewModel == nullptr )
                continue;
            if ( UiBindingValueUtil::copyValue( binding._target, &widget, binding._source, _pViewModel ) == false )
                continue;
            _pViewModel->notifyFieldChanged( binding._sourceField );
            binding._skipSerial = _pViewModel->getChangeSerial();
        }
    }
} // namespace sw
