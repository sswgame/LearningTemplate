/**
 * @file BodyProfile.h
 * @brief 캐릭터 몸 프로필(`bodyprofiles.xml`)입니다. 캐릭터마다 고르는 몸 동작 프리셋 — 기본 몸 채널 층, 지글 묶음 물성, 동작 배율, 주름 매핑, 눈동자 반응, 체액(눈물 · 침) 방출 — 을 하나로 묶습니다.
 * @details 외형 프리셋(`<CharacterAppearance bodyProfile="…">`)이 id 로 고른다(부모 프리셋의 것을 물려받는다). 항목은 `_base="<id>"` 로 다른 프로필을 상속하고 다른 칸만 적는다 —
 *          카탈로그 상속 표(`datacataloginheritance.xml` 의 `BodyProfileCatalog`)가 목록 원소를 열쇠(`name` · `group` · `region` · `tag`)로 합친다(코드 없이).
 *          언리얼에서는 캐릭터 블루프린트의 AnimDynamics · 포즈 에셋 · 머티리얼 커브 설정이 흩어진 자리를, 데이터 한 벌로 모았다.
 *
 *          @code
 *          <BodyProfileCatalog>
 *            <BodyProfile id="HumanFemaleBase" wrinkleIntensity="1">
 *              <BodyChannel name="Bust.Lift" value="0.1" priority="-10" blend="Additive"/>
 *              <Jiggle group="Bust" stiffness="0.25" damping="0.2" gravityScale="0.3" maxSwingDegrees="18" impulseScale="1"/>
 *              <ActionScale tag="State.Locomotion.Run" scale="1.4"/>
 *              <Wrinkle region="Forehead" mask="0"><Source expression="BrowUp" weight="1"/></Wrinkle>
 *              <Pupil parameter="_pupilDilation" minSize="0.2" maxSize="0.8" constrictSeconds="0.3" dilateSeconds="4" darkLuminance="0.02" brightLuminance="20" emotionCurve="Pupil.Emotion"/>
 *              <Fluid name="TearL" channel="Tear" residueChannel="TearResidue" trigger="Cry" path="0.42 0.40; 0.41 0.47; 0.40 0.55" ratePerSecond="0.5" speed="0.08" radius="0.006"/>
 *            </BodyProfile>
 *            <BodyProfile id="Heroine" _base="HumanFemaleBase"><Jiggle group="Bust" stiffness="0.35"/></BodyProfile>
 *          </BodyProfileCatalog>
 *          @endcode
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Facial/WrinkleMap.h"
#include "Engine/Animation/Rig/RigBodyInput.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/BodyChannel.h"

namespace sw
{
    class AppearanceLoadReport;
    class TagContainer;
    class XMLNode;

    /** @brief 상태 태그 하나가 지글 동작 배율을 정합니다(`<ActionScale tag scale/>`). 맞는 태그가 여럿이면 가장 큰 배율, 없으면 1 입니다. */
    REFLECT()
    struct SW_GF_API BodyActionScaleDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _tag{};
        PROPERTY( Min = 0.0, Max = 2.0 )
        float32 _scale{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 눈동자 반응입니다(`<Pupil …/>`). 눈에 닿는 실제 빛(장면 노출의 휘도 — 날씨 · 시간대 · 실내 · 국소 조명이 이미 반영된 값)으로 동공 크기를 정하고,
     *        좁힐 때는 빠르게(`constrictSeconds`), 넓힐 때는 느리게(`dilateSeconds`) 따라가며, 감정 커브를 더합니다.
     * @details 크기 = lerp( maxSize, minSize, t ), t = log 휘도의 [darkLuminance, brightLuminance] 안 비율. 사람 동공의 빛 반사(밝으면 좁아짐)와 순응 시간(좁힘 ~0.3 s, 넓힘 수 초)이다.
     *          머티리얼 매개변수 `parameter` 에 0..1 로 쓴다(눈 셰이딩 기능이 같은 이름을 읽는다).
     */
    REFLECT()
    struct SW_GF_API BodyPupilDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _parameter{};
        PROPERTY()
        hashed_string _emotionCurve{}; ///< 애니메이터 커브(놀람 · 흥분 +, 졸림 −). 비어 있으면 감정 입력이 없다
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _minSize{ 0.2f };
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _maxSize{ 0.8f };
        PROPERTY( Min = 0.001, Units = s )
        float32 _constrictSeconds{ 0.3f };
        PROPERTY( Min = 0.001, Units = s )
        float32 _dilateSeconds{ 4.0f };
        PROPERTY( Min = 0.000001 )
        float32 _darkLuminance{ 0.02f };
        PROPERTY( Min = 0.000001 )
        float32 _brightLuminance{ 20.0f };
        PROPERTY()
        float32 _emotionScale{ 0.3f };

        /** @brief 휘도 @p luminance 에서의 목표 크기(0..1)입니다. */
        float32 computeTargetSize( float32 luminance ) const;
        /** @brief 지금 크기 @p current 를 목표 @p target 쪽으로 @p deltaSeconds 만큼 옮깁니다(좁힘 · 넓힘 시간 상수가 다르다 — 지수 접근). */
        float32 advance( float32 current, float32 target, float32 deltaSeconds ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 체액 방출 하나입니다(`<Fluid …/>` — 눈물 · 침). 출처 점(경로의 첫 점)에서 UV 경로를 따라 흘러내리며 표면 채널(`channel`)에 자국을 찍고, 마르면 잔여 채널(`residueChannel`)이 남습니다.
     * @details 흐름은 얼굴마다 그려 둔 UV 경로(`path`)다 — 월드 중력을 UV 로 사영하려면 텍셀마다 탄젠트 틀이 필요하고 얼굴 UV 이음새에서 흐름이 끊기는데, 얼굴 하나에 경로 몇 개면
     *          같은 자리에 같은 자국이 나서 결정적이다(검은사막 · 언리얼 게임들이 그린 마스크로 하는 길). 마르기는 표면 채널의 초당 감쇠(`surfacechannels.xml`)가 한다 — 같은 기반을 쓴다.
     *          방아쇠(`trigger`)는 표정 이름 또는 애니메이터 커브다(울음 · 웃음 눈물 · 침 · 먹기). 비는 표면 노출(`SurfaceExposureComponent`)이 얼굴 전체를 적신다.
     */
    struct SW_GF_API BodyFluidDef
    {
        vector<float2> _listPathPoint{}; ///< UV 경로(출처 → 끝)
        hashed_string  _name{};
        hashed_string  _channel{};
        hashed_string  _residueChannel{};
        hashed_string  _trigger{};             ///< 표정 또는 커브 이름
        hashed_string  _part{};                ///< 자국을 찍을 몸 부품 이름(비어 있으면 몸 첫 부품)
        float32        _ratePerSecond{ 0.5f }; ///< 방아쇠 1 에서 초당 새 줄기 수
        float32        _speed{ 0.08f };        ///< 경로를 따라 흐르는 빠르기(UV / s)
        float32        _radius{ 0.006f };      ///< 자국 반지름(UV)
        float32        _wetValue{ 1.0f };      ///< 자국 값
        float32        _residueValue{ 0.25f }; ///< 잔여 자국 값(마른 소금 자국 · 번진 화장)

        /** @brief 경로의 전체 길이(UV)입니다. */
        float32 computePathLength() const;
        /** @brief 경로를 따라 @p distance 만큼 간 UV 점입니다(끝을 넘으면 끝). */
        float2 samplePath( float32 distance ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 몸 프로필 하나입니다. 상속(`_base`)은 읽기 전에 합쳐져 있다. */
    struct SW_GF_API CharacterBodyProfileDef
    {
        vector<BodyChannelLayerDef>  _listChannel{};
        vector<RigJiggleGroupParams> _listJiggle{};
        vector<BodyActionScaleDef>   _listActionScale{};
        vector<WrinkleRegionDef>     _listWrinkle{};
        vector<BodyFluidDef>         _listFluid{};
        BodyPupilDef                 _pupil{};
        hashed_string                _id{};
        float32                      _wrinkleIntensity{ 1.0f };
        uint8                        _bHasPupil{ SW_FALSE };

        /** @brief 상태 태그 @p tags 의 동작 배율입니다. 맞는 것 중 가장 큰 배율, 없으면 1 입니다. */
        float32 computeActionScale( const TagContainer& tags ) const;
    };
} // namespace sw

namespace sw
{
    /** @class CharacterBodyProfileCatalog @brief `<BodyProfileCatalog>` 를 읽습니다. 형식은 파일 머리말에 있습니다. */
    class SW_GF_API CharacterBodyProfileCatalog
    {
    public:
        [[nodiscard]] bool loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listProfile.clear(); }

        const CharacterBodyProfileDef*         findProfile( const hashed_string& id ) const;
        const vector<CharacterBodyProfileDef>& getProfiles() const { return _listProfile; }

    private:
        vector<CharacterBodyProfileDef> _listProfile{};
    };
} // namespace sw
