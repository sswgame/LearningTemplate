// EXPECT-GEN: struct RegistrarKey_sw_fixture_HiddenOwner_HiddenRecord
// EXPECT-GEN: struct Registrar<RegistrarKey_sw_fixture_HiddenOwner_HiddenRecord>
// EXPECT-GEN: static Registrar<RegistrarKey_sw_fixture_HiddenOwner_HiddenRecord> s_sw_fixture_HiddenOwner_HiddenRecord_registrar
// EXPECT-GEN: struct RegistrarKey_sw_fixture_HiddenOwner_HiddenMode
// EXPECT-GEN: &Registrar<RegistrarKey_sw_fixture_HiddenOwner_HiddenMode>::registerEnum
// EXPECT-GEN: offsetof( sw::fixture::HiddenOwner::HiddenRecord, _count )
// EXPECT-GEN: struct Registrar<sw::fixture::HiddenOwner>
// EXPECT-GEN: struct Registrar<sw::fixture::HiddenOwner::PublicRecord>
// EXPECT-GEN: struct ReflectTypeTraits<sw::fixture::HiddenOwner::PublicRecord>
// EXPECT-NOT-GEN: ReflectTypeTraits<sw::fixture::HiddenOwner::HiddenRecord>
// EXPECT-NOT-GEN: Registrar<sw::fixture::HiddenOwner::HiddenRecord>
// EXPECT-NOT-GEN: Registrar<sw::fixture::HiddenOwner::HiddenMode>
// EXPECT-TYPE: sw::fixture::HiddenOwner::HiddenRecord
// 바깥 형식(REFLECT)의 private 중첩 REFLECT · ENUM 은 등록기 특수화를 열쇠 구조체로 낸다 — 숨은 이름은 이름공간 범위에 나오지 않는다. public 중첩은 그대로 FQN 이다.
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::fixture
{
    REFLECT()
    class HiddenOwner
    {
    public:
        REFLECT_BODY();

        REFLECT()
        struct PublicRecord
        {
            REFLECT_BODY();
            PROPERTY()
            int32 _value{ 0 };
        };

    private:
        ENUM()
        enum class HiddenMode : uint8
        {
            First = 0,
            Second
        };

        REFLECT()
        struct HiddenRecord
        {
            REFLECT_BODY();
            PROPERTY()
            int32 _count{ 0 };
            PROPERTY()
            HiddenMode _mode{ HiddenMode::First };
        };

        PROPERTY()
        HiddenRecord _record{};
    };
} // namespace sw::fixture
