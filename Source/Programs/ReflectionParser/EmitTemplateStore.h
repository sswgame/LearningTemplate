/**
 * @file EmitTemplateStore.h
 * @brief 이름 붙은 .tpl 골격을 로드하고 $VAR / ${VAR} 자리 표시자를 확장합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) emit — *.tpl 골격 이름
    // ------------------------------------------------------------------------------
    /**
     * @brief 생성기가 쓰는 골격 하나 = `Templates/<이름>.tpl` 파일 하나입니다(이름은 `kArrEmitTemplateName`).
     * @details 이름을 글자로 넘기면 철자가 틀린 골격이 출력 때 조용히 비었다. 이제 시작할 때 모두 있어야 하고(`loadDirectory`), 부르는 쪽은 열거자로만 고른다.
     */
    enum class EmitTemplate : uint8
    {
        FileHeader,
        ReflectTypeTraits,
        TypeInfoAccessors,
        TypeRegistrarBegin,
        TypeRegistrarEnd,
        EnumRegistrarBegin,
        EnumRegistrarEnd,
        BuiltinFileHeader,
        BuiltinTypeRegistrar,
        BuiltinFileFooter,
        Count,
    };

    /** @brief `EmitTemplate` 열거자 순서의 파일 이름(확장자 뺀 것)입니다. */
    inline constexpr const utf8* kArrEmitTemplateName[] = {
        "FileHeader",
        "ReflectTypeTraits",
        "TypeInfoAccessors",
        "TypeRegistrarBegin",
        "TypeRegistrarEnd",
        "EnumRegistrarBegin",
        "EnumRegistrarEnd",
        "BuiltinFileHeader",
        "BuiltinTypeRegistrar",
        "BuiltinFileFooter",
    };
    static_assert( std::size( kArrEmitTemplateName ) == static_cast<size_t>( EmitTemplate::Count ), "kArrEmitTemplateName must list every EmitTemplate" );

    // ------------------------------------------------------------------------------
    // 2) emit — *.tpl 골격 로드·$VAR 확장
    // ------------------------------------------------------------------------------
    /**
     * @class EmitTemplateStore
     * @brief 파일로 두는 emit 골격(registrar · traits 등)입니다. 제어 흐름은 C++ 에 둡니다.
     */
    class EmitTemplateStore
    {
    public:
        using TemplateVars = std::initializer_list<pair<string_view, string_view>>;

        EmitTemplateStore()  = default;
        ~EmitTemplateStore() = default;

        /**
         * @brief 절대 경로 디렉터리에서 골격을 모두 로드합니다. 파일 이름(확장자 뺀 것)이 골격 이름입니다.
         * @return 폴더를 못 훑거나, 파일 하나라도 못 읽거나, `EmitTemplate` 의 골격이 하나라도 없거나, 모르는 이름의 파일이 있으면 이유를 알리고 false
         */
        [[nodiscard]] bool loadDirectory( const string_view absDir, const string_view extension );

        /** @brief 골격을 확장합니다. 모르는 변수는 빈 문자열입니다. */
        string render( EmitTemplate kind, TemplateVars vars ) const;

        /**
         * @brief 메모리 속 골격을 확장합니다. `$$` 는 `$` 하나, `$Name` · `${Name}` 은 변수입니다.
         */
        static string expand( const string_view tpl, TemplateVars vars );

    private:
        string _arrTemplate[static_cast<size_t>( EmitTemplate::Count )];
    };
} // namespace sw
