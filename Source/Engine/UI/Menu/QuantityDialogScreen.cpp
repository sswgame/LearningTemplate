#include "pch.h"

#include "Engine/UI/Menu/QuantityDialogScreen.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/SliderWidget.h"

namespace sw
{
    QuantityDialogViewModel::QuantityDialogViewModel()
        : UIViewModel{}
        , _title{}
        , _count{ 0 }
    {
    }

    QuantityDialogViewModel::~QuantityDialogViewModel() = default;

    const TypeInfo* QuantityDialogViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void QuantityDialogViewModel::setTitle( const string& title )
    {
        (void)setField( _title, title, "_title" );
    }

    void QuantityDialogViewModel::setCount( int32 count )
    {
        (void)setField( _count, count, "_count" );
    }

    QuantityDialogScreen::QuantityDialogScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _viewModel{}
        , _onResult{}
        , _maxCount{ 1 }
        , _initialCount{ 1 }
        , _bFinished{ SW_FALSE }
    {
        setViewModel( &_viewModel );
    }

    QuantityDialogScreen::~QuantityDialogScreen()
    {
        setViewModel( nullptr );
    }

    UIScreenHandle QuantityDialogScreen::open( UISystem& ui, const string& title, int32 maxCount, int32 initialCount, const ResultDelegate& onResult )
    {
        if ( maxCount < 1 || title.empty() || onResult.isBound() == false )
        {
            SW_LOG_ERROR_ID( "QuantityDialogScreen.QuantityDialogNeedsTitle", "Quantity dialog needs a title, a maximum of at least 1 and a result handler - '%#' (max %#) was not opened",
                             title.c_str(), maxCount );
            return kInvalidUIScreenHandle;
        }
        const UIScreenHandle handle  = ui.openScreen<QuantityDialogScreen>( ui.getEngineDocuments()._quantityDialog );
        UIScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUIScreenHandle; // 문서를 열지 못했다 — openScreen 이 까닭을 남겼다
        static_cast<QuantityDialogScreen&>( *pScreen ).present( title, maxCount, initialCount, onResult );
        return handle;
    }

    void QuantityDialogScreen::present( const string& title, int32 maxCount, int32 initialCount, const ResultDelegate& onResult )
    {
        if ( _bFinished == SW_TRUE )
            return;
        _maxCount     = MathUtil::max( maxCount, 1 );
        _initialCount = MathUtil::clamp( initialCount, 1, _maxCount );
        _onResult     = onResult;
        _viewModel.setTitle( title );
        _viewModel.setCount( _initialCount );
        SliderWidget* pAmount = getTree().findWidget<SliderWidget>( hashed_string( kAmountName ) );
        if ( pAmount != nullptr )
        {
            pAmount->setRange( 1.0f, static_cast<float32>( _maxCount ), 1.0f );
            pAmount->setValue( static_cast<float32>( _initialCount ) );
        }
    }

    int32 QuantityDialogScreen::getCount() const
    {
        const SliderWidget* pAmount = getTree().findWidget<SliderWidget>( hashed_string( kAmountName ) );
        // 슬라이더가 없는 문서는 처음 값 그대로다(고를 수 없는 창 — 문서 오류는 열 때 문서 생성 단계가 알린다).
        const int32 count = pAmount != nullptr ? static_cast<int32>( MathUtil::round( pAmount->getValue() ) ) : _initialCount;
        return MathUtil::clamp( count, 1, _maxCount );
    }

    bool QuantityDialogScreen::onCommand( const hashed_string& command, Widget& source )
    {
        if ( command == hashed_string( ConfirmDialogScreen::kAcceptCommand ) )
        {
            finish( ConfirmDialogResult::Accepted );
            return true;
        }
        if ( command == hashed_string( ConfirmDialogScreen::kCancelCommand ) )
        {
            finish( ConfirmDialogResult::Cancelled );
            return true;
        }
        return UIScreen::onCommand( command, source );
    }

    bool QuantityDialogScreen::onBack()
    {
        finish( ConfirmDialogResult::Cancelled );
        return true;
    }

    void QuantityDialogScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        _viewModel.setCount( getCount() );
    }

    void QuantityDialogScreen::finish( ConfirmDialogResult result )
    {
        if ( _bFinished == SW_TRUE )
            return; // 답은 한 번
        _bFinished        = SW_TRUE;
        const int32 count = result == ConfirmDialogResult::Accepted ? getCount() : 0;
        close();
        const ResultDelegate onResult = _onResult;
        _onResult                     = ResultDelegate{};
        if ( onResult.isBound() )
            onResult( result, count );
    }
} // namespace sw
