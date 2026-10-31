#include "pch.h"

#include "Engine/Renderer/Capture/PortraitBatchUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Asset/Image/ImageFileWriter.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Graphics/RHI/IRHIContextThreadBinding.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Renderer/Capture/PortraitRenderer.h"
#include "Engine/Renderer/RenderThread.h"

namespace sw
{
    SW_LOG_CALLER( "PortraitBatchUtil" );

    namespace
    {
        struct PortraitBatchUtilInternal
        {
            /** @brief 읽은 장을 RGBA8 로 옮겨 DDS · PNG 로 쓰는 병렬 본문입니다. 입력은 const 포인터로만 읽고, 결과는 장마다 제 칸(`_pWritten`)에 쓴다. */
            struct WriteJob
            {
                const PortraitCapture* _pCapture{ nullptr };
                const string*          _pBase{ nullptr }; ///< 확장자 앞까지의 쓸 경로
                uint8*                 _pWritten{ nullptr };

                void runRange( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        const PortraitCapture& capture = _pCapture[index];
                        vector<uint8>          rgbaBytes;
                        PortraitRenderer::makeRGBA8( capture, rgbaBytes );
                        const bool bDDS  = ImageFileWriter::writeDDSRGBA8( _pBase[index] + ".dds", rgbaBytes, capture._width, capture._height );
                        const bool bPNG  = ImageFileWriter::writePNGRGBA8( _pBase[index] + ".png", rgbaBytes, capture._width, capture._height );
                        _pWritten[index] = bDDS && bPNG ? 1 : 0;
                    }
                }
            };
        };
    } // namespace

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

        // 그리기 · 읽기(GPU)는 이 스레드가 차례로 하고, 옮기기(RGBA8) · 인코딩 · 쓰기(DDS · PNG)는 다 읽은 뒤 장마다 워커가 제 칸에 한다(`WriteJob`).
        // shortcut: 읽은 장을 모두 들고 있다가 한 번에 쓴다 - 아주 큰 장을 수백 장 찍으면 메모리가 그만큼 오른다. 그런 목록이 생기면 몇 장씩 끊어 쓴다.
        PortraitRenderer        portrait;
        bool                    bAllSucceeded = portrait.initialize( &device, services );
        vector<PortraitCapture> listCapture;
        vector<string>          listPath;
        const string_splitter   parts( prefabList, { "," } );
        for ( const string_view part : parts.getSplitList() )
        {
            const string_view path = StringUtil::trim( part );
            if ( path.empty() || bAllSucceeded == false )
                continue;
            PortraitRequest request;
            request._prefabPath = string( path );
            request._width      = MathUtil::clamp( size, 16u, 4096u );
            request._height     = request._width;
            PortraitCapture capture;
            if ( portrait.renderPrefab( request, capture ) == false )
            {
                bAllSucceeded = false;
                continue;
            }
            listCapture.push_back( std::move( capture ) );
            listPath.push_back( string( path ) );
        }
        portrait.shutdown();
        if ( pContextBinding != nullptr )
            pContextBinding->unbindGraphicsContext();

        vector<string> listBase;
        listBase.reserve( listPath.size() );
        for ( const string& path : listPath )
        {
            listBase.push_back( directory + "/" + string( makeFileStem( path ) ) + ".portrait" );
        }
        vector<uint8>                       listWritten( listCapture.size(), 0 ); // 장마다 제 칸(워커가 쓴다) - 아래 직렬 단계가 정해진 차례로 모은다
        PortraitBatchUtilInternal::WriteJob job{ listCapture.data(), listBase.data(), listWritten.data() };
        engine::runParallelByCost( static_cast<uint32>( listCapture.size() ), SW_DELEGATE_METHOD( ParallelBlockDelegate, &PortraitBatchUtilInternal::WriteJob::runRange, &job ) );
        for ( size_t index = 0; index < listCapture.size(); ++index )
        {
            bAllSucceeded = bAllSucceeded && listWritten[index] != 0;
            SW_LOG_INFO( "Portrait '%#' -> %#.dds / .png (%#x%#)", listPath[index].c_str(), listBase[index].c_str(), listCapture[index]._width, listCapture[index]._height );
        }
        return bAllSucceeded;
    }

    string_view PortraitBatchUtil::makeFileStem( string_view prefabPath )
    {
        string_view stem;
        FileUtil::getFileNamePart( prefabPath, stem );
        return stem.substr( 0, stem.find( '.' ) );
    }
} // namespace sw
