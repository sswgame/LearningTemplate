/**
 * @file Skeleton.h
 * @brief 스켈레톤 에셋(`.skeleton.xml`)입니다. 본 이름, 부모, 레퍼런스 포즈와, 임포트가 본에 붙어 있던 메시를 기록한 읽기 전용 테이블을 담습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ResourceContext;

    class IDataInheritanceSource;

    struct MeshSkinBinding;

    /**
     * @struct SkeletonBone
     * @brief 본 하나입니다. 부모는 항상 자기보다 앞에 있습니다(루트는 -1). 모든 칸을 파일에 반드시 적습니다(`PROPERTY( Required )`).
     */
    REFLECT()
    struct SW_API SkeletonBone
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Bone name" )
        hashed_string _name;
        PROPERTY( Required, Tooltip = "Index of the parent bone, an earlier bone in the list (-1: root)" )
        int32 _parentIndex{ constant::invalid_index::kInt32 };
        /** @brief 부모 기준 레퍼런스(바인드) 로컬 변환입니다. */
        PROPERTY( Required, Tooltip = "Reference (bind) local transform from the parent" )
        BoneTransform _referencePose{};
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SkeletonAttachment
     * @brief 원본(glTF)에서 본 아래에 붙어 있던 스킨 없는 메시 하나입니다. **임포트가 기록하는 읽기 전용 데이터**입니다.
     * @details 소켓은 이 테이블이 아니라 따로 둔 소켓 에셋(`*.sockets.xml`)이 정한다. 재임포트가 이 파일을 다시 쓰기 때문이다.
     *          이 테이블은 그 소켓 파일을 처음 만들 때(무기나 투구가 어느 본의 어디에 있었는지) 읽는 근거다.
     */
    REFLECT()
    struct SW_API SkeletonAttachment
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Source node name" )
        string _name;
        /** @brief 따로 임포트된 메시(`.mesh`)의 리소스 경로입니다. 메시 공간은 노드 로컬입니다. */
        PROPERTY( Required, Tooltip = "Resource path of the separately imported mesh (mesh space is the node's local space)" )
        string _meshPath;
        PROPERTY( Required, Tooltip = "Parent bone name" )
        hashed_string _parentBone;
        PROPERTY( Required, Tooltip = "Local transform from the parent bone" )
        BoneTransform _localTransform;
    };
} // namespace sw

