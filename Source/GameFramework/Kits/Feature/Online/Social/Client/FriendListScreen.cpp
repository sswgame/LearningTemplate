#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Client/FriendListScreen.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/ListViewWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct FriendListScreenInternal
        {
            static constexpr const utf8* kFriendsName    = "Friends";
            static constexpr const utf8* kStatusName     = "Status";
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
                {
                    StringBuilder<constant::buffer::kMax256> resultBuilder2;
                    resultBuilder2.appendFormat( "#%#  %#", entry._accountID, getPresenceText( entry._status ) );
                    return string( resultBuilder2.view() );
                }
                StringBuilder<constant::buffer::kMax256> resultBuilder;
                resultBuilder.appendFormat( "#%#  %#  %#", entry._accountID, getPresenceText( entry._status ), entry._activity );
                return string( resultBuilder.view() );
            }
        };
    } // namespace

    FriendListScreen::FriendListScreen( const UIScreenDef& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _model{}
        , _shownRevision{ 0 }
        , _bBuilt{ SW_FALSE }
    {
    }

    FriendListScreen::~FriendListScreen() = default;

    UIScreenHandle FriendListScreen::open( UISystem& ui, string_view documentPath, FriendList& model )
    {
        const UIScreenHandle handle  = ui.openScreen<FriendListScreen>( documentPath );
        UIScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUIScreenHandle;
        FriendListScreen& screen = static_cast<FriendListScreen&>( *pScreen );
        screen._model            = WeakRef<FriendList>{ model };
        if ( model.getState() == FriendListState::Idle )
            (void)model.refresh(); // 실패는 모델이 Failed 로 보이고 rebuild 가 상태 글로 적는다
        screen.rebuild();
        return handle;
    }

    unique_ptr<Widget> FriendListScreen::makeRow()
    {
        return make_unique<TextWidget>();
    }

    void FriendListScreen::bindRow( Widget& row, const FriendEntry& entry )
    {
        static_cast<TextWidget&>( row ).setText( FriendListScreenInternal::makeRowText( entry ) );
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
            if ( FriendList* const pModel = _model.get() )
                (void)pModel->refresh(); // 묻는 중이면 보내지 않는다 — 상태 글이 이미 받는 중이다
            return true;
        }
        return UIScreen::onCommand( command, source );
    }

    void FriendListScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        FriendList* const pModel = _model.get();
        if ( pModel == nullptr )
        {
            close(); // 모델을 소유한 쪽(게임 인스턴스)이 먼저 내려갔다 — 화면도 함께 닫는다
            return;
        }
        pModel->update();
        if ( _bBuilt == false || pModel->getRevision() != _shownRevision )
            rebuild();
    }

    void FriendListScreen::rebuild()
    {
        using Internal                 = FriendListScreenInternal;
        const FriendList* const pModel = _model.get();
        if ( pModel == nullptr )
            return;
        WidgetTree&     tree     = getTree();
        ListViewWidget* pFriends = tree.findWidget<ListViewWidget>( hashed_string( Internal::kFriendsName ) );
        TextWidget*     pStatus  = tree.findWidget<TextWidget>( hashed_string( Internal::kStatusName ) );
        if ( pFriends == nullptr || pStatus == nullptr )
        {
            SW_LOG_ERROR_ID( "FriendListScreen.FriendListDocumentNeeds", "Friend list document %# needs a '%#' list view and a '%#' text", getDocumentPath().c_str(), Internal::kFriendsName, Internal::kStatusName );
            _bBuilt = SW_TRUE; // 문서가 틀렸다 — 프레임마다 같은 오류를 되풀이하지 않는다
            return;
        }
        // 줄은 목록이 보이는 만큼만 만들고 다시 쓴다(친구 수와 무관하다). 항목 수를 다시 넣으면 모든 줄을 다시 잇는다.
        pFriends->setRowFactory( SW_DELEGATE_LAMBDA( ListViewWidget::RowFactoryDelegate, []()
        { return makeRow(); } ) );
        pFriends->setRowBinder( SW_DELEGATE_LAMBDA( ListViewWidget::RowBinderDelegate, [this]( Widget& row, uint32 itemIndex )
        {
            if ( const FriendList* const pBound = _model.get() ) // 줄은 다음 배치에서 이어진다 — 그 사이 모델이 사라졌을 수 있다
                bindRow( row, pBound->getEntries()[itemIndex] );
        } ) );
        const vector<FriendEntry>& listEntry = pModel->getEntries();
        pFriends->setItemCount( static_cast<uint32>( listEntry.size() ) );
        string statusText;
        switch ( pModel->getState() )
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
                StringBuilder<constant::buffer::kMax256> statusTextBuilder;
                statusTextBuilder.append( listEntry.empty() ? "No friends yet" : "Friends" );
                if ( pModel->getPendingRequestCount() > 0 )
                    statusTextBuilder.appendFormat( "  (%# pending requests)", pModel->getPendingRequestCount() );
                statusText = string( statusTextBuilder.view() );
                break;
            }
            case FriendListState::Failed:
            {
                StringBuilder<constant::buffer::kMax256> statusTextBuilder;
                statusTextBuilder.appendFormat( "Could not load the friend list (error %#)", static_cast<uint32>( pModel->getErrorCode() ) );
                statusText = string( statusTextBuilder.view() );
                break;
            }
        }
        pStatus->setText( statusText );
        _shownRevision = pModel->getRevision();
        _bBuilt        = SW_TRUE;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct FriendListCommandInternal
        {
            static bool runOpen( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                FriendList* const pFriends = game::findSubsystem<FriendList>();
                UISystem* const   pUI      = game::findSubsystem<UISystem>();
                if ( pFriends == nullptr || pUI == nullptr )
                    outReply = "social.friendlist: no friend list model in the game scope";
                else
                    outReply = FriendListScreen::open( *pUI, listArgument[0], *pFriends ) != kInvalidUIScreenHandle ? "social.friendlist: opened" : "social.friendlist: the document did not open";
                return true;
            }
        };
    } // namespace

    // 게임은 친구 목록 모델(`FriendList`)을 게임 범위에 건다 — 게임 모듈은 클라이언트 키트를 링크하지 않으므로 화면은 키트가 연다.
    SW_DEV_COMMAND( SocialFriendList, "social.friendlist", "social.friendlist <ui document>", "Open the friend list screen on the game's friend list model", &FriendListCommandInternal::runOpen );
} // namespace sw
