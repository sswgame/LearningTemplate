/**
 * @file RetargetProfile.h
 * @brief 리타깃 프로필(`*.retarget.xml`)입니다. 원본과 대상 스켈레톤 사이의 사슬 쌍(이름, 본 목록), 골반과 루트 처리, IK 목표 사슬을 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ResourceContext;

    class XMLNode;

    /** @brief 루트와 골반 이동을 옮기는 방법입니다(파일에는 이름으로 적는다). */
    ENUM()
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
    /** @brief `<chains><item name ik_goal>` 의 칸입니다. 본 목록(`source` · `target`)은 이름 목록이라 손으로 푼다. */
    REFLECT()
    struct SW_API RetargetChainParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "name", Required )
        hashed_string _name{};
        PROPERTY( Name = "ik_goal" )
        bool _bIKGoal{ false };
    };

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
    REFLECT()
    struct SW_API RetargetBonePair
    {
        REFLECT_BODY();

        PROPERTY( Name = "source", Required )
        hashed_string _source{};
        PROPERTY( Name = "target", Required )
        hashed_string _target{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 리타깃 포즈(대상 레퍼런스를 원본 레퍼런스 자세에 맞추는 포즈, 언리얼 IK Retargeter 의 Retarget Pose)의 한 줄입니다.
     * @details 대상 본 하나를 **부모 공간에서** 도(`[피치, 요, 롤]`, `quaternion::makeFromYawPitchRoll` 배치) 만큼 더 돌립니다 — 새 로컬 회전 = 돌림 × 레퍼런스 로컬 회전.
     *          A 자세 대상을 T 자세 원본에 맞출 때 팔을 내리는 식으로 쓴다. 이동 · 스케일은 건드리지 않는다(체형은 본 비율 `BoneProportion` 의 몫).
     */
    struct RetargetPoseBone
    {
        hashed_string _bone{};
        float3        _rotationDegrees{};
    };

    /** @brief `<target_pose><item bone rotation>` 의 칸입니다(파일 스키마 — 회전은 파일 글 그대로 도 단위다). */
    REFLECT()
    struct SW_API RetargetPoseBoneParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "rotation", Required, Units = deg )
        float3 _rotation{};
    };

    /** @brief 파일 루트(`<RetargetProfile …>`)의 칸입니다. 루트 · 골반 짝과 목록은 자식 요소라 손으로 푼다. */
    REFLECT()
    struct SW_API RetargetProfileRootParams
    {
        REFLECT_BODY();

        PROPERTY( AssetPath, Name = "source_skeleton" )
        string _sourceSkeleton{};
        PROPERTY( AssetPath, Name = "target_skeleton" )
        string _targetSkeleton{};
        PROPERTY( Name = "translation" )
        RetargetTranslationMode _translation{ RetargetTranslationMode::ScaleByPelvisHeight };
        PROPERTY( Name = "map_same_names" )
        bool _bMapSameNames{ false };
        PROPERTY( Name = "height_ratio", Min = 0.0001, Units = ratio ) ///< 적지 않으면 0(골반 높이 비를 계산한다) — 적으면 양수여야 한다
        float32 _heightRatio{ 0.0f };
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
     *   <target_pose><item bone="upperarm.l" rotation="0,0,-40" /></target_pose>
     * </RetargetProfile>
     * @endcode
     *          - `target_pose`(선택): 리타깃 포즈다. 대상 본을 부모 공간에서 도(`[피치, 요, 롤]`)만큼 더 돌려, 대상 레퍼런스(A 자세)를 원본 레퍼런스(T 자세)에 맞춘다.
     *            회전 차이는 이 포즈를 기준으로 옮긴다(`PoseRetargeter::initialize`가 대상 레퍼런스에 얹는다). 같은 본을 두 번 적으면 오류다.
     *          - `bone_map`(선택): 본 이름 테이블이다. 사슬로 묶지 않은 본을 한 줄에 하나씩 짝짓는다.
     *          - `map_same_names`(선택): 사슬, 테이블 어디에도 없는 대상 본 중 원본에 같은 이름이 있으면 짝짓는다(언리얼 IK Retargeter 의 이름 자동 매핑).
     *          - `height_ratio`(선택, 0 보다 큼): 루트와 골반 이동, IK 목표 사슬의 끝 위치에 곱하는 비율이다. 적지 않으면 골반 높이 비(대상 / 원본)를 쓴다.
     *            골반 높이가 체형을 대표하지 못하는 스켈레톤(네 발, 웅크린 레퍼런스)에 쓴다.
     *          짝이 없는 대상 본은 대상 레퍼런스 포즈 그대로입니다(IK 보조 본, 손가락). 짝은 사슬, 테이블, 같은 이름 순으로 먼저 정한 것이 이깁니다.
     */
    class SW_API RetargetProfile
    {
        friend struct AssetTypeLoader;

    public:
        /** @brief 프로필 확장자입니다. */
        static constexpr string_view kExtension = ".retarget.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `RetargetProfile.cpp` 의 캐시 등록 줄 `SW_SHARED_ASSET_CACHE`). */
        static constexpr AssetTypeDef kAssetType{ "RetargetProfile", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        RetargetProfile();

        /** @brief XML 본문을 읽습니다. 틀리면 false 이고 내용은 비웁니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /** @brief 같은 형식의 XML 로 씁니다(`loadFromXMLText` 이 그대로 다시 읽는다 — 에디터 리타깃 창이 저장한다). 기본값인 선택 키는 쓰지 않습니다. */
        string toXML() const;
        /**
         * @brief @p absPath 에 씁니다. 부모(`_base`)를 가리키던 글에서 읽었으면 부모와의 차이로 씁니다(`DataInheritance::makeDelta`) — 에디터 리타깃 창이 저장한다.
         * @details 차이를 다시 읽어 같은 프로필이 나올 때만 씁니다. 기본값인 선택 키는 적지 않으므로, 부모가 적은 값을 자식이 기본값으로 되돌린 편집은 차이로 적을 수 없어 쓰지 않고 오류다(부모를 끊지 않는다).
         */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /** @brief 읽은 글이 가리킨 부모의 리소스 id 입니다. 부모 없이 읽었거나 비웠으면 비어 있습니다. */
        const string& getBaseID() const { return _baseID; }
        /** @brief 부모를 정합니다. 저장이 그 부모와의 차이로 씁니다(되돌리기가 펼친 글로 복원한 프로필에 문서의 부모를 다시 단다). */
        void setBaseID( string_view baseID ) { _baseID = string{ baseID }; }
        void clear();

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
        /** @brief 루트와 골반 이동 비율의 덮어쓰기입니다. 0 이면 골반 높이 비를 계산합니다. */
        float32 getHeightRatioOverride() const { return _heightRatioOverride; }
        /** @brief 사슬을 추가합니다(코드로 만드는 프로필과 테스트용). */
        void addChain( const RetargetChain& chain ) { _listChain.push_back( chain ); }
        /** @brief 사슬 목록을 바꿉니다. 이름이 겹치면 다시 읽을 때 오류이므로 넣는 쪽이 지킨다. */
        void setChains( vector<RetargetChain>&& listChain ) { _listChain = std::move( listChain ); }
        /** @brief 본 이름 테이블을 바꿉니다. */
        void setBonePairs( vector<RetargetBonePair>&& listBonePair ) { _listBonePair = std::move( listBonePair ); }
        /** @brief 리타깃 포즈 줄입니다(대상 본 순서 무관). */
        const vector<RetargetPoseBone>& getTargetPose() const { return _listTargetPose; }
        /** @brief 리타깃 포즈 줄을 바꿉니다. 같은 본이 겹치면 다시 읽을 때 오류이므로 넣는 쪽이 지킨다. */
        void setTargetPose( vector<RetargetPoseBone>&& listPose ) { _listTargetPose = std::move( listPose ); }
        /** @brief 두 스켈레톤 경로를 정합니다. */
        void setSkeletonPaths( string_view sourcePath, string_view targetPath );
        /** @brief 본 이름 테이블에 한 줄을 추가합니다(코드로 만드는 프로필과 테스트용). */
        void addBonePair( const RetargetBonePair& pair ) { _listBonePair.push_back( pair ); }
        /** @brief 같은 이름 자동 짝짓기를 정합니다(코드로 만드는 프로필과 테스트용). */
        void setMapSameNames( bool bMap ) { _bMapSameNames = bMap ? SW_TRUE : SW_FALSE; }
        /** @brief 이동 비율 덮어쓰기를 정합니다. 0 이하이면 계산한 골반 높이 비를 씁니다. */
        void setHeightRatioOverride( float32 ratio ) { _heightRatioOverride = ratio > 0.0f ? ratio : 0.0f; }
        /** @brief 루트와 골반 쌍, 이동 방법을 정합니다(코드로 만드는 프로필과 테스트용). */
        void setRootAndPelvis( const hashed_string& sourceRoot, const hashed_string& targetRoot, const hashed_string& sourcePelvis, const hashed_string& targetPelvis,
                               RetargetTranslationMode mode );

    private:
        /** @brief 합성 문맥을 받는 읽기의 본문입니다. `loadFromAsset` 은 리소스 id 를 실은 문맥(`withAsset`)을, 글 입구는 id 없는 문맥을 넘깁니다(덮어쓰기 층 · 다시 읽기 기록 · 접속 대조는 id 로 읽을 때만 받는다). */
        [[nodiscard]] bool loadComposed( string_view xml, string_view sourceLabel, const ResourceContext& context );
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<RetargetProfile>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        [[nodiscard]] bool parseRoot( const XMLNode& root, string_view sourceLabel );

    private:
        vector<RetargetChain>    _listChain;
        vector<RetargetBonePair> _listBonePair;
        vector<RetargetPoseBone> _listTargetPose;
        string                   _sourceSkeletonPath;
        string                   _targetSkeletonPath;
        string                   _baseID; ///< 읽은 글의 `_base`. `saveToFile` 이 부모와의 차이로 쓴다
        hashed_string            _sourceRoot;
        hashed_string            _targetRoot;
        hashed_string            _sourcePelvis;
        hashed_string            _targetPelvis;
        float32                  _heightRatioOverride;
        RetargetTranslationMode  _translationMode;
        uint8                    _bMapSameNames;
    };

    /**
     * @brief 리타깃 프로필을 경로마다 한 벌 나눠 줍니다("RetargetProfile", 인스턴스화는 `RetargetProfile.cpp`). 제자리에서 다시 읽습니다.
     * @details 같은 프로필을 쓰는 캐릭터가 파일을 다시 읽지 않는다. 쥔 쪽(`PoseRetargetComponent`)은 핫 리로드 알림(`onPropertyChanged`)에서 리타기터를 다시 묶는다.
     *          에디터 리타깃 창은 이 한 벌을 사본으로 받아 고친다(언리얼 IK Retargeter 에셋 한 벌, 기억에 의존).
     */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_API_EXTERN_TEMPLATE, RetargetProfile );
} // namespace sw
