/**
 * @file KartTrack.h
 * @brief 카트 트랙을 정의합니다. 닫힌 중심선(Catmull-Rom 보간과 누적 거리 테이블), 폭, 체크포인트 문(지름길 방지), 아이템 상자, 부스트 패드, 오프로드 구간과 XML 카탈로그를 다룹니다.
 * @details 좌표는 기반 `ArcadeVehicleMotor` 와 같다. +Y 가 위이고, 요 0 이 +Z 이며, 오른쪽은 (tz, 0, -tx) 이다. 트랙 위의 위치는 "중심선 거리(0..길이) + 옆 비킴(오른쪽 +)" 으로 나타낸다.
 *          트랙은 `IVehicleGround` 이므로 차가 그대로 밟는다. 높이는 중심선 높이이고, 폭 밖과 오프로드 구간은 속도 배율이 1 보다 작다.
 *          결승선은 거리 0 이고 체크포인트는 그 사이의 거리이다. 문은 중심선에 직각인 선분이므로, 안쪽 풀밭을 가로지르는 지름길은 문을 지나지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Movement/ArcadeVehicleMotor.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief `<Point x="0" y="0" z="100"/>` — 길 중심선의 조절점 하나입니다(y 는 적지 않으면 0). */
    REFLECT()
    struct KartTrackPointDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        float32 _x{ 0.0f };
        PROPERTY()
        float32 _y{ 0.0f };
        PROPERTY( Required )
        float32 _z{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief `<Checkpoint at="0.5"/>` — 체크포인트 하나입니다(비율 0..1). */
    REFLECT()
    struct KartCheckpointDef
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _at{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템 상자의 위치입니다. `_at` 은 한 바퀴에 대한 비율(0..1)입니다. */
    REFLECT()
    struct KartItemBoxDef
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _at{ 0.0f };
        PROPERTY()
        float32 _offset{ 0.0f }; ///< 옆 비킴이다(m, 오른쪽이 +)
    };
} // namespace sw

namespace sw
{
    /** @brief 부스트 패드입니다. 중심선 거리 `_at` 을 가운데로 하고 길이 `_length`, 폭 `_width` 인 사각형입니다. */
    REFLECT()
    struct KartBoostPadDef
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _at{ 0.0f };
        PROPERTY()
        float32 _offset{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _length{ 4.0f };
        PROPERTY( Min = 0.0 )
        float32 _width{ 4.0f };
        PROPERTY( Min = 0.0 )
        float32 _duration{ 1.0f }; ///< 부스트 시간(초)
    };
} // namespace sw

namespace sw
{
    /** @brief 오프로드 구간입니다. 거리 `_from`..`_to`(비율이며 결승선을 넘어 감겨도 됩니다)와 옆 비킴 범위 안의 속도 배율을 정합니다(모래, 잔디, 물웅덩이). */
    REFLECT()
    struct KartOffroadZoneDef
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _from{ 0.0f };
        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _to{ 0.0f };
        PROPERTY()
        float32 _minOffset{ -1000.0f };
        PROPERTY( Min = "_minOffset" )
        float32 _maxOffset{ 1000.0f };
        PROPERTY( Name = "scale", Min = 0.0 )
        float32 _speedScale{ 0.5f };
    };
} // namespace sw

namespace sw
{
    /** @brief 트랙 정의 하나입니다(XML 의 `<Track>` 하나에 해당합니다). */
    REFLECT()
    struct KartTrackDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string                     _name{};
        vector<float3>             _listControlPoint{}; ///< 닫힌 곡선의 조절점이다(3개 이상, 진행 순서)
        vector<float32>            _listCheckpoint{};   ///< 체크포인트 위치이다(비율 0..1이며 오름차순으로 정리된다)
        vector<KartItemBoxDef>     _listItemBox{};
        vector<KartBoostPadDef>    _listBoostPad{};
        vector<KartOffroadZoneDef> _listOffroadZone{};
        PROPERTY( Min = 0.1 )
        float32 _width{ 12.0f }; ///< 길 폭(m)이다. 밖은 오프로드이다
        PROPERTY( Min = 0.0 )
        float32 _offroadScale{ 0.5f }; ///< 길 밖의 속도 배율
        PROPERTY( Min = 0.0 )
        float32 _gateMargin{ 6.0f }; ///< 문이 길 가장자리 밖으로 더 뻗는 길이(m)이다. 길을 살짝 벗어나도 지나간 것으로 본다
        PROPERTY( Name = "laps", Min = 1 )
        int32 _lapCount{ 3 };
        PROPERTY( Name = "samples", Min = 1 )
        int32 _samplesPerSegment{ 16 }; ///< 조절점 사이의 샘플 수
    };
} // namespace sw

