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

    /**
     * @struct TextureImportRule
     * @brief 텍스처 임포트와 압축 규칙입니다.
     */
    struct TextureImportRule
    {
        string                 _name;
        string                 _inherits;
        AssetImportPathFilter  _filter;
        string                 _format;
        TextureSwizzle         _swizzle;
        uint8                  _bGenerateMips : 1;
        uint8                  _bSrgb         : 1;
        uint8                  _bInvertGreen  : 1;
        [[maybe_unused]] uint8 _reserved      : 5;

        TextureImportRule()
            : _name{}
            , _inherits{}
            , _filter{}
            , _format{ "BC7_UNORM" }
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
    /**
     * @class TextureImportConfig
     * @brief TextureImportConfig.json 을 읽고 프리셋 상속과 패턴 매칭을 담당합니다.
     */
    class TextureImportConfig
    {
    public:
        TextureImportConfig();
        ~TextureImportConfig() = default;

        /** @brief 파일에서 설정을 로드하고 프리셋 상속 트리를 해석합니다. */
        [[nodiscard]] bool loadFromFile( string_view configPath );

        /** @brief JSON 문자열에서 설정을 파싱합니다. */
        [[nodiscard]] bool loadFromJSONString( string_view jsonString );

        /**
         * @brief 상대 텍스처 경로(예: "editor/textures_raw/splash.png")에 처음으로 일치하는 규칙을 찾습니다.
         * @return 일치하는 규칙을 찾으면 true 입니다.
         */
        bool findMatchingRule( string_view relativePath, TextureImportRule& outRule ) const;

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
         * @brief 뒤의 규칙을 모두 가리는 규칙의 인덱스입니다. 없으면 `getRules().size()` 입니다.
         *
         * @details `findMatchingRule` 은 첫 매칭이 이기는 방식이다. 그래서 무엇에나 매칭되는 규칙이 목록 중간에 있으면
         *          그 뒤의 규칙은 절대 선택되지 않는다. 설정을 적은 사람은 규칙을 적어 두었는데 아무 일도 일어나지
         *          않으니, 조용한 실패다.
         *
         *          맨 끝의 캐치올은 정상이고 흔한 쓰임이다(`Fallback_Default`). 그래서 "비어 있다" 만 보지 않고
         *          "비어 있는데 뒤에 규칙이 더 있다" 를 본다.
         */
        size_t findShadowingRuleIndex() const;

    private:
        /**
         * @brief 프리셋이나 규칙 객체 하나를 @p inoutRule 에 읽습니다. 객체가 아니거나 모르는 키, 모르는 swizzle 이면 오류를 남기고 false 입니다.
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