namespace sw
{
    /** @brief 변형이 뒤에 붙이는 본 하나입니다(머리카락 · 망토). 부모는 이름으로 적어 부모 스켈레톤의 번호가 바뀌어도 잇습니다. */
    REFLECT()
    struct SW_API SkeletonAddedBone
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Bone name; must not exist in the base skeleton" )
        hashed_string _name;
        PROPERTY( Required, Tooltip = "Parent bone name, in the base skeleton or an earlier added bone" )
        hashed_string _parentBone;
        PROPERTY( Required, Tooltip = "Reference local transform from the parent" )
        BoneTransform _referencePose;
    };

    /**
     * @struct SkeletonBoneOverride
     * @brief 변형이 부모의 본 하나를 고치는 차이입니다 — 기준 포즈와, 비율이 달라진 이 본에 클립을 재생하는 규칙입니다.
     * @details 기준 포즈는 `이동 * _translationScale + _translationOffset`, `_rotation * 회전`, `크기 * _scale` 이다. 클립의 회전과 크기 트랙에는 같은 차이를 곱하고,
     *          이동 트랙은 `_translationMode` 로 옮긴다 — 리타깃과 같은 뜻이다(`RetargetTranslationMode`): None 은 이 스켈레톤의 기준 이동(뼈 길이를 지킨다 — 언리얼 Translation Retargeting 의
     *          Skeleton 과 같고 기본값이다), Copy 는 클립 값, ScaleByPelvisHeight 는 부모 기준에서 움직인 양을 골반 높이 비로 옮긴 값(골반 · 루트에 쓴다).
     */
    REFLECT()
    struct SW_API SkeletonBoneOverride
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Name of the base skeleton bone to change" )
        hashed_string _bone;
        PROPERTY( Tooltip = "Multiplies the local translation (bone length)" )
        float32 _translationScale{ 1.0f };
        PROPERTY( Tooltip = "Added to the local translation after the scale" )
        float3 _translationOffset{};
        PROPERTY( Tooltip = "Rotation applied before the bone's own local rotation" )
        quaternion _rotation{ quaternion::Identity };
        PROPERTY( Tooltip = "Multiplies the local scale" )
        float3 _scale{ 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "How clip translation tracks reach this bone" )
        RetargetTranslationMode _translationMode{ RetargetTranslationMode::None };
    };

    /** @brief 스켈레톤이 정한 소켓 하나입니다. 변형이 무기 자리 같은 것을 뼈 추가 없이 더합니다(`SkeletalMeshComponent::findSkeletonSocketTransform`). */
    REFLECT()
    struct SW_API SkeletonSocket
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Socket name" )
        hashed_string _name;
        PROPERTY( Required, Tooltip = "Parent bone name" )
        hashed_string _parentBone;
        PROPERTY( Required, Tooltip = "Local transform from the parent bone" )
        BoneTransform _localTransform;
    };

    /**
     * @struct SkeletonVariant
     * @brief 부모 스켈레톤을 가리키고 차이만 적는 `.skeleton.xml` 의 내용입니다(루트 요소 `<SkeletonVariant>`). 읽으면 부모와 합친 `Skeleton` 이 됩니다(`Skeleton::applyVariant`).
     * @details 언리얼 USkeleton 은 뼈가 메시를 따라 합쳐지고 Virtual Bone 이 그 위에 더해지며, Godot SkeletonProfile · 유니티 Avatar 는 매핑표다 — 여기서는 파일 하나가 차이를 적어 데이터로 쌓는다(변형의 변형도 된다).
     *          차이는 뒤에 붙이는 본 · 본 고치기 · 소켓뿐이다. **본 삭제는 받지 않는다** — 번호가 밀리면 부모의 메시(`MeshSkinVertex`) · 클립 지문 · 본 배치 지문(`getHierarchyHash`)이 모두 깨진다.
     *          평가를 빼려면 본 LOD(`SkeletonBoneLOD`), 섞기에서 빼려면 마스크(`AnimationBoneMaskDef`), 보이지 않게 하려면 `_scale` 0 을 쓴다.
     *          `_base` 의 사슬(순환 · 깊이 · 없는 부모)은 공통 데이터 상속의 것이다(`DataInheritance::collectChain`). 칸 합치기만 칸별 덮어쓰기로 풀리지 않아(본을 뒤에 붙이고 비율 · 클립 규칙을 쌓는다) `applyVariant` 한 함수에 있다.
     *          합친 결과는 부모와 같은 평평한 `<Skeleton>` 글이다(`Skeleton::mergeVariantText`, `SW_DATA_INHERITANCE_MERGE`) — 읽기와 팩 쿠커가 같은 함수를 지나므로 쿠킹본에 `_base` 가 남지 않는다. "부모의 본 번호를 바꾸지 않는다" 는 합친 뒤에 본다.
     */
    REFLECT()
    struct SW_API SkeletonVariant
    {
        REFLECT_BODY();

        PROPERTY( Required, AssetPath, AssetType = "Skeleton", Tooltip = "The base skeleton (a variant may base on a variant)" )
        string _base;
        PROPERTY( Tooltip = "Bones appended after the base bones; base bone numbers stay, so its meshes and clips keep working" )
        vector<SkeletonAddedBone> _listAddedBone;
        PROPERTY( Tooltip = "Reference pose and proportion changes of base bones" )
        vector<SkeletonBoneOverride> _listBoneOverride;
        PROPERTY( Tooltip = "Sockets added to the base" )
        vector<SkeletonSocket> _listSocket;
        PROPERTY( Tooltip = "Meshes the import found under bones of this model (read only, the base for socket files)" )
        vector<SkeletonAttachment> _listAttachment;
        PROPERTY( Tooltip = "Pelvis bone; required when an override uses ScaleByPelvisHeight" )
        hashed_string _pelvisBone;
        PROPERTY( Tooltip = "This skeleton is a part of another one; clip tracks and leader bones it lacks are not errors" )
        uint8                  _bPartial : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        SkeletonVariant();
        /** @brief XML 본문을 만듭니다(임포터가 모델마다 쓰는 얇은 파일). */
        string toXML() const;
    };

    /** @brief 변형이 본 하나에 거는 클립 재생 규칙을 합친 것입니다(변형의 변형이면 쌓입니다). `Skeleton::applyClipRule` 이 씁니다. */
    REFLECT()
    struct SW_API SkeletonClipRule
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Translation of the top skeleton the clips were authored on" )
        float3 _sourceTranslation;
        PROPERTY( Tooltip = "Reference translation of this skeleton" )
        float3 _translation;
        PROPERTY( Tooltip = "Rotation multiplied onto clip rotation tracks" )
        quaternion _rotation;
        PROPERTY( Tooltip = "Scale multiplied onto clip scale tracks" )
        float3 _scale;
        PROPERTY( Tooltip = "How clip translation tracks reach this bone" )
        RetargetTranslationMode _translationMode;
        PROPERTY( Tooltip = "A variant changed this bone" )
        uint8                  _bActive  : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        SkeletonClipRule();
    };

    /**
     * @brief 변형을 펼친 글에만 있는 값입니다. 평평한 스켈레톤의 칸으로는 알 수 없는, 층을 쌓아 나온 결과입니다.
     * @details 저장 · 임포트가 쓰는 평범한 스켈레톤에는 이 칸이 없다(`SkipIfEmpty`) — 공용 스켈레톤 파일 이름이 글의 지문이라 글이 바뀌면 안 된다.
     */
    REFLECT()
    struct SW_API SkeletonVariantState
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Clip rules per bone" )
        vector<SkeletonClipRule> _listClipRule;
        PROPERTY( Tooltip = "Bases, nearest first" )
        vector<string> _listBasePath;
        PROPERTY( Tooltip = "First bone a variant appended" )
        uint32 _firstAddedBone;
        PROPERTY( Tooltip = "Pelvis height ratio to the top skeleton" )
        float32 _translationRatio;

        SkeletonVariantState();
    };
} // namespace sw

