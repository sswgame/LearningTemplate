/**
 * @file ReferencePoseOverride.h
 * @brief 스켈레톤 레퍼런스 포즈 덮어쓰기(`*.refpose.xml`) — 본별 위치 · 회전 · 스케일과 좌우 대칭 짝입니다.
 * @details 스켈레톤은 glTF 에서 다시 임포트되는 산출물이라 고치면 재임포트가 지웁니다. 그래서 레퍼런스 포즈 편집은 소켓처럼 **따로 둔 덮어쓰기
 *          데이터**입니다(계층 추가 · 삭제는 DCC 몫). 적은 칸만 바꿉니다(위치만 적으면 회전 · 스케일은 임포트 값).
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
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct CharacterBoneArray;

    class CharacterDataReader;
    class XmlNode;

    /** @brief 본 하나의 덮어쓰기입니다. `_fieldMask` 는 적은 칸(1 위치 · 2 회전 · 4 스케일)입니다. */
    struct SW_API BoneOverride
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
    struct SW_API BoneMirrorPair
    {
        hashed_string _left{};
        hashed_string _right{};
    };
} // namespace sw

namespace sw
{
    /** @brief 레퍼런스 포즈 덮어쓰기 에셋 하나입니다. */
    class SW_API ReferencePoseOverride
    {
    public:
        /** @brief XML 텍스트에서 읽습니다. @p pBones 가 있으면 본 이름을 대조합니다(없는 본은 오류). */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName, const CharacterBoneArray* pBones = nullptr );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path, const CharacterBoneArray* pBones = nullptr );
        /** @brief XML 텍스트로 씁니다. */
        string saveToXmlText() const;

        /** @brief 본 덮어쓰기를 더합니다(같은 본이면 적은 칸을 합칩니다). */
        void setOverride( const BoneOverride& boneOverride );
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
        /** @brief 덮어쓰기 · 짝의 본 이름이 @p bones 에 모두 있으면 true, 아니면 오류 글을 더하고 false 입니다. */
        bool validateBones( const CharacterBoneArray& bones, string* pOutError ) const;
        /** @brief 본 배열의 로컬 변환에 덮어쓰기를 적용하고 모델 변환을 다시 계산합니다. */
        void apply( CharacterBoneArray& inoutBones ) const;

        /** @brief 덮어쓰기들입니다. */
        const vector<BoneOverride>& getOverrides() const { return _listOverride; }
        /** @brief 대칭 축(0 = X, 1 = Y, 2 = Z)입니다. */
        uint8 getMirrorAxis() const { return _mirrorAxis; }
        /** @brief 대칭 축을 정합니다. */
        void setMirrorAxis( uint8 axis ) { _mirrorAxis = axis < 3 ? axis : 0; }

    private:
        void readRoot( const XmlNode& root, CharacterDataReader& reader );

    private:
        vector<BoneOverride>   _listOverride;
        vector<BoneMirrorPair> _listMirrorPair;
        uint8                  _mirrorAxis{ 0 };
    };
} // namespace sw
