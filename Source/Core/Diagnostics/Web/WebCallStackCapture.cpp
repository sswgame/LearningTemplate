/**
 * @file WebCallStackCapture.cpp
 * @brief 웹(wasm32)의 호출 스택 — 반환 주소를 읽을 길이 없다(호출 스택이 wasm 실행기 안이고, 이 Emscripten 판에는 주소를 모으는 API 가 없다).
 * @details 주소 캡처(`capture` · `captureFromContext`)는 빈 스택을 낸다 — 같은 스택 묶기(교착 감시 · 메모리 기록)는 웹에서 한 묶음이 된다.
 *          글로 된 스택은 `symbolizeFrames` 가 아니라 지금 자리의 `emscripten_get_callstack`(브라우저 Error.stack — 함수 이름이 든 wasm 프레임)이다.
 *          웹 빌드는 배포 클라이언트뿐이라 이 진단들의 주 사용처(에디터 · 개발 빌드)가 없다. 언리얼 HTML5 도 주소 없이 JS 스택 글을 남겼다(기억).
 */
#include "pch.h"

#include "Core/Diagnostics/CallStackCapture.h"

#if defined( SW_PLATFORM_WEB )
    #include <emscripten/emscripten.h>

namespace sw
{
    void CallStackCapture::initialize()
    {
    }

    void CallStackCapture::shutdown()
    {
    }

    void CallStackCapture::capture( CallStack& outStack, [[maybe_unused]] uint32 skipFrames )
    {
        outStack._frameCount = 0;
        outStack._hash       = 0;
    }

    void CallStackCapture::captureFromContext( DeepCallStack& outStack, [[maybe_unused]] void* pPlatformContext, [[maybe_unused]] void* pPlatformThread )
    {
        outStack._frameCount = 0;
    }

    string CallStackCapture::symbolizeFrames( void* const* ppFrame, uint32 frameCount, [[maybe_unused]] atomic<uint32>* pFrameProgress )
    {
        if ( ppFrame == nullptr || frameCount == 0 )
        {
            // 주소가 없으니 지금 자리의 글 스택을 낸다 — 보고를 쓰는 자리(크래시 · 행 보고)가 곧 문제의 자리다.
            utf8        arrText[constant::buffer::kMax4096]{};
            const int32 written = emscripten_get_callstack( EM_LOG_C_STACK | EM_LOG_NO_PATHS, arrText, static_cast<int32>( sizeof( arrText ) ) );
            return written > 0 ? string( arrText ) : string( kEmptyCallStackText );
        }
        return formatRawCallStackFrames( ppFrame, frameCount );
    }

    string CallStackCapture::symbolize( const CallStack& stack )
    {
        return symbolizeFrames( stack._arrFrame, stack._frameCount, nullptr );
    }

    string CallStackCapture::symbolize( const DeepCallStack& stack, atomic<uint32>* pFrameProgress )
    {
        return symbolizeFrames( stack._arrFrame, stack._frameCount, pFrameProgress );
    }
} // namespace sw
#endif
