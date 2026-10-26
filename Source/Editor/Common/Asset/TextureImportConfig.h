#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Asset/AssetImportPathFilter.h"

namespace sw
{
    enum class ConfigReadResult : uint8;

    class JSONValue;
} // namespace sw

namespace sw::editor
{
    /**
     * @enum TextureSwizzle
     * @brief 채널 순서입니다. 기본은 RGBA 이고, UI 용 BGRA, 레거시 ARGB, 불투명 RGB1 등이 있습니다.
     */
    enum class TextureSwizzle : uint8
    {
        RGBA = 0,
        BGRA,
        ARGB,
        RGB1,
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @enum TextureImportFormat
     * @brief 임포트 결과의 픽셀 형식입니다. 규칙의 `format` 이름은 `TextureImportFormatUtil::parse` 로만 이 값이 됩니다.
     * @details 이름 표는 하나뿐입니다. 설정 로드가 모르는 이름을 거절하고, 임포터는 이 열거형을 DXGI 로 바꿀 뿐이라 "모르는 이름이면 BC7" 같은 기본값이 설 자리가 없습니다.
     *          BC2 는 없다 — 같은 크기에 BC3 가 알파를 더 잘 담고 언리얼 · 유니티의 압축 설정에도 없다(규칙의 `bc2` 는 로드 오류).
     */
    enum class TextureImportFormat : uint8
    {
        BC1 = 0,
        BC3,
        BC4,
        BC5,
        BC6H,
        BC7,
        B8G8R8A8,
        R8G8B8A8,
        R16G16B16A16Float,
        /** @brief 1 채널(R8_UNORM, DXGI 61). 마스크 · 높이 같은 데이터 텍스처이며 원본의 R 채널만 남습니다. DDS 는 DX10 머리로 씁니다. */
        R8,
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct TextureImportFormatUtil
     * @brief `format` 이름(`BC7_UNORM` · `bc7` …)과 `TextureImportFormat` 을 잇는 유일한 표입니다.
     */
    struct TextureImportFormatUtil
    {
        /** @brief @p name 을 형식으로 읽습니다. 모르는 이름이면 false 이고 @p outFormat 은 그대로입니다. */
        [[nodiscard]] static bool parse( string_view name, TextureImportFormat& outFormat );
        /** @brief @p format 의 DXGI 이름(`BC7_UNORM` …)입니다. 로그에 씁니다. */
        [[nodiscard]] static string_view getName( TextureImportFormat format );
        /**
         * @brief 임포터가 @p format · @p bSrgb 로 써 내는 DDS 의 DXGI 형식 번호입니다. sRGB 짝이 없는 형식(BC4 · BC5 · BC6H · R16G16B16A16_FLOAT · R8)은 @p bSrgb 를 보지 않습니다.
         * @details 임포터(`TextureImporter`)는 이 함수로만 DXGI 를 고른다 — 이름 · 번호 · sRGB 짝이 한 표에 있어 에셋 규칙과 대조하는 테스트가 같은 표를 읽는다.
         */
        [[nodiscard]] static uint32 getDxgiFormat( TextureImportFormat format, bool bSrgb );
        /**
         * @brief 임포터가 써 낼 수 있는 모든 DDS 형식의 DXGI 이름입니다(형식마다 UNORM 과, 있으면 sRGB 짝).
         * @details 에셋 검증 규칙(`Config/Editor/AssetValidationRules.json` 의 `allowed_formats` · `rejected_formats`)이 이 이름을 모두 허용 · 거절 중 하나로 적었는지
         *          테스트가 본다(`EditorTexturePipelineTest.EveryImportOutputFormatIsDecidedByTheAssetRules`). 새 형식을 더하면 규칙 파일도 고쳐야 그 테스트가 통과한다.
         */
        [[nodiscard]] static vector<string_view> getWrittenFormatNames();
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct TextureImportRule
     * @brief 텍스처 임포트와 압축 규칙입니다.
     * @details 형식은 이름이 아니라 `TextureImportFormat` 으로 든다 — 이름은 설정을 읽을 때 한 번 풀고 모르는 이름은 그때 거절한다. 쓰는 쪽에 "못 푸는 형식" 이 없다.
     */
    struct TextureImportRule
    {
        string                 _name;
        string                 _inherits;
        AssetImportPathFilter  _filter;
        TextureImportFormat    _format;
        TextureSwizzle         _swizzle;
        uint8                  _bGenerateMips : 1;
        uint8                  _bSrgb         : 1;
        uint8                  _bInvertGreen  : 1;
        [[maybe_unused]] uint8 _reserved      : 5;

