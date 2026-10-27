#include "pch.h"

#include "GameFramework/Base/Actor/Control/Intent/IntentReplayBar.h"

#include "Core/Container/formatString.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/Actor/Control/Intent/IntentRewindSession.h"

namespace sw
{
    SW_LOG_CALLER( "IntentReplayBar" );

    namespace
    {
        struct IntentReplayBarInternal
        {
            static constexpr const utf8* kScrub          = "Scrub";
            static constexpr const utf8* kTimeText       = "TimeText";
            static constexpr const utf8* kPlayPauseLabel = "PlayPauseLabel";
            static constexpr const utf8* kStepBack       = "Replay.StepBack";
            static constexpr const utf8* kTogglePause    = "Replay.TogglePause";
            static constexpr const utf8* kClose          = "Replay.Close";
        };
    } // namespace

    IntentReplayBar::IntentReplayBar()
        : _pUI{ nullptr }
        , _pSession{ nullptr }
        , _scrubSubscription{}
        , _tickSeconds{ 1.0f / 60.0f }
        , _shownMin{ 0.0f }
        , _shownMax{ 0.0f }
        , _furthestTick{ 0 }
        , _seekTicksPerFrame{ 1 }
        , _screen{ kInvalidUIScreenHandle }
        , _bOpen{ SW_FALSE }
        , _bPaused{ SW_FALSE }
        , _bSyncing{ SW_FALSE }
    {
    }

    IntentReplayBar::~IntentReplayBar()
    {
        close();
    }

    bool IntentReplayBar::open( UISystem& ui, string_view documentPath, IntentRewindSession& session, float32 tickSeconds, uint32 seekTicksPerFrame )
    {
        close();
        if ( tickSeconds <= 0.0f || seekTicksPerFrame == 0 )
        {
            SW_LOG_ERROR( "IntentReplayBar: refused to open - tick length %# s and %# seek ticks per frame must both be positive", tickSeconds, seekTicksPerFrame );
            return false;
        }
        const UIScreenHandle handle  = ui.openScreen( documentPath );
        UIScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return false; // 문서 단계가 이유를 남겼다
        SliderWidget* pScrub         = pScreen->getTree().findWidget<SliderWidget>( hashed_string( IntentReplayBarInternal::kScrub ) );
        TextWidget*   pTime          = pScreen->getTree().findWidget<TextWidget>( hashed_string( IntentReplayBarInternal::kTimeText ) );
        TextWidget*   pPauseText     = pScreen->getTree().findWidget<TextWidget>( hashed_string( IntentReplayBarInternal::kPlayPauseLabel ) );
        const bool    bMissingWidget = pScrub == nullptr || pTime == nullptr || pPauseText == nullptr;
        if ( bMissingWidget )
        {
            SW_LOG_ERROR( "IntentReplayBar: '%#' needs a SliderWidget '%#' and TextWidgets '%#' and '%#'", string( documentPath ).c_str(), IntentReplayBarInternal::kScrub,
                          IntentReplayBarInternal::kTimeText, IntentReplayBarInternal::kPlayPauseLabel );
            ui.closeScreen( handle );
            return false;
        }
        _pUI                                   = &ui;
        _pSession                              = &session;
        _tickSeconds                           = tickSeconds;
        _seekTicksPerFrame                     = seekTicksPerFrame;
        _screen                                = handle;
        _bOpen                                 = SW_TRUE;
        _bPaused                               = SW_FALSE;
        _furthestTick                          = session.getWorldTick();
        _shownMin                              = 0.0f;
        _shownMax                              = 0.0f;
        _scrubSubscription                     = pScrub->getOnValueChanged().add( SW_DELEGATE_METHOD( Delegate<void( float32 )>, &IntentReplayBar::onScrub, this ) );
        const UICommandDelegate commandHandler = SW_DELEGATE_METHOD( UICommandDelegate, &IntentReplayBar::onCommand, this );
        _arrCommandRegistration[0]             = pScreen->registerCommand( hashed_string( IntentReplayBarInternal::kStepBack ), commandHandler );
        _arrCommandRegistration[1]             = pScreen->registerCommand( hashed_string( IntentReplayBarInternal::kTogglePause ), commandHandler );
        _arrCommandRegistration[2]             = pScreen->registerCommand( hashed_string( IntentReplayBarInternal::kClose ), commandHandler );
        updatePauseLabel();
        return true;
    }

    void IntentReplayBar::close()
    {
        if ( _bOpen == SW_FALSE )
            return;
        _scrubSubscription.reset();
        for ( Subscription& registration : _arrCommandRegistration )
        {
            registration.reset(); // 화면이 먼저 닫혔으면 이미 무효다
        }
        UIScreen* pScreen = _pUI != nullptr ? _pUI->findScreen( _screen ) : nullptr;
        if ( pScreen != nullptr )
            _pUI->closeScreen( _screen );
        _screen  = kInvalidUIScreenHandle;
        _bOpen   = SW_FALSE;
        _bPaused = SW_FALSE;
    }