namespace sw
{
    /**
     * @class Skeleton
     * @brief 본 배열입니다(부모가 자식보다 앞에 옵니다). 런타임 포즈 계산(`Pose`)과 스킨 팔레트가 이것을 읽습니다.
     * @details 파일은 리플렉션 구조체로 읽고 쓴다(`ConfigManager::readConfigXMLDocument`). 키는 PROPERTY 이름(`_listBone`, `_listAttachment`)이 기준이고
     *          모두 필수다(`PROPERTY( Required )`) — 빠진 키 · 모르는 키 · 읽지 못한 값은 오류다. 벡터 · 쿼터니언은 쉼표로 이은 숫자 글이다.
     *          변형은 차이만 적은 파일이고(`SkeletonVariant`) 읽으면 합쳐져 이 클래스 하나로 보인다. 쓰는 쪽은 변형인지 모른다.
     *          `_parentIndex` 는 본 배열의 인덱스(루트 -1)이고 자기보다 앞이어야 한다. 역 바인드는 스켈레톤이 아니라 메시의 값이다(`MeshSkinBinding`). 읽은 뒤 부모 순서와 부착 본 이름을 따로 본다.
     */
    REFLECT()
    class SW_API Skeleton
    {
        friend struct AssetTypeLoader;

    public:
        REFLECT_BODY();

        /** @brief 스켈레톤 에셋 확장자입니다. */
        static constexpr string_view kExtension = ".skeleton.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `Skeleton.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "Skeleton", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        Skeleton();

