#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Client/ServerPickerScreen.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Localization/LocText.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/ListViewWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    namespace
    {
        struct ServerPickerScreenInternal
        {
            static constexpr const utf8* kServersName    = "Servers";
            static constexpr const utf8* kPickCommand    = "Pick";
            static constexpr const utf8* kRefreshCommand = "Refresh";
            static constexpr const utf8* kCloseCommand   = "Close";

            static const utf8* getStateText( ServerState state )
            {
                switch ( state )
                {
                    case ServerState::Starting:
                    {
                        return SW_LOCKEYREF( "ServerPicker", "StateStarting" );
                    }
                    case ServerState::Open:
                    {
                        return SW_LOCKEYREF( "ServerPicker", "StateOpen" );
                    }
                    case ServerState::Draining:
                    {
                        return SW_LOCKEYREF( "ServerPicker", "StateDraining" );
                    }
                    case ServerState::Maintenance:
                    {
                        return SW_LOCKEYREF( "ServerPicker", "StateMaintenance" );
                    }
                    case ServerState::Count:
                    {
                        break;
                    }
                }
                return SW_LOCKEYREF( "ServerPicker", "StateUnknown" );
            }

            static string makeRowText( const ServerListEntry& server )
            {
                return SW_LOCALIZED_MESSAGE( "ServerPicker", "Row" ).addKey( "region", server._region ).addInteger( "fill", static_cast<int64>( server._fillPercent ) ).addKey( "state", getStateText( server._state ) ).str();
            }
        };
    } // namespace

    ServerPickerScreen::ServerPickerScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _onPicked{}
        , _kind{}
        , _model{}
        , _shownRevision{ 0 }
        , _bBuilt{ SW_FALSE }
        , _viewModel{}
    {
        setViewModel( &_viewModel );
    }

    ServerPickerScreen::~ServerPickerScreen()
    {
        setViewModel( nullptr );
    }

    UIScreenHandle ServerPickerScreen::open( UISystem& ui, string_view documentPath, ServerList& model, string_view kind, const ServerPickedDelegate& onPicked )
    {
        const UIScreenHandle handle  = ui.openScreen<ServerPickerScreen>( documentPath );
        UIScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUIScreenHandle;
        ServerPickerScreen& screen = static_cast<ServerPickerScreen&>( *pScreen );
        screen._model              = WeakRef<ServerList>{ model };
        screen._kind               = string( kind );
        screen._onPicked           = onPicked;
        if ( model.getState() == ServerListState::Idle )
            (void)model.refresh( kind ); // 실패는 모델이 Failed 로 보이고 rebuild 가 상태 글로 적는다
        screen.rebuild();
        return handle;
    }

    unique_ptr<Widget> ServerPickerScreen::makeRow()
    {
        unique_ptr<ButtonWidget> button = make_unique<ButtonWidget>();
        button->setCommand( hashed_string( ServerPickerScreenInternal::kPickCommand ) );
        (void)button->addChild( make_unique<TextWidget>() );
        return button;
    }

    void ServerPickerScreen::bindRow( Widget& row, const ServerListEntry& server )
    {
        ButtonWidget& button = static_cast<ButtonWidget&>( row );
        button.setEnabled( server._state == ServerState::Open );
        static_cast<TextWidget*>( button.getChild( 0 ) )->setText( ServerPickerScreenInternal::makeRowText( server ) );
    }

    bool ServerPickerScreen::onCommand( const hashed_string& command, Widget& source )
    {
        using Internal = ServerPickerScreenInternal;
        if ( command == hashed_string( Internal::kCloseCommand ) )
        {
            close();
            return true;
        }
        if ( command == hashed_string( Internal::kRefreshCommand ) )
        {
            if ( ServerList* const pModel = _model.get() )
                (void)pModel->refresh( _kind ); // 묻는 중이면 보내지 않는다 — 상태 글이 이미 받는 중이다
            return true;
        }
        const ServerList* const pModel = _model.get();
        if ( command == hashed_string( Internal::kPickCommand ) && pModel != nullptr )
        {
            const ListViewWidget*  pServers = getTree().findWidget<ListViewWidget>( hashed_string( Internal::kServersName ) );
            const optional<uint32> index    = pServers != nullptr ? pServers->findItemIndex( source ) : nullopt;
            if ( index.has_value() == false || *index >= pModel->getEntries().size() )
                return false;
            const ServerListEntry server = pModel->getEntries()[*index]; // 델리게이트가 모델을 바꿔도 안전하게 복사해 둔다
            if ( server._state != ServerState::Open )
                return false; // 받지 않는 서버는 고를 수 없다(단추도 꺼져 있다)
            if ( _onPicked.isBound() )
                _onPicked( server );
            close();
            return true;
        }
        return UIScreen::onCommand( command, source );
    }

    void ServerPickerScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        const ServerList* const pModel = _model.get();
        if ( pModel == nullptr )
        {
            close(); // 모델을 소유한 쪽(게임 인스턴스)이 먼저 내려갔다 — 화면도 함께 닫는다
            return;
        }
        if ( _bBuilt == false || pModel->getRevision() != _shownRevision )
            rebuild();
    }

    void ServerPickerScreen::rebuild()
    {
        using Internal                 = ServerPickerScreenInternal;
        const ServerList* const pModel = _model.get();
        if ( pModel == nullptr )
            return;
        WidgetTree&     tree     = getTree();
        ListViewWidget* pServers = tree.findWidget<ListViewWidget>( hashed_string( Internal::kServersName ) );
        if ( pServers == nullptr )
        {
            SW_LOG_ERROR_ID( "ServerPickerScreen.ServerPickerDocumentNeeds", "Server picker document %# needs a '%#' list view", getDocumentPath().c_str(), Internal::kServersName );
            _bBuilt = SW_TRUE; // 문서가 틀렸다 — 프레임마다 같은 오류를 되풀이하지 않는다
            return;
        }
        // 줄은 목록이 보이는 만큼만 만들고 다시 쓴다(서버 수와 무관하다). 항목 수를 다시 넣으면 모든 줄을 다시 잇는다.
        pServers->setRowFactory( SW_DELEGATE_LAMBDA( ListViewWidget::RowFactoryDelegate, []()
        { return makeRow(); } ) );
        pServers->setRowBinder( SW_DELEGATE_LAMBDA( ListViewWidget::RowBinderDelegate, [this]( Widget& row, uint32 itemIndex )
        {
            if ( const ServerList* const pBound = _model.get() ) // 줄은 다음 배치에서 이어진다 — 그 사이 모델이 사라졌을 수 있다
                bindRow( row, pBound->getEntries()[itemIndex] );
        } ) );
        const vector<ServerListEntry>& listEntry = pModel->getEntries();
        pServers->setItemCount( static_cast<uint32>( listEntry.size() ) );
        switch ( pModel->getState() )
        {
            case ServerListState::Idle:
            {
                _viewModel.setStatus( SW_LOCKEYREF( "ServerPicker", "Idle" ) );
                break;
            }
            case ServerListState::Loading:
            {
                _viewModel.setStatus( SW_LOCKEYREF( "ServerPicker", "Loading" ) );
                break;
            }
            case ServerListState::Loaded:
            {
                _viewModel.setStatus( listEntry.empty() ? SW_LOCKEYREF( "ServerPicker", "None" ) : SW_LOCKEYREF( "ServerPicker", "Pick" ) );
                break;
            }
            case ServerListState::Failed:
            {
                _viewModel.setStatus( SW_LOCALIZED_MESSAGE( "ServerPicker", "LoadFailed" ).addInteger( "error", static_cast<int64>( pModel->getErrorCode() ) ).str() );
                break;
            }
        }
        _shownRevision = pModel->getRevision();
        _bBuilt        = SW_TRUE;
    }
} // namespace sw
