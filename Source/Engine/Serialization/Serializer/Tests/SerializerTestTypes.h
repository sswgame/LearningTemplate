/**
 * @file SerializerTestTypes.h
 * @brief 직렬화 도우미 단위 테스트(`Engine/Serialization/Serializer/Tests`)가 쓰는 반사 타입입니다 — 코드젠을 거쳐 Engine 모듈에 오른다.
 * @details 개발 구성만 코드젠한다(배포 구성은 `Tests/` 의 반사 헤더를 훑지 않는다). 컴포넌트가 아닌 값 타입만 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
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
} // namespace sw
