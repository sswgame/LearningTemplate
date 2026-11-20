/**
 * @file ReflectionFwd.h
 * @brief 리플렉션 표 형식(`TypeInfo` · `PropertyInfo` · `EnumInfo` · `FunctionInfo` · `EventInfo` · `TypeRegistry`)의 이름만 알리는 전방 선언 헤더입니다.
 * @details 이 형식을 포인터 · 참조로만 받는 헤더가 `TypeRegistry.h` · `TypeInfo.h`(EnumInfo · FunctionInfo · PropertyInfo 묶음)를 끌어오지 않게 한다(언리얼의 `*Fwd.h` 와 같은 자리,
 *          DesignBacklog "헤더 팬인 다이어트"). 멤버를 읽거나 레지스트리를 부르는 파일은 정의 헤더를 직접 include 한다.
 */
#pragma once

namespace sw
{
    struct TypeInfo;
    struct PropertyInfo;
    struct EnumInfo;
    struct FunctionInfo;
    struct EventInfo;
    class TypeRegistry;
} // namespace sw
