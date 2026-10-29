#include "pch.h"

#include "Engine/UI/Screen/UIOffscreenScreens.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Animation/UIStyleTransition.h"
#include "Engine/UI/Document/UIDocumentCache.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Render/UIPaintPass.h"
#include "Engine/UI/Style/UIStylePass.h"
#include "Engine/UI/Style/UIStyleSet.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/Style/UIStyleSheetCache.h"
#include "Engine/UI/Style/UIThemeController.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

namespace sw
{
    SW_LOG_CALLER( "UIOffscreenScreens" );

    UIOffscreenScreens::UIOffscreenScreens()
        : _listOffscreen{}
        , _pUI{ nullptr }
        , _pThemes{ nullptr }
        , _updateSubscription{}
        , _documentReloadedSubscription{}
        , _styleSheetReloadedSubscription{}
        , _themeChangedSubscription{}
    {
    }

    UIOffscreenScreens::~UIOffscreenScreens() = default;

    SubsystemInitResult UIOffscreenScreens::initializeSubsystem()
    {
        _pUI                            = &engine::getSubsystem<UISystem>();
        _pThemes                        = &engine::getSubsystem<UIThemeController>();
        _updateSubscription             = _pUI->onUpdateAfterPaint().add( SW_DELEGATE_METHOD( Delegate<void( float32 )>, &UIOffscreenScreens::update, this ) );
        _documentReloadedSubscription   = engine::getSubsystem<UIDocumentCache>().onReloaded().add( SW_DELEGATE_METHOD( UIAssetReloadedDelegate, &UIOffscreenScreens::onDocumentReloaded, this ) );
        _styleSheetReloadedSubscription = engine::getSubsystem<UIStyleSheetCache>().onReloaded().add( SW_DELEGATE_METHOD( UIAssetReloadedDelegate, &UIOffscreenScreens::onStyleSheetReloaded, this ) );
        _themeChangedSubscription       = _pThemes->onThemeChanged().add( SW_DELEGATE_METHOD( Delegate<void()>, &UIOffscreenScreens::onThemeChanged, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void UIOffscreenScreens::shutdownSubsystem()
    {
        _themeChangedSubscription.reset();
        _styleSheetReloadedSubscription.reset();
        _documentReloadedSubscription.reset();
        _updateSubscription.reset();
        _listOffscreen.clear(); // 종료 누수 보고는 이 서브시스템의 소멸보다 먼저다 — 화면 트리와 그리기 목록을 지금 놓는다
        _pThemes = nullptr;
        _pUI     = nullptr;
    }

    span<const utf8* const> UIOffscreenScreens::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { UISystem::kSubsystemName, UIThemeController::kSubsystemName, UIDocumentCache::kSubsystemName, UIStyleSheetCache::kSubsystemName };
        return kArrDependency;
    }

    UIScreenHandle UIOffscreenScreens::open( string_view documentPath, string_view targetPath )
    {
        unique_ptr<UIScreen> screen = _pUI->createDocumentScreen( documentPath );
        if ( screen == nullptr )
            return kInvalidUIScreenHandle; // 오류는 문서 생성 단계가 남겼다
        // 애니메이션은 등록만 됐다 — 스택 밖 화면은 `Open` 을 틀지 않아 문서 값 그대로다(미리보기가 고른 것만 튼다).
        UIOffscreenScreen& offscreen = _listOffscreen.emplace_back();
        offscreen._screen            = std::move( screen );
        offscreen._targetPath        = hashed_string( targetPath );
        return offscreen._screen->getHandle();
    }

    void UIOffscreenScreens::remove( UIScreenHandle handle )
    {
        for ( size_t index = 0; index < _listOffscreen.size(); ++index )
        {
            if ( _listOffscreen[index]._screen->getHandle() != handle )
                continue;
            _listOffscreen.erase( _listOffscreen.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    UIScreen* UIOffscreenScreens::findScreen( UIScreenHandle handle ) const
    {
        for ( const UIOffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->getHandle() == handle )
                return offscreen._screen.get();
        }
        return nullptr;
    }

    void UIOffscreenScreens::setView( UIScreenHandle handle, const UIViewport& viewport, float32 textScale, const hashed_string& theme )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->getHandle() != handle )
                continue;
            offscreen._viewport  = viewport;
            offscreen._textScale = textScale > 0.0f ? textScale : 1.0f;
            if ( offscreen._theme == theme )
                return;
            // 테마가 바뀌면 그 화면의 스타일 시트만 다시 적용한다(게임 UI 의 테마는 그대로다).
            offscreen._theme = theme;
            _pThemes->rebuildStyleSet( *offscreen._screen, theme );
            return;
        }
    }

    void UIOffscreenScreens::setLocalization( UIScreenHandle handle, const LocalizationManager* pLocalization )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->getHandle() != handle )
                continue;
            offscreen._pLocalization = pLocalization;
            // 문화권이나 그 텍스트가 바뀌었다 — 텍스트 위젯이 글을 다시 해석하고, 흐름 방향(RTL)이 바뀔 수 있으므로 루트부터 다시 맞춘다.
            WidgetTree&     tree = offscreen._screen->getTree();
            vector<Widget*> listWidget;
            tree.collectWidgetsInDocumentOrder( listWidget );
            for ( Widget* pWidget : listWidget )
            {
                pWidget->onTextRevisionChanged();
            }
            UILayoutPass::invalidateAllLayout( tree );
            return;
        }
    }

    void UIOffscreenScreens::setTargetPath( UIScreenHandle handle, const hashed_string& targetPath )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->getHandle() != handle )
                continue;
            offscreen._targetPath = targetPath;
            ++offscreen._revision; // 새 대상은 내용을 처음부터 받아야 한다
            return;
        }
    }

    void UIOffscreenScreens::setNavigationMode( UIScreenHandle handle, bool bNavigationMode )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->getHandle() != handle )
                continue;
            if ( offscreen._bNavigationMode == bNavigationMode )
                return;
            offscreen._bNavigationMode = bNavigationMode;
            // `:focus-visible` 은 입력 방식을 따르므로 포커스 위젯의 스타일을 다시 맞춘다(게임 UI 의 `UIInputRouter::setInputMode` 와 같다).
            WidgetTree& tree     = offscreen._screen->getTree();
            Widget*     pFocused = tree.findWidgetByID( tree.getFocusedWidget() );
            if ( pFocused != nullptr )
                pFocused->invalidate( WidgetDirty::kStyle );
            return;
        }
    }

    void UIOffscreenScreens::updateAnimations( float32 deltaSeconds )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            UIAnimationPlayer& player = offscreen._screen->getAnimationPlayer();
            if ( player.isAnyPlaying() )
                player.update( deltaSeconds );
        }
    }

    void UIOffscreenScreens::update( float32 deltaSeconds )
    {
        if ( _listOffscreen.empty() )
            return;
        updateAnimations( deltaSeconds );
        paintScreens( _pUI->makeLayoutContext(), _pUI->makePaintContext() );
    }

    void UIOffscreenScreens::onDocumentReloaded( string_view documentPath )
    {
        uint32 rebuiltCount = 0;
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            UIScreen& screen = *offscreen._screen;
            if ( _pUI->isDocumentUsing( screen.getDocumentPath(), documentPath ) && _pUI->rebuildScreenFromDocument( screen ) )
            {
                _pThemes->rebuildStyleSet( screen, offscreen._theme ); // 다시 짓기는 지금 테마로 맞췄다 — 이 화면의 테마로 되돌린다
                ++rebuiltCount;
            }
        }
        if ( rebuiltCount > 0 )
            SW_LOG_INFO( "[UI] Reloaded %# offscreen screen(s) for %#", rebuiltCount, documentPath );
    }

    void UIOffscreenScreens::onStyleSheetReloaded( string_view sheetPath )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            const UIStyleSet* pStyleSet = offscreen._screen->getStyleSet();
            if ( pStyleSet == nullptr )
                continue;
            bool bUses = false;
            for ( const shared_ptr<const UIStyleSheetAsset>& sheet : pStyleSet->getSheets() )
            {
                bUses = bUses || ( sheet != nullptr && sheet->_path == sheetPath );
            }
            if ( bUses )
                _pThemes->rebuildStyleSet( *offscreen._screen, offscreen._theme );
        }
    }

    void UIOffscreenScreens::onThemeChanged()
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            _pThemes->rebuildStyleSet( *offscreen._screen, offscreen._theme );
        }
    }

    void UIOffscreenScreens::paintScreens( const UILayoutContext& baseLayout, const UIPaintContext& basePaint )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            const UIViewport& viewport = offscreen._viewport;
            if ( viewport._physicalSize._x <= 0.0f || viewport._physicalSize._y <= 0.0f )
                continue;
            UIScreen&   screen = *offscreen._screen;
            WidgetTree& tree   = screen.getTree();
            if ( screen.getStyleSet() != nullptr )
                (void)UIStylePass::update( tree, *screen.getStyleSet(), offscreen._bNavigationMode );
            // 미리보기는 애니메이션 단계를 돌지 않으므로, 방금 시작한 스타일 전환을 끝 값으로 바로 맞춘다(옛 값에 멈추지 않게).
            (void)UIStyleTransition::update( tree, MathUtil::kMaxFloat );
            // 화면 UI 와 같은 컨텍스트에서 이 화면의 뷰포트와 글자 배율만 바꿔 쓴다(배율이나 글자 배율이 바뀌면 루트부터 다시 측정한다. 트리가 지난 값을 보관하기 때문이다).
            UILayoutContext layout = baseLayout;
            layout._viewportSize   = viewport._size;
            layout._safeInsets     = viewport._safeInsets;
            layout._uiScale        = viewport._uiScale;
            layout._textScale      = offscreen._textScale;
            UIPaintContext paint   = basePaint;
            paint._uiScale         = viewport._uiScale;
            paint._textScale       = offscreen._textScale;
            if ( offscreen._pLocalization != nullptr )
            {
                // 이 화면만 다른 문화권(다른 언어 · 의사 문화권)으로 본다 — 글 해석, 텍스트 버전, 흐름 방향을 그 문화권으로 건다.
                const uint32 textRevision = offscreen._pLocalization->getTextRevision();
                layout._pLocalization     = offscreen._pLocalization;
                layout._textRevision      = textRevision;
                layout._bRightToLeft      = UILayoutPass::isCultureRightToLeft( offscreen._pLocalization );
                paint._pLocalization      = offscreen._pLocalization;
                paint._textRevision       = textRevision;
            }
            (void)UILayoutPass::update( tree, layout );
            offscreen._scratch.clear();
            offscreen._scratch._targetSize = viewport._physicalSize;
            CanvasPainter painter( offscreen._scratch, viewport._uiScale );
            (void)UIPaintPass::paint( tree, paint, painter, offscreen._scratch );
            if ( offscreen._scratch.isSameContent( offscreen._canvas ) && offscreen._scratch._targetSize == offscreen._canvas._targetSize )
                continue;
            std::swap( offscreen._canvas, offscreen._scratch );
            ++offscreen._revision;
        }
    }

    void UIOffscreenScreens::appendCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const
    {
        // 오프스크린 화면(에디터 미리보기)은 불투명 바탕으로 지운다(ImGui 이미지가 알파를 섞지 않게 하기 위해서다).
        for ( const UIOffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._viewport._physicalSize._x <= 0.0f || offscreen._viewport._physicalSize._y <= 0.0f )
                continue;
            CanvasTargetDrawList& target = inoutListTarget.emplace_back();
            target._targetPath           = offscreen._targetPath;
            target._list                 = offscreen._canvas;
            target._list._targetSize     = offscreen._viewport._physicalSize;
            target._clearColor           = float4{ 0.06f, 0.07f, 0.09f, 1.0f };
            target._contentRevision      = offscreen._revision;
        }
    }
} // namespace sw