    bool IntentReplayBar::shouldHostStep() const
    {
        if ( _bOpen == SW_FALSE )
            return true;
        return _bPaused == SW_FALSE && ( _pSession == nullptr || _pSession->isSeeking() == false );
    }

    void IntentReplayBar::update()
    {
        if ( _bOpen == SW_FALSE )
            return;
        UIScreen* pScreen = _pUI->findScreen( _screen );
        if ( pScreen == nullptr || pScreen->isClosing() )
        {
            // 사용자가 뒤로 가기로 화면을 닫았다 — 막대도 닫힌 것으로 치고 호스트가 평소대로 돈다.
            _screen  = kInvalidUIScreenHandle;
            _bOpen   = SW_FALSE;
            _bPaused = SW_FALSE;
            return;
        }
        if ( _pSession->isSeeking() )
            (void)_pSession->stepSeek( _seekTicksPerFrame ); // 남은 틱 수는 세션이 들고 있다. 다음 프레임에 이어 돈다
        const uint32 tick = _pSession->getWorldTick();
        _furthestTick     = MathUtil::max( _furthestTick, tick );
        uint32 earliest   = 0;
        if ( _pSession->findEarliestSeekableTick( earliest ) == false )
            earliest = tick;
        SliderWidget* pScrub = pScreen->getTree().findWidget<SliderWidget>( hashed_string( IntentReplayBarInternal::kScrub ) );
        TextWidget*   pTime  = pScreen->getTree().findWidget<TextWidget>( hashed_string( IntentReplayBarInternal::kTimeText ) );
        if ( pScrub != nullptr )
        {
            const float32 minValue = static_cast<float32>( earliest );
            const float32 maxValue = static_cast<float32>( MathUtil::max( _furthestTick, earliest + 1 ) );
            _bSyncing              = SW_TRUE;
            if ( minValue != _shownMin || maxValue != _shownMax )
            {
                pScrub->setRange( minValue, maxValue, 1.0f );
                _shownMin = minValue;
                _shownMax = maxValue;
            }
            pScrub->setValue( static_cast<float32>( tick ) );
            _bSyncing = SW_FALSE;
        }
        if ( pTime != nullptr )
        {
            fixed_string<constant::kMaxBuffer64> text;
            formatstring( text.data(), text.capacity(), "%.1f / %.1f s", static_cast<float32>( tick ) * _tickSeconds, static_cast<float32>( _furthestTick ) * _tickSeconds );
            pTime->setText( text.c_str() );
        }
    }

    void IntentReplayBar::onCommand( const hashed_string& command, Widget& source )
    {
        (void)source;
        if ( command == hashed_string( IntentReplayBarInternal::kStepBack ) )
        {
            const uint32 tick      = _pSession->getWorldTick();
            const uint32 stepTicks = static_cast<uint32>( MathUtil::round( 1.0f / _tickSeconds ) );
            uint32       earliest  = 0;
            if ( _pSession->findEarliestSeekableTick( earliest ) == false )
                return;
            const uint32 target = tick > earliest + stepTicks ? tick - stepTicks : earliest;
            (void)_pSession->beginSeek( target ); // 못 하면 세션이 이유를 남겼고 세계는 그대로다
            return;
        }
        if ( command == hashed_string( IntentReplayBarInternal::kTogglePause ) )
        {
            _bPaused = _bPaused == SW_FALSE ? SW_TRUE : SW_FALSE;
            updatePauseLabel();
            return;
        }
        if ( command == hashed_string( IntentReplayBarInternal::kClose ) )
        {
            // 명령을 처리하는 중이라 명령 등록을 지우지 않는다 — 화면 닫기는 지연 처리이고, 닫는 중인 화면은 더 이상 명령을 내지 않는다.
            _pUI->closeScreen( _screen );
            _screen  = kInvalidUIScreenHandle;
            _bOpen   = SW_FALSE;
            _bPaused = SW_FALSE;
        }
    }

    void IntentReplayBar::onScrub( float32 value )
    {
        if ( _bSyncing != SW_FALSE || _pSession == nullptr )
            return;
        (void)_pSession->beginSeek( static_cast<uint32>( MathUtil::max( MathUtil::round( value ), 0.0f ) ) ); // 못 하면 세션이 이유를 남겼고 세계는 그대로다
    }

    void IntentReplayBar::updatePauseLabel()
    {
        UIScreen* pScreen = _pUI != nullptr ? _pUI->findScreen( _screen ) : nullptr;
        if ( pScreen == nullptr )
            return;
        TextWidget* pLabel = pScreen->getTree().findWidget<TextWidget>( hashed_string( IntentReplayBarInternal::kPlayPauseLabel ) );
        if ( pLabel != nullptr )
            pLabel->setText( _bPaused != SW_FALSE ? "Play" : "Pause" );
    }
} // namespace sw
