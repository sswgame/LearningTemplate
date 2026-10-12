#include "pch.h"

#include "Engine/UI/Screen/UIOffscreenScreens.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/UI/Animation/UIStyleTransition.h"
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
                (void)UIStylePass::update( tree, *screen.getStyleSet(), false );
            // 미리보기는 애니메이션 단계를 돌지 않으므로, 방금 시작한 스타일 전환을 끝 값으로 바로 맞춘다(옛 값에 멈추지 않게).
            (void)UIStyleTransition::update( tree, MathUtil::kMaxFloat );
            // 화면 UI 와 같은 컨텍스트에서 이 화면의 뷰포트와 글자 배율만 바꿔 쓴다(배율이나 글자 배율이 바뀌면 루트부터 다시 측정한다. 트리가 지난 값을 보관하기 때문이다).
            UILayoutContext layout = baseLayout;
            layout._viewportSize   = viewport._size;
            layout._safeInsets     = viewport._safeInsets;
            layout._uiScale        = viewport._uiScale;
            layout._textScale      = offscreen._textScale;
            (void)UILayoutPass::update( tree, layout );
            UIPaintContext paint = basePaint;
            paint._uiScale       = viewport._uiScale;
            paint._textScale     = offscreen._textScale;
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