        TextureImportRule()
            : _name{}
            , _inherits{}
            , _filter{}
            , _format{ TextureImportFormat::BC7 }
            , _swizzle{ TextureSwizzle::RGBA }
            , _bGenerateMips{ SW_TRUE }
            , _bSrgb{ SW_TRUE }
            , _bInvertGreen{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 텍스처 규칙 선택의 결과입니다(`TextureImportConfig::findMatchingRule`). */
    using TextureImportRuleMatch = AssetImportRuleMatch<TextureImportRule>;

    /**
     * @class TextureImportConfig
     * @brief TextureImportConfig.json 을 읽고 프리셋 상속과 패턴 매칭을 담당합니다. 한 원본에는 조건 있는 규칙이 하나만 맞아야 합니다.
     */
    class TextureImportConfig
    {
    public:
        TextureImportConfig();
        ~TextureImportConfig() = default;

        /** @brief 파일에서 설정을 로드하고 프리셋 상속 트리를 해석합니다. 파일이 없으면 `Missing`(기본 규칙 — 정상), 있는데 읽지 못하면 오류를 남기고 `Invalid` 입니다. */
        [[nodiscard]] ConfigReadResult loadFromFile( string_view configPath );

        /** @brief JSON 문자열에서 설정을 파싱합니다. */
        [[nodiscard]] bool loadFromJSONString( string_view jsonString );

        /**
         * @brief 상대 텍스처 경로(예: "editor/textures_raw/splash.png")에 맞는 규칙을 찾습니다.
         * @details 조건 있는 규칙은 하나만 맞아야 하고 둘 이상 맞으면 `_error` 를 채웁니다(순서로 고르지 않는다). 조건 있는 규칙이 하나도 맞지 않으면
         *          조건 없는 규칙(나머지 규칙)이 받고, 그것도 없으면 `_bMatched` 가 거짓이며 기본 규칙입니다.
         */
        [[nodiscard]] TextureImportRuleMatch findMatchingRule( string_view relativePath ) const;

        /** @brief 등록된 프리셋 맵을 반환합니다. */
        const map<string, TextureImportRule>& getPresets() const { return _mapPreset; }

        /** @brief 등록된 규칙 목록을 반환합니다. */
        const vector<TextureImportRule>& getRules() const { return _listRule; }

        /**
         * @brief 이 규칙이 무엇에나 매칭되는지 확인합니다. 네 목록이 모두 비어 있으면 매칭됩니다.
         * @details `findMatchingRule` 은 include 목록이 비어 있으면 그 검사를 건너뛰고, exclude 목록이 비어 있으면 거를 것이 없다. 즉 네 목록이 모두
         *          비어 있으면 어떤 경로에도 바로 매칭된다.
         */
        static bool isCatchAllRule( const TextureImportRule& rule );

        /**
         * @brief 맨 끝이 아닌 자리에 있는 조건 없는 규칙의 인덱스입니다. 없으면 `getRules().size()` 입니다.
         *
         * @details 조건 없는 규칙은 나머지를 받는 규칙(`Fallback_Default`)이라 맨 끝에 하나만 둡니다. 맨 끝의 캐치올은 정상이고 흔한 쓰임이라
         *          "비어 있다" 만 보지 않고 "비어 있는데 뒤에 규칙이 더 있다" 를 봅니다. 로드는 이 자리가 있으면 오류로 멈춥니다.
         */
        size_t findShadowingRuleIndex() const;

    private:
        /**
         * @brief 프리셋이나 규칙 객체 하나를 @p inoutRule 에 읽습니다. 객체가 아니거나 모르는 키, 모르는 swizzle · format 이면 오류를 남기고 false 입니다.
         * @details 키 테이블은 `TextureImportConfig.cpp` 의 `kArrTextureImportRuleKeyDoc` 에 있다. 생성 문서는 `docs/Config/TextureImportConfig.md` 이다.
         */
        [[nodiscard]] static bool parseRuleObject( const sw::JSONValue& jsonValue, TextureImportRule& inoutRule, string_view context );
        /**
         * @brief `inherits` 가 가리키는 프리셋을 @p inoutRule 의 바탕으로 적용합니다. `inherits` 가 없으면 아무것도 하지 않고 true 입니다.
         * @details 찾기는 그 시점까지 파싱된 프리셋만 본다. 오타이거나 부모를 아래쪽에 적었으면 오류를 남기고 false 를 반환한다.
         *          상속이 통째로 사라진 채 기본값으로 임포트되면 아무도 알 수 없기 때문이다.
         */
        [[nodiscard]] bool applyInheritance( const sw::JSONValue& jsonValue, TextureImportRule& inoutRule ) const;
        /** @brief 반쯤 읽은 테이블을 남기지 않도록 비우고 false 를 반환합니다. */
        bool clearAndFail();

    private:
        map<string, TextureImportRule> _mapPreset;
        vector<TextureImportRule>      _listRule;
    };
} // namespace sw::editor
