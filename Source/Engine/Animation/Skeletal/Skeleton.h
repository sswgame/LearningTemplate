/**
 * @file Skeleton.h
 * @brief 스켈레톤 에셋(`.skeleton.xml`)입니다. 본 이름, 부모, 레퍼런스 포즈, 역 바인드와, 임포트가 본에 붙어 있던 메시를 기록한 읽기 전용 테이블을 담습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Asset/AssetID.h"
#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
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
        int32 _parentIndex{ -1 };
        /** @brief 부모 기준 레퍼런스(바인드) 로컬 변환입니다. */
        PROPERTY( Required, Tooltip = "Reference (bind) local transform from the parent" )
        BoneTransform _referencePose{};
        /** @brief 모델 공간 바인드 행렬의 역입니다(행 우선 16 개). 스킨 팔레트 = 역 바인드 * 모델 공간. */
        PROPERTY( Required, Tooltip = "Inverse of the model space bind matrix (16 numbers, row major)" )
        float4x4 _inverseBind{ float4x4::Identity };
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
    /**
     * @class Skeleton
     * @brief 본 배열입니다(부모가 자식보다 앞에 옵니다). 런타임 포즈 계산(`Pose`)과 스킨 팔레트가 이것을 읽습니다.
     * @details 파일은 리플렉션 구조체로 읽고 쓴다(`ConfigManager::readConfigXMLElement`). 키는 PROPERTY 이름(`_listBone`, `_listAttachment`)이 기준이고
     *          모두 필수다(`PROPERTY( Required )`) — 빠진 키 · 모르는 키 · 읽지 못한 값은 오류다. 벡터 · 쿼터니언 · 행렬은 쉼표로 이은 숫자 글이다.
     *          `_parentIndex` 는 본 배열의 인덱스(루트 -1)이고 자기보다 앞이어야 한다. 행렬은 행 우선 16 개다. 읽은 뒤 부모 순서와 부착 본 이름을 따로 본다.
     */
    REFLECT()
    class SW_API Skeleton
    {
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

        Skeleton() = default;

        /**
         * @brief 본 하나를 끝에 붙이고 그 인덱스를 반환합니다.
         * @param parentIndex 부모 본 인덱스. **이미 추가된 본만 가리킬 수 있습니다**(루트는 -1). 아니면 아무것도 추가하지 않고 -1 입니다.
         */
        int32 addBone( const hashed_string& name, int32 parentIndex, const BoneTransform& referencePose, const float4x4& inverseBind );
        /** @brief 레퍼런스 포즈로 모든 본의 역 바인드를 다시 구합니다(손으로 만든 스켈레톤과 테스트용). */
        void computeInverseBindFromReference();
        /** @brief 이름으로 본 인덱스를 찾습니다. 없으면 -1 입니다. */
        int32 findBoneIndex( const hashed_string& name ) const;
        /** @brief 본 수입니다. */
        uint32 getBoneCount() const { return static_cast<uint32>( _listBone.size() ); }
        /** @brief 본 하나입니다(범위는 부르는 쪽이 지킵니다). */
        const SkeletonBone& getBone( uint32 boneIndex ) const { return _listBone[boneIndex]; }
        /** @brief 본마다 부모 인덱스입니다(`Pose::computeModelSpace` 의 입력). */
        const vector<int32>& getParentIndices() const { return _listParentIndex; }
        /** @brief 임포트가 적은 본 부착 메시 표입니다. */
        const vector<SkeletonAttachment>& getAttachments() const { return _listAttachment; }
        /** @brief 본 부착 메시를 하나 더합니다(임포터). */
        void addAttachment( const SkeletonAttachment& attachment ) { _listAttachment.push_back( attachment ); }
        /** @brief 모두 비웁니다. */
        void clear();

        /** @brief XML 본문을 읽습니다. 형식이 틀리면(모르는 키, 빠진 키, 앞에 없는 부모, 모르는 본 이름) false 를 반환하고 내용은 비웁니다. */
        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief XML 본문을 만듭니다(모든 PROPERTY). */
        string toXML() const;
        /** @brief 파일로 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] bool saveToFile( string_view path ) const;

    private:
        /** @brief 읽은 본과 부착을 검증하며 다시 쌓습니다(부모가 앞에 있는지, 부착 본이 있는지). `_listParentIndex` 도 여기서 채웁니다. */
        [[nodiscard]] bool rebuildFromLoaded( string_view sourceLabel );

        PROPERTY( Required, Tooltip = "Bones; a parent comes before its children" )
        vector<SkeletonBone> _listBone;
        /** @brief 본마다 부모 인덱스입니다. `_listBone` 에서 다시 만들므로 파일에 적지 않습니다. */
        vector<int32> _listParentIndex;
        PROPERTY( Required, Tooltip = "Meshes the import found under bones (read only, the base for socket files)" )
        vector<SkeletonAttachment> _listAttachment;
    };
} // namespace sw
