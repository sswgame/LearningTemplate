#include "pch.h"

#include "Engine/UI/Menu/ConfirmDialogScreen.h"

#include "Core/Log/Logger.h"

#include "Engine/UI/UISystem.h"

namespace sw
{
    ConfirmDialogViewModel::ConfirmDialogViewModel()
        : UIViewModel{}
        , _title{}
        , _message{}
        , _acceptText{}
        , _cancelText{}
    {
    }

    ConfirmDialogViewModel::~ConfirmDialogViewModel() = default;

    const TypeInfo* ConfirmDialogViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void ConfirmDialogViewModel::apply( const ConfirmDialogDesc& desc )
    {
        (void)setField( _title, desc._title, "_title" );
        (void)setField( _message, desc._message, "_message" );
        (void)setField( _acceptText, desc._acceptText, "_acceptText" );
        (void)setField( _cancelText, desc._cancelText, "_cancelText" );
    }

    ConfirmDialogScreen::ConfirmDialogScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _viewModel{}
        , _onResult{}
        , _bFinished{ SW_FALSE }
    {
        setViewModel( &_viewModel );
    }

    ConfirmDialogScreen::~ConfirmDialogScreen()
    {
        setViewModel( nullptr );
    }

    UIScreenHandle ConfirmDialogScreen::open( UISystem& ui, const ConfirmDialogDesc& desc, const ResultDelegate& onResult )
    {
        const bool bMissingText = desc._title.empty() || desc._message.empty() || desc._acceptText.empty() || desc._cancelText.empty();
        if ( bMissingText || onResult.isBound() == false )
        {
            SW_LOG_ERROR_ID( "ConfirmDialogScreen.ConfirmDialogNeedsTitle", "Confirm dialog needs a title, a message, both button texts and a result handler - '%#' was not opened",
                             desc._message.c_str() );
            return kInvalidUIScreenHandle;
        }
        const string&        documentPath = ui.getEngineDocuments()._confirmDialog;
        const UIScreenHandle handle       = ui.openScreen<ConfirmDialogScreen>( documentPath );
        UIScreen*            pScreen      = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUIScreenHandle; // 문서를 열지 못했다 — openScreen 이 까닭을 남겼다
        static_cast<ConfirmDialogScreen&>( *pScreen ).present( desc, onResult );
        return handle;
    }

    void ConfirmDialogScreen::present( const ConfirmDialogDesc& desc, const ResultDelegate& onResult )
    {
        if ( _bFinished == SW_TRUE )
            return;
        _viewModel.apply( desc );
        _onResult = onResult;
    }

    bool ConfirmDialogScreen::onCommand( const hashed_string& command, Widget& source )
    {
        if ( command == hashed_string( kAcceptCommand ) )
        {
            finish( ConfirmDialogResult::Accepted );
            return true;
        }
        if ( command == hashed_string( kCancelCommand ) )
        {
            finish( ConfirmDialogResult::Cancelled );
            return true;
        }
        return UIScreen::onCommand( command, source );
    }

    bool ConfirmDialogScreen::onBack()
    {
        finish( ConfirmDialogResult::Cancelled );
        return true;
    }

    void ConfirmDialogScreen::finish( ConfirmDialogResult result )
    {
        if ( _bFinished == SW_TRUE )
            return; // 답은 한 번 — 닫기 애니메이션 중 다시 누른 것 · 같은 프레임의 두 번째 단추
        _bFinished = SW_TRUE;
        close();
        // 델리게이트를 복사해 두고 부른다 — 받는 쪽이 새 화면을 열어도 이 객체의 칸을 읽지 않게.
        const ResultDelegate onResult = _onResult;
        _onResult                     = ResultDelegate{};
        if ( onResult.isBound() )
            onResult( result );
    }
} // namespace sw
