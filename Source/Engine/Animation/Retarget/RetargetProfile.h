/**
 * @file RetargetProfile.h
 * @brief 리타깃 프로필(`*.retarget.xml`)입니다. 원본과 대상 스켈레톤 사이의 사슬 쌍(이름, 본 목록), 골반과 뿌리 처리, IK 목표 사슬을 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Common/AssetTypeRegistry.h"

namespace sw
{
    class XMLNode;

    /** @brief 뿌리와 골반 이동을 옮기는 방법입니다. */
    enum class RetargetTranslationMode : uint8
    {
        ScaleByPelvisHeight = 0, ///< 골반 높이 비(대상 / 원본)만큼 줄이거나 늘린다. 다리 길이가 다른 캐릭터가 같은 보폭 비로 걷는다.
        Copy,                    ///< 그대로 옮긴다
        None,                    ///< 대상 레퍼런스 위치에 둔다(제자리 동작).
    };

    /**
     * @brief 사슬 하나의 쌍입니다. 본 수가 달라도 됩니다. 대상 본마다 사슬 길이 비율로 원본 본을 고릅니다.
     * @details `_bIKGoal` 이면 회전을 옮긴 뒤 끝을 원본 끝 위치(골반 높이 비로 바꾼 값)로 IK 해서, 발이 미끄러지지 않고 디디게 한다.
     */
    struct RetargetChain
    {
        hashed_string         _name{};
        vector<hashed_string> _listSourceBone{};
        vector<hashed_string> _listTargetBone{};
        uint8                 _bIKGoal{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 본 이름 테이블의 한 줄입니다. 원본 본 하나의 모델 공간 회전 차이를 대상 본 하나로 옮깁니다(사슬이 아닌 머리, 손가락, 장식 본). */
    struct RetargetBonePair
    {
        hashed_string _source{};
        hashed_string _target{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class RetargetProfile
     * @brief 프로필 하나입니다. 형식은 XML 이며, 모르는 속성 · 자식이나 겹친 사슬 이름은 오류입니다(읽기는 후처리 리그와 같은 `RigXMLReader` 다 — 값은 속성, 목록은 `<item>`).
     * @code
     * <RetargetProfile source_skeleton="game/.../knight.skeleton.xml" target_skeleton="game/.../skeleton_minion.skeleton.xml"
     *                  translation="ScaleByPelvisHeight" map_same_names="true" height_ratio="1.25">
     *   <root source="root" target="root" />
     *   <pelvis source="hips" target="hips" />
     *   <chains>
     *     <item name="LeftLeg" ik_goal="true">
     *       <source><item>upperleg.l</item><item>lowerleg.l</item><item>foot.l</item></source>
     *       <target><item>upperleg.l</item><item>lowerleg.l</item><item>foot.l</item></target>
     *     </item>
     *   </chains>
     *   <bone_map><item source="head" target="Head" /></bone_map>
     * </RetargetProfile>
     * @endcode
     *          - `bone_map`(선택): 본 이름 테이블이다. 사슬로 묶지 않은 본을 한 줄에 하나씩 짝짓는다.
     *          - `map_same_names`(선택): 사슬, 테이블 어디에도 없는 대상 본 중 원본에 같은 이름이 있으면 짝짓는다(언리얼 IK Retargeter 의 이름 자동 매핑).
     *          - `height_ratio`(선택, 0 보다 큼): 뿌리와 골반 이동, IK 목표 사슬의 끝 위치에 곱하는 비율이다. 적지 않으면 골반 높이 비(대상 / 원본)를 쓴다.
     *            골반 높이가 체형을 대표하지 못하는 스켈레톤(네 발, 웅크린 레퍼런스)에 쓴다.
     *          짝이 없는 대상 본은 대상 레퍼런스 포즈 그대로입니다(IK 보조 본, 손가락). 짝은 사슬, 테이블, 같은 이름 순으로 먼저 정한 것이 이깁니다.
     */
    class SW_API RetargetProfile
    {
    public:
        /** @brief 프로필 확장자입니다. */
        static constexpr string_view kExtension = ".retarget.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `RetargetProfile.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "RetargetProfile", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        RetargetProfile();

        /** @brief XML 본문을 읽습니다. 틀리면 false 이고 내용은 비웁니다. */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        void               clear();

        const string&                getSourceSkeletonPath() const { return _sourceSkeletonPath; }
        const string&                getTargetSkeletonPath() const { return _targetSkeletonPath; }
        const hashed_string&         getSourceRoot() const { return _sourceRoot; }
        const hashed_string&         getTargetRoot() const { return _targetRoot; }
        const hashed_string&         getSourcePelvis() const { return _sourcePelvis; }
        const hashed_string&         getTargetPelvis() const { return _targetPelvis; }
        RetargetTranslationMode      getTranslationMode() const { return _translationMode; }
        const vector<RetargetChain>& getChains() const { return _listChain; }
        /** @brief 본 이름 테이블입니다. */
        const vector<RetargetBonePair>& getBonePairs() const { return _listBonePair; }
        /** @brief 같은 이름의 본을 자동으로 짝지을지입니다. */
        bool isMappingSameNames() const { return _bMapSameNames == SW_TRUE; }
        /** @brief 뿌리와 골반 이동 비율의 덮어쓰기입니다. 0 이면 골반 높이 비를 계산합니다. */
        float32 getHeightRatioOverride() const { return _heightRatioOverride; }
        /** @brief 사슬을 추가합니다(코드로 만드는 프로필과 테스트용). */
        void addChain( const RetargetChain& chain ) { _listChain.push_back( chain ); }
        /** @brief 본 이름 테이블에 한 줄을 추가합니다(코드로 만드는 프로필과 테스트용). */
        void addBonePair( const RetargetBonePair& pair ) { _listBonePair.push_back( pair ); }
        /** @brief 같은 이름 자동 짝짓기를 정합니다(코드로 만드는 프로필과 테스트용). */
        void setMapSameNames( bool bMap ) { _bMapSameNames = bMap ? SW_TRUE : SW_FALSE; }
        /** @brief 이동 비율 덮어쓰기를 정합니다. 0 이하이면 계산한 골반 높이 비를 씁니다. */
        void setHeightRatioOverride( float32 ratio ) { _heightRatioOverride = ratio > 0.0f ? ratio : 0.0f; }
        /** @brief 뿌리와 골반 쌍, 이동 방법을 정합니다(코드로 만드는 프로필과 테스트용). */
        void setRootAndPelvis( const hashed_string& sourceRoot, const hashed_string& targetRoot, const hashed_string& sourcePelvis, const hashed_string& targetPelvis,
                               RetargetTranslationMode mode );

    private:
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );

        vector<RetargetChain>    _listChain;
        vector<RetargetBonePair> _listBonePair;
        string                   _sourceSkeletonPath;
        string                   _targetSkeletonPath;
        hashed_string            _sourceRoot;
        hashed_string            _targetRoot;
        hashed_string            _sourcePelvis;
        hashed_string            _targetPelvis;
        float32                  _heightRatioOverride;
        RetargetTranslationMode  _translationMode;
        uint8                    _bMapSameNames;
    };
} // namespace sw
