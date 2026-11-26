// EXPECT-TYPE: sw::fixture::SourceOnlyRecord
// EXPECT-TYPE: sw::fixture::SourceOnlyMode
// EXPECT-GEN: struct Registrar<sw::fixture::SourceOnlyRecord>
// EXPECT-GEN: #if !defined(__REFLECT_PARSER__)
// EXPECT-GEN: #if !defined( SW_SHIPPING )
// EXPECT-NOT-GEN: SourceReflected.cpp"
// EXPECT-META: { Target::Property, Field::Tooltip, "_count", "Count kept only in this source" }
// EXPECT-META: TypeEditorMetadataRegistrar
// EXPECT-ASSET-FIELD: _path -
// TARGET-DEFINE: SW_FIXTURE_REGISTRAR_MODULE="ParserFixture"
// .cpp 입력 — 다른 파일이 쓰지 않는 형식을 헤더 없이 .cpp 에 두고, 등록 코드 · 에디터 메타데이터를 그 .cpp 끝에서 include 하는 inl 하나로 받는다.
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Reference/DataPath.h"

namespace sw::fixture
{
    // .cpp 입력은 그 타깃의 TU 그대로라 타깃의 컴파일 정의가 있어야 파싱된다(빌드는 COMPILE_DEFINITIONS 를 넘긴다). 정의가 없으면 이 줄에서 진다.
    static constexpr const char* kFixtureRegistrarModule = SW_FIXTURE_REGISTRAR_MODULE;

    ENUM()
    enum class SourceOnlyMode : uint8
    {
        Idle,
        Busy,
    };

    REFLECT()
    struct SourceOnlyRecord
    {
        REFLECT_BODY();
        PROPERTY( Tooltip = "Count kept only in this source" )
        int32 _count{ 0 };
        PROPERTY()
        DataPath _path;
        PROPERTY()
        SourceOnlyMode _mode{ SourceOnlyMode::Idle };
    };
} // namespace sw::fixture

#include "SourceReflected.cpp.gen.inl"