        /**
         * @brief 본 하나를 끝에 붙이고 그 인덱스를 반환합니다.
         * @param parentIndex 부모 본 인덱스. **이미 추가된 본만 가리킬 수 있습니다**(루트는 -1). 아니면 아무것도 추가하지 않고 -1 입니다.
         */
        int32 addBone( const hashed_string& name, int32 parentIndex, const BoneTransform& referencePose );
        /** @brief 레퍼런스 포즈를 바인드로 삼은 메시의 역 바인드를 본마다 구합니다(코드가 지은 메시와 테스트용 — 임포트한 메시는 원본의 값을 싣는다). */
        void computeReferenceInverseBinds( vector<float4x4>& outListInverseBind ) const;
        /**
         * @brief 앞 @p boneCount 개 본의 배치(이름 · 부모 번호) 지문입니다. 프로세스와 무관하게 같은 값이라 메시 파일에 실립니다(`MeshSkinBinding::_layoutHash`). 범위 밖이면 0 입니다.
         * @details 기준 포즈는 섞지 않는다 — 비율만 다른 스켈레톤은 같은 메시 · 클립을 쓴다. 뒤에 본을 더해도 앞의 지문은 그대로라 변형이 부모의 메시를 받는다.
         */
        uint64 getHierarchyHash( uint32 boneCount ) const;
        /** @brief 이 스켈레톤으로 @p skinBinding 의 메시를 그릴 수 있으면 true 입니다 — 메시의 본이 모두 있고 앞쪽 본 배치가 같습니다. */
        [[nodiscard]] bool canDrawSkinBinding( const MeshSkinBinding& skinBinding ) const;
        /** @brief 이름으로 본 인덱스를 찾습니다. 없으면 없음입니다. */
        optional<uint32> findBoneIndex( const hashed_string& name ) const;
        /** @brief 본 수입니다. */
        uint32 getBoneCount() const { return static_cast<uint32>( _listBone.size() ); }
        /** @brief 본 하나입니다(범위는 부르는 쪽이 지킵니다). */
        const SkeletonBone& getBone( uint32 boneIndex ) const { return _listBone[boneIndex]; }
        /** @brief 본마다 부모 인덱스입니다(`Pose::computeModelSpace` 의 입력). */
        const vector<int32>& getParentIndices() const { return _listParentIndex; }
        /** @brief 임포트가 적은 본 부착 메시 표입니다. */
        const vector<SkeletonAttachment>& getAttachments() const { return _listAttachment; }
        /** @brief 이름의 소켓입니다. 없으면 nullptr 입니다. */
        const SkeletonSocket* findSocket( const hashed_string& name ) const;
        /** @brief 이 스켈레톤이 정한 소켓 전부입니다(본 LOD 가 부모 본을 핵심 본으로 지킨다). */
        const vector<SkeletonSocket>& getSockets() const { return _listSocket; }
        /** @brief 다른 스켈레톤의 일부인 부품(복장 · 소품)이면 true 입니다 — 클립 트랙과 리더 본 중 이 스켈레톤에 없는 것은 오류가 아닙니다. */
        bool isPartial() const { return _bPartial == SW_TRUE; }
        /** @brief 부품으로 선언합니다(임포트 규칙 `_bPartialSkeleton`, 코드가 지은 스켈레톤). */
        void setPartial( bool bPartial ) { _bPartial = bPartial ? SW_TRUE : SW_FALSE; }
        /** @brief 변형이 뒤에 붙인 본이면 true 입니다 — 부모에 없으므로 리더 · 클립에 없어도 됩니다. */
        bool isAddedBone( uint32 boneIndex ) const { return boneIndex >= _firstAddedBone; }
        /** @brief 변형이 고친 본이 있어 클립 트랙에 규칙을 걸어야 하면 true 입니다. */
        bool hasClipRules() const { return _listClipRule.empty() == false; }
        /** @brief 클립이 준 본 변환에 변형의 재생 규칙을 겁니다(`SkeletonBoneOverride`). 규칙이 없는 본은 그대로입니다. */
        BoneTransform applyClipRule( uint32 boneIndex, const BoneTransform& sampled ) const;
        /** @brief 변형이면 가까운 부모부터의 경로입니다. 옆 파일(본 LOD)을 변형 곁에 두지 않으면 부모 것을 씁니다. */
        const vector<string>& getBasePaths() const { return _listBasePath; }
        /** @brief 모두 비웁니다. */
        void clear();

