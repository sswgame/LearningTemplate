#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatScreen.h"

#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/TextInputWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    SW_LOG_CALLER( "ChatScreen" );

    namespace
    {
        struct ChatScreenInternal
        {
            static constexpr const utf8* kLinesName    = "Lines";
            static constexpr const utf8* kStatusName   = "Status";
            static constexpr const utf8* kInputName    = "Input";
            static constexpr const utf8* kRowPrefix    = "Line.";
            static constexpr const utf8* kSendCommand  = "Send";
            static constexpr const utf8* kCloseCommand = "Close";
        };
    } // namespace

    ChatScreen::ChatScreen( const UIScreenDesc& desc, unique_ptr<Widget> root )
        : UIScreen{ desc, std::move( root ) }
        , _channelID{}
        , _pModel{ nullptr }
        , _shownRevision{ 0 }
        , _bBuilt{ false }
    {
    }

    ChatScreen::~ChatScreen() = default;

    UIScreenHandle ChatScreen::open( UISystem& ui, string_view documentPath, ChatLogModel& model, string_view channelID )
    {
        const UIScreenHandle handle  = ui.openScreen<ChatScreen>( documentPath );
        UIScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUIScreenHandle;
        ChatScreen& screen = static_cast<ChatScreen&>( *pScreen );
        screen._pModel     = &model;
        screen._channelID  = string( channelID );
        screen.rebuild();
        return handle;
    }

    string ChatScreen::makeStatusText( ChatResult result, int64 retryAfterMs )
    {
        switch ( result )
        {
            case ChatResult::Ok:
            {
                return "";
            }
            case ChatResult::NotMember:
            {
                return "You have not joined this channel";
            }
            case ChatResult::NotJoinable:
            {
                return "This channel cannot be joined";
            }
            case ChatResult::Muted:
            {
                return formatToString( "You are muted (%# s left)", retryAfterMs / 1000 );
            }
            case ChatResult::RateLimited:
            {
                return formatToString( "Too fast, wait %# ms", retryAfterMs );
            }
            case ChatResult::Repeated:
            {
                return "You already said that";
            }
            case ChatResult::Rejected:
            {
                return "Message was blocked by the word filter";
            }
            case ChatResult::TargetOffline:
            {
                return "That player is offline";
            }
            case ChatResult::Invalid:
            {
                return "Message is empty or not valid";
            }
            case ChatResult::Unavailable:
            {
                return "Chat is not available right now";
            }
            case ChatResult::TooManyChannels:
            {
                return "Too many channels";
            }
            case ChatResult::NotSignedIn:
            {
                return "Sign in to chat";
            }
            case ChatResult::UpdateRequired:
            {
                return "Update the game to chat";
            }
            case ChatResult::NotFound:
            {
                return "Message not found";
            }
        }
        return "Chat failed";
    }

    bool ChatScreen::onCommand( const hashed_string& command, Widget& source )
    {
        using Internal = ChatScreenInternal;
        if ( command == hashed_string( Internal::kCloseCommand ) )
        {
            close();
            return true;
        }
        if ( command == hashed_string( Internal::kSendCommand ) && _pModel != nullptr )
        {
            TextInputWidget* pInput = getTree().findWidget<TextInputWidget>( hashed_string( Internal::kInputName ) );
            if ( pInput == nullptr )
            {
                SW_LOG_ERROR( "Chat document %# needs a '%#' text input", getDocumentPath().c_str(), Internal::kInputName );
                return false;
            }
            const string text = pInput->getText(); // 모델이 입력 줄을 건드리는 길은 없지만 복사해 두고 비운다
            if ( _pModel->send( _channelID, text ) )
                pInput->setText( "" );
            return true;
        }
        return UIScreen::onCommand( command, source );
    }

    void ChatScreen::onTick( float32 deltaSeconds )
    {
        UIScreen::onTick( deltaSeconds );
        if ( _pModel == nullptr )
            return;
        _pModel->update();
        if ( _bBuilt == false || _pModel->getRevision() != _shownRevision )
            rebuild();
    }

    void ChatScreen::rebuild()
    {
        using Internal = ChatScreenInternal;
        if ( _pModel == nullptr )
            return;
        WidgetTree&  tree    = getTree();
        PanelWidget* pLines  = tree.findWidget<PanelWidget>( hashed_string( Internal::kLinesName ) );
        TextWidget*  pStatus = tree.findWidget<TextWidget>( hashed_string( Internal::kStatusName ) );
        if ( pLines == nullptr || pStatus == nullptr )
        {
            SW_LOG_ERROR( "Chat document %# needs a '%#' panel and a '%#' text", getDocumentPath().c_str(), Internal::kLinesName, Internal::kStatusName );
            _bBuilt = true; // 문서가 틀렸다 — 프레임마다 같은 오류를 되풀이하지 않는다
            return;
        }
        pLines->clearChildren();
        // 이 채널의 줄만, 최근 kVisibleLines 줄을 오래된 것부터 위에서 아래로 놓는다.
        const deque<ChatMessage>& listLine = _pModel->getLines();
        size_t                    shown    = 0;
        size_t                    first    = listLine.size();
        while ( first > 0 && shown < kVisibleLines )
        {
            --first;
            if ( listLine[first]._channelID == _channelID )
                ++shown;
        }
        // first 는 보일 줄 가운데 가장 오래된 채널 줄의 위치다(채널 줄이 모자라면 0 까지 내려간다).
        size_t rowIndex = 0;
        for ( size_t index = first; index < listLine.size(); ++index )
        {
            if ( listLine[index]._channelID != _channelID )
                continue;
            unique_ptr<TextWidget> row = make_unique<TextWidget>();
            row->setName( hashed_string( string{ Internal::kRowPrefix } + to_string( static_cast<uint64>( rowIndex ) ) ) );
            row->setText( "[" + listLine[index]._senderName + "] " + listLine[index]._text );
            (void)pLines->addChild( std::move( row ) );
            ++rowIndex;
        }
        pStatus->setText( makeStatusText( _pModel->getLastResult(), _pModel->getRetryAfterMs() ) );
        _shownRevision = _pModel->getRevision();
        _bBuilt        = true;
    }
} // namespace sw
