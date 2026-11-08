/**
 * @file BodyShape.h
 * @brief 체형입니다. 축(홀쭉함과 뚱뚱함, 약함과 근육, 키) 데이터(`BodyShapeSet`, `*.bodyshape.xml`)와 본 비율 보정(`BoneProportion`)을 담습니다.
 * @details 같은 스켈레톤으로 여러 외형을 내는 두 가지 방법이다. 축 값(슬라이더)마다 양쪽과 음쪽에 모프 대상 하나와 본 비율 몇 줄을 적고, 값이
 *          그쪽 끝으로 갈수록 모프 가중치와 본 비율이 커진다. 본 비율은 **가산 층**이라 애니메이션이 정한 로컬 변환 위에 겹친다
 *          (스케일은 곱하고 오프셋은 더하며, 키와 팔다리 길이를 바꾼다). 모프는 통합 쪽이 GPU 모프 풀에 적용한다. `AppearanceGeometry::applyMorphs` · `skinToPose` 는 CPU 에서 같은 일을 해서,
 *          피팅과 소켓 보정이 체형을 적용한 바인드 형상을 보게 한다.
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
 *            <Axis name="Heel.Height" min="0" max="0.12">
 *              <Positive><Bone name="foot_l" rotation="0 -25 0"/><Bone name="pelvis" offset="0 0.12 0"/></Positive>
 *            </Axis>
 *            <Unsupported name="Tail.Length"/>
 *          </BodyShapeSet>
 *          @endcode
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct AppearanceGeometry;
    struct CharacterBoneArray;

    class CharacterDataReader;
    class Pose;
    class Skeleton;
    class XMLNode;

    /** @brief 본 하나의 비율 보정입니다. 스케일은 로컬 스케일에 곱하고, 오프셋은 로컬 위치에 더합니다. */
    REFLECT()
    struct SW_API BoneProportionEntry
    {
        REFLECT_BODY();

        PROPERTY( Name = "name", Required )
        hashed_string _bone{};
        PROPERTY()
        float3 _scale{ 1.0f };
        PROPERTY( Units = m )
        float3 _offset{};
        /** @brief 로컬 회전에 더하는 회전(도, `makeFromYawPitchRoll` 순서)입니다. 하이힐의 발목 · 발가락 각처럼 채널이 관절을 굽힐 때 쓴다. 겹칠 때는 가중치만큼 각을 더한다. */
        PROPERTY( Units = deg )
        float3 _rotation{};
    };
} // namespace sw

namespace sw
{
    /** @brief 본 비율 보정 모음입니다(본별 스케일과 오프셋이며, 애니메이션 위의 가산 층입니다). */
    class SW_API BoneProportion
    {
    public:
        /** @brief 본 하나의 보정을 정합니다(있으면 바꿈). */
        void setBone( const hashed_string& bone, const float3& scale, const float3& offset );
        /** @brief 보정을 @p weight 만큼 겹칩니다. 스케일은 lerp(1, s, w) 를 곱하고, 오프셋은 o × w, 회전 각은 r × w 를 더합니다. */
        void accumulate( const BoneProportionEntry& entry, float32 weight );
        /** @brief 보정의 회전 각(도)을 쿼터니언으로 바꿉니다. 적용은 지금 로컬 회전 뒤(본 자기 축)에 곱한다. */
        static quaternion makeRotation( const BoneProportionEntry& entry );
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
         * @brief 같은 보정을 애니메이션 포즈(SoA)에 겹칩니다. 스켈레톤 본 이름으로 찾고, 스케일은 곱하고 오프셋은 더합니다.
         * @details 레퍼런스 포즈에 적용하면 "비율이 다른 스켈레톤"이 된다. 리타기터의 대상 레퍼런스 덮어쓰기(`PoseRetargeter::initialize`)로 넘긴다.
         */
        void applyToPose( const Skeleton& skeleton, Pose& inoutPose ) const;
        /** @brief 비율이 가리키는 본이 스켈레톤에 모두 있는지 봅니다. 없는 본은 오류를 남기고 false 입니다(`apply` · `applyToPose` 는 프레임마다 돌아 건너뛰기만 한다). */
        [[nodiscard]] bool checkBones( const Skeleton& skeleton, string_view sourceLabel ) const;

    private:
        BoneProportionEntry& getOrCreateEntry( const hashed_string& bone );

    private:
        vector<BoneProportionEntry> _listEntry;
    };
} // namespace sw

namespace sw
{
    /** @brief 축의 한쪽(양쪽 또는 음쪽) 끝에서 적용하는 모프와 본 비율입니다. */
    REFLECT()
    struct SW_API BodyShapeAxisSide
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string               _morph{};
        vector<BoneProportionEntry> _listBone{};
    };
} // namespace sw