namespace sw
{
    /** @brief 중심선 위로 내린 투영 결과입니다. */
    struct KartTrackProjection
    {
        float3  _position{}; ///< 중심선 위에서 가장 가까운 점
        float3  _tangent{ 0.0f, 0.0f, 1.0f };
        float32 _distance{ 0.0f }; ///< 결승선부터의 중심선 거리(0..길이)
        float32 _offset{ 0.0f };   ///< 옆 비킴(오른쪽이 +)
        int32   _segment{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 중심선 위 한 지점의 좌표계입니다. */
    struct KartTrackFrame
    {
        float3 _position{};
        float3 _tangent{ 0.0f, 0.0f, 1.0f }; ///< 수평 단위 접선
        float3 _right{ 1.0f, 0.0f, 0.0f };   ///< 수평 단위 벡터이며 오른쪽을 가리킨다
    };
} // namespace sw

namespace sw
{
    /** @brief 문 하나입니다. 중심선에 직각인 선분이며, 0번은 결승선이고 1번부터는 체크포인트입니다. */
    struct KartTrackGate
    {
        float3  _left{};
        float3  _right{};
        float3  _forward{ 0.0f, 0.0f, 1.0f };
        float32 _distance{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 부스트 패드와 오프로드 구간을 거리(m)로 바꾼 것입니다. */
    struct KartBoostPad
    {
        KartBoostPadDef _def{};
        float32         _distance{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class KartTrack
     * @brief 정의를 샘플 점 목록으로 풀어서 보관하고, 거리, 투영, 문 통과를 계산합니다. 차의 땅(`IVehicleGround`)이기도 합니다.
     */
    class SW_KIT_API KartTrack final : public IVehicleGround
    {
    public:
        /** @brief 샘플 점 하나입니다. */
        struct Sample
        {
            float3  _position{};
            float32 _distance{ 0.0f }; ///< 시작점부터의 누적 거리(m)
        };

        KartTrack();

        /** @brief 정의로 곡선을 만듭니다. 조절점이 3개보다 적거나 폭이 0 이하이면 false 를 반환하고 비어 있는 상태가 됩니다. */
        [[nodiscard]] bool initialize( const KartTrackDef& def );

        /** @brief 가장 가까운 중심선 위치를 반환합니다(모든 구간을 검사합니다). */
        KartTrackProjection project( const float3& position ) const;
        /** @brief @p hintDistance 앞뒤 @p searchRange(m) 안의 구간만 검사합니다(겹쳐 지나는 트랙이나 질의가 많을 때 씁니다). */
        KartTrackProjection project( const float3& position, float32 hintDistance, float32 searchRange ) const;
        /** @brief 거리 @p distance 의 좌표계를 반환합니다(감긴 거리도 허용합니다). */
        KartTrackFrame sample( float32 distance ) const;
        /** @brief 거리를 [0, 길이) 로 감습니다. */
        float32 wrapDistance( float32 distance ) const;
        /** @brief @p fromDistance 에서 @p toDistance 로 앞으로 간 거리를 (-길이/2, 길이/2] 범위로 반환합니다. */
        float32 computeSignedGap( float32 fromDistance, float32 toDistance ) const;
        /** @brief @p distance 에서 @p ahead(m) 앞까지 진행 방향이 도는 각(라디안, 오른쪽이 +)을 반환합니다. AI 가 곡선을 읽을 때 씁니다. */
        float32 computeHeadingChange( float32 distance, float32 ahead ) const;
        /** @brief 이동 @p from 에서 @p to 가 문 @p gateIndex 를 지났으면 방향(1 은 앞, -1 은 뒤)을 반환하고, 아니면 0 을 반환합니다. 수평으로만 봅니다. */
        int32 computeGateCrossing( int32 gateIndex, const float3& from, const float3& to ) const;
        /** @brief 그 위치의 부스트 패드 번호를 반환합니다. 없으면 -1 입니다. */
        int32 findBoostPadAt( const float3& position ) const;
        /**
         * @brief 부스트 패드 규칙입니다. 땅에 붙은 차가 패드에 **새로 올라서면** 부스트를 겁니다(같은 패드 위에 있는 동안은 한 번만 겁니다).
         * @details 경기와 고스트 재생이 이 함수를 같은 순서(차 걸음 다음)로 호출하므로 경로가 같아진다. 부스트를 걸었으면 true 를 반환한다.
         */
        [[nodiscard]] bool applyBoostPad( ArcadeVehicleMotor& motor, int32& inoutLastPad ) const;
        /** @brief 길 폭 안에 있는지 반환합니다. */
        bool isOnRoad( const float3& position ) const;

        float32 sampleHeight( float32 x, float32 z ) const override;
        float32 sampleSpeedScale( float32 x, float32 z ) const override;

        bool    isValid() const { return _listSample.size() >= 3; }
        float32 getLength() const { return _length; }
        float32 getWidth() const { return _def._width; }
        int32   getLapCount() const { return _def._lapCount; }
        /** @brief 체크포인트 수를 반환합니다(결승선은 뺍니다). */
        int32                        getCheckpointCount() const { return static_cast<int32>( _listGate.size() ) - 1; }
        const KartTrackGate&         getGate( int32 gateIndex ) const { return _listGate[static_cast<size_t>( gateIndex )]; }
        const vector<KartTrackGate>& getGates() const { return _listGate; }
        const vector<float3>&        getItemBoxPositions() const { return _listItemBoxPosition; }
        const vector<KartBoostPad>&  getBoostPads() const { return _listBoostPad; }
        const vector<Sample>&        getSamples() const { return _listSample; }
        const KartTrackDef&          getDef() const { return _def; }

    private:
        KartTrackProjection projectSegment( const float3& position, int32 segment ) const;
        KartTrackGate       makeGate( float32 distance ) const;
        float3              makeSurfacePoint( float32 fraction, float32 offset ) const;
        /** @brief @p distance 가 [@p fromDistance, @p toDistance] 안에 있는지 반환합니다(감김을 허용합니다). */
        bool isWithinSpan( float32 distance, float32 fromDistance, float32 toDistance ) const;

        KartTrackDef          _def;
        vector<Sample>        _listSample;
        vector<KartTrackGate> _listGate;
        vector<float3>        _listItemBoxPosition;
        vector<KartBoostPad>  _listBoostPad;
        float32               _length;
    };
} // namespace sw

namespace sw
{
    /**
     * @class KartTrackCatalog
     * @brief `<KartTrackCatalog><Track id="ring" name="Ring" width="12" laps="3" offroadScale="0.5" samples="16"><Point x="0" y="0" z="0"/>...
     *        <Checkpoint at="0.33"/><ItemBox at="0.5" offset="-3"/><BoostPad at="0.2" offset="0" length="4" width="4" duration="1"/>
     *        <Offroad from="0.6" to="0.65" minOffset="-6" maxOffset="0" scale="0.4"/></Track></KartTrackCatalog>` 를 읽습니다.
     * @details 점이 3개보다 적은 트랙은 경고하고 건너뛴다. 같은 id 가 둘이면 뒤의 것이 이긴다.
     */
    class SW_KIT_API KartTrackCatalog : public GameCatalog<KartTrackDef, XMLCatalog<KartTrackCatalog>>
    {
        friend class XMLCatalog<KartTrackCatalog>;

    public:
    private:
        static constexpr const utf8* kXMLRootName = "KartTrackCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Track>` 하나를 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadTrack( const XMLNode& node, string_view sourceName );
    };
} // namespace sw
