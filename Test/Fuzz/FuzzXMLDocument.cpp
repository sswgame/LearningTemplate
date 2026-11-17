// XMLDocument 퍼저 — 모드 · 세이브 글이 드는 XML 파서(`XMLDocument::parse`)에 임의 바이트를 넣는다. 거절은 정상이고, 크래시 · 새니타이저 보고만 결함이다.
#include "pch.h"

#include "Engine/Serialization/XML/XMLDocument.h"

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* pData, size_t size )
{
    sw::XMLDocument document;
    (void)document.parse( sw::string_view{ reinterpret_cast<const char*>( pData ), size }, "fuzz" );
    return 0;
}
