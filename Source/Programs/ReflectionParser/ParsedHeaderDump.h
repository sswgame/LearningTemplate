/**
 * @file ParsedHeaderDump.h
 * @brief `--dump` — 입력 하나에서 뽑은 리플렉션 사실을 사람이 읽는 꼴로 표준 출력에 씁니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    struct ParsedHeader;
    class LayoutSchemaGenerator;

    /**
     * @brief `--dump` — 헤더 하나에서 뽑은 것을 사람이 읽는 꼴로 표준 출력에 한 번에 씁니다.
     * @details "왜 이 프로퍼티가 인스펙터에 없나 · 왜 이 컴포넌트를 씬이 못 찾나" 를 묻는 자리다 — 파서가 무엇을 봤는지 보여 준다.
     *          타입(부모 · 팩토리 · 추상), 프로퍼티(타입 · 값 자리 · 컨테이너 · 범위 ·
     *          플래그), 함수, enum 값까지 적는다. 헤더 여럿을 동시에 쓸 수 있어 한 덩어리로 내보낸다.
     */
    void printParsedHeaderDump( const string& inputFile, const ParsedHeader& parsed, const LayoutSchemaGenerator& layoutSchema );
} // namespace sw
