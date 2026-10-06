/**
 * @file FontSystem.h
 * @brief 글꼴 서비스입니다 — 카탈로그 · 면 열기 · 문화권 대체 사슬(이 단위), 아틀라스 · 셰이핑 · 배치는 뒤 단위가 더합니다.
 * @details 언리얼 `FCompositeFont`(서체 + 문자 범위별 대체) · 유니티 TextCore 폴백 목록의 자리입니다. 게임 스레드에서만 부릅니다.
 *          기동 단계 `Fonts`(Client 대상 — 전용 서버는 글자를 그리지 않아 열지 않는다)가 `initialize` 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Text/FontCatalog.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/IFontRasterizer.h"
#include "Engine/Text/TextTypes.h"

namespace sw
{
    class LocalizationManager;
} // namespace sw

namespace sw
{
    /** @brief 글꼴 하나를 고르는 값입니다 — 가족 이름 · 굵기 · 기울기. 크기는 배치 인자라 여기 없습니다. */
    REFLECT()
    struct SW_API FontSpec
    {
        REFLECT_BODY();
        PROPERTY( DisplayName = "Family", Tooltip = "Font family name from the font catalog; empty means the catalog default" )
        string _family{};
        PROPERTY( DisplayName = "Weight" )
        FontWeight _weight{ FontWeight::Regular };
        PROPERTY( DisplayName = "Slant" )
        FontSlant _slant{ FontSlant::Upright };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 글꼴 하나를 찾는 순서입니다 — 고른 가족의 면 → 지금 문화권의 대체 가족들 → 카탈로그 기본 가족. 코드 포인트마다 앞에서부터 글리프가 있는 면을 씁니다.
     * @details 사슬은 (가족 · 굵기 · 기울기 · 문화권)마다 한 번 만들어 캐시합니다. 값으로 주고받습니다(캐시 표가 자라면 원소 자리가 옮겨진다).
     */
    struct FontFaceChain
    {
        static constexpr uint32 kMaxFaceCount = 8; ///< 사슬에 담는 면 수 상한(대체 목록이 더 길면 뒤를 버린다)

        FontFaceId _arrFace[kMaxFaceCount]{}; ///< 앞이 먼저. 쓰지 않는 칸은 kInvalidFontFaceId
        uint8      _faceCount{ 0 };           ///< 쓰는 칸 수
        uint8      _bFauxBold{ SW_FALSE };    ///< 고른 가족에 굵은 면이 없어 일반 면을 굵게 그린다(SDF 문턱 이동)
        uint8      _bFauxItalic{ SW_FALSE };  ///< 기운 면이 없어 기울여 그린다
    };
} // namespace sw

namespace sw
{
    /**
     * @class FontSystem
     * @brief 엔진 글꼴 서비스입니다(`engine::getFontSystem()`).
     * @details 글꼴이 하나도 없는 기계(시스템 글꼴이 없는 리눅스 CI · 서버)에서도 저장소 글꼴(기본 가족)은 늘 열립니다 — 사슬 끝이 기본 가족이고,
     *          시스템 가족을 못 찾으면 처음 한 번 경고하고 건너뜁니다. 그 문자는 두부(글리프 0)로 그려집니다.
     */
    class SW_API FontSystem
    {
    public:
        FontSystem();
        /**
         * @brief 래스터라이저(시험은 결정적인 가짜)와 문화권 출처를 주입합니다.
         * @param pLocalization 대체 목록을 묻는 곳. nullptr 이면 엔진 서비스(`engine::getLocalizationManager()`)를 씁니다.
         */
        explicit FontSystem( unique_ptr<IFontRasterizer> rasterizer, const LocalizationManager* pLocalization = nullptr );
        ~FontSystem();

        FontSystem( const FontSystem& )            = delete;
        FontSystem& operator=( const FontSystem& ) = delete;

        /** @brief 카탈로그 파일을 읽고 기본 가족을 엽니다. 기본 가족을 못 열면 false(글자를 그릴 수 없다 — 기동 오류). */
        [[nodiscard]] bool initialize( string_view catalogPath );
        /** @brief 이미 읽은 카탈로그로 시작합니다(시험 · 도구). 검사 · 기본 가족 열기는 `initialize` 와 같습니다. */
        [[nodiscard]] bool initializeFromCatalog( FontCatalogDesc catalog );
        /** @brief 연 면 · 사슬 · 경고 기록을 모두 놓습니다. 다시 `initialize` 할 수 있습니다. */
        void shutdown();

