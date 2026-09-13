/**
 * @file TestReflectionFixtures.h
 * @brief 리플렉션 테스트가 함께 쓰는 더미 타입 — 코드젠이 아니라 **손으로** TypeInfo 를 만들어 붙이는 쪽.
 * @details 코드젠을 거치는 샘플은 `TestSampleActor.h` 에 있다. 여기 있는 타입들은 반대로, 리플렉션 데이터를
 *          손으로 채워 넣었을 때 레지스트리·직렬화가 어떻게 동작하는지 보기 위한 것이다.
 *
 *          **등록은 여기서 하지 않는다.** `RegisterTypes` · `RegisterEnums` 와 그것을 정적 초기화에 거는
 *          `RegistrarInit` 은 `TestReflectionFixtures.cpp` 한 곳에만 있다 — 헤더에 두면 include 한 TU 마다
 *          등록이 돌아 같은 타입이 레지스트리에 여러 번 들어간다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct DummyBase
    {
    };

    struct DummyActor
    {
        int32   _hp{ 0 };
        string  _name = "";
        float32 _speed{ 0.0f };
    };

    enum class DummyType : int64
    {
        None  = 0,
        TypeA = 1,
        TypeB = 2,
    };

    enum class DummyBitFlag : int64
    {
        None    = 0,
        OptionA = 1 << 0,
        OptionB = 1 << 1,
        OptionC = 1 << 2,
    };

    /** @brief uint8 폭 enum — 역직렬화가 실제 크기만큼만 써야 인접 필드가 안 깨진다. */
    enum class NarrowEnum : uint8
    {
        Zero = 0,
        One  = 1,
        Two  = 2,
    };

    /** @brief NarrowEnum 뒤에 1바이트 필드를 두어 초과 기록을 감지한다. */
    struct NarrowEnumHost
    {
        NarrowEnum _mode{ NarrowEnum::Zero };
        uint8      _guard{ 0xAB };
        uint8      _guard2{ 0xCD };
        uint8      _guard3{ 0xEF };
    };

    struct ComplexData
    {
        int32              _id    = 101;
        string             _title = "HeroData";
        int64              _flags = static_cast<int64>( DummyBitFlag::OptionA ) | static_cast<int64>( DummyBitFlag::OptionC );
        vector<int32>      _listScore;
        map<string, int32> _mapStat;
    };
} // namespace sw
