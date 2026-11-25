// JSONDocument 퍼저 — 설정 · HTTP 응답 · 토큰 본문이 드는 JSON 파서(`JSONDocument::parse`)에 임의 바이트를 넣는다. 받아들인 글은 다시 써 본다(`dump` — 잘못된 UTF-8 을
// 오류 값으로 돌려주는 갈래). 거절은 정상이고, 크래시 · 새니타이저 보고만 결함이다.
#include "pch.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* pData, size_t size )
{
    const sw::string_view text{ reinterpret_cast<const char*>( pData ), size };
    sw::JSONDocument      document;
    const bool            bAccepted = document.parse( text, "fuzz" );
    if ( bAccepted )
        (void)document.dump( sw::JSONDocument::kSingleLineIndent );
    // 실패를 남기지 않는 입구도 같은 판정이어야 한다 — 다르면 크래시로 알린다.
    sw::JSONDocument probe;
    if ( probe.tryParse( text ) != bAccepted )
        std::abort();
    return 0;
}
