/**
 * @file FacialRig.h
 * @brief 얼굴 리그(`<얼굴>.facial.xml`)입니다. 표정과 비즘을 모프 타깃 가중치로 바꾸고, 눈 깜빡임과 시선(눈 본, 사카드)의 수치를 데이터로 둡니다.
 * @details 코드는 이름으로 고를 뿐이고(표정 "Happy", 비즘 "AA"), 어느 타깃을 얼마나 움직일지는 이 테이블이 정한다(언리얼 MetaHuman 의 포즈 에셋과
 *          Live Link Face 의 커브 컨트롤 매핑에 해당한다). 모르는 키는 오류이고, 메시에 없는 타깃이나 스켈레톤에 없는 본은 바인딩할 때 오류다(`validate`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class Skeleton;

    /** @brief 모프 타깃 하나의 가중치입니다. */
    REFLECT()
    struct SW_API FacialTargetWeight
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Morph target name of the mesh" )
        hashed_string _target;
        PROPERTY( Tooltip = "Target weight at pose weight 1" )
        float32 _weight{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 이름 붙은 얼굴 포즈(표정, 비즘)입니다. 타깃 가중치 묶음이며, 포즈 가중치를 곱해 더합니다. */
    REFLECT()
    struct SW_API FacialPose
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Pose name (expression curve name, or a viseme name of the lip sync table); unique in its list" )
        hashed_string _name;
        PROPERTY( Tooltip = "Morph target weights of this pose (empty: the pose moves nothing)" )
        vector<FacialTargetWeight> _listTarget;
    };
} // namespace sw

namespace sw
{
    /** @brief 눈 깜빡임입니다. 타깃(눈꺼풀)과 간격, 길이(초)를 담습니다. 타깃이 없으면 깜빡이지 않습니다. */
    REFLECT()
    struct SW_API FacialBlinkDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Eyelid morph targets (empty: no blinking)" )
        vector<hashed_string> _listTarget;
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Shortest time between blinks (greater than 0)" )
        float32 _minInterval{ 2.0f };
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Longest time between blinks (not below the shortest)" )
        float32 _maxInterval{ 5.0f };
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Length of one blink (greater than 0)" )
        float32 _duration{ 0.15f };
    };
} // namespace sw

namespace sw
{
    /** @brief 시선입니다. 눈 본들이 목표를 봅니다(앞 축과 최대 각). 사카드(작은 튐)는 간격과 크기로 정합니다. 눈 본이 없으면 보지 않습니다. */
    REFLECT()
    struct SW_API FacialGazeDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Eye bones of the skeleton (empty: no gaze)" )
        vector<hashed_string> _listEyeBone;
        PROPERTY( Tooltip = "Eye forward axis in bone space (normalized on load, must not be zero)" )
        float3 _forwardAxis{ 0.0f, 0.0f, 1.0f };
        PROPERTY( Min = 0.0, Units = rad, Tooltip = "Largest angle the eyes turn from the forward axis" )
        float32 _maxAngle{ 0.5235988f }; // 30 degrees
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Shortest time between saccades (greater than 0)" )
        float32 _saccadeMinInterval{ 0.4f };
        PROPERTY( Min = 0.0, Units = s, Tooltip = "Longest time between saccades (not below the shortest)" )
        float32 _saccadeMaxInterval{ 1.6f };
        PROPERTY( Min = 0.0, Units = rad, Tooltip = "Size of one saccade" )
        float32 _saccadeAmplitude{ 0.0523599f }; // 3 degrees
    };
} // namespace sw

namespace sw
{
    /**
     * @class FacialRig
     * @brief 얼굴 리그 파일(`<얼굴>.facial.xml`) 하나입니다. 비즘 이름은 립싱크 표(`engine/animation/lipsync.xml`)의 것과 맞춥니다.
     * @details 파일은 리플렉션 구조체로 읽는다(`ConfigManager::readConfigXMLElement` — 모르는 속성 · 자식 · 읽지 못한 값 · 범위 밖 숫자는 오류). 칸 표는 PROPERTY 가 기준이다.
     *          `_blink` · `_gaze` 를 적지 않으면 기본값이고, 타깃 · 눈 본이 비면 그 기능을 쓰지 않는다. 읽은 뒤 `validateFile` 이 이름 중복 · 간격 순서를 본다.
     */
    REFLECT()
    class SW_API FacialRig
    {
    public:
        REFLECT_BODY();

        /** @brief 얼굴 리그 확장자입니다. */
        static constexpr string_view kExtension = ".facial.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `FacialRig.cpp` 의 `SW_ASSET_TYPE`). */
        static constexpr AssetTypeDef kAssetType{ "FacialRig", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        [[nodiscard]] bool parseXML( string_view xml, string_view sourceLabel );
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
        /** @brief 메시의 모프 타깃 이름에 없는 타깃, 스켈레톤에 없는 눈 본, 타깃과 같은 이름의 표정(커브가 둘 다 움직임)이 있으면 오류를 남기고 false 를 반환합니다. */
        [[nodiscard]] bool validate( const vector<hashed_string>& listMorphTargetName, const Skeleton& skeleton, string_view sourceLabel ) const;

        const vector<FacialPose>& getExpressions() const { return _listExpression; }
        const vector<FacialPose>& getVisemes() const { return _listViseme; }
        const FacialBlinkDef&     getBlink() const { return _blink; }
        const FacialGazeDef&      getGaze() const { return _gaze; }
        /** @brief 이름의 표정 번호입니다. 없으면 -1 입니다. */
        int32 findExpressionIndex( const hashed_string& name ) const;
        /** @brief 이름의 비즘 번호입니다. 없으면 -1 입니다. */
        int32 findVisemeIndex( const hashed_string& name ) const;

    private:
        /** @brief 파일만으로 알 수 있는 의미를 봅니다(포즈 이름 · 깜빡임과 사카드 간격 · 앞 축). 어긋나면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateFile( string_view sourceLabel ) const;

        PROPERTY( Tooltip = "Expressions: named morph target weight sets, driven by the curve of the same name" )
        vector<FacialPose> _listExpression;
        PROPERTY( Tooltip = "Visemes: morph target weight sets the lip sync drives by viseme name" )
        vector<FacialPose> _listViseme;
        PROPERTY( Tooltip = "Eye blink" )
        FacialBlinkDef _blink;
        PROPERTY( Tooltip = "Eye gaze and saccades" )
        FacialGazeDef _gaze;
    };
} // namespace sw