        /**
         * @brief XML 본문을 읽습니다. 루트가 `<SkeletonVariant>` 이면 부모(`_base`)를 읽어 합칩니다. 형식이 틀리면(모르는 키, 빠진 키, 앞에 없는 부모, 모르는 본 이름) false 를 반환하고 내용은 비웁니다.
         */
        [[nodiscard]] bool loadFromXMLText( string_view xml, string_view sourceLabel );
        /**
         * @brief @p base 위에 @p variant 의 차이를 합쳐 이 스켈레톤으로 삼습니다. 부모의 본 번호는 그대로이고 더한 본은 뒤에 붙습니다.
         * @return 모르는 본을 고치거나, 있는 이름의 본 · 소켓을 더하거나, 골반 없이 ScaleByPelvisHeight 를 쓰면 오류를 남기고 내용을 비운 채 false 입니다.
         */
        [[nodiscard]] bool applyVariant( const SkeletonVariant& variant, const Skeleton& base, string_view sourceLabel );
        /**
         * @brief `<SkeletonVariant>` 글의 사슬을 위 층부터 쌓아 부모 없는 평평한 `<Skeleton>` 글을 씁니다. 읽기와 팩 쿠커가 지나는 합치기입니다(`SW_DATA_INHERITANCE_MERGE`).
         * @return 사슬 오류(순환 · 깊이 · 없는 부모)이거나 층이 틀리면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool mergeVariantText( string_view text, string_view sourceName, IDataInheritanceSource& source, string& outText );
        /**
         * @brief 파일을 지금 읽어 이 객체를 채웁니다(공유 캐시를 거치지 않는 사본). 쿠킹 작업 전용이다.
         * @details 쿠킹 작업(`CookJob`)은 범위 안의 리소스 읽기를 입력으로 기록하므로 캐시가 쥔 한 벌을 쓰면 입력이 빠진다 — 그래서 사본을 읽는 이름을 따로 둔다.
         *          런타임 · 에디터의 쓰는 쪽은 `SharedAssetCache<Skeleton>::acquire` 로 한 벌을 쥔다.
         */
        [[nodiscard]] bool loadCopyFromAsset( const AssetID& assetID ) { return loadFromAsset( assetID ); }
        /** @brief XML 본문을 만듭니다(모든 PROPERTY). */
        string toXML() const;
        /** @brief 파일로 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;

    private:
        /** @brief 합성 문맥을 받는 읽기의 본문입니다. `loadFromAsset` 은 리소스 id 를 실은 문맥(`withAsset`)을, 글 입구는 id 없는 문맥을 넘깁니다(덮어쓰기 층 · 다시 읽기 기록 · 접속 대조는 id 로 읽을 때만 받는다). */
        [[nodiscard]] bool loadComposed( string_view xml, string_view sourceLabel, const ResourceContext& context );
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<Skeleton>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 읽은 본과 부착을 검증하며 다시 쌓습니다(부모가 앞에 있는지, 부착 · 소켓 본이 있는지). `_listParentIndex` 도 여기서 채웁니다. */
        [[nodiscard]] bool rebuildFromLoaded( string_view sourceLabel );
        /** @brief 평평한 `<Skeleton>` 문서를 읽습니다. 사슬의 맨 위 층입니다. 실패하면 내용을 비웁니다. */
        [[nodiscard]] bool parseFlat( string_view xml, string_view sourceLabel );
        /** @brief 소켓 하나를 더합니다. 이름이 겹치거나 부모 본이 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool addSocket( const SkeletonSocket& socket, string_view sourceLabel );
        /** @brief 본 고치기 하나를 기준 포즈와 클립 규칙에 겁니다. 모르는 본이면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool applyBoneOverride( const SkeletonBoneOverride& boneOverride, string_view sourceLabel );

        PROPERTY( Required, Tooltip = "Bones; a parent comes before its children" )
        vector<SkeletonBone> _listBone;
        /** @brief 본마다 부모 인덱스입니다. `_listBone` 에서 다시 만들므로 파일에 적지 않습니다. */
        vector<int32> _listParentIndex;
        /** @brief n 번째 칸이 앞 n + 1 개 본의 배치 지문입니다(`getHierarchyHash`). `addBone` 이 채웁니다. */
        vector<uint64> _listHierarchyHash;
        PROPERTY( Required, Tooltip = "Meshes the import found under bones (read only, the base for socket files)" )
        vector<SkeletonAttachment> _listAttachment;
        PROPERTY( Tooltip = "Sockets this skeleton defines" )
        vector<SkeletonSocket> _listSocket;
        PROPERTY( SkipIfEmpty, Tooltip = "What stacking variants produced; only a flattened variant has it (read into the fields below)" )
        vector<SkeletonVariantState> _listVariantState;
        /** @brief 변형의 클립 재생 규칙입니다(본 번호 순). 변형이 본을 고치지 않았으면 비어 있고, 파일에는 차이로 적습니다. */
        vector<SkeletonClipRule> _listClipRule;
        /** @brief 변형의 부모 경로입니다(`getBasePaths`). */
        vector<string> _listBasePath;
        /** @brief 변형이 붙인 첫 본의 번호입니다(`isAddedBone`). 붙인 본이 없으면 어느 번호보다 큽니다. */
        uint32 _firstAddedBone;
        /** @brief ScaleByPelvisHeight 의 비입니다(이 스켈레톤 골반 높이 / 클립이 저작된 맨 위 스켈레톤 골반 높이). */
        float32 _translationRatio;
        PROPERTY( Tooltip = "This skeleton is a part of another one; clip tracks and leader bones it lacks are not errors" )
        uint8                  _bPartial : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
