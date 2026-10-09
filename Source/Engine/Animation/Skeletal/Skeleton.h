/**
 * @file Skeleton.h
 * @brief 스켈레톤 에셋(`.skeleton.json`) — 본 이름 · 부모 · 레퍼런스 포즈 · 역 바인드와, 임포트가 본에 붙어 있던 메시를 적은 읽기 전용 표입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"

namespace sw
{
    class JsonValue;

    /**
     * @struct SkeletonBone
     * @brief 본 하나입니다. 부모는 항상 자기보다 앞에 있습니다(루트는 -1).
     */
    struct SkeletonBone
    {
        hashed_string _name;
        int32         _parentIndex{ -1 };
        BoneTransform _referencePose{};                   ///< 부모 기준 레퍼런스(바인드) 로컬 변환입니다.
        float4x4      _inverseBind{ float4x4::Identity }; ///< 모델 공간 바인드 행렬의 역입니다. 스킨 팔레트 = 역 바인드 * 모델 공간.
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SkeletonAttachment
     * @brief 원본(glTF)에서 본 아래에 붙어 있던 스킨 없는 메시 하나입니다. **임포트가 적는 읽기 전용 데이터**입니다.
     * @details 소켓은 이 표가 아니라 따로 둔 소켓 에셋(`*.sockets.xml`)이 정합니다 — 재임포트가 이 파일을 다시 쓰기 때문입니다.
     *          이 표는 그 소켓 파일을 처음 만들 때(무기 · 투구가 어느 본의 어디에 있었나) 읽는 근거입니다.
     */
    struct SkeletonAttachment
    {
        string        _name;           ///< 원본 노드 이름입니다.
        string        _meshPath;       ///< 따로 임포트된 메시(`.mesh`)의 리소스 경로입니다. 메시 공간은 노드 로컬입니다.
        hashed_string _parentBone;     ///< 부모 본 이름입니다.
        BoneTransform _localTransform; ///< 부모 본 기준 로컬 변환입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class Skeleton
     * @brief 본 배열(부모가 자식보다 앞)입니다. 런타임 포즈 계산(`Pose`)과 스킨 팔레트가 이것을 읽습니다.
     * @details 파일 형식(JSON, 키는 모두 필수이고 모르는 키는 오류):
     *          `{ "bones": [ { "name", "parent", "translation": [3], "rotation": [x,y,z,w], "scale": [3], "inverse_bind": [16] } ],
     *             "attachments": [ { "name", "mesh", "bone", "translation", "rotation", "scale" } ] }`.
     *          `parent` 는 본 배열의 인덱스(루트 -1)이고 자기보다 앞이어야 합니다. 행렬은 행 우선 16 개입니다.
     */
    class SW_API Skeleton
    {
    public:
        /** @brief 스켈레톤 에셋 확장자입니다. */
        static constexpr string_view kExtension = ".skeleton.json";

        Skeleton() = default;

        /**
         * @brief 본 하나를 끝에 붙이고 그 인덱스를 반환합니다.
         * @param parentIndex 부모 본 인덱스. **이미 추가된 본만 가리킬 수 있습니다**(루트는 -1). 아니면 아무것도 추가하지 않고 -1 입니다.
         */
        int32 addBone( const hashed_string& name, int32 parentIndex, const BoneTransform& referencePose, const float4x4& inverseBind );
        /** @brief 레퍼런스 포즈로 모든 본의 역 바인드를 다시 구합니다(손으로 만든 스켈레톤 · 시험). */
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

        /** @brief JSON 본문을 읽습니다. 형식이 틀리면(모르는 키 · 빠진 키 · 앞에 없는 부모 · 모르는 본 이름) false 이고 내용은 비웁니다. */
        [[nodiscard]] bool parseJson( string_view json, string_view sourceLabel );
        /** @brief 리소스 경로(또는 절대 경로)의 파일을 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief JSON 본문을 만듭니다(들여쓰기 2). */
        string toJson() const;
        /** @brief 파일로 씁니다(부모 폴더를 만듭니다). */
        [[nodiscard]] bool saveToFile( string_view path ) const;

    private:
        /** @brief 파싱된 루트에서 내용을 읽습니다. */
        [[nodiscard]] bool parseRoot( const JsonValue& root, string_view sourceLabel );

        vector<SkeletonBone>       _listBone;
        vector<int32>              _listParentIndex;
        vector<SkeletonAttachment> _listAttachment;
    };
} // namespace sw
