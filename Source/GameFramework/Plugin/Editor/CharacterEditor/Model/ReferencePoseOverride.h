/**
 * @file ReferencePoseOverride.h
 * @brief 스켈레톤 레퍼런스 포즈 덮어쓰기(`*.refpose.xml`)입니다. 본별 위치, 회전, 스케일과 좌우 대칭 쌍을 담습니다.
 * @details 스켈레톤은 glTF 에서 다시 임포트되는 산출물이라 고치면 재임포트가 지웁니다. 그래서 레퍼런스 포즈 편집은 소켓처럼 **따로 둔 덮어쓰기
 *          데이터**입니다. 계층 추가와 삭제는 DCC 에서 합니다. 적은 필드만 바꿉니다(위치만 적으면 회전과 스케일은 임포트 값을 씁니다).
 *
 *          XML 예:
 *          @code
 *          <ReferencePose mirrorAxis="X">
 *            <Bone name="upperarm_l" rotation="0 0 -40"/>
 *            <Mirror left="upperarm_l" right="upperarm_r"/>
 *          </ReferencePose>
 *          @endcode
 *          덮어쓴 포즈는 새 레퍼런스 포즈이므로, 스키닝 쪽은 덮어쓴 모델 변환으로 레퍼런스 역행렬을 다시 만듭니다(통합 쪽 일).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/CharacterIssue.h"
#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    struct CharacterBoneArray;

    class CharacterDataReader;
    class XMLNode;

    /** @brief 본 하나의 덮어쓰기입니다. `_fieldMask` 는 적은 필드를 나타냅니다(1 위치, 2 회전, 4 스케일). */
    struct BoneOverride
    {
        static constexpr uint8 kTranslationBit = 1u << 0;
        static constexpr uint8 kRotationBit    = 1u << 1;
        static constexpr uint8 kScaleBit       = 1u << 2;

        hashed_string _bone{};
        quaternion    _rotation{};
        float3        _translation{};
        float3        _scale{ 1.0f };
        uint8         _fieldMask{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 좌우 대칭 짝입니다. */
    struct BoneMirrorPair
    {
        hashed_string _left{};
        hashed_string _right{};
    };
} // namespace sw

namespace sw
{
    /** @brief 레퍼런스 포즈 덮어쓰기 에셋 하나입니다. */
    class ReferencePoseOverride
    {
    public:
        /** @brief XML 텍스트에서 읽습니다. @p pBones 가 있으면 본 이름을 대조합니다(없는 본은 오류). */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, const CharacterBoneArray* pBones = nullptr );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID, const CharacterBoneArray* pBones = nullptr );
        /** @brief XML 텍스트로 씁니다. */
        string saveToXMLText() const;

        /** @brief 본 덮어쓰기를 추가합니다(같은 본이면 적은 필드를 합칩니다). */
        void setOverride( const BoneOverride& boneOverride );
        /** @brief 본의 덮어쓰기를 지웁니다(임포트 값으로 돌아갑니다). 없으면 false 입니다. 대칭 쌍은 그대로 둡니다. */
        [[nodiscard]] bool removeOverride( const hashed_string& bone );
        /** @brief 본의 덮어쓰기입니다. 없으면 nullptr 입니다. */
        const BoneOverride* findOverride( const hashed_string& bone ) const;
        /** @brief 대칭 짝을 더합니다. */
        void addMirrorPair( const hashed_string& left, const hashed_string& right );
        /** @brief 짝의 다른 쪽 본입니다. 짝이 없으면 빈 이름입니다. */
        hashed_string findMirrorBone( const hashed_string& bone ) const;
        /**
         * @brief @p sourceBone 의 덮어쓰기를 대칭면(`_mirrorAxis` 에 수직인 면)으로 뒤집어 짝 본에 적습니다(좌우 대칭 편집).
         * @return 짝이나 원본 덮어쓰기가 없으면 false 입니다.
         */
        bool mirrorOverride( const hashed_string& sourceBone );
        /** @brief 덮어쓰기와 쌍의 본 이름이 @p bones 에 모두 있으면 true 를 반환하고, 아니면 문제(종류 id 와 글)를 @p pOutIssues 에 추가하고 false 를 반환합니다. */
        bool validateBones( const CharacterBoneArray& bones, CharacterIssueList* pOutIssues ) const;
        /** @brief 본 배열의 로컬 변환에 덮어쓰기를 적용하고 모델 변환을 다시 계산합니다. */
        void apply( CharacterBoneArray& inoutBones ) const;

        /** @brief 덮어쓰기들입니다. */
        const vector<BoneOverride>& getOverrides() const { return _listOverride; }
        /** @brief 대칭 축(0 = X, 1 = Y, 2 = Z)입니다. */
        uint8 getMirrorAxis() const { return _mirrorAxis; }
        /** @brief 대칭 축을 정합니다. */
        void setMirrorAxis( uint8 axis ) { _mirrorAxis = axis < 3 ? axis : 0; }

    private:
        void readRoot( const XMLNode& root, CharacterDataReader& reader );

    private:
        vector<BoneOverride>   _listOverride;
        vector<BoneMirrorPair> _listMirrorPair;
        uint8                  _mirrorAxis{ 0 };
    };
} // namespace sw
