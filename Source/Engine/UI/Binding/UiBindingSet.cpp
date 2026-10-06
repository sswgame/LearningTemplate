#include "pch.h"

#include "Engine/UI/Binding/UiBindingSet.h"

#include "Core/Log/Logger.h"

#include "Engine/Localization/LocalizationManager.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Binding/UiViewModel.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Screen/UiScreen.h"

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
        , _seenSerial{ 0 }
        , _writeCount{ 0 }
        , _polledCount{ 0 }
        , _bBound{ false }
        , _bApplying{ false }
    {
    }

    UiBindingSet::~UiBindingSet()
    {
        releaseViewModel();
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
                (void)apply( binding, context ); // 칸이 바뀌었는지는 쓴 수(`_writeCount`)가 센다
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
                    if ( bViewModelMoved == false )
                        break;
                    const uint64 fieldSerial = _pViewModel->findFieldChangeSerial( binding._sourceField );
                    if ( fieldSerial > _seenSerial && fieldSerial != binding._skipSerial )
                        (void)apply( binding, context ); // 칸이 바뀌었는지는 쓴 수(`_writeCount`)가 센다
                    break;
                }
                case UiBindingSource::Poll:
                {
                    ++_polledCount;
                    const string text = UiBindingValueUtil::readValue( binding._source, _pViewModel ).toText();
                    if ( binding._bPollKnown && binding._lastPollText == text )
                        break;
                    (void)apply( binding, context ); // 칸이 바뀌었는지는 쓴 수(`_writeCount`)가 센다
                    break;
                }
                case UiBindingSource::Setting:
                {
                    break;
                }
            }
        }
        _seenSerial = viewModelSerial;
    }

    void UiBindingSet::bind( const vector<UiBindingDesc>& listBinding, const UiBindingContext& context )
    {
        _listBinding.clear();
        _listError.clear();
        _seenSerial = _pViewModel != nullptr ? _pViewModel->getChangeSerial() : 0;
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
        _bBound = true;
    }

    bool UiBindingSet::bindOne( const UiBindingDesc& desc, const UiBindingContext& context, string& outError )
    {
        ActiveBinding binding{};
        binding._widget     = desc._widget;
        binding._skipSerial = 0;
        binding._bPollKnown = false;
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
        if ( binding._expression._source == UiBindingSource::Setting )
        {
            outError = "setting bindings are not available";
            return false;
        }
        // 뷰모델 소스 — 뷰모델이 없으면 쉰다(오류 아님 — `setViewModel` 이 다시 건다).
        if ( _pViewModel == nullptr || _pViewModel->getTypeInfo() == nullptr )
            return true;
        if ( UiBindingValueUtil::resolvePath( *_pViewModel->getTypeInfo(), binding._expression._path, binding._source, outError ) == false )
            return false;
        binding._sourceField = binding._source.getRootProperty()._name;

        UiBindingValueKind sourceKind = binding._source._kind;
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
        if ( UiBindingSetInternal::canBind( sourceKind, binding._source.getLeafTypeName(), binding._target ) == false )
        {
            outError = string( "cannot bind " ) + UiBindingValueUtil::getKindName( sourceKind ) + " '" + binding._source.getLeafTypeName().c_str() + "' to " +
                       UiBindingValueUtil::getKindName( binding._target._kind ) + " '" + binding._target.getLeafTypeName().c_str() + "' without a converter";
            return false;
        }
        if ( binding._expression._mode == UiBindingMode::TwoWay )
        {
            const bool bTransformed = binding._converter._pConvert != nullptr || binding._expression._format.empty() == false;
            if ( bTransformed || UiBindingSetInternal::canBind( binding._target._kind, binding._target.getLeafTypeName(), binding._source ) == false )
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

    bool UiBindingSet::apply( ActiveBinding& binding, const UiBindingContext& context )
    {
        Widget* pWidget = _pScreen->getTree().findWidgetById( binding._widget );
        if ( pWidget == nullptr || _pViewModel == nullptr )
            return false;
        UiBindingValue value = UiBindingValueUtil::readValue( binding._source, _pViewModel );
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
        if ( _bApplying || _pViewModel == nullptr )
            return;
        for ( ActiveBinding& binding : _listBinding )
        {
            const bool bMatches = binding._widget == widget.getId() && binding._expression._mode == UiBindingMode::TwoWay &&
                                  binding._target.getRootProperty()._name == propertyName;
            if ( bMatches == false || binding._expression._source == UiBindingSource::Setting )
                continue;
            if ( UiBindingValueUtil::copyValue( binding._target, &widget, binding._source, _pViewModel ) == false )
                continue;
            _pViewModel->notifyFieldChanged( binding._sourceField );
            binding._skipSerial = _pViewModel->getChangeSerial();
        }
    }
} // namespace sw
