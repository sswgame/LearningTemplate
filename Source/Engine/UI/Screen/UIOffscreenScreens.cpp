#include "pch.h"

#include "Engine/UI/Screen/UIOffscreenScreens.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/UI/Animation/UIAnimationPlayer.h"
#include "Engine/UI/Animation/UIStyleTransition.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/Render/UIPaintPass.h"
#include "Engine/UI/Style/UIStylePass.h"
#include "Engine/UI/Style/UIStyleSet.h"

namespace sw
{
    UIOffscreenScreens::UIOffscreenScreens()
        : _listOffscreen{}
    {
    }

    UIOffscreenScreens::~UIOffscreenScreens() = default;

    UIScreen& UIOffscreenScreens::add( unique_ptr<UIScreen> screen, const hashed_string& targetPath )
    {
        UIOffscreenScreen& offscreen = _listOffscreen.emplace_back();
        offscreen._screen            = std::move( screen );
        offscreen._targetPath        = targetPath;
        return *offscreen._screen;
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

    bool UIOffscreenScreens::setView( UIScreenHandle handle, const UIViewport& viewport, float32 textScale, const hashed_string& theme )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            if ( offscreen._screen->getHandle() != handle )
                continue;
            offscreen._viewport  = viewport;
            offscreen._textScale = textScale > 0.0f ? textScale : 1.0f;
            if ( offscreen._theme == theme )
                return false;
            offscreen._theme = theme;
            return true;
        }
        return false;
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

    void UIOffscreenScreens::tickAnimations( float32 deltaSeconds )
    {
        for ( UIOffscreenScreen& offscreen : _listOffscreen )
        {
            UIAnimationPlayer& player = offscreen._screen->getAnimationPlayer();
            if ( player.isAnyPlaying() )
                player.tick( deltaSeconds );
        }
    }

    void UIOffscreenScreens::update( const UILayoutContext& baseLayout, const UIPaintContext& basePaint )
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
