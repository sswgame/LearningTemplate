/**
 * @file NestedObjectCodec.h
 * @brief 칸 하나에 든 구조체 값(`TypeInfo` 가 있는 객체)을 바이너리 · JSON 본문으로 읽고 쓰는 창구입니다. 아래 층(Base)이 선언하고 형식 층(Format)이 몸통을 둡니다.
 * @details `SerializerUtil` 의 칸 경로(구조체 칸 · 소유 포인터 원소 · 텍스트 칸)는 구조체 본문을 형식 직렬화기에 맡겨야 한다. Base 가 Format 의
 *          `BinarySerializer` · `JSONSerializer` 를 include 하면 층이 거꾸로 되므로, Base 는 이 선언만 알고 몸통은 각 형식의 `.cpp` 가 정의한다
 *          (바이너리 둘은 `BinarySerializer.cpp`, JSON 둘은 `JSONSerializer.cpp`). 실행 중에 갈아 끼우는 표(가변 전역)는 두지 않는다 —
 *          형식은 빌드마다 하나뿐이고, 새 가변 전역은 만들지 않는다. 몸통이 없으면 링크가 실패하므로 빠뜨릴 수 없다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    enum class FieldReadResult : uint8;

    struct TypeInfo;

    class SerializeContext;

    /** @brief 구조체 값 하나의 본문 읽기 · 쓰기입니다. 길이 머리 · 이름 머리는 부르는 쪽(`SerializerUtil`)이 붙입니다. */
    struct NestedObjectCodec
    {
        /** @brief @p pObject(@p typeInfo 의 객체)의 바이너리 본문을 @p outListBuffer 끝에 붙입니다. 실을 수 없는 필드가 있으면 false 입니다. */
        [[nodiscard]] static bool writeBinary( const void* pObject, const TypeInfo& typeInfo, vector<uint8>& outListBuffer, const SerializeContext& context );
        /** @brief 바이너리 본문 하나를 @p pObject 로 읽습니다. 객체째 실패(금지된 필드)는 `ObjectRejected` 로 올립니다. */
        [[nodiscard]] static FieldReadResult readBinary( void* pObject, const TypeInfo& typeInfo, const uint8* pData, size_t dataSize, const SerializeContext& context );
        /** @brief @p pObject 를 JSON 글 하나로 씁니다. */
        static string writeJSON( const void* pObject, const TypeInfo& typeInfo, const SerializeContext& context );
        /** @brief JSON 글 하나를 @p pObject 로 읽습니다. */
        [[nodiscard]] static bool readJSON( void* pObject, const TypeInfo& typeInfo, string_view jsonText, const SerializeContext& context );
    };
} // namespace sw