        /**
         * @brief 파일 대신 메모리의 글꼴 바이트를 그 경로로 등록합니다(시험 · 게임이 글꼴을 묶어 넣을 때). 카탈로그 면 경로가 같으면 이것을 먼저 씁니다.
         * @details 이미 연 면에는 영향이 없습니다 — `initialize` 앞에 등록합니다.
         */
        void registerMemoryFontFile( string_view path, vector<uint8> bytes );

        /** @brief 그 글꼴로 코드 포인트를 그릴 면 사슬입니다. 지금 문화권의 대체 목록을 씁니다. 시작 전이면 빈 사슬입니다. */
        FontFaceChain getFaceChain( const FontSpec& spec );
        /**
         * @brief 사슬에서 코드 포인트의 글리프를 가진 첫 면입니다.
         * @details 어디에도 없으면 사슬 첫 면 + 글리프 0(두부)이고, 코드 포인트마다 처음 한 번 경고합니다. 빈 사슬이면 kInvalidFontFaceId.
         */
        FontFaceId findFaceForCodepoint( const FontFaceChain& chain, uint32 codepoint, uint32& outGlyphIndex );
        /** @brief 문화권이 바뀌면 사슬 캐시를 비웁니다(UI 가 글 판 변경에서 부른다). 연 면은 그대로 둡니다. */
        void invalidateFaceChains();

        /** @brief 시작했는지(기본 가족이 열렸는지)입니다. */
        bool isInitialized() const { return _bInitialized; }
        /** @brief 래스터라이저입니다. 시작 전에는 부르지 않습니다. */
        IFontRasterizer& getRasterizer() { return *_rasterizer; }
        /** @brief 글리프 캐시(SDF 아틀라스)입니다. 시작 전에는 부르지 않습니다. */
        GlyphCache& getGlyphCache() { return *_glyphCache; }
        /** @brief 읽은 카탈로그입니다. */
        const FontCatalogDesc& getCatalog() const { return _catalog; }
        /** @brief 한 번만 남긴 경고(못 찾은 가족 · 글리프 없는 코드 포인트)의 수입니다 — 경고가 되풀이되지 않는지 시험이 봅니다. */
        uint32 getWarnedOnceCount() const { return static_cast<uint32>( _uniqueWarned.size() ); }

    private:
        /** @brief 가족 이름으로 면을 엽니다 — 저장소 가족 → 시스템 가족. 못 찾으면 kInvalidFontFaceId(처음 한 번 경고). 결과(없음 포함)를 캐시합니다. */
        FontFaceId openFamilyFace( string_view family, FontWeight weight, FontSlant slant, uint8& outFauxBold, uint8& outFauxItalic );
        /** @brief 글꼴 파일 하나를 엽니다 — 등록한 메모리 파일 → 리소스 → 절대 경로. 같은 (경로 · 면 번호)는 한 번만 엽니다. */
        FontFaceId openFontFile( const string& path, uint32 faceIndex, bool bResourcePath );
        /** @brief 처음이면 경고를 남기고 true 입니다(키 하나에 한 번). */
        bool markWarnedOnce( uint64 key );
        /** @brief 지금 대체 목록을 묻는 곳입니다(주입하지 않았으면 엔진 서비스). */
        const LocalizationManager* findLocalization() const;

        /** @brief 연 가족 면 하나입니다(없음도 캐시해 다시 찾지 않는다). */
        struct OpenedFamilyFace
        {
            FontFaceId _face{ kInvalidFontFaceId };
            uint8      _bFauxBold{ SW_FALSE };
            uint8      _bFauxItalic{ SW_FALSE };
        };

        unique_ptr<IFontRasterizer>             _rasterizer;
        unique_ptr<GlyphCache>                  _glyphCache; ///< 시작에서 만들고 종료에서 놓는다(래스터라이저를 빌린다)
        FontCatalogDesc                         _catalog;
        unordered_map<uint64, OpenedFamilyFace> _mapOpenedFamilyFace; ///< (가족 · 굵기 · 기울기) → 연 면
        unordered_map<uint64, FontFaceId>       _mapOpenedFile;       ///< (경로 · 면 번호) → 연 면
        unordered_map<uint64, FontFaceChain>    _mapChain;            ///< (FontSpec · 문화권) → 사슬
        unordered_map<string, vector<uint8>>    _mapMemoryFile;       ///< 등록한 메모리 글꼴(소문자 경로 → 바이트)
        unordered_set<uint64>                   _uniqueWarned;        ///< 한 번 경고한 것(가족 · 코드 포인트)
        const LocalizationManager*              _pLocalization;       ///< 주입한 문화권 출처(nullptr = 엔진 서비스)
        bool                                    _bInitialized;        ///< 기본 가족이 열렸다
    };
} // namespace sw
