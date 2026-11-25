#include "pch.h"

#include "Engine/UI/Service/UISubtitleService.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEngineState.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Localization/LocText.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    namespace
    {
        struct UISubtitleServiceInternal
        {
            /** @brief 배치 폭을 넘었다고 보는 여유(UI 단위)입니다. 글리프 가장자리의 반올림을 넘침으로 세지 않는다. */
            static constexpr float32 kOverflowSlack = 0.5f;

            /** @brief 줄 슬롯 @p slot 의 위젯 이름 `<prefix><slot>` 입니다. */
            static hashed_string makeSlotName( const utf8* pPrefix, uint32 slot )
            {
                StringBuilder<constant::buffer::kMax64> name;
                name.appendFormat( "%#%#", pPrefix, slot );
                return hashed_string( name.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UISubtitleService::UISubtitleService()
        : _subtitleSink{ *this }
        , _config{}
        , _pUI{ nullptr }
        , _updateSubscription{}
        , _listActive{}
        , _listQueued{}
        , _authoredBackground{}
        , _authoredTextFontSize{ 0.0f }
        , _authoredSpeakerFontSize{ 0.0f }
        , _screen{}
        , _lineIDAllocator{}
        , _bGamePaused{ SW_FALSE }
        , _bDocumentFailed{ SW_FALSE }
    {
    }

    UISubtitleService::~UISubtitleService() = default;

    SubsystemInitResult UISubtitleService::initializeSubsystem()
    {
        // 표시 규칙은 엔진 설정이 정한다 — 없으면(설정 파일에 `_subtitles` 가 없다) 코드 값으로 대신하지 않고 기동을 멈춘다.
        if ( configure( engine::getSubsystem<EngineConfig>()._subtitles ) == false )
            return SubsystemInitResult::Failed;
        _pUI                = &engine::getSubsystem<UISystem>();
        _updateSubscription = _pUI->onUpdateBeforeScreens().add( SW_DELEGATE_METHOD( Delegate<void( float32 )>, &UISubtitleService::update, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void UISubtitleService::shutdownSubsystem()
    {
        _updateSubscription.reset();
        clear(); // 화면 스택이 아직 있다(이것이 먼저 내려간다) — 연 화면을 닫는다
        _pUI = nullptr;
    }

    span<const utf8* const> UISubtitleService::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { UISystem::kSubsystemName };
        return kArrDependency;
    }

    bool UISubtitleService::configure( const SubtitleConfig& config )
    {
        bool bValid = config._maxVisibleLineCount > 0 && config._minDisplaySeconds > 0.0f && config._readingSecondsPerCharacter > 0.0f;
        for ( const float32 scale : config._arrSizeScale )
        {
            bValid = bValid && scale > 0.0f;
        }
        if ( bValid == false )
        {
            SW_LOG_ERROR_ID( "UISubtitle.InvalidConfig",
                             "EngineConfig _subtitles: _maxVisibleLineCount, _minDisplaySeconds, _readingSecondsPerCharacter and every _arrSizeScale must be > 0 "
                             "(lines %#, min %# s, per character %# s, scales %# %# %#)",
                             config._maxVisibleLineCount, config._minDisplaySeconds, config._readingSecondsPerCharacter, config._arrSizeScale[0], config._arrSizeScale[1], config._arrSizeScale[2] );
            return false;
        }
        _config = config;
        return true;
    }

    uint32 UISubtitleService::post( string_view speaker, string_view text, float32 durationSeconds )
    {
        SW_ASSERT( _config._maxVisibleLineCount > 0 ); // `configure` 전에 올리면 규칙 없이 시간을 정한다 — 기동 순서 결함
        UISubtitleLine line{};
        line._speaker          = string{ speaker };
        line._text             = string{ text };
        line._durationSeconds  = durationSeconds > 0.0f ? durationSeconds : computeReadingSeconds( LocalizedText::resolveNow( text ) ); // 읽는 시간은 보이는 글의 길이다
        line._remainingSeconds = line._durationSeconds;
        line._id               = _lineIDAllocator.allocate();
        _listQueued.push_back( std::move( line ) );
        return _listQueued.back()._id;
    }

    void UISubtitleService::remove( uint32 lineID )
    {
        const auto matches = [lineID]( const UISubtitleLine& line )
        { return line._id == lineID; };
        VectorUtil::removeIf( _listActive, matches );
        VectorUtil::removeIf( _listQueued, matches );
    }

    void UISubtitleService::postAudioCues( const vector<AudioSubtitleCue>& listCue )
    {
        for ( const AudioSubtitleCue& cue : listCue )
        {
            // 키는 `Namespace.Key` 이다 — 메시지로 들어 그릴 때 풀리고, 표에 없으면 빠진 키로 알리며 키가 보인다.
            (void)post( cue._speakerKey.empty() ? string{} : LocalizedMessage( cue._speakerKey.c_str() ).str(), LocalizedMessage( cue._textKey.c_str() ).str() );
            // 신호는 보이스를 얻은 클립만 낸다 — 다음 `refreshVoiceState` 까지는 나는 중으로 둔다.
            _listQueued.back()._voicePlayingID   = cue._playingID;
            _listQueued.back()._bVoicePlaying    = cue._playingID != 0;
            _listQueued.back()._bRunsInGamePause = cue._bPlaysInGamePause;
        }
    }

    void UISubtitleService::refreshVoiceState( const AudioEngine* pAudio )
    {
        for ( vector<UISubtitleLine>* pList : { &_listActive, &_listQueued } )
        {
            for ( UISubtitleLine& line : *pList )
            {
                // 오디오가 내려갔으면(장치 · 서비스 없음) 아무 음성도 나지 않는다 — 줄이 영원히 남지 않게 시간만 따른다.
                line._bVoicePlaying = line._voicePlayingID != 0 && pAudio != nullptr && pAudio->isPlaying( line._voicePlayingID );
            }
        }
    }

    void UISubtitleService::resetGameInstanceState( GameInstanceBoundary boundary )
    {
        if ( boundary == GameInstanceBoundary::Ended )
            clear();
    }

    const utf8* UISubtitleService::findGameInstanceState() const
    {
        return _listActive.empty() && _listQueued.empty() ? nullptr : "subtitles";
    }

    void UISubtitleService::clear()
    {
        _listActive.clear();
        _listQueued.clear();
        _screen.reset();
    }

    void UISubtitleService::update( float32 deltaSeconds )
    {
        // 1) 시간: 게임이 멈추면 게임 소리 · 시퀀스와 같이 선다(실제 시간으로 흐르면 다시 시작한 뒤 줄이 소리보다 먼저 사라진다). 설정이 꺼져 있어도 흐른다(켜면 지금 줄부터 보인다). 음성이 나는 줄은 시간이 다 돼도 남는다.
        for ( UISubtitleLine& line : _listActive )
        {
            if ( _bGamePaused == SW_FALSE || line._bRunsInGamePause )
                line._remainingSeconds -= deltaSeconds;
        }
        VectorUtil::removeIf( _listActive, []( const UISubtitleLine& line )
        { return line._remainingSeconds <= 0.0f && line._bVoicePlaying == false; } );
        // 2) 대기열: 자리가 나면 먼저 온 줄부터 올린다. 시간은 보이기 시작한 이 프레임부터 센다.
        while ( _listActive.size() < _config._maxVisibleLineCount && _listQueued.empty() == false )
        {
            _listActive.push_back( std::move( _listQueued.front() ) );
            _listQueued.erase( _listQueued.begin() );
        }

        // 3) 화면: 켜져 있고 줄이 있을 때만 연다.
        const bool bShow = gv_subtitles && _listActive.empty() == false;
        if ( bShow == false )
        {
            _screen.reset();
            return;
        }
        if ( _screen.isHeld() == false && openScreen() == false )
            return;
        UIScreen* pScreen = _screen.find();
        if ( pScreen == nullptr )
        {
            _screen.reset(); // 누군가 닫았으므로 다음 프레임에 다시 연다
            return;
        }
        const float32 sizeScale         = computeSizeScale( gv_subtitleSize );
        const float32 backgroundOpacity = MathUtil::clamp( static_cast<float32>( gv_subtitleBackgroundOpacity ), 0.0f, 1.0f );
        for ( uint32 slot = 0; slot < _config._maxVisibleLineCount; ++slot )
        {
            applySlot( *pScreen, slot, slot < _listActive.size() ? &_listActive[slot] : nullptr, sizeScale, backgroundOpacity );
        }
    }

    float32 UISubtitleService::computeReadingSeconds( string_view text ) const
    {
        uint32 characterCount = 0;
        size_t offset         = 0;
        while ( offset < text.size() )
        {
            (void)StringUtil::decodeUTF8( text, offset );
            ++characterCount;
        }
        return MathUtil::max( _config._minDisplaySeconds, static_cast<float32>( characterCount ) * _config._readingSecondsPerCharacter );
    }

    float32 UISubtitleService::computeSizeScale( int32 sizeSetting ) const
    {
        constexpr int32 kLast = static_cast<int32>( SubtitleConfig::kSizeSettingCount ) - 1;
        return _config._arrSizeScale[static_cast<size_t>( MathUtil::clamp( sizeSetting, 0, kLast ) )];
    }

    UISubtitleLayoutReport UISubtitleService::makeLayoutReport() const
    {
        using Internal = UISubtitleServiceInternal;
        UISubtitleLayoutReport report;
        const UIScreen*        pScreen = _screen.find();
        if ( pScreen == nullptr )
            return report;
        const WidgetTree& tree          = pScreen->getTree();
        const float32     viewportWidth = _pUI->getViewport()._size._x;
        const uint32      visibleCount  = MathUtil::min( static_cast<uint32>( _listActive.size() ), _config._maxVisibleLineCount );
        for ( uint32 slot = 0; slot < visibleCount; ++slot )
        {
            const BorderPanel* pBack = tree.findWidget<BorderPanel>( Internal::makeSlotName( "Line", slot ) );
            const TextWidget*  pText = tree.findWidget<TextWidget>( Internal::makeSlotName( "Text", slot ) );
            if ( pBack == nullptr || pText == nullptr )
                continue;
            const TextLayoutResult& layout  = pText->getLastLayout();
            const WidgetGeometry&   textBox = pText->getGeometry();
            const WidgetGeometry&   lineBox = pBack->getGeometry();
            // 텍스트는 픽셀 맞춤 여유(물리 픽셀 하나)만큼 상자를 넘어 놓일 수 있다(TextWidget::computeSnapSlack) — 그만큼은 넘침이 아니다.
            const float32 textSlack     = Internal::kOverflowSlack + TextWidget::computeSnapSlack( textBox._size._x, _pUI->getViewport()._uiScale );
            const bool    bTextWider    = layout._size._x > textBox._size._x + textSlack;
            const bool    bOffScreen    = lineBox._position._x < -Internal::kOverflowSlack || lineBox._position._x + lineBox._size._x > viewportWidth + Internal::kOverflowSlack;
            report._bOverflow           = report._bOverflow || layout._bTruncated == SW_TRUE || bTextWider || bOffScreen;
            report._maxWrappedLineCount = MathUtil::max( report._maxWrappedLineCount, static_cast<uint32>( layout._listLine.size() ) );
            report._newestTextFontSize  = pText->getTextStyle()._fontSize;
        }
        return report;
    }

    bool UISubtitleService::openScreen()
    {
        if ( _bDocumentFailed )
            return false;
        const string& documentPath = _pUI->getEngineDocuments()._subtitles;
        _screen                    = UIOwnedScreen( *_pUI, _pUI->openScreen( documentPath ) );
        UIScreen* pScreen          = _screen.find();
        if ( pScreen == nullptr )
        {
            SW_LOG_ERROR_ID( "UISubtitle.SubtitleScreenCouldNot", "[UI] Subtitle screen '%#' could not be opened - subtitles are not shown", documentPath.c_str() );
            _screen.reset();
            _bDocumentFailed = SW_TRUE;
            return false;
        }
        // 설정이 정한 줄 수만큼 자리(`Line<n>` · `Speaker<n>` · `Text<n>`)가 있어야 한다 — 모자라면 쌓인 줄이 말없이 사라진다.
        const WidgetTree& tree = pScreen->getTree();
        for ( uint32 slot = 0; slot < _config._maxVisibleLineCount; ++slot )
        {
            const bool bMissingSlotWidgets = tree.findWidget<BorderPanel>( UISubtitleServiceInternal::makeSlotName( "Line", slot ) ) == nullptr ||
                                             tree.findWidget<TextWidget>( UISubtitleServiceInternal::makeSlotName( "Speaker", slot ) ) == nullptr ||
                                             tree.findWidget<TextWidget>( UISubtitleServiceInternal::makeSlotName( "Text", slot ) ) == nullptr;
            if ( bMissingSlotWidgets )
            {
                SW_LOG_ERROR_ID( "UISubtitle.SubtitleScreenLacksLineSlot", "[UI] Subtitle screen '%#' lacks Line%# / Speaker%# / Text%# for %# visible lines (EngineConfig _subtitles) - subtitles are not shown",
                                 documentPath.c_str(), slot, slot, slot, _config._maxVisibleLineCount );
                _screen.reset();
                _bDocumentFailed = SW_TRUE;
                return false;
            }
        }
        // 문서에 적힌 크기와 바탕이 기준이다(설정이 곱하고 알파를 바꾼다). 슬롯 0 의 값을 모든 슬롯에 쓴다.
        _authoredBackground      = tree.findWidget<BorderPanel>( UISubtitleServiceInternal::makeSlotName( "Line", 0 ) )->getBackground();
        _authoredSpeakerFontSize = tree.findWidget<TextWidget>( UISubtitleServiceInternal::makeSlotName( "Speaker", 0 ) )->getTextStyle()._fontSize;
        _authoredTextFontSize    = tree.findWidget<TextWidget>( UISubtitleServiceInternal::makeSlotName( "Text", 0 ) )->getTextStyle()._fontSize;
        return true;
    }

    void UISubtitleService::applySlot( UIScreen& screen, uint32 slot, const UISubtitleLine* pLine, float32 sizeScale, float32 backgroundOpacity )
    {
        using Internal                        = UISubtitleServiceInternal;
        const WidgetTree& tree                = screen.getTree();
        BorderPanel*      pBack               = tree.findWidget<BorderPanel>( Internal::makeSlotName( "Line", slot ) );
        TextWidget*       pName               = tree.findWidget<TextWidget>( Internal::makeSlotName( "Speaker", slot ) );
        TextWidget*       pText               = tree.findWidget<TextWidget>( Internal::makeSlotName( "Text", slot ) );
        const bool        bMissingSlotWidgets = pBack == nullptr || pName == nullptr || pText == nullptr;
        if ( bMissingSlotWidgets )
            return;
        if ( pLine == nullptr )
        {
            pBack->setVisibility( WidgetVisibility::Collapsed );
            return;
        }
        pBack->setVisibility( WidgetVisibility::HitTestInvisible );

        UIBrush background   = _authoredBackground;
        background._color._w = backgroundOpacity;
        if ( background._color != pBack->getBackground()._color )
            pBack->setBackground( background );

        pName->setVisibility( pLine->_speaker.empty() ? WidgetVisibility::Collapsed : WidgetVisibility::HitTestInvisible );
        pName->setText( pLine->_speaker );
        pText->setText( pLine->_text );
        TextLayoutStyle nameStyle = pName->getTextStyle();
        if ( nameStyle._fontSize != _authoredSpeakerFontSize * sizeScale )
        {
            nameStyle._fontSize = _authoredSpeakerFontSize * sizeScale;
            pName->setTextStyle( nameStyle );
        }
        TextLayoutStyle textStyle = pText->getTextStyle();
        if ( textStyle._fontSize != _authoredTextFontSize * sizeScale )
        {
            textStyle._fontSize = _authoredTextFontSize * sizeScale;
            pText->setTextStyle( textStyle );
        }
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( UISubtitleService, GameVisible, GameInstance, Startup, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        (void)build;
        UISubtitleService& service  = scope.emplaceSubsystem<UISubtitleService>();
        const bool         bExposed = scope.exposeMember<ISubtitleSink>( service, service.getSubtitleSink() );
        SW_ASSERT( bExposed );
        (void)bExposed;
    } );
} // namespace sw
