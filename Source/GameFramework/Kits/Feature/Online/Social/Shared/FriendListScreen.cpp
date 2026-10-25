#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Shared/FriendListScreen.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    SW_LOG_CALLER( "FriendListScreen" );

    namespace
    {
        struct FriendListScreenInternal
        {
            static constexpr const utf8* kFriendsName    = "Friends";
            static constexpr const utf8* kStatusName     = "Status";
            static constexpr const utf8* kRowPrefix      = "Friend.";
            static constexpr const utf8* kRefreshCommand = "Refresh";
            static constexpr const utf8* kCloseCommand   = "Close";

            static const utf8* getPresenceText( SocialPresenceStatus status )
            {
                switch ( status )
                {
                    case SocialPresenceStatus::Offline:
                    {
                        return "offline";
                    }
                    case SocialPresenceStatus::Online:
                    {
                        return "online";
                    }
                    case SocialPresenceStatus::Away:
                    {
                        return "away";
                    }
                    case SocialPresenceStatus::Busy:
                    {
                        return "busy";
                    }
                    case SocialPresenceStatus::InGame:
                    {
                        return "in game";
                    }
                    case SocialPresenceStatus::Count:
                    {
                        break;
                    }
                }
                return "unknown";
            }

            static string makeRowText( const FriendEntry& entry )
            {
                if ( entry._activity.empty() )
                    return "#" + to_string( entry._accountID ) + "  " + getPresenceText( entry._status );
                return "#" + to_string( entry._accountID ) + "  " + getPresenceText( entry._status ) + "  " + entry._activity;
            }
        };
    } // namespace

    FriendListScreen::FriendListScreen( const UIScreenDesc& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _pModel{ nullptr }
        , _shownRevision{ 0 }
        , _bBuilt{ false }
    {
    }

    FriendListScreen::~FriendListScreen() = default;

    UIScreenHandle FriendListScreen::open( UISystem& ui, string_view documentPath, FriendListModel& model )
    {
        const UIScreenHandle handle  = ui.openScreen<FriendListScreen>( documentPath );
        UIScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUIScreenHandle;
        FriendListScreen& screen = static_cast<FriendListScreen&>( *pScreen );
        screen._pModel           = &model;
        if ( model.getState() == FriendListState::Idle )
            (void)model.refresh(); // 실패는 모델이 Failed 로 보이고 rebuild 가 상태 글로 적는다
        screen.rebuild();
        return handle;
    }

    bool FriendListScreen::onCommand( const hashed_string& command, Widget& source )
    {
        using Internal = FriendListScreenInternal;
        if ( command == hashed_string( Internal::kCloseCommand ) )
        {
            close();
            return true;
        }
        if ( command == hashed_string( Internal::kRefreshCommand ) )
        {
            if ( _pModel != nullptr )
                (void)_pModel->refresh(); // 묻는 중이면 보내지 않는다 — 상태 글이 이미 받는 중이다
            return true;
        }
        return UIScreen::onCommand( command, source );
    }

    void FriendListScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        if ( _pModel == nullptr )
            return;
        _pModel->update();
        if ( _bBuilt == false || _pModel->getRevision() != _shownRevision )
            rebuild();
    }

    void FriendListScreen::rebuild()
    {
        using Internal = FriendListScreenInternal;
        if ( _pModel == nullptr )
            return;
        WidgetTree&  tree     = getTree();
        PanelWidget* pFriends = tree.findWidget<PanelWidget>( hashed_string( Internal::kFriendsName ) );
        TextWidget*  pStatus  = tree.findWidget<TextWidget>( hashed_string( Internal::kStatusName ) );
        if ( pFriends == nullptr || pStatus == nullptr )
        {
            SW_LOG_ERROR( "Friend list document %# needs a '%#' panel and a '%#' text", getDocumentPath().c_str(), Internal::kFriendsName, Internal::kStatusName );
            _bBuilt = true; // 문서가 틀렸다 — 프레임마다 같은 오류를 되풀이하지 않는다
            return;
        }
        pFriends->clearChildren();
        const vector<FriendEntry>& listEntry = _pModel->getEntries();
        for ( size_t index = 0; index < listEntry.size(); ++index )
        {
            unique_ptr<TextWidget> row = make_unique<TextWidget>();
            row->setName( hashed_string( string{ Internal::kRowPrefix } + to_string( static_cast<uint64>( index ) ) ) );
            row->setText( Internal::makeRowText( listEntry[index] ) );
            (void)pFriends->addChild( std::move( row ) );
        }
        string statusText;
        switch ( _pModel->getState() )
        {
            case FriendListState::Idle:
            {
                statusText = "Press Refresh to list friends";
                break;
            }
            case FriendListState::Loading:
            {
                statusText = "Loading friends...";
                break;
            }
            case FriendListState::Loaded:
            {
                statusText = listEntry.empty() ? "No friends yet" : "Friends";
                if ( _pModel->getPendingRequestCount() > 0 )
                    statusText += "  (" + to_string( _pModel->getPendingRequestCount() ) + " pending requests)";
                break;
            }
            case FriendListState::Failed:
            {
                statusText = "Could not load the friend list (error " + to_string( static_cast<uint32>( _pModel->getErrorCode() ) ) + ")";
                break;
            }
        }
        pStatus->setText( statusText );
        _shownRevision = _pModel->getRevision();
        _bBuilt        = true;
    }
} // namespace sw
