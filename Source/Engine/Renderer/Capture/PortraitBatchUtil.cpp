#include "pch.h"

#include "Engine/Renderer/Capture/PortraitBatchUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Asset/Image/ImageFileWriter.h"
#include "Engine/Graphics/RHI/IRHIContextThreadBinding.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Capture/PortraitRenderer.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw
{
    SW_LOG_CALLER( "PortraitBatchUtil" );

    bool PortraitBatchUtil::renderPrefabList( IRHIDevice& device, const SceneEngineServices& services, RenderThread* pRenderThread, string_view prefabList,
                                              string_view outputDirectory, uint32 size )
    {
        if ( prefabList.empty() )
            return false;
        // 렌더 스레드가 받은 일을 모두 끝내게 하고, 컨텍스트가 스레드에 묶이는 백엔드(GL, DX11)는 이 스레드가 잡는다.
        if ( pRenderThread != nullptr )
            pRenderThread->waitIdle( RHIWaitIdleReason::Capture );
        IRHIContextThreadBinding* pContextBinding = device.findContextThreadBinding();
        if ( pContextBinding != nullptr && pContextBinding->bindGraphicsContext() == false )
        {
            SW_LOG_ERROR( "Portrait: could not bind the graphics context" );
            return false;
        }
        const string directory = outputDirectory.empty() ? FileUtil::joinPath( ProcessRoots::get()._saved.str(), "Portraits" ) : string( outputDirectory );
        (void)FileUtil::ensureDirectoryExists( directory ); // 못 만들면 ensure 가 폴더와 이유를 남기고 뒤따르는 쓰기가 실패로 답한다

        PortraitRenderer      portrait;
        bool                  bAllSucceeded = portrait.initialize( &device, services );
        const string_splitter parts( prefabList, { "," } );
        for ( const string_view part : parts.getSplitList() )
        {
            const string_view path = StringUtil::trim( part );
            if ( path.empty() || bAllSucceeded == false )
                continue;
            PortraitRequest request;
            request._prefabPath = string( path );
            request._width      = MathUtil::clamp( size, 16u, 4096u );
            request._height     = request._width;
            vector<uint8> rgbaBytes;
            if ( portrait.renderPrefab( request, rgbaBytes ) == false )
            {
                bAllSucceeded = false;
                continue;
            }
            const string base = directory + "/" + string( makeFileStem( path ) ) + ".portrait";
            const bool   bDDS = ImageFileWriter::writeDDSRGBA8( base + ".dds", rgbaBytes, request._width, request._height );
            const bool   bPNG = ImageFileWriter::writePNGRGBA8( base + ".png", rgbaBytes, request._width, request._height );
            bAllSucceeded     = bAllSucceeded && bDDS && bPNG;
            SW_LOG_INFO( "Portrait '%#' -> %#.dds / .png (%#x%#)", string( path ).c_str(), base.c_str(), request._width, request._height );
        }
        portrait.shutdown();
        if ( pContextBinding != nullptr )
            pContextBinding->unbindGraphicsContext();
        return bAllSucceeded;
    }

    string_view PortraitBatchUtil::makeFileStem( string_view prefabPath )
    {
        string_view stem;
        FileUtil::getFileNamePart( prefabPath, stem );
        return stem.substr( 0, stem.find( '.' ) );
    }
} // namespace sw
