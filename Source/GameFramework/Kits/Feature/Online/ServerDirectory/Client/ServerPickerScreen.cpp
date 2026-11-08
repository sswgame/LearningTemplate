#include "pch.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Client/ServerPickerScreen.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

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
            static constexpr const utf8* kStatusName     = "Status";
            static constexpr const utf8* kPickCommand    = "Pick";
            static constexpr const utf8* kRefreshCommand = "Refresh";
            static constexpr const utf8* kCloseCommand   = "Close";

            static const utf8* getStateText( ServerState state )
            {
                switch ( state )
                {
                    case ServerState::Starting:
                    {
                        return "starting";
                    }
                    case ServerState::Open:
                    {
                        return "open";
                    }
                    case ServerState::Draining:
                    {
                        return "draining";
                    }
                    case ServerState::Maintenance:
                    {
                        return "maintenance";
                    }
                    case ServerState::Count:
                    {
                        break;
                    }
                }
                return "unknown";
            }

            static string makeRowText( const ServerListEntry& server )
            {
                return formatToString( "%#  %#%%  %#", server._region, static_cast<int32>( server._fillPercent ), getStateText( server._state ) );
            }
        };
    } // namespace

    ServerPickerScreen::ServerPickerScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _onPicked{}
        , _kind{}
        , _pModel{ nullptr }
        , _shownRevision{ 0 }
        , _bBuilt{ SW_FALSE }
    {
    }

    ServerPickerScreen::~ServerPickerScreen() = default;

    UIScreenHandle ServerPickerScreen::open( UISystem& ui, string_view documentPath, ServerListModel& model, string_view kind, const ServerPickedDelegate& onPicked )
    {
        const UIScreenHandle handle  = ui.openScreen<ServerPickerScreen>( documentPath );
        UIScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUIScreenHandle;
        ServerPickerScreen& screen = static_cast<ServerPickerScreen&>( *pScreen );
        screen._pModel             = &model;
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
            if ( _pModel != nullptr )
                (void)_pModel->refresh( _kind ); // 묻는 중이면 보내지 않는다 — 상태 글이 이미 받는 중이다
            return true;
        }
        if ( command == hashed_string( Internal::kPickCommand ) && _pModel != nullptr )
        {
            const ListViewWidget*  pServers = getTree().findWidget<ListViewWidget>( hashed_string( Internal::kServersName ) );
            const optional<uint32> index    = pServers != nullptr ? pServers->findItemIndex( source ) : nullopt;
            if ( index.has_value() == false || *index >= _pModel->getEntries().size() )
                return false;
            const ServerListEntry server = _pModel->getEntries()[*index]; // 델리게이트가 모델을 바꿔도 안전하게 복사해 둔다
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
        if ( _pModel != nullptr && ( _bBuilt == false || _pModel->getRevision() != _shownRevision ) )
            rebuild();
    }

    void ServerPickerScreen::rebuild()
    {
        using Internal = ServerPickerScreenInternal;
        if ( _pModel == nullptr )
            return;
        WidgetTree&     tree     = getTree();
        ListViewWidget* pServers = tree.findWidget<ListViewWidget>( hashed_string( Internal::kServersName ) );
        TextWidget*     pStatus  = tree.findWidget<TextWidget>( hashed_string( Internal::kStatusName ) );
        if ( pServers == nullptr || pStatus == nullptr )
        {
            SW_LOG_ERROR_ID( "ServerPickerScreen.ServerPickerDocumentNeeds", "Server picker document %# needs a '%#' list view and a '%#' text", getDocumentPath().c_str(), Internal::kServersName, Internal::kStatusName );
            _bBuilt = SW_TRUE; // 문서가 틀렸다 — 프레임마다 같은 오류를 되풀이하지 않는다
            return;
        }
        // 줄은 목록이 보이는 만큼만 만들고 다시 쓴다(서버 수와 무관하다). 항목 수를 다시 넣으면 모든 줄을 다시 잇는다.
        pServers->setRowFactory( ListViewWidget::RowFactoryDelegate::create( []()
        { return makeRow(); } ) );
        pServers->setRowBinder( ListViewWidget::RowBinderDelegate::create( [this]( Widget& row, uint32 itemIndex )
        { bindRow( row, _pModel->getEntries()[itemIndex] ); } ) );
        const vector<ServerListEntry>& listEntry = _pModel->getEntries();
        pServers->setItemCount( static_cast<uint32>( listEntry.size() ) );
        switch ( _pModel->getState() )
        {
            case ServerListState::Idle:
            {
                pStatus->setText( "Press Refresh to list servers" );
                break;
            }
            case ServerListState::Loading:
            {
                pStatus->setText( "Loading servers..." );
                break;
            }
            case ServerListState::Loaded:
            {
                pStatus->setText( listEntry.empty() ? "No server is available right now" : "Pick a server" );
                break;
            }
            case ServerListState::Failed:
            {
                pStatus->setText( formatToString( "Could not load the server list (error %#)", static_cast<int32>( _pModel->getErrorCode() ) ) );
                break;
            }
        }
        _shownRevision = _pModel->getRevision();
        _bBuilt        = SW_TRUE;
    }
} // namespace sw
