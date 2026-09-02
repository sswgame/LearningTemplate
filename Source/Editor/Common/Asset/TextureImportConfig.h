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
        bool matchRule( string_view relativePath, TextureImportRule& outRule ) const;

        /** @brief 등록된 프리셋 맵을 반환합니다. */
        const map<string, TextureImportRule>& getPresets() const { return _mapPreset; }

        /** @brief 등록된 규칙 목록을 반환합니다. */
        const vector<TextureImportRule>& getRules() const { return _listRule; }

    private:
        void parseRuleObject( const sw::JsonValue& jsonValue, TextureImportRule& inoutRule );

    private:
        map<string, TextureImportRule> _mapPreset;
        vector<TextureImportRule>      _listRule;
    };
} // namespace sw::editor
