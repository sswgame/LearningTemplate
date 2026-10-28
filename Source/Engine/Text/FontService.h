/**
 * @file FontService.h
 * @brief 폰트 서비스를 정의합니다. 이 계층은 카탈로그, 면 열기, 문화권 대체 연쇄를 담당하고, 아틀라스, 셰이핑, 배치는 이후 계층이 추가합니다.
 * @details 언리얼 `FCompositeFont`(서체 + 문자 범위별 대체)와 유니티 TextCore 폴백 목록에 해당한다. 게임 스레드에서만 부른다.
 *          기동 단계 `Fonts`(Client 대상이며, 전용 서버는 글자를 그리지 않아 열지 않는다)가 `initialize` 한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Common/EngineSubsystemHost.h"
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
    /** @brief 폰트 하나를 고르는 값입니다. 패밀리 이름, 굵기, 기울기를 가집니다. 크기는 배치 인자라 여기에 없습니다. */
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
     * @brief 폰트 하나를 찾는 순서입니다. 고른 패밀리의 면, 지금 문화권의 대체 패밀리들, 카탈로그 기본 패밀리 순서이며, 코드 포인트마다 앞에서부터 글리프가 있는 면을 씁니다.
     * @details 연쇄는 (패밀리, 굵기, 기울기, 문화권)마다 한 번 만들어 캐시한다. 값으로 주고받는다(캐시 테이블이 늘어나면 원소 위치가 옮겨지기 때문이다).
     */
    struct FontFaceChain
    {
        static constexpr uint32 kMaxFaceCount = 8; ///< 사슬에 담는 면 수 상한(대체 목록이 더 길면 뒤를 버린다)

        FontFaceID _arrFace[kMaxFaceCount]{}; ///< 앞이 먼저입니다. 쓰지 않는 슬롯은 kInvalidFontFaceID 입니다.
        uint8      _faceCount{ 0 };           ///< 쓰는 슬롯 수입니다.
        uint8      _bFauxBold{ SW_FALSE };    ///< 고른 가족에 굵은 면이 없어 일반 면을 굵게 그린다(SDF 임계값 이동)
        uint8      _bFauxItalic{ SW_FALSE };  ///< 기운 면이 없어 기울여 그립니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class FontService
     * @brief 엔진 폰트 서비스입니다(`engine::getSubsystem<FontService>()`).
     * @details 폰트가 하나도 없는 기계(시스템 폰트가 없는 리눅스 CI, 서버)에서도 저장소 폰트(기본 패밀리)는 항상 열린다. 연쇄의 끝이 기본 패밀리이고,
     *          시스템 패밀리를 찾지 못하면 처음 한 번 경고하고 건너뛴다. 그 문자는 두부(글리프 0)로 그려진다.
     *          엔진에서는 서브시스템 `Fonts`(Client 대상)입니다 — 엔진 데이터의 글꼴 카탈로그로 서고, 쿠킹(Headless)이면 열지 않는다.
     */
    class SW_API FontService : public ISubsystem
    {
    public:
        FontService();
        /**
         * @brief 래스터라이저(테스트는 결정적인 가짜)와 문화권 출처를 주입합니다.
         * @param pLocalization 대체 목록을 묻는 곳. nullptr 이면 문화권 대체 없이 카탈로그 기본 연쇄만 씁니다(부를 때 전역 표를 찾지 않습니다).
         */
        explicit FontService( unique_ptr<IFontRasterizer> rasterizer, const LocalizationManager* pLocalization = nullptr );
        ~FontService() override;

        FontService( const FontService& )            = delete;
        FontService& operator=( const FontService& ) = delete;

        /** @brief 카탈로그 파일을 읽고 기본 패밀리를 엽니다. 기본 패밀리를 열지 못하면 false 입니다(글자를 그릴 수 없으므로 기동 오류입니다). */
        [[nodiscard]] bool initialize( const AssetID& assetID );
        /** @brief 이미 읽은 카탈로그로 시작합니다(테스트와 도구). 검사와 기본 패밀리 열기는 `initialize` 와 같습니다. */
        [[nodiscard]] bool initializeFromCatalog( FontCatalogDesc catalog );
        /** @brief 연 면, 연쇄, 경고 기록을 모두 해제합니다. 다시 `initialize` 할 수 있습니다. */
        void shutdown();

        // ISubsystem — 기동 단계 `Fonts`. 기본 가족(저장소 글꼴)을 못 열면 글자를 그릴 수 없으므로 기동 오류다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Fonts";

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override { shutdown(); }
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        /** @brief 문화권 표(EngineDefaultAssets)를 읽은 뒤에 선다. 쿠킹(Headless)이면 열지 않는데, 그 순서는 높은 층인 `HeadlessSubsystem` 이 역방향으로 적는다. */
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override { return EngineSubsystemHost::kClient; }

        /**
         * @brief 파일 대신 메모리의 폰트 바이트를 그 경로로 등록합니다(테스트나 게임이 폰트를 묶어 넣을 때). 카탈로그 면 경로가 같으면 이것을 먼저 씁니다.
         * @details 이미 연 면에는 영향이 없으므로 `initialize` 앞에 등록한다.
         */
        void registerMemoryFontFile( string_view path, vector<uint8> bytes );

        /** @brief 그 폰트로 코드 포인트를 그릴 면 연쇄입니다. 지금 문화권의 대체 목록을 씁니다. 시작 전이면 빈 연쇄입니다. */
        FontFaceChain getFaceChain( const FontSpec& spec );
        /**
         * @brief 사슬에서 코드 포인트의 글리프를 가진 첫 면입니다.
         * @details 어디에도 없으면 사슬 첫 면 + 글리프 0(두부)이고, 코드 포인트마다 처음 한 번 경고합니다. 빈 사슬이면 kInvalidFontFaceID.
         */
        FontFaceID findFaceForCodepoint( const FontFaceChain& chain, uint32 codepoint, uint32& outGlyphIndex );
        /** @brief 문화권이 바뀌면 연쇄 캐시를 비웁니다(UI 가 텍스트 버전 변경에서 부릅니다). 연 면은 그대로 둡니다. */
        void invalidateFaceChains();

        /** @brief 시작했는지(기본 가족이 열렸는지)입니다. */
        bool isInitialized() const { return _bInitialized; }
        /** @brief 래스터라이저입니다. 시작 전에는 부르지 않습니다. */
        IFontRasterizer& getRasterizer() { return *_rasterizer; }
        /** @brief 글리프 캐시(SDF 아틀라스)입니다. 시작 전에는 부르지 않습니다. */
        GlyphCache& getGlyphCache() { return *_glyphCache; }
        /** @brief 읽은 카탈로그입니다. */
        const FontCatalogDesc& getCatalog() const { return _catalog; }
        /** @brief 한 번만 남긴 경고(못 찾은 패밀리, 글리프 없는 코드 포인트)의 수입니다. 경고가 반복되지 않는지 테스트가 확인합니다. */
        uint32 getWarnedOnceCount() const { return static_cast<uint32>( _uniqueWarned.size() ); }

    private:
        /** @brief 패밀리 이름으로 면을 엽니다. 저장소 패밀리를 먼저, 시스템 패밀리를 다음으로 봅니다. 못 찾으면 kInvalidFontFaceID 를 반환하고 처음 한 번 경고합니다. 결과(없음 포함)를 캐시합니다. */
        FontFaceID openFamilyFace( string_view family, FontWeight weight, FontSlant slant, uint8& outFauxBold, uint8& outFauxItalic );
        /** @brief 폰트 파일 하나를 엽니다. 등록한 메모리 파일, 리소스, 절대 경로 순서로 찾습니다. 같은 (경로, 면 번호)는 한 번만 엽니다. */
        FontFaceID openFontFile( const AssetID& assetID, uint32 faceIndex, bool bAssetPath );
        /** @brief 처음이면 경고를 남기고 true 입니다(키 하나에 한 번). */
        bool markWarnedOnce( const string& key );
        /** @brief 연 가족 면 하나입니다(없음도 캐시해 다시 찾지 않는다). */
        struct OpenedFamilyFace
        {
            FontFaceID _face{ kInvalidFontFaceID };
            uint8      _bFauxBold{ SW_FALSE };
            uint8      _bFauxItalic{ SW_FALSE };
        };

        unique_ptr<IFontRasterizer> _rasterizer;
        unique_ptr<GlyphCache>      _glyphCache; ///< 시작할 때 만들고 종료할 때 해제합니다(래스터라이저를 빌립니다).
        FontCatalogDesc             _catalog;
        // 아래 세 맵의 키는 해시가 아니라 원문 문자열이다(`FontServiceInternal::kKeySeparator` 로 이은 값) — 해시 단독 키는 부딪히면 다른 면을 조용히 돌려준다.
        unordered_map<string, OpenedFamilyFace> _mapOpenedFamilyFace; ///< (패밀리, 굵기, 기울기)에서 연 면을 찾는 맵입니다.
        unordered_map<string, FontFaceID>       _mapOpenedFile;       ///< (경로, 면 번호)에서 연 면을 찾는 맵입니다.
        unordered_map<string, FontFaceChain>    _mapChain;            ///< (FontSpec, 문화권)에서 연쇄를 찾는 맵입니다.
        unordered_map<string, vector<uint8>>    _mapMemoryFile;       ///< 등록한 메모리 폰트입니다(소문자 경로에서 바이트를 찾습니다).
        unordered_set<string>                   _uniqueWarned;        ///< 한 번 경고한 항목입니다(패밀리, 코드 포인트). 키는 원문입니다.
        const LocalizationManager*              _pLocalization;       ///< 문화권 출처(생성자 주입, 엔진 서브시스템은 `initializeSubsystem` 이 한 번 받음. nullptr = 대체 없음)
        bool                                    _bInitialized;        ///< 기본 패밀리를 열었는지 나타냅니다.
    };
} // namespace sw
