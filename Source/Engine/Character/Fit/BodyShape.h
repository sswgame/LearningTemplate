/**
 * @file BodyShape.h
 * @brief 체형 — 축(홀쭉↔뚱뚱 × 약함↔근육 · 키) 데이터(`BodyShapeSet`, `*.bodyshape.xml`)와 본 비율 보정(`BoneProportion`)입니다.
 * @details 같은 스켈레톤 위에 여러 외형을 내는 두 갈래입니다. 축 값(슬라이더)마다 양 · 음 쪽에 모프 대상 하나와 본 비율 몇 줄을 적고, 값이
 *          그 쪽 끝으로 갈수록 모프 가중치와 본 비율이 커집니다. 본 비율은 **가산 층**이라 애니메이션이 정한 로컬 변환 위에 겹칩니다
 *          (스케일은 곱, 오프셋은 더함 — 키 · 팔다리 길이). 모프는 통합 쪽이 GPU 모프 풀에 겁니다. 여기의 `BodyShapeUtil` 은 CPU 에서 같은 일을
 *          해 피팅 · 소켓 보정이 체형을 건 바인드 형상을 보게 합니다.
 *
 *          XML 예:
 *          @code
 *          <BodyShapeSet>
 *            <Axis name="Weight" min="-1" max="1" default="0">
 *              <Positive morph="Heavy"><Bone name="spine_01" scale="1.15 1 1.15"/></Positive>
 *              <Negative morph="Thin"/>
 *            </Axis>
 *            <Axis name="Height" min="-1" max="1">
 *              <Positive><Bone name="pelvis" offset="0 0.08 0"/></Positive>
 *            </Axis>
 *          </BodyShapeSet>
 *          @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct AppearanceGeometry;
    struct CharacterBoneArray;

    class CharacterDataReader;
    class Pose;
    class Skeleton;
    class XmlNode;

    /** @brief 본 하나의 비율 보정입니다. 스케일은 로컬 스케일에 곱하고, 오프셋은 로컬 위치에 더합니다. */
    struct SW_API BoneProportionEntry
    {
        hashed_string _bone{};
        float3        _scale{ 1.0f };
        float3        _offset{};
    };
} // namespace sw

namespace sw
{
    /** @brief 본 비율 보정 묶음입니다(본별 스케일 · 오프셋, 애니메이션 위의 가산 층). */
    class SW_API BoneProportion
    {
    public:
        /** @brief 본 하나의 보정을 정합니다(있으면 바꿈). */
        void setBone( const hashed_string& bone, const float3& scale, const float3& offset );
        /** @brief 보정을 @p weight 만큼 겹칩니다 — 스케일은 lerp(1, s, w) 를 곱하고, 오프셋은 o × w 를 더합니다. */
        void accumulate( const BoneProportionEntry& entry, float32 weight );
        /** @brief 본의 보정입니다. 없으면 nullptr 입니다. */
        const BoneProportionEntry* findBone( const hashed_string& bone ) const;
        /** @brief 보정들입니다. */
        const vector<BoneProportionEntry>& getEntries() const { return _listEntry; }
        /** @brief 모두 비웁니다. */
        void clear() { _listEntry.clear(); }
        /**
         * @brief 본 배열의 **지금 로컬 변환**(애니메이션이 정한 것) 위에 보정을 겹치고 모델 변환을 다시 계산합니다.
         * @details 매 프레임 애니메이션 평가 뒤, 스키닝 앞에서 부릅니다(가산 층이라 애니메이션과 겹친다). 배열에 없는 본은 건너뜁니다.
         */
        void apply( CharacterBoneArray& inoutBones ) const;
        /**
         * @brief 같은 보정을 애니메이션 포즈(SoA)에 겹칩니다 — 스켈레톤 본 이름으로 찾고, 스케일은 곱하고 오프셋은 더합니다.
         * @details 레퍼런스 포즈에 걸면 "비율이 다른 스켈레톤" 이 됩니다 — 리타기터의 대상 레퍼런스 덮어쓰기(`PoseRetargeter::initialize`)로 넘깁니다.
         */
        void applyToPose( const Skeleton& skeleton, Pose& inoutPose ) const;

