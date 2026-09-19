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

        /**
         * @brief 이 규칙이 **무엇에나 매칭되는가** — 네 목록이 모두 비어 있으면 그렇습니다.
         * @details `findMatchingRule` 은 include 목록이 비면 그 검사를 건너뛰고 exclude 목록이 비면
         *          거를 것이 없다. 즉 넷 다 비면 **첫 경로에서 바로 매칭된다.**
         */
        static bool isCatchAllRule( const TextureImportRule& rule );

        /**
         * @brief **뒤 규칙을 전부 가리는** 규칙의 인덱스입니다. 없으면 `getRules().size()`.
         *
         * @details `findMatchingRule` 은 "첫 매칭이 이긴다". 그래서 무엇에나 매칭되는 규칙이 목록
         *          **중간**에 있으면 그 뒤 규칙은 **영원히 선택되지 않는다** — 설정을 적은 사람은
         *          규칙을 적어 뒀는데 아무 일도 일어나지 않는, 조용한 실패다.
         *
         *          맨 **끝**의 캐치올은 정상이고 흔한 쓰임이다(`Fallback_Default`). 그래서 "비어 있다"
         *          가 아니라 **"비어 있는데 뒤에 뭔가 더 있다"** 를 본다.
         *
         *          이 함수가 있는 이유는 `rules` 배열이 관대하게 파싱되기 때문이기도 하다 — 객체가
         *          아닌 원소는 필드를 하나도 못 읽어 **캐치올이 되어** 뒤를 전부 덮는다. 그 동작은
         *          의도적으로 유지하되(에디터 전용, 손으로 적는 파일), 결과는 소리 나게 한다.
         */
        size_t findShadowingRuleIndex() const;

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
