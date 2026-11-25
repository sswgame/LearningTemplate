// StreamFrame 퍼저 — 게임 세션 스트림이 받은 바이트를 프레임으로 자르는 해독기(`StreamFrameDecoder`)에 임의 바이트를 넣는다.
// 입력을 두 조각으로 나눠 붙여 "더 받아야 한다" 갈래도 지난다. 거절(Malformed)은 정상이고 연결처럼 거기서 멈춘다. 크래시 · 새니타이저 보고만 결함이다.
#include "pch.h"

#include "Core/Network/Message/StreamFrame.h"

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* pData, size_t size )
{
    if ( size > static_cast<size_t>( sw::StreamFrameConstant::kDefaultMaxFrameSize ) )
        return 0; // 받는 쪽도 한 번에 이보다 많이 붙이지 않는다(int32 크기)
    sw::StreamFrameDecoder decoder;
    const size_t           split         = size / 2;
    const size_t           arrChunkEnd[] = { split, size };
    size_t                 chunkStart    = 0;
    for ( const size_t chunkEnd : arrChunkEnd )
    {
        decoder.append( pData + chunkStart, static_cast<int32>( chunkEnd - chunkStart ) );
        chunkStart = chunkEnd;
        sw::StreamFrameView         frame;
        sw::StreamFrameDecodeResult result = decoder.next( frame );
        while ( result == sw::StreamFrameDecodeResult::Frame )
        {
            result = decoder.next( frame );
        }
        if ( result == sw::StreamFrameDecodeResult::Malformed )
            return 0; // 연결은 여기서 닫힌다(ProtocolError)
    }
    return 0;
}
