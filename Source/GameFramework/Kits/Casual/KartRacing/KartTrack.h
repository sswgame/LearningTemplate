/**
 * @file KartTrack.h
 * @brief 카트 트랙 — 닫힌 중심선(Catmull-Rom 보간 · 누적 거리 표), 폭, 체크포인트 문(지름길 방지), 아이템 상자 · 부스트 패드 · 오프로드 구간과 XML 카탈로그입니다.
 * @details 좌표는 기반 `ArcadeVehicleMotor` 와 같습니다 — +Y 위, 요 0 이 +Z, 오른쪽은 (tz, 0, −tx). 트랙 위 자리는 "중심선 거리(0..길이) + 옆 비킴(오른쪽 +)" 로 말합니다.
 *          트랙은 `IVehicleGround` 라 차가 그대로 밟습니다 — 높이는 중심선 높이, 폭 밖과 오프로드 구간은 속도 배율이 1 보다 작습니다.
 *          결승선은 거리 0 이고 체크포인트는 그 사이의 거리입니다. 문은 중심선에 직각인 선분이라, 안쪽 풀밭을 가로지르는 지름길은 문을 지나지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Movement/ArcadeVehicleMotor.h"

namespace sw
{
    class XmlNode;

    /** @brief 아이템 상자 자리입니다. `_at` 은 한 바퀴에 대한 비율(0..1)입니다. */
    struct KartItemBoxDef
    {
        float32 _at{ 0.0f };
        float32 _offset{ 0.0f }; ///< 옆 비킴(m, 오른쪽 +)
    };

    /** @brief 부스트 패드입니다 — 중심선 거리 `_at` 을 가운데로 길이 `_length`, 폭 `_width` 의 사각형입니다. */
    struct KartBoostPadDef
    {
        float32 _at{ 0.0f };
        float32 _offset{ 0.0f };
        float32 _length{ 4.0f };
        float32 _width{ 4.0f };
        float32 _duration{ 1.0f }; ///< 부스트 시간(초)
    };

    /** @brief 오프로드 구간 — 거리 `_from`..`_to`(비율, 결승선을 넘어 감겨도 된다)와 옆 비킴 범위 안의 속도 배율입니다(모래 · 잔디 · 물웅덩이). */
    struct KartOffroadZoneDef
    {
        float32 _from{ 0.0f };
        float32 _to{ 0.0f };
        float32 _minOffset{ -1000.0f };
        float32 _maxOffset{ 1000.0f };
        float32 _speedScale{ 0.5f };
    };

    /** @brief 트랙 정의 하나입니다(XML 한 `<Track>`). */
    struct KartTrackDef
    {
        hashed_string              _id{};
        string                     _name{};
        vector<float3>             _listControlPoint{}; ///< 닫힌 곡선의 조절점(셋 이상, 진행 순서)
        vector<float32>            _listCheckpoint{};   ///< 체크포인트 자리(비율 0..1, 오름차순으로 정리된다)
        vector<KartItemBoxDef>     _listItemBox{};
        vector<KartBoostPadDef>    _listBoostPad{};
        vector<KartOffroadZoneDef> _listOffroadZone{};
        float32                    _width{ 12.0f };       ///< 길 폭(m) — 밖은 오프로드
        float32                    _offroadScale{ 0.5f }; ///< 길 밖의 속도 배율
        float32                    _gateMargin{ 6.0f };   ///< 문이 길 가장자리 밖으로 더 뻗는 길이(m) — 길을 살짝 벗어나도 지나간 것으로 본다
        int32                      _lapCount{ 3 };
        int32                      _samplesPerSegment{ 16 }; ///< 조절점 사이의 샘플 수
    };

    /** @brief 중심선 위로 내린 자리입니다. */
    struct KartTrackProjection
    {
        float3  _position{}; ///< 중심선 위 가장 가까운 점
        float3  _tangent{ 0.0f, 0.0f, 1.0f };
        float32 _distance{ 0.0f }; ///< 결승선부터 중심선 거리(0..길이)
        float32 _offset{ 0.0f };   ///< 옆 비킴(오른쪽 +)
        int32   _segment{ 0 };
    };

    /** @brief 중심선 위 한 자리의 좌표계입니다. */
    struct KartTrackFrame
    {
        float3 _position{};
        float3 _tangent{ 0.0f, 0.0f, 1.0f }; ///< 수평 단위 접선
        float3 _right{ 1.0f, 0.0f, 0.0f };   ///< 수평 단위 오른쪽
    };

    /** @brief 문 하나 — 중심선에 직각인 선분입니다. 0 번은 결승선, 1.. 은 체크포인트입니다. */
    struct KartTrackGate
    {
        float3  _left{};
        float3  _right{};
        float3  _forward{ 0.0f, 0.0f, 1.0f };
        float32 _distance{ 0.0f };
    };

    /** @brief 부스트 패드 · 오프로드 구간을 거리(m)로 푼 것입니다. */
    struct KartBoostPad
    {
        KartBoostPadDef _def{};
        float32         _distance{ 0.0f };
    };

    /**
     * @class KartTrack
     * @brief 정의를 샘플 점 목록으로 풀어 들고 거리 · 투영 · 문 통과를 답합니다. 차의 땅(`IVehicleGround`)이기도 합니다.
     */
    class SW_GF_API KartTrack final : public IVehicleGround
    {
    public:
        /** @brief 샘플 점 하나입니다. */
        struct Sample
        {
            float3  _position{};
            float32 _distance{ 0.0f }; ///< 시작점부터의 누적 거리(m)
        };

        KartTrack();

        /** @brief 정의로 곡선을 짓습니다. 조절점이 셋보다 적거나 폭이 0 이하면 false 이고 비어 있습니다. */
        [[nodiscard]] bool initialize( const KartTrackDef& def );

        /** @brief 가장 가까운 중심선 자리입니다(모든 구간을 본다). */
        KartTrackProjection project( const float3& position ) const;
        /** @brief @p hintDistance 앞뒤 @p searchRange(m) 안의 구간만 봅니다(겹쳐 지나는 트랙 · 많은 질의). */
        KartTrackProjection project( const float3& position, float32 hintDistance, float32 searchRange ) const;
        /** @brief 거리 @p distance 의 좌표계입니다(감긴다). */
        KartTrackFrame sample( float32 distance ) const;
        /** @brief 거리를 [0, 길이) 로 감습니다. */
        float32 wrapDistance( float32 distance ) const;
        /** @brief @p fromDistance 에서 @p toDistance 로 앞으로 간 거리(−길이/2, 길이/2] 입니다. */
        float32 computeSignedGap( float32 fromDistance, float32 toDistance ) const;
        /** @brief @p distance 에서 @p ahead(m) 앞까지 진행 방향이 도는 각(라디안, 오른쪽 +)입니다 — AI 의 곡선 읽기. */
        float32 computeHeadingChange( float32 distance, float32 ahead ) const;
        /** @brief 이동 @p from → @p to 가 문 @p gateIndex 를 지났으면 방향(1 앞 · −1 뒤), 아니면 0 입니다(수평으로 본다). */
        int32 computeGateCrossing( int32 gateIndex, const float3& from, const float3& to ) const;
        /** @brief 자리의 부스트 패드 번호입니다. 없으면 −1 입니다. */
        int32 findBoostPadAt( const float3& position ) const;
        /**
         * @brief 부스트 패드 규칙 — 땅에 붙은 차가 패드에 **새로 올라서면** 부스트를 겁니다(같은 패드 위에 있는 동안은 한 번).
         * @details 경기와 고스트 재생이 같은 이 함수를 같은 순서(차 걸음 다음)로 불러 경로가 같아집니다. 걸었으면 true 입니다.
         */
        [[nodiscard]] bool applyBoostPad( ArcadeVehicleMotor& motor, int32& inoutLastPad ) const;
        /** @brief 길 폭 안인가입니다. */
        bool isOnRoad( const float3& position ) const;

        float32 sampleHeight( float32 x, float32 z ) const override;
        float32 sampleSpeedScale( float32 x, float32 z ) const override;

        bool    isValid() const { return _listSample.size() >= 3; }
        float32 getLength() const { return _length; }
        float32 getWidth() const { return _def._width; }
        int32   getLapCount() const { return _def._lapCount; }
        /** @brief 체크포인트 수입니다(결승선 빼고). */
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
        /** @brief @p distance 가 [@p fromDistance, @p toDistance] (감김 허용) 안인가입니다. */
        bool isWithinSpan( float32 distance, float32 fromDistance, float32 toDistance ) const;

        KartTrackDef          _def;
        vector<Sample>        _listSample;
        vector<KartTrackGate> _listGate;
        vector<float3>        _listItemBoxPosition;
        vector<KartBoostPad>  _listBoostPad;
        float32               _length;
    };

    /**
     * @class KartTrackCatalog
     * @brief `<KartTrackCatalog><Track id="ring" name="Ring" width="12" laps="3" offroadScale="0.5" samples="16"><Point x="0" y="0" z="0"/>...
     *        <Checkpoint at="0.33"/><ItemBox at="0.5" offset="-3"/><BoostPad at="0.2" offset="0" length="4" width="4" duration="1"/>
     *        <Offroad from="0.6" to="0.65" minOffset="-6" maxOffset="0" scale="0.4"/></Track></KartTrackCatalog>` 를 읽습니다.
     * @details 점이 셋보다 적은 트랙은 경고하고 건너뜁니다. 같은 id 는 뒤의 것이 이깁니다.
     */
    class SW_GF_API KartTrackCatalog
    {
    public:
        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addTrack( const KartTrackDef& def ) { (void)_catalog.add( def ); }

        const KartTrackDef*              findTrack( const hashed_string& id ) const { return _catalog.find( id ); }
        const GameCatalog<KartTrackDef>& getCatalog() const { return _catalog; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<KartTrackDef> _catalog{};
    };
} // namespace sw
