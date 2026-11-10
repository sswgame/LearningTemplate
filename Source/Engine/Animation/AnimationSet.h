/**
 * @file AnimationSet.h
 * @brief 애니메이션 셋(`.animset.xml`)입니다. 이름 → 클립 표이고, 다른 셋을 바탕으로 차이만 적을 수 있습니다.
 * @details 캐릭터와 그래프는 경로가 아니라 셋의 이름으로 클립을 찾는다. 클립 파일은 내용 키의 공유 저장소에 한 벌만 있어(`ImportedAssetStore`) 모델 수만큼 생기지 않고, 뼈는 이름으로 찾으므로
 *          (`AnimationClip::makeTrackToBoneMap`) 스켈레톤이 달라도 나눠 쓴다.
 *          상용 엔진 비교(기억에 의존): 언리얼은 Animation Sequence 를 스켈레톤 에셋이 나눠 쓰게 하고 Animation Blueprint 가 슬롯 이름으로 고른다. 유니티는 Avatar 로 근육 공간을 나누고
 *          AnimatorOverrideController 가 "기반 + 바꾼 클립" 이다. 여기는 뼈 이름 일치로 나누고(Avatar 의 근육 공간 없음), 셋의 `_base` 가 오버라이드 컨트롤러 자리다 —
 *          그래프는 클립 이름(슬롯)만 적고 애니메이터의 셋이 내용을 정한다. 바탕은 공통 상속(`DataInheritance`, 이름이 열쇠)이고 쿠킹이 펼친다.
 *          `set` 은 부모가 아니라 줄 하나를 다른 셋에서 빌리는 믹스인이다(`_listRenderPassRef` 와 같은 까닭으로 합치지 않는다 — 여러 파일을 섞는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ResourceContext;

    /**
     * @brief 셋의 클립 한 줄(`<clip name path/>` · `<clip name set [source] [retarget]/>`)입니다. 경로와 셋 중 하나만 적는다.
     * @details `path` 는 임포트가 쓰는 공유 파일이다 — 손으로 쓰는 셋은 키가 내용과 함께 바뀌는 경로를 적지 않고 `set`(그 셋의 같은 이름, 또는 `source` 이름)으로 가리킨다.
     *          바탕의 `path` 줄을 `set` 으로 바꾸는 차이는 `_op="Replace"` 를 적는다(칸을 합치면 둘 다 남아 오류다).
     */
    REFLECT()
    struct SW_API AnimationSetClipParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _name{};
        PROPERTY( AssetPath )
        string _path{};
        PROPERTY( AssetPath )
        string _set{};
        PROPERTY()
        string _source{};
        PROPERTY( AssetPath )
        string _retarget{}; ///< 다른 뼈대의 클립이면 그 리타깃 프로필(`.retarget.xml`). 가져오기(`--import-models`)가 구운 클립(`AnimationSet::makeRetargetedClipPath`)을 쓴다 — 언리얼 리타깃 배치처럼 구운 결과는 가져오기 산출물이다
    };

    /**
     * @struct AnimationSetEntry
     * @brief 펼친 셋의 한 줄입니다(이름은 소문자 — 애니메이터의 클립 이름 규칙).
     */
    struct AnimationSetEntry
    {
        string _name;
        string _clipPath;
        string _retargetProfile{};    ///< 구운 클립이면 그 프로필(비면 원본 그대로다)
        string _retargetSourceClip{}; ///< 구운 클립이면 원본 클립 경로(가져오기가 이것을 `_clipPath` 로 굽는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationSet
     * @brief 이름 → 클립 경로 표입니다. 읽을 때 `_base` · `set` 을 모두 펼쳐 표 하나로 둡니다.
     */
    class SW_API AnimationSet
    {
        friend struct AssetTypeLoader;

    public:
        /** @brief 셋 파일의 확장자입니다. */
        static constexpr string_view kExtension = ".animset.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `AnimationSet.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "AnimationSet", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
        /** @brief `set` 사슬의 최대 길이입니다. 넘으면 순환으로 보고 오류입니다(`_base` 사슬은 `DataInheritance` 가 본다). */
        static constexpr uint32 kMaxChainDepth = 8;

        /** @brief 이름의 클립 경로입니다(대소문자 무시). 없으면 nullptr 입니다. */
        [[nodiscard]] const string* findClipPath( const hashed_string& name ) const;
        /** @brief 원본 클립을 프로필로 구운 클립의 자원 경로입니다(`common/models/_retargeted/<프로필 이름>/<원본 클립 파일 이름>`). 가져오기가 쓰고 셋이 읽는다 — 두 쪽이 이 하나로 맞춘다. */
        [[nodiscard]] static string makeRetargetedClipPath( string_view profilePath, string_view sourceClipPath );
        /** @brief 이름순 표입니다. */
        [[nodiscard]] const vector<AnimationSetEntry>& getEntries() const { return _listEntry; }

        /** @brief 클립 한 줄을 넣습니다(임포트가 쓴다). 같은 이름이면 바꿉니다. */
        void setClip( string_view name, string_view clipPath );
        /** @brief 파일 글입니다(`path` 줄만 — 이미 펼친 표다). 이름순이라 같은 표는 같은 바이트입니다. */
        [[nodiscard]] string saveToString() const;

        /** @brief XML 을 읽고 `_base` · `set` 을 펼칩니다. 틀리면 오류를 남기고 false 이며 내용은 비웁니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /**
         * @brief 파일을 지금 읽어 이 객체를 채웁니다(공유 캐시를 거치지 않는 사본). 쿠킹 작업 전용이다.
         * @details 쿠킹 작업(`CookJob`)은 범위 안의 리소스 읽기를 입력으로 기록하므로 캐시가 쥔 한 벌을 쓰면 입력이 빠진다 — 그래서 사본을 읽는 이름을 따로 둔다.
         *          런타임 · 에디터의 쓰는 쪽은 `SharedAssetCache<AnimationSet>::acquire` 로 한 벌을 쥔다.
         */
        [[nodiscard]] bool loadCopyFromAsset( const AssetID& assetID ) { return loadFromAsset( assetID ); }

    private:
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<AnimationSet>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief @p depth 번째 사슬 칸을 읽습니다. */
        [[nodiscard]] bool parseChained( string_view xml, string_view sourceLabel, uint32 depth, const ResourceContext& context );
        /** @brief 리소스 경로의 셋 파일을 @p depth 번째 사슬 칸으로 읽습니다. */
        [[nodiscard]] bool loadChained( string_view path, uint32 depth );
        /** @brief 정렬을 지키며 한 줄을 넣습니다. */
        void insertEntry( string name, string clipPath );

    private:
        vector<AnimationSetEntry> _listEntry;
    };
} // namespace sw