namespace sw
{
    /** @brief 체형 축 하나(슬라이더)입니다. 값이 `_max` 면 양쪽이 1, `_min` 이면 음쪽이 1 입니다(0 은 기본 체형). */
    REFLECT()
    struct SW_API BodyShapeAxis
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string     _name{};
        BodyShapeAxisSide _positive{};
        BodyShapeAxisSide _negative{};
        PROPERTY()
        float32 _min{ -1.0f };
        PROPERTY()
        float32 _max{ 1.0f };
        PROPERTY()
        float32 _default{ 0.0f };
        /** @brief 응답 곡선의 지수입니다. 끝 쪽 가중치 = (|값| / 범위)^response — 1 이면 선형, 1 보다 크면 끝에서 빨리 커진다(조임 · 처짐 채널의 비선형 반응). */
        PROPERTY( Min = 0.01 )
        float32 _response{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 체형 축 데이터 루트(`<BodyShapeSet>`)의 칸입니다. */
    REFLECT()
    struct SW_API BodyShapeSetParams
    {
        REFLECT_BODY();

        /** @brief 발 들림(굽 높이, m)으로 읽는 축 이름입니다. 발 디딤 IK 가 이 값만큼 발바닥을 땅 위로 둔다(`RigInstance::setFootLift`). 비어 있으면 발 들림이 없다. */
        PROPERTY()
        hashed_string _footLiftAxis{};
    };
} // namespace sw

namespace sw
{
    /** @brief 축 하나의 값입니다(외형 프리셋이나 슬라이더가 줍니다). */
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
        friend struct AssetTypeLoader;

    public:
        /** @brief XML 텍스트에서 읽습니다. 모르는 속성이나 원소, 겹친 축은 오류입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 이름의 축입니다. 없으면 nullptr 입니다. */
        const BodyShapeAxis* findAxis( const hashed_string& name ) const;
        /** @brief 축들입니다. */
        const vector<BodyShapeAxis>& getAxes() const { return _listAxis; }
        /**
         * @brief 이 스켈레톤이 **일부러** 받지 않는 채널(`<Unsupported name/>`)인지 반환합니다. 이런 채널 값은 `evaluate` 가 오류 없이 버린다.
         * @details 아이템은 뼈가 아니라 채널 이름(`Bust.Compression`)에 값을 적는다. 채널을 본 · 모프로 옮기는 매핑은 스켈레톤마다 이 파일이 가진다 —
         *          같은 아이템이 사람 · 작은 몸 · 짐승 스켈레톤에 그대로 맞는다. 매핑도 선언도 없는 채널은 오류다(조용히 건너뛰지 않는다).
         */
        bool                         isUnsupported( const hashed_string& axis ) const;
        const vector<hashed_string>& getUnsupported() const { return _listUnsupported; }
        /** @brief 발 들림 축 이름입니다(`footLiftAxis`). 비어 있으면 없다. */
        const hashed_string& getFootLiftAxis() const { return _params._footLiftAxis; }
        /** @brief 축 값들에서 발 들림(m)을 구합니다. 발 들림 축이 없으면 0, 값이 없으면 그 축의 기본값이고 범위로 묶습니다. */
        float32 computeFootLift( vector_reference<const BodyShapeValue> listValue ) const;
        /**
         * @brief 축이 적은 모프 이름과 본 이름이 몸 형상(@p pBody)과 본 배열(@p pBones)에 있는지 검사합니다(널이면 그쪽은 건너뜁니다).
         * @return 없는 이름이 있으면 오류 문장을 추가하고 false 를 반환합니다.
         */
        bool validate( const AppearanceGeometry* pBody, const CharacterBoneArray* pBones, string* pOutError ) const;
        /**
         * @brief 축 값들을 모프 가중치와 본 비율로 풉니다. 적지 않은 축은 기본값, 범위 밖은 범위로 묶습니다.
         * @return 모르는 축 이름이 있으면 오류 글을 더하고 false 입니다(아는 축은 그래도 풉니다).
         */
        bool evaluate( vector_reference<const BodyShapeValue> listValue, vector<BodyMorphWeight>& outListMorphWeight, BoneProportion& outProportion,
                       string* pOutError ) const;

    private:
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<BodyShapeSet>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        void               readRoot( const XMLNode& root, CharacterDataReader& reader );
        void               readSide( const XMLNode& node, BodyShapeAxisSide& outSide, CharacterDataReader& reader );

    private:
        vector<BodyShapeAxis> _listAxis;
        vector<hashed_string> _listUnsupported; ///< 이 스켈레톤이 받지 않는다고 선언한 채널
        BodyShapeSetParams    _params;
    } // namespace sw
