#include "pch.h"

#include "Engine/UI/Screen/UiSubtitleService.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/BorderPanel.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "UiSubtitle" );

    namespace
    {
        struct UiSubtitleServiceInternal
        {
            /** @brief 크기 설정 0 · 1 · 2 의 글 크기 배입니다(XAG 의 "작게 · 보통 · 크게"). */
            static constexpr float32 kArrSizeScale[] = { 0.85f, 1.0f, 1.3f };

            /** @brief 줄 자리 @p slot 의 위젯 이름 `<prefix><slot>` 입니다. */
            static hashed_string makeSlotName( const utf8* pPrefix, uint32 slot ) { return hashed_string( string( pPrefix ) + to_string( slot ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiSubtitleService::UiSubtitleService( UiSystem& uiSystem )
        : _uiSystem{ uiSystem }
        , _listActive{}
        , _listQueued{}
        , _authoredBackground{}
        , _authoredTextFontSize{ 0.0f }
        , _authoredSpeakerFontSize{ 0.0f }
        , _screen{ kInvalidUiScreenHandle }
        , _nextLineId{ 1 }
        , _bDocumentFailed{ false }
    {
    }

    UiSubtitleService::~UiSubtitleService() = default;

    uint32 UiSubtitleService::post( string_view speaker, string_view text, float32 durationSeconds )
    {
        UiSubtitleLine line{};
        line._speaker          = string{ speaker };
        line._text             = string{ text };
        line._durationSeconds  = durationSeconds > 0.0f ? durationSeconds : computeReadingSeconds( text );
        line._remainingSeconds = line._durationSeconds;
        line._id               = _nextLineId++;
        _listQueued.push_back( std::move( line ) );
        return _listQueued.back()._id;
    }

    void UiSubtitleService::clear()
    {
        _listActive.clear();
        _listQueued.clear();
        closeScreen();
    }

    void UiSubtitleService::update( float32 deltaSeconds )
    {
        // 1) 시간 — 설정이 꺼져 있어도 흐른다(켜면 지금 줄부터 보인다).
        for ( UiSubtitleLine& line : _listActive )
        {
            line._remainingSeconds -= deltaSeconds;
        }
        _listActive.erase( std::remove_if( _listActive.begin(), _listActive.end(), []( const UiSubtitleLine& line )
        { return line._remainingSeconds <= 0.0f; } ),
                           _listActive.end() );
        // 2) 대기열 — 자리가 나면 먼저 온 줄부터. 시간은 보이기 시작한 이 프레임부터 센다.
        while ( _listActive.size() < kMaxVisibleLineCount && _listQueued.empty() == false )
        {
            _listActive.push_back( std::move( _listQueued.front() ) );
            _listQueued.erase( _listQueued.begin() );
        }

        // 3) 화면 — 켜져 있고 줄이 있을 때만 연다.
        const bool bShow = gv_subtitles && _listActive.empty() == false;
        if ( bShow == false )
        {
            closeScreen();
            return;
        }
        if ( _screen == kInvalidUiScreenHandle && openScreen() == false )
            return;
        UiScreen* pScreen = _uiSystem.findScreen( _screen );
        if ( pScreen == nullptr )
        {
            _screen = kInvalidUiScreenHandle; // 누군가 닫았다 — 다음 프레임에 다시 연다
            return;
        }
        const float32 sizeScale         = computeSizeScale( gv_subtitleSize );
        const float32 backgroundOpacity = MathUtil::clamp( static_cast<float32>( gv_subtitleBackgroundOpacity ), 0.0f, 1.0f );
        for ( uint32 slot = 0; slot < kMaxVisibleLineCount; ++slot )
        {
            applySlot( *pScreen, slot, slot < _listActive.size() ? &_listActive[slot] : nullptr, sizeScale, backgroundOpacity );
        }
    }

    float32 UiSubtitleService::computeReadingSeconds( string_view text )
    {
        uint32 characterCount = 0;
        size_t offset         = 0;
        while ( offset < text.size() )
        {
            (void)StringUtil::decodeUtf8( text, offset );
            ++characterCount;
        }
        return MathUtil::max( kMinReadingSeconds, static_cast<float32>( characterCount ) * kReadingSecondsPerCharacter );
    }

    float32 UiSubtitleService::computeSizeScale( int32 sizeSetting )
    {
        constexpr int32 kLast = static_cast<int32>( std::size( UiSubtitleServiceInternal::kArrSizeScale ) ) - 1;
        return UiSubtitleServiceInternal::kArrSizeScale[MathUtil::clamp( sizeSetting, 0, kLast )];
    }

    bool UiSubtitleService::openScreen()
    {
        if ( _bDocumentFailed )
            return false;
        _screen           = _uiSystem.openScreen( kDocumentPath );
        UiScreen* pScreen = _uiSystem.findScreen( _screen );
        if ( pScreen == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Subtitle screen '%#' could not be opened - subtitles are not shown", kDocumentPath );
            _screen          = kInvalidUiScreenHandle;
            _bDocumentFailed = true;
            return false;
        }
        // 문서에 적힌 크기 · 바탕이 기준이다(설정이 곱하고 알파를 바꾼다). 자리 0 의 것을 모든 자리에 쓴다.
        const WidgetTree&  tree     = pScreen->getTree();
        const BorderPanel* pLine    = tree.findWidget<BorderPanel>( UiSubtitleServiceInternal::makeSlotName( "Line", 0 ) );
        const TextWidget*  pSpeaker = tree.findWidget<TextWidget>( UiSubtitleServiceInternal::makeSlotName( "Speaker", 0 ) );
        const TextWidget*  pText    = tree.findWidget<TextWidget>( UiSubtitleServiceInternal::makeSlotName( "Text", 0 ) );
        if ( pLine == nullptr || pSpeaker == nullptr || pText == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Subtitle screen '%#' lacks Line0 / Speaker0 / Text0 - subtitles are not shown", kDocumentPath );
            closeScreen();
            _bDocumentFailed = true;
            return false;
        }
        _authoredBackground      = pLine->getBackground();
        _authoredSpeakerFontSize = pSpeaker->getTextStyle()._fontSize;
        _authoredTextFontSize    = pText->getTextStyle()._fontSize;
        return true;
    }

    void UiSubtitleService::closeScreen()
    {
        if ( _screen == kInvalidUiScreenHandle )
            return;
        _uiSystem.closeScreen( _screen );
        _screen = kInvalidUiScreenHandle;
    }

    void UiSubtitleService::applySlot( UiScreen& screen, uint32 slot, const UiSubtitleLine* pLine, float32 sizeScale, float32 backgroundOpacity )
    {
        using Internal          = UiSubtitleServiceInternal;
        const WidgetTree& tree  = screen.getTree();
        BorderPanel*      pBack = tree.findWidget<BorderPanel>( Internal::makeSlotName( "Line", slot ) );
        TextWidget*       pName = tree.findWidget<TextWidget>( Internal::makeSlotName( "Speaker", slot ) );
        TextWidget*       pText = tree.findWidget<TextWidget>( Internal::makeSlotName( "Text", slot ) );
        if ( pBack == nullptr || pName == nullptr || pText == nullptr )
            return;
        if ( pLine == nullptr )
        {
            pBack->setVisibility( WidgetVisibility::Collapsed );
            return;
        }
        pBack->setVisibility( WidgetVisibility::HitTestInvisible );

        UiBrush background   = _authoredBackground;
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
