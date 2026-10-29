#include "pch.h"

#include "Engine/UI/Screen/UINotificationService.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Document/UIDocumentLoader.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "UINotificationService" );

    namespace
    {
        struct UINotificationServiceInternal
        {
            static constexpr utf8 kStackName[]   = "Stack";
            static constexpr utf8 kMessageName[] = "Message";
            static constexpr utf8 kCountName[]   = "Count";

            static const utf8* getKindClass( UINotificationKind kind )
            {
                switch ( kind )
                {
                    case UINotificationKind::Info:
                        return "notification info";
                    case UINotificationKind::Achievement:
                        return "notification achievement";
                    case UINotificationKind::Warning:
                        return "notification warning";
                    case UINotificationKind::Hint:
                        return "notification hint";
                }
                return "notification info";
            }

            /** @brief @p widget 아래에서 이름 @p name 의 위젯을 찾습니다(항목마다 같은 이름이라 트리 전체가 아니라 그 항목 안에서). */
            static Widget* findDescendant( Widget& widget, const hashed_string& name )
            {
                if ( widget.getName() == name )
                    return &widget;
                PanelWidget* pPanel = castTo<PanelWidget>( &widget );
                if ( pPanel == nullptr )
                    return nullptr;
                for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
                {
                    if ( Widget* pFound = findDescendant( *pPanel->getChild( index ), name ); pFound != nullptr )
                        return pFound;
                }
                return nullptr;
            }
        };
    } // namespace

    UINotificationService::UINotificationService( UISystem& ui )
        : _listVisible{}
        , _listQueued{}
        , _listFading{}
        , _ui{ ui }
        , _nextOrder{ 0 }
        , _screen{ kInvalidUIScreenHandle }
    {
    }

    void UINotificationService::post( const UINotificationDesc& desc )
    {
        if ( desc._text.empty() )
            return;
        if ( desc._bMergeSameText )
        {
            if ( Entry* pSame = findSameText( desc._text ); pSame != nullptr )
            {
                ++pSame->_count;
                pSame->_ageSeconds     = 0.0f;
                pSame->_desc._priority = desc._priority > pSame->_desc._priority ? desc._priority : pSame->_desc._priority;
                refreshEntry( *pSame );
                return;
            }
        }
        Entry entry{};
        entry._desc  = desc;
        entry._order = _nextOrder++;
        _listQueued.push_back( entry );
    }

    hashed_string UINotificationService::getEntryWidgetName( uint64 order, string_view part )
    {
        string name = to_string( order );
        if ( part.empty() == false )
        {
            name += ".";
            name.append( part.data(), part.size() );
        }
        return hashed_string( name );
    }

    void UINotificationService::update( float32 deltaSeconds )
    {
        // 0) 사라지는 중인 항목: 페이드가 끝나면 위젯을 지운다.
        // 시간을 더하고 끝난 것의 뒤처리를 한 번 돈 뒤, 지우는 것은 `removeIf` 한 번이다(한 프레임에 여럿이 끝나도 뒤 원소를 한 번만 옮긴다).
        // 위젯을 지우거나 페이드를 거는 호출은 두 목록을 건드리지 않는다. 남은 순서는 그대로다(보이는 알림의 쌓는 순서).
        const auto isFadeDone = []( const FadingEntry& fading )
        { return ( fading._remainingSeconds > 0.0f ) == false; };
        for ( FadingEntry& fading : _listFading )
        {
            fading._remainingSeconds -= deltaSeconds;
            if ( isFadeDone( fading ) )
                removeEntryWidget( fading._widget );
        }
        VectorUtil::removeIf( _listFading, isFadeDone );
        // 1) 보이는 알림의 시간: 끝난 것을 내린다(자리는 바로 비고, 위젯은 페이드 뒤에 지운다).
        const auto isTimeUp = []( const Entry& entry )
        { return ( entry._ageSeconds < entry._desc._durationSeconds ) == false; };
        for ( Entry& entry : _listVisible )
        {
            entry._ageSeconds += deltaSeconds;
            if ( isTimeUp( entry ) )
                startFadeOut( entry ); // 페이드 목록에 더한다(위 순회는 이미 끝났다)
        }
        VectorUtil::removeIf( _listVisible, isTimeUp );
        // 2) 자리가 나면 기다리는 알림을 올린다. 우선순위가 큰 것이 먼저이고, 같으면 먼저 온 것이 먼저다.
        while ( _listVisible.size() < kMaxVisible && _listQueued.empty() == false )
        {
            size_t best = 0;
            for ( size_t index = 1; index < _listQueued.size(); ++index )
            {
                const Entry& candidate = _listQueued[index];
                const Entry& current   = _listQueued[best];
                if ( candidate._desc._priority > current._desc._priority ||
                     ( candidate._desc._priority == current._desc._priority && candidate._order < current._order ) )
                    best = index;
            }
            Entry entry = _listQueued[best];
            _listQueued.erase( _listQueued.begin() + static_cast<ptrdiff_t>( best ) );
            entry._ageSeconds = 0.0f;
            _listVisible.push_back( entry );
            showEntry( _listVisible.back() );
        }
        announceVisible();
        // 3) 보일 것이 없으면 화면을 닫는다(오버레이라 입력과 무관하며, 열고 닫는 비용은 알림 단위로 든다).
        const bool bIdleScreenOpen = _listVisible.empty() && _listFading.empty() && _screen != kInvalidUIScreenHandle;
        if ( bIdleScreenOpen )
        {
            _ui.closeScreen( _screen );
            _screen = kInvalidUIScreenHandle;
        }
    }

    void UINotificationService::announceVisible()
    {
        UIScreen* pScreen = _screen != kInvalidUIScreenHandle ? _ui.findScreen( _screen ) : nullptr;
        if ( pScreen == nullptr )
            return;
        for ( Entry& entry : _listVisible )
        {
            if ( entry._bAnnounced )
                continue;
            const TextWidget* pMessage = pScreen->getTree().findWidget<TextWidget>( getEntryWidgetName( entry._order, "Message" ) );
            if ( pMessage == nullptr || pMessage->getDisplayText().empty() )
                continue; // 아직 배치 전이다 — 다음 갱신에 다시 본다
            const UIAnnouncePriority priority = entry._desc._kind == UINotificationKind::Warning ? UIAnnouncePriority::Assertive : UIAnnouncePriority::Polite;
            _ui.announce( pMessage->getDisplayText(), priority );
            entry._bAnnounced = true;
        }
    }

    void UINotificationService::clear()
    {
        _listVisible.clear();
        _listQueued.clear();
        _listFading.clear();
        if ( _screen != kInvalidUIScreenHandle && _ui.findScreen( _screen ) != nullptr )
            _ui.closeScreen( _screen );
        _screen = kInvalidUIScreenHandle;
    }

    UINotificationService::Entry* UINotificationService::findSameText( const string& text )
    {
        for ( Entry& entry : _listVisible )
        {
            if ( entry._desc._text == text )
                return &entry;
        }
        return VectorUtil::findIf( _listQueued, [&]( Entry& entry )
        { return entry._desc._text == text; } );
    }

    UIScreen* UINotificationService::acquireScreen()
    {
        UIScreen* pScreen = _ui.findScreen( _screen );
        if ( pScreen != nullptr && pScreen->isClosing() == false )
            return pScreen;
        _screen = _ui.openScreen( _ui.getEngineDocuments()._notifications );
        return _ui.findScreen( _screen );
    }

    void UINotificationService::showEntry( Entry& entry )
    {
        using Internal    = UINotificationServiceInternal;
        UIScreen* pScreen = acquireScreen();
        if ( pScreen == nullptr )
            return; // 문서를 만들지 못했다. 오류는 문서 생성 단계가 남겼다
        PanelWidget* pStack = pScreen->getTree().findWidget<PanelWidget>( Internal::kStackName );
        if ( pStack == nullptr )
        {
            SW_LOG_ERROR( "[UI] Notification document %# has no '%#' panel", _ui.getEngineDocuments()._notifications.c_str(), Internal::kStackName );
            return;
        }
        string                                  error;
        const shared_ptr<const UIDocumentAsset> document = _ui.getDocumentCache().findOrLoad( _ui.getEngineDocuments()._notificationEntry, error );
        vector<UIBindingDesc>                   listBinding;
        unique_ptr<Widget>                      widget = document != nullptr ? UIDocumentLoader::instantiate( *document, _ui.getDocumentCache(), listBinding, error ) : nullptr;
        if ( widget == nullptr )
        {
            SW_LOG_ERROR( "[UI] Notification entry failed: %#", error.c_str() );
            return;
        }
        widget->setStyleClass( Internal::getKindClass( entry._desc._kind ) );
        // 같은 조각을 항목마다 붙이므로 붙이기 전에 이름을 항목 번호로 가른다(붙은 뒤에는 "name is used twice" 경고가 난다).
        widget->setName( getEntryWidgetName( entry._order ) );
        if ( Widget* pMessage = Internal::findDescendant( *widget, Internal::kMessageName ); pMessage != nullptr )
            pMessage->setName( getEntryWidgetName( entry._order, Internal::kMessageName ) );
        if ( Widget* pCount = Internal::findDescendant( *widget, Internal::kCountName ); pCount != nullptr )
            pCount->setName( getEntryWidgetName( entry._order, Internal::kCountName ) );
        entry._widget = widget->getID();
        widget->setOpacity( 0.0f ); // 페이드로 들어온다(움직임 줄이기이면 아래 트윈이 바로 1 을 쓴다)
        (void)pStack->addChild( std::move( widget ) );
        if ( _ui.tween( entry._widget, "_opacity", "1", kFadeSeconds, BlendCurve::EaseOut ) == false )
        {
            if ( Widget* pAdded = pScreen->getTree().findWidgetByID( entry._widget ); pAdded != nullptr )
                pAdded->setOpacity( 1.0f ); // 트윈을 걸지 못하면 보이지 않는 채로 남지 않게 한다
        }
        refreshEntry( entry );
    }

    void UINotificationService::refreshEntry( const Entry& entry )
    {
        using Internal    = UINotificationServiceInternal;
        UIScreen* pScreen = _ui.findScreen( _screen );
        Widget*   pEntry  = pScreen != nullptr ? pScreen->getTree().findWidgetByID( entry._widget ) : nullptr;
        if ( pEntry == nullptr )
            return; // 아직 기다리는 중이다
        if ( TextWidget* pMessage = castTo<TextWidget>( Internal::findDescendant( *pEntry, getEntryWidgetName( entry._order, Internal::kMessageName ) ) ); pMessage != nullptr )
            pMessage->setText( entry._desc._text );
        if ( TextWidget* pCount = castTo<TextWidget>( Internal::findDescendant( *pEntry, getEntryWidgetName( entry._order, Internal::kCountName ) ) ); pCount != nullptr )
        {
            pCount->setText( formatToString( "x%#", entry._count ) );
            pCount->setVisibility( entry._count > 1 ? WidgetVisibility::Visible : WidgetVisibility::Collapsed );
        }
    }

    void UINotificationService::startFadeOut( const Entry& entry )
    {
        UIScreen* pScreen = _ui.findScreen( _screen );
        Widget*   pWidget = pScreen != nullptr ? pScreen->getTree().findWidgetByID( entry._widget ) : nullptr;
        if ( pWidget == nullptr )
            return; // 위젯을 만들지 못했던 항목이다
        if ( gv_uiReduceMotion || _ui.tween( entry._widget, "_opacity", "0", kFadeSeconds, BlendCurve::EaseIn ) == false )
        {
            removeEntryWidget( entry._widget );
            return;
        }
        FadingEntry fading{};
        fading._widget           = entry._widget;
        fading._remainingSeconds = kFadeSeconds;
        _listFading.push_back( fading );
    }

    void UINotificationService::removeEntryWidget( WidgetID widget )
    {
        UIScreen*    pScreen = _ui.findScreen( _screen );
        Widget*      pWidget = pScreen != nullptr ? pScreen->getTree().findWidgetByID( widget ) : nullptr;
        PanelWidget* pParent = pWidget != nullptr ? pWidget->getParent() : nullptr;
        if ( pParent != nullptr )
            (void)pParent->removeChild( pWidget ); // 부모에서 찾은 위젯이라 항상 제거된다
    }
} // namespace sw
