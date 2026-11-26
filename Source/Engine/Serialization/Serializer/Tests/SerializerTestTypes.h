/**
 * @file SerializerTestTypes.h
 * @brief 직렬화 도우미 단위 테스트(`Engine/Serialization/Serializer/Tests`)가 쓰는 반사 타입입니다 — 코드젠을 거쳐 Engine 모듈에 오른다.
 * @details 개발 구성만 코드젠한다(배포 구성은 `Tests/` 의 반사 헤더를 훑지 않는다). 컴포넌트가 아닌 값 타입만 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"
#include "Engine/Serialization/Serializer/SecretValue.h"

namespace sw
{
    /** @brief 비밀 값 샘플입니다 — 글로는 나가지 않고 바이너리로만 왕복한다. */
    REFLECT()
    struct SecretValueSample
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _visible{ 0 };

        PROPERTY()
        Secret32 _digest{};

        PROPERTY()
        SecretText _password{};
    };

    /** @brief 이름 · 강제 변환 샘플의 열거형입니다. */
    ENUM()
    enum class SerializerSampleMode : uint8
    {
        Off,
        Slow,
        Fast,
    };

    /**
     * @brief 직렬화 도우미 샘플 — 범위 있는 정수, 실수, 글, 좁은 정수, 열거형, 이웃한 비트필드 둘, 기본값 있는 칸, 데이터 경로(`DataPath`)입니다.
     * @details 비트필드 둘은 같은 바이트에 산다 — 한 칸을 쓰는 도우미가 이웃 비트를 지우는지 본다.
     */
    REFLECT()
    struct SerializerSample
    {
        REFLECT_BODY();

        SerializerSample()
            : _bFirst{ 0 }
            , _bSecond{ 0 }
        {
        }

        PROPERTY( Min = 0, Max = 100 )
        int32 _count = 0;

        PROPERTY()
        float32 _ratio = 0.0f;

        PROPERTY()
        string _name;

        PROPERTY()
        uint8 _small = 0;

        PROPERTY()
        SerializerSampleMode _mode = SerializerSampleMode::Off;

        PROPERTY()
        uint8 _bFirst : 1;

        PROPERTY()
        uint8 _bSecond : 1;

        PROPERTY( Default = "7" )
        int32 _withDefault = 0;

        PROPERTY()
        DataPath _path;
    };

    /** @brief 이름이 같은 칸 옮기기(`SerializerUtil::copySameNamedProperties`)의 받는 쪽 샘플입니다 — `_count` · `_name` 만 `SerializerSample` 과 이름이 같다. */
    REFLECT()
    struct SerializerSamplePart
    {
        REFLECT_BODY();

        PROPERTY()
        int32 _count = 0;

        PROPERTY()
        string _name;

        PROPERTY()
        float32 _extra = 1.5f;
    };

    /** @brief 원소로 적히는 칸(글 목록 · 중첩 구조체)과 속성으로 적히는 칸이 함께 있는 샘플입니다. */
    REFLECT()
    struct SerializerSampleGroup
    {
        REFLECT_BODY();

        PROPERTY()
        vector<string> _listText;

        PROPERTY()
        SerializerSamplePart _part;

        PROPERTY()
        int32 _count = 0;
    };
} // namespace sw
