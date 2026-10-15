#include "pch.h"

#include "Editor/Common/Backend/EditorUICapture.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"

#include "Engine/Config/RHIBackendType.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Frame/Capture/RenderTargetImageFile.h"

#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    /**
     * @brief `-gv_screenshotEditor=<경로>.png` 입니다. 에디터 UI 전체(패널 + 플로팅 창)를 한 장 찍습니다. 게임 뷰만 찍는 `-gv_screenshot` 의 에디터판입니다.
     * @details `-gv_screenshotEditorFrame=N`(기본 60) 번째 에디터 UI 프레임에 찍는다. 도킹 배치와 글꼴 아틀라스가 자리 잡을 때까지 기다리는 값이다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_screenshotEditor, "", "에디터 UI 전체(ImGui 패널 + 플로팅 창)를 이 경로(.png)에 한 장 찍는다 (비면 사용 안 함)" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_screenshotEditorFrame, 60, "-gv_screenshotEditor 를 찍을 에디터 UI 프레임 번호 (기본 60)" );
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorUICapture" );

    namespace
    {
        struct EditorUICaptureInternal
        {
            /** @brief 캡처 텍스처 한 변의 상한입니다. 모니터 여럿에 걸친 플로팅 창도 담고, 백엔드 텍스처 한계(16384)보다 작게 둔다. */
            static constexpr uint32 kMaxTextureSide = 8192;
            /** @brief 결과를 이만큼만 들고 오래된 것부터 버립니다. 시나리오는 요청 직후 결과를 읽는다. */
            static constexpr size_t kMaxResultCount = 64;
            /** @brief 마젠타 판정 문턱입니다(0..255). 보라색 머티리얼 썸네일(약 160,60,220)은 걸리지 않는다. */
            static constexpr uint8 kMagentaMinRedBlue = 230;
            static constexpr uint8 kMagentaMaxGreen   = 40;
            /** @brief 스크롤 여유가 이 픽셀보다 크면 내용이 넘친 것으로 봅니다(반올림 오차를 거른다). */
            static constexpr float32 kClipTolerancePixels = 1.0f;

            /**
             * @brief 창 하나의 내용이 잘린 방향을 돌려줍니다(`x` · `y` · `xy`, 안 잘렸으면 nullptr). 내용이 창보다 큰데 그 방향 스크롤바가 없어 넘친 부분을 볼 수 없는 경우입니다.
             * @details 도킹 호스트 창은 뺀다. 그 내용은 도킹 노드라 넘침이 아니다.
             */
            static const utf8* findClippedAxis( const ImGuiWindow& window )
            {
                if ( window.DockNodeAsHost != nullptr )
                    return nullptr;
                const bool bClippedX = window.ScrollMax.x > kClipTolerancePixels && window.ScrollbarX == false;
                const bool bClippedY = window.ScrollMax.y > kClipTolerancePixels && window.ScrollbarY == false;
                if ( bClippedX && bClippedY )
                    return "xy";
                if ( bClippedX )
                    return "x";
                return bClippedY ? "y" : nullptr;
            }

            /** @brief 이번 프레임에 그린 창을 훑어 잘린 창을 셉니다. `ImGui::Render` 뒤, 다음 `NewFrame` 전에 부릅니다. */
            static void measureWindows( EditorUICaptureJob& outJob )
            {
                const ImGuiContext* pContext = ImGui::GetCurrentContext();
                if ( pContext == nullptr )
                    return;
                for ( const ImGuiWindow* pWindow : pContext->Windows )
                {
                    const bool bDrawn = pWindow != nullptr && pWindow->WasActive && pWindow->Hidden == false && pWindow->Collapsed == false;
                    if ( bDrawn == false )
                        continue;
                    ++outJob._windowCount;
                    const utf8* const pAxis = findClippedAxis( *pWindow );
                    if ( pAxis == nullptr )
                        continue;
                    ++outJob._clippedWindowCount;
                    if ( outJob._clippedWindowNames.empty() == false )
                        outJob._clippedWindowNames.append( ", " );
                    outJob._clippedWindowNames.append( pWindow->Name );
                    outJob._clippedWindowNames.append( "(" );
                    outJob._clippedWindowNames.append( pAxis );
                    outJob._clippedWindowNames.append( ")" );
                }
            }

            /** @brief RGBA8 행을 위아래로 뒤집습니다(OpenGL FBO 는 0 행이 아래쪽이다). */
            static void flipRows( vector<uint8>& inoutRgbaBytes, uint32 width, uint32 height )
            {
                const size_t  rowBytes = static_cast<size_t>( width ) * 4u;
                vector<uint8> rowBytesTemp( rowBytes );
                for ( uint32 row = 0; row < height / 2u; ++row )
                {
                    uint8* const pTop    = inoutRgbaBytes.data() + static_cast<size_t>( row ) * rowBytes;
                    uint8* const pBottom = inoutRgbaBytes.data() + static_cast<size_t>( height - 1u - row ) * rowBytes;
                    std::memcpy( rowBytesTemp.data(), pTop, rowBytes );
                    std::memcpy( pTop, pBottom, rowBytes );
                    std::memcpy( pBottom, rowBytesTemp.data(), rowBytes );
                }
            }

            /** @brief 마젠타 픽셀 비율입니다. */
            static float64 computeMagentaFraction( const vector<uint8>& rgbaBytes )
            {
                const size_t pixelCount = rgbaBytes.size() / 4u;
                if ( pixelCount == 0 )
                    return 0.0;
                size_t magentaCount = 0;
                for ( size_t pixel = 0; pixel < pixelCount; ++pixel )
                {
                    const uint8* const pPixel   = rgbaBytes.data() + pixel * 4u;
                    const bool         bMagenta = pPixel[0] >= kMagentaMinRedBlue && pPixel[2] >= kMagentaMinRedBlue && pPixel[1] <= kMagentaMaxGreen;
                    if ( bMagenta )
                        ++magentaCount;
                }
                return static_cast<float64>( magentaCount ) / static_cast<float64>( pixelCount );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    uint32 EditorUICapture::request( string_view path )
    {
        EditorUICaptureJob job{};
        job._ticket = _nextTicket++;
        job._path   = string{ path };
        _listQueued.push_back( std::move( job ) );
        return _listQueued.back()._ticket;
    }

    bool EditorUICapture::takeJob( EditorUICaptureJob& outJob )
    {
        ++_uiFrameCount;
        const bool bGlobalDue = gv_screenshotEditor.empty() == false && _bGlobalRequested == SW_FALSE &&
                                _uiFrameCount >= static_cast<uint32>( gv_screenshotEditorFrame > 1 ? gv_screenshotEditorFrame : 1 );
        if ( bGlobalDue )
        {
            _bGlobalRequested = SW_TRUE;
            (void)request( gv_screenshotEditor ); // 결과는 finishJob 이 로그로 남긴다
        }
        if ( _listQueued.empty() )
            return false;

        outJob = std::move( _listQueued.front() );
        _listQueued.erase( _listQueued.begin() );
        EditorUICaptureInternal::measureWindows( outJob );
        return true;
    }

    bool EditorUICapture::findResult( uint32 ticket, EditorUICaptureResult& outResult ) const
    {
        const std::scoped_lock<mutex> lock{ _resultMutex };
        for ( const EditorUICaptureResult& result : _listResult )
        {
            if ( result._job._ticket == ticket )
            {
                outResult = result;
                return true;
            }
        }
        return false;
    }

    void EditorUICapture::renderJob( IRHIDevice& device, IImGuiRendererBackend& rendererBackend, ImDrawData& drawData, const EditorUICaptureJob& job )
    {
        IRHICommandContext* const pStream = device.getFrameStreamContext();
        const uint32              width   = static_cast<uint32>( drawData.DisplaySize.x * drawData.FramebufferScale.x );
        const uint32              height  = static_cast<uint32>( drawData.DisplaySize.y * drawData.FramebufferScale.y );
        const bool                bUsable = pStream != nullptr && width > 0u && height > 0u;
        if ( bUsable == false )
        {
            EditorUICaptureResult failed{};
            failed._job = job;
            addResult( failed );
            return;
        }
        ensureTexture( device, width, height );
        if ( _texture == 0 )
        {
            EditorUICaptureResult failed{};
            failed._job = job;
            addResult( failed );
            return;
        }

        // 메인 UI 를 그린 백버퍼 패스를 먼저 닫는다. 열린 채로 다음 패스를 열면 Vulkan 은 대상 레이아웃 전이(배리어)를 그 서브패스 안에 기록한다(검증 오류).
        pStream->endRenderPass();
        RHIRenderPassBeginInfo capturePass{};
        capturePass._bBindColor        = SW_TRUE;
        capturePass._colorTargetCount  = 1;
        capturePass._arrColorTarget[0] = _texture;
        capturePass._arrLoadOp[0]      = RHIRenderPassLoadOp::Clear;
        capturePass._arrClearColor[0]  = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
        pStream->beginRenderPass( capturePass );
        rendererBackend.render( &device, &drawData );
        pStream->prepareTextureForShaderRead( _texture );

        _renderedJob = job;
        _bRendered   = SW_TRUE;
    }

    void EditorUICapture::finishJob( IRHIDevice& device )
    {
        if ( _bRendered == SW_FALSE )
            return;
        _bRendered = SW_FALSE;

        EditorUICaptureResult result{};
        result._job    = std::move( _renderedJob );
        result._width  = _textureWidth;
        result._height = _textureHeight;

        IRHIResourceFactory* const pFactory = device.getResourceFactory();
        vector<uint8>              bytes;
        RHITextureMipSpan          layout{};
        vector<uint8>              rgbaBytes;
        const bool                 bRead = pFactory != nullptr && pFactory->readbackTexture2D( _texture, 0, 0, bytes, layout ) &&
                           RenderTargetImageFile::unpackRgba8( bytes, layout, device.getBackBufferFormat(), rgbaBytes );
        if ( bRead )
        {
            if ( device.getBackendType() == RHIBackend::OpenGL )
                EditorUICaptureInternal::flipRows( rgbaBytes, layout._width, layout._height );
            result._magentaFraction = EditorUICaptureInternal::computeMagentaFraction( rgbaBytes );

            RHITextureMipSpan rgbaLayout{};
            rgbaLayout._width     = layout._width;
            rgbaLayout._height    = layout._height;
            rgbaLayout._rowBytes  = layout._width * 4u;
            rgbaLayout._sizeBytes = rgbaLayout._rowBytes * layout._height;
            rgbaLayout._pData     = rgbaBytes.data();
            (void)FileUtil::ensureParentDirectoryExists( result._job._path ); // 만들지 못하면 아래 쓰기가 경로와 함께 실패를 남긴다
            result._bWritten = RenderTargetImageFile::writeImageFile( rgbaBytes, rgbaLayout, RHIFormat::R8G8B8A8_UNORM, result._job._path ) ? SW_TRUE : SW_FALSE;
        }
        if ( result._bWritten == SW_TRUE )
        {
            SW_LOG_INFO( "Editor UI capture -> %# (%#x%#, windows %#, clipped %# [%#], magenta %#)", result._job._path.c_str(), result._width, result._height,
                         result._job._windowCount, result._job._clippedWindowCount, result._job._clippedWindowNames.c_str(), result._magentaFraction );
        }
        else
        {
            SW_LOG_WARNING( "Editor UI capture was not written: %#", result._job._path.c_str() );
        }
        addResult( result );
    }

    void EditorUICapture::releaseRHI( IRHIDevice* pDevice )
    {
        const bool bCanDestroy = _texture != 0 && pDevice != nullptr && pDevice->getResourceFactory() != nullptr;
        if ( bCanDestroy )
            pDevice->getResourceFactory()->destroyTexture( _texture );
        _texture       = 0;
        _textureWidth  = 0;
        _textureHeight = 0;
        _bRendered     = SW_FALSE;
    }

    void EditorUICapture::ensureTexture( IRHIDevice& device, uint32 width, uint32 height )
    {
        const uint32 clampedWidth  = width < EditorUICaptureInternal::kMaxTextureSide ? width : EditorUICaptureInternal::kMaxTextureSide;
        const uint32 clampedHeight = height < EditorUICaptureInternal::kMaxTextureSide ? height : EditorUICaptureInternal::kMaxTextureSide;
        const bool   bSameTexture  = _texture != 0 && _textureWidth == clampedWidth && _textureHeight == clampedHeight;
        if ( bSameTexture )
            return;
        IRHIResourceFactory* const pFactory = device.getResourceFactory();
        if ( pFactory == nullptr )
            return;
        if ( _texture != 0 )
            pFactory->destroyTexture( _texture );

        // ImGui 렌더러 백엔드의 파이프라인은 백버퍼 포맷으로 만들어져 있다. 같은 포맷이어야 Vulkan 렌더 패스가 호환되고 DX12 PSO 의 RTV 포맷이 맞는다.
        RHITextureDesc desc{};
        desc._width             = clampedWidth;
        desc._height            = clampedHeight;
        desc._format            = device.getBackBufferFormat();
        desc._bIsRenderTarget   = SW_TRUE;
        desc._bIsShaderResource = SW_TRUE;
        _texture                = pFactory->createTexture2D( desc );
        _textureWidth           = _texture != 0 ? clampedWidth : 0u;
        _textureHeight          = _texture != 0 ? clampedHeight : 0u;
        if ( _texture == 0 )
            SW_LOG_ERROR( "Editor UI capture texture (%#x%#) could not be created", clampedWidth, clampedHeight );
    }

    void EditorUICapture::addResult( const EditorUICaptureResult& result )
    {
        const std::scoped_lock<mutex> lock{ _resultMutex };
        if ( _listResult.size() >= EditorUICaptureInternal::kMaxResultCount )
            _listResult.erase( _listResult.begin() );
        _listResult.push_back( result );
    }
} // namespace sw::editor
