#include "pch.h"

#include "Engine/UI/Screens/UiNotificationService.h"

#include "Core/Log/Logger.h"

#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Document/UiDocumentCache.h"
#include "Engine/UI/Document/UiDocumentLoader.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    SW_LOG_CALLER( "UiNotificationService" );

    namespace
    {
        struct UiNotificationServiceInternal
        {
            static constexpr utf8 kStackName[]   = "Stack";
            static constexpr utf8 kMessageName[] = "Message";
            static constexpr utf8 kCountName[]   = "Count";

            static const utf8* getKindClass( UiNotificationKind kind )
            {
                switch ( kind )
                {
                    case UiNotificationKind::Info:
                        return "notification info";
                    case UiNotificationKind::Achievement:
                        return "notification achievement";
                    case UiNotificationKind::Warning:
                        return "notification warning";
                    case UiNotificationKind::Hint:
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

    UiNotificationService::UiNotificationService( UiSystem& ui )
        : _listVisible{}
        , _listQueued{}
        , _ui{ ui }
        , _nextOrder{ 0 }
        , _screen{ kInvalidUiScreenHandle }
    {
    }

    void UiNotificationService::post( const UiNotificationDesc& desc )
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

    void UiNotificationService::update( float32 deltaSeconds )
    {
        // 1) 보이는 것의 시간 — 끝난 것을 내린다.
        UiScreen* pScreen = _ui.findScreen( _screen );
        for ( size_t index = _listVisible.size(); index > 0; --index )
        {
            Entry& entry = _listVisible[index - 1];
            entry._ageSeconds += deltaSeconds;
            if ( entry._ageSeconds < entry._desc._durationSeconds )
                continue;
            Widget*      pWidget = pScreen != nullptr ? pScreen->getTree().findWidgetById( entry._widget ) : nullptr;
            PanelWidget* pParent = pWidget != nullptr ? pWidget->getParent() : nullptr;
            if ( pParent != nullptr )
                (void)pParent->removeChild( pWidget );
            _listVisible.erase( _listVisible.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
        // 2) 자리가 나면 기다리는 것을 올린다 — 우선순위가 큰 것, 같으면 먼저 온 것.
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
        // 3) 보일 것이 없으면 화면을 닫는다(오버레이라 입력과 무관 — 열고 닫는 비용은 알림 단위).
        if ( _listVisible.empty() && _screen != kInvalidUiScreenHandle )
        {
            _ui.closeScreen( _screen );
            _screen = kInvalidUiScreenHandle;
        }
    }

    void UiNotificationService::clear()
    {
        _listVisible.clear();
        _listQueued.clear();
        if ( _screen != kInvalidUiScreenHandle && _ui.findScreen( _screen ) != nullptr )
            _ui.closeScreen( _screen );
        _screen = kInvalidUiScreenHandle;
    }

    UiNotificationService::Entry* UiNotificationService::findSameText( const string& text )
    {
        for ( Entry& entry : _listVisible )
        {
            if ( entry._desc._text == text )
                return &entry;
        }
        for ( Entry& entry : _listQueued )
        {
            if ( entry._desc._text == text )
                return &entry;
        }
        return nullptr;
    }

    UiScreen* UiNotificationService::acquireScreen()
    {
        UiScreen* pScreen = _ui.findScreen( _screen );
        if ( pScreen != nullptr && pScreen->isClosing() == false )
            return pScreen;
        _screen = _ui.openScreen( kScreenDocument );
        return _ui.findScreen( _screen );
    }

    void UiNotificationService::showEntry( Entry& entry )
    {
        using Internal    = UiNotificationServiceInternal;
        UiScreen* pScreen = acquireScreen();
        if ( pScreen == nullptr )
            return; // 문서를 짓지 못했다 — 오류는 짓기가 남겼다
        PanelWidget* pStack = pScreen->getTree().findWidget<PanelWidget>( Internal::kStackName );
        if ( pStack == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Notification document %# has no '%#' panel", kScreenDocument, Internal::kStackName );
            return;
        }
        string                                  error;
        const shared_ptr<const UiDocumentAsset> document = _ui.getDocumentCache().findOrLoad( kEntryDocument, error );
        vector<UiBindingDesc>                   listBinding;
        unique_ptr<Widget>                      widget = document != nullptr ? UiDocumentLoader::instantiate( *document, _ui.getDocumentCache(), listBinding, error ) : nullptr;
        if ( widget == nullptr )
        {
            SW_LOG_ERROR( "[Ui] Notification entry failed: %#", error.c_str() );
            return;
        }
        widget->setStyleClass( Internal::getKindClass( entry._desc._kind ) );
        entry._widget = widget->getId();
        (void)pStack->addChild( std::move( widget ) );
        refreshEntry( entry );
    }

    void UiNotificationService::refreshEntry( const Entry& entry )
    {
        using Internal    = UiNotificationServiceInternal;
        UiScreen* pScreen = _ui.findScreen( _screen );
        Widget*   pEntry  = pScreen != nullptr ? pScreen->getTree().findWidgetById( entry._widget ) : nullptr;
        if ( pEntry == nullptr )
            return; // 아직 기다리는 중
        if ( TextWidget* pMessage = castTo<TextWidget>( Internal::findDescendant( *pEntry, Internal::kMessageName ) ); pMessage != nullptr )
            pMessage->setText( entry._desc._text );
        if ( TextWidget* pCount = castTo<TextWidget>( Internal::findDescendant( *pEntry, Internal::kCountName ) ); pCount != nullptr )
        {
            pCount->setText( "x" + to_string( entry._count ) );
            pCount->setVisibility( entry._count > 1 ? WidgetVisibility::Visible : WidgetVisibility::Collapsed );
        }
    }
} // namespace sw
