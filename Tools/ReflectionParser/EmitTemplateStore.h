/**
 * @file EmitTemplateStore.h
 * @brief 이름 붙은 .tpl 골격을 로드하고 $VAR / ${VAR} 자리 표시자를 확장합니다.
 */
#pragma once
#include "Engine/EngineMinimal.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) emit — *.tpl 골격 로드·$VAR 확장
    // ------------------------------------------------------------------------------
    /**
     * @class EmitTemplateStore
     * @brief 파일로 두는 emit 골격(registrar · traits 등)입니다. 제어 흐름은 C++ 에 둡니다.
     */
    class EmitTemplateStore
    {
    public:
        using TemplateVars = std::initializer_list<pair<string_view, string_view>>;

        EmitTemplateStore();
        ~EmitTemplateStore() = default;

        /** @brief 로드된 템플릿을 비웁니다. */
        void clear();
        /**
         * @brief 절대 경로 디렉터리에서 템플릿 파일을 로드합니다. 파일 이름(확장자 뺀 것)이 템플릿 이름입니다.
         * @param extension 템플릿 확장자(parser_config 의 `template_extension`, 기본 ".tpl")
         */
        bool loadDirectory( const string_view absDir, const string_view extension );
        /** @brief 템플릿이 로드되었는지 반환합니다. */
        bool isLoaded() const noexcept { return _bLoaded == SW_TRUE; }

        /** @brief 이름에 해당하는 템플릿이 있는지 조회합니다. */
        bool has( const string_view name ) const;
        /** @brief 템플릿을 확장합니다. 모르는 변수는 빈 문자열, 템플릿이 없으면 빈 결과를 반환합니다. */
        string render( const string_view name, TemplateVars vars ) const;

        /**
         * @brief 메모리 속 골격을 확장합니다. `$$` 는 `$` 하나, `$Name` · `${Name}` 은 변수입니다.
         * @details 예전에는 이 함수가 변수 표의 꼴(맵 · 초기화 목록)마다 한 벌씩 두 벌 있었고, 맵 쪽은 쓰는 곳이 없었습니다.
         */
        static string expand( const string_view tpl, TemplateVars vars );

    private:
        unordered_map<string, string> _mapTemplate;
        uint8                         _bLoaded  : 1;
        [[maybe_unused]] uint8        _reserved : 7;
    };
} // namespace sw
