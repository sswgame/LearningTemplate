#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorViewSurface.h"

#include "Editor/Common/Backend/EditorUICapture.h"
#include "Editor/Common/Backend/ImGuiRenderer.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    namespace
    {
        struct EditorViewSurfaceInternal
        {
            static EditorRenderViews* findRenderViews()
            {
                EditorContext* const pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? &pContext->getSubsystem<EditorRenderViews>() : nullptr;
            }
        };
    } // namespace

    void* EditorViewSurface::getTextureID( EditorViewKind kind )
    {
        const EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        return pViews != nullptr ? pViews->getViewTarget( kind )._pTextureID : nullptr;
    }

    void EditorViewSurface::fitTargetSize( EditorViewKind kind, uint32 width, uint32 height )
    {
        EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        if ( pViews == nullptr )
            return;
        const EditorViewTarget& view = pViews->getViewTarget( kind );
        if ( EditorViewTargetUtil::needsResize( view.getRequestedWidth(), view.getRequestedHeight(), width, height ) )
            pViews->ensureViewTargetSize( kind, width, height );
    }

    void EditorViewSurface::markDrawn( EditorViewKind kind, const float2& imageClientPos )
    {
        EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        if ( pViews != nullptr )
            pViews->markViewDrawn( kind, imageClientPos );
    }

    bool EditorViewSurface::wasViewDrawn( EditorViewKind kind )
    {
        const EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        return pViews != nullptr && pViews->wasViewDrawn( kind );
    }

    bool EditorViewSurface::isViewRequested( EditorViewKind kind )
    {
        const EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        if ( pViews == nullptr )
            return false;
        return pViews->isViewRequested( kind ) && pViews->getViewTarget( kind )._renderTarget != 0;
    }

    void EditorViewSurface::setGameViewInput( bool bFocused, bool bHovered )
    {
        EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        if ( pViews == nullptr )
            return;
        pViews->setGameViewFocused( bFocused );
        pViews->setGameViewHovered( bHovered );
    }

    bool EditorViewSurface::isGameViewFocused()
    {
        const EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        return pViews != nullptr && pViews->isGameViewFocused();
    }

    void EditorViewSurface::recordFocusedView( EditorViewKind kind )
    {
        EditorScreenshotCommands::recordFocusedView( kind );
    }

    void EditorViewSurface::captureScreenshot( EditorViewKind kind )
    {
        if ( kind == EditorViewKind::Game )
            EditorScreenshotCommands::captureGameView();
        else
            EditorScreenshotCommands::captureSceneView();
    }

    void* EditorViewSurface::registerImage( uint64 rhiTexture )
    {
        EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        if ( rhiTexture == 0 || pViews == nullptr || pViews->getRenderer() == nullptr )
            return nullptr; // 렌더 스레드가 아직 만들지 않았다(다음 프레임)
        return pViews->getRenderer()->registerTexture( rhiTexture );
    }

    void EditorViewSurface::unregisterImage( void* pTextureID )
    {
        EditorRenderViews* const pViews = EditorViewSurfaceInternal::findRenderViews();
        if ( pTextureID != nullptr && pViews != nullptr && pViews->getRenderer() != nullptr )
            pViews->getRenderer()->unregisterTexture( pTextureID );
    }

    void* EditorViewSurface::registerOverlayTexture( uint64 rhiTexture )
    {
        void* const          pTextureID = registerImage( rhiTexture );
        EditorContext* const pContext   = editor::findSubsystem<EditorContext>();
        if ( pTextureID != nullptr && pContext != nullptr )
            pContext->getSubsystem<EditorUICapture>().setTextureExcluded( pTextureID, true );
        return pTextureID;
    }

    void EditorViewSurface::unregisterOverlayTexture( void* pTextureID )
    {
        EditorContext* const pContext = editor::findSubsystem<EditorContext>();
        if ( pTextureID != nullptr && pContext != nullptr )
            pContext->getSubsystem<EditorUICapture>().setTextureExcluded( pTextureID, false ); // ID 칸은 다른 등록이 다시 받는다
        unregisterImage( pTextureID );
    }
} // namespace sw::editor
