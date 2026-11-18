#include "pch.h"

#include "Engine/Content/Pack/Web/WebPackChunkSource.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include <cstring>
#include <emscripten/fetch.h>

namespace sw
{
    namespace
    {
        /** @brief 받기 하나의 문맥입니다 — fetch 의 사용자 값이고 콜백이 지운다. */
        struct WebPackChunkRequest
        {
            string                     _url;
            string                     _localPath;
            PackChunkFetchDoneDelegate _onDone;
        };

        /** @brief 웹 받기입니다. 콜백은 받기를 건 스레드(게임 루프가 도는 브라우저 메인 스레드)의 이벤트 루프에서 온다. */
        class WebPackChunkSource final : public IPackChunkSource
        {
        public:
            /** @brief 페이지 기준 팩 자리입니다 — 웹 패키지(`Scripts/package/web_package.py`)가 미룬 청크를 이 폴더에 둔다. */
            static constexpr const utf8* kRemoteFolder = "Packs/";

            void fetch( const string& packFileName, const string& localPath, PackChunkFetchDoneDelegate onDone ) override
            {
                WebPackChunkRequest* const pRequest = new WebPackChunkRequest{ string( kRemoteFolder ) + packFileName, localPath, std::move( onDone ) };
                emscripten_fetch_attr_t    attributes;
                emscripten_fetch_attr_init( &attributes );
                std::strcpy( attributes.requestMethod, "GET" );
                attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
                attributes.userData   = pRequest;
                attributes.onsuccess  = &WebPackChunkSource::onSuccess;
                attributes.onerror    = &WebPackChunkSource::onError;
                (void)emscripten_fetch( &attributes, pRequest->_url.c_str() );
            }

        private:
            static void finish( emscripten_fetch_t* pFetch, bool bOk )
            {
                WebPackChunkRequest* const pRequest = static_cast<WebPackChunkRequest*>( pFetch->userData );
                emscripten_fetch_close( pFetch );
                pRequest->_onDone( bOk );
                delete pRequest;
            }

            static void onSuccess( emscripten_fetch_t* pFetch )
            {
                const WebPackChunkRequest& request  = *static_cast<const WebPackChunkRequest*>( pFetch->userData );
                const bool                 bWritten = FileUtil::writeFile( request._localPath, reinterpret_cast<const uint8*>( pFetch->data ), static_cast<uint64>( pFetch->numBytes ) );
                if ( bWritten == false )
                    SW_LOG_ERROR_ID( "WebPackChunkSource.WriteFailed", "Chunk pack '%#' was fetched but could not be written to %#", request._url, request._localPath );
                finish( pFetch, bWritten );
            }

            static void onError( emscripten_fetch_t* pFetch )
            {
                const WebPackChunkRequest& request = *static_cast<const WebPackChunkRequest*>( pFetch->userData );
                SW_LOG_ERROR_ID( "WebPackChunkSource.FetchFailed", "Chunk pack '%#' could not be fetched (HTTP %#)", request._url, static_cast<uint32>( pFetch->status ) );
                finish( pFetch, false );
            }
        };
    } // namespace

    unique_ptr<IPackChunkSource> makeWebPackChunkSource()
    {
        return make_unique<WebPackChunkSource>();
    }
} // namespace sw