    private:
        BoneProportionEntry& findOrAddEntry( const hashed_string& bone );

    private:
        vector<BoneProportionEntry> _listEntry;
    };
} // namespace sw

namespace sw
{
    /** @brief 축의 한쪽(양 · 음) 끝에서 거는 모프와 본 비율입니다. */
    struct SW_API BodyShapeAxisSide
    {
        hashed_string               _morph{};
        vector<BoneProportionEntry> _listBone{};
    };
} // namespace sw

namespace sw
{
    /** @brief 체형 축 하나(슬라이더)입니다. 값이 `_max` 면 양쪽이 1, `_min` 이면 음쪽이 1 입니다(0 은 기본 체형). */
    struct SW_API BodyShapeAxis
    {
        hashed_string     _name{};
        BodyShapeAxisSide _positive{};
        BodyShapeAxisSide _negative{};
        float32           _min{ -1.0f };
        float32           _max{ 1.0f };
        float32           _default{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 축 하나의 값입니다(외형 프리셋 · 슬라이더가 줌). */
    struct BodyShapeValue
    {
        hashed_string _axis{};
        float32       _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 모프 대상 하나의 가중치입니다. */
    struct BodyMorphWeight
    {
        hashed_string _morph{};
        float32       _weight{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 체형 축 데이터 에셋입니다. */
    class SW_API BodyShapeSet
    {
    public:
        /** @brief XML 텍스트에서 읽습니다. 모르는 속성 · 원소 · 겹친 축은 오류입니다. */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 축을 더합니다(같은 이름이면 바꿈). */
        void addAxis( const BodyShapeAxis& axis );
        /** @brief 이름의 축입니다. 없으면 nullptr 입니다. */
        const BodyShapeAxis* findAxis( const hashed_string& name ) const;
        /** @brief 축들입니다. */
        const vector<BodyShapeAxis>& getAxes() const { return _listAxis; }
        /**
         * @brief 축이 적은 모프 이름 · 본 이름이 몸 형상(@p pBody) · 본 배열(@p pBones)에 있는지 봅니다(널이면 그쪽은 건너뜀).
         * @return 없는 이름이 있으면 오류 글을 더하고 false 입니다.
         */
        bool validate( const AppearanceGeometry* pBody, const CharacterBoneArray* pBones, string* pOutError ) const;
        /**
         * @brief 축 값들을 모프 가중치와 본 비율로 풉니다. 적지 않은 축은 기본값, 범위 밖은 범위로 묶습니다.
         * @return 모르는 축 이름이 있으면 오류 글을 더하고 false 입니다(아는 축은 그래도 풉니다).
         */
        bool evaluate( vector_reference<const BodyShapeValue> listValue, vector<BodyMorphWeight>& outListMorphWeight, BoneProportion& outProportion,
                       string* pOutError ) const;

    private:
        void readRoot( const XmlNode& root, CharacterDataReader& reader );
        void readSide( const XmlNode& node, BodyShapeAxisSide& outSide, CharacterDataReader& reader );

    private:
        vector<BodyShapeAxis> _listAxis;
    };
} // namespace sw

namespace sw
{
    /** @brief 체형을 CPU 형상에 거는 도우미입니다(전부 static). */
    struct SW_API BodyShapeUtil
    {
        /** @brief 모프 가중치를 형상 위치(와 법선)에 겁니다. 형상에 없는 모프는 건너뜁니다. */
        static void applyMorphs( AppearanceGeometry& inoutGeometry, vector_reference<const BodyMorphWeight> listMorphWeight );
        /**
         * @brief 바인드 형상을 다른 포즈(본 비율을 건 레퍼런스 포즈 · 애니메이션 포즈)로 선형 블렌드 스키닝합니다.
         * @details 스킨 행렬 = 바인드 모델의 역 × 포즈 모델(행벡터). 스킨이 없는 형상은 그대로 복사합니다. 법선도 같은 행렬로 돌립니다.
         */
        static void skinToPose( const AppearanceGeometry& bindGeometry, const CharacterBoneArray& bindBones, const CharacterBoneArray& poseBones,
                                AppearanceGeometry& outGeometry );
    };
} // namespace sw
