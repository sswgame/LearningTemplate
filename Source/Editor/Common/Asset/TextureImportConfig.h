#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class JsonValue;
} // namespace sw

namespace sw::editor
{
    /**
     * @enum TextureSwizzle
     * @brief 채널 순서 (기본 RGBA, UI용 BGRA, 레거시 ARGB, 불투명 RGB1 등)
     */
    enum class TextureSwizzle : uint8
    {
        RGBA = 0,
        BGRA,
        ARGB,
        RGB1,
    };

    /**
     * @struct TextureImportRule
     * @brief 텍스처 임포트/압축 베이킹 규칙
     */
    struct TextureImportRule
    {
        string                 _name;
        string                 _inherits;
        vector<string>         _listIncludePattern;
        vector<string>         _listExcludePattern;
        vector<string>         _listIncludePath;
        vector<string>         _listExcludePath;
        string                 _format;
        TextureSwizzle         _swizzle;
        uint8                  _bGenerateMips : 1;
        uint8                  _bSrgb         : 1;
        uint8                  _bInvertGreen  : 1;
        [[maybe_unused]] uint8 _reserved      : 5;

        TextureImportRule()
            : _name{}
            , _inherits{}
            , _listIncludePattern{}
            , _listExcludePattern{}
            , _listIncludePath{}
            , _listExcludePath{}
            , _format{ "BC7_UNORM" }
            , _swizzle{ TextureSwizzle::RGBA }
            , _bGenerateMips{ SW_TRUE }
            , _bSrgb{ SW_TRUE }
            , _bInvertGreen{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @class TextureImportConfig
     * @brief TextureImportConfig.json 로드 및 프리셋 상속/패턴 매칭 관리자
     */
    class TextureImportConfig
    {
    public:
        TextureImportConfig();
        ~TextureImportConfig() = default;

        /** @brief 파일에서 설정을 로드하고 프리셋 상속 트리를 해석합니다. */
        bool loadFromFile( string_view configPath );

        /** @brief JSON 문자열에서 설정을 파싱합니다. */
        bool loadFromJsonString( string_view jsonString );

        /**
         * @brief 상대 텍스처 경로(예: "editor/textures_raw/splash.jpg")에 대해 가장 먼저 일치하는 규칙을 찾아 반환합니다.
         * @return 매칭되는 규칙이 발견되면 true
         */
        bool findMatchingRule( string_view relativePath, TextureImportRule& outRule ) const;

        /** @brief 등록된 프리셋 맵을 반환합니다. */
        const map<string, TextureImportRule>& getPresets() const { return _mapPreset; }

        /** @brief 등록된 규칙 목록을 반환합니다. */
        const vector<TextureImportRule>& getRules() const { return _listRule; }

    private:
        void parseRuleObject( const sw::JsonValue& jsonValue, TextureImportRule& inoutRule );
        /**
         * @brief `inherits` 가 가리키는 프리셋을 @p inoutRule 의 바탕으로 깔아 줍니다.
         * @details **못 찾으면 경고를 남긴다.** 예전에는 프리셋 쪽과 규칙 쪽이 이 일을 각자
         *          복사해 갖고 있었고 둘 다 **조용히 넘어갔다** — `inherits` 에 오타가 있거나
         *          부모를 아래쪽에 적으면(찾기는 그 시점까지 파싱된 것만 본다) 상속이 통째로
         *          사라진 채 기본값으로 구워졌고, 아무도 그것을 알 수 없었다.
         */
        void applyInheritance( const sw::JsonValue& jsonValue, TextureImportRule& inoutRule ) const;

    private:
        map<string, TextureImportRule> _mapPreset;
        vector<TextureImportRule>      _listRule;
    };
} // namespace sw::editor
