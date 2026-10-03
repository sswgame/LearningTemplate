/**
 * @file CoasterTrack.h
 * @brief 롤러코스터 트랙 — 조각(스테이션 · 리프트 · 낙하 · 언덕 · 회전 · 루프 · 브레이크 · 부스터)으로 짓고 거리로 샘플하는 곡선입니다.
 *
 * @details 좌표계는 엔진과 같습니다 — +Y 위, 요 0 이 +Z 앞, 오른쪽은 `up.cross( forward )`. 모든 조각은 **수평으로 들어와 수평으로 나갑니다**
 *          (기울기가 조각 경계에서 이어진다). 그래서 조각을 아무 순서로 이어도 곡선이 꺾이지 않고, 마지막에 `closeCircuit` 이 끝에서 시작으로
 *          에르미트 곡선을 이어 회로를 닫습니다. 롤러코스터 타이쿤처럼 "조각을 이어 붙이는" 짓기 방식입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    // ------------------------------------------------------------------------------
    // 1) 조각 · 구간 표시
    // ------------------------------------------------------------------------------
    /** @brief 트랙 조각의 종류입니다. 데이터(XML)는 열거자 이름 그대로 적습니다(대소문자 무시). */
    enum class CoasterPieceType : uint8
    {
        Station = 0, ///< 수평 직선 + 스테이션 표시(열차를 정해진 속도로 맞춘다)
        Straight,    ///< 수평 직선
        LiftHill,    ///< 체인 리프트로 올라간다(`_height` 만큼, 수평 길이 `_length`)
        Drop,        ///< 내려간다(`_height` 만큼, 수평 길이 `_length`)
        Hill,        ///< 올라갔다 내려오는 낙타 등(`_height`) — 빠르면 에어타임
        TurnLeft,    ///< 왼쪽으로 `_angle` 도, 반지름 `_radius`, 뱅크 `_bank` 도, 높이 변화 `_height`(헬릭스)
        TurnRight,   ///< 오른쪽으로 — 나머지는 TurnLeft 와 같다
        Loop,        ///< 수직 클로소이드 루프(높이 2 × `_radius`, 아래 1.4R · 위 0.6R), 끝이 옆으로 `_width` 비켜 나간다(자기 자신과 겹치지 않게)
        Brakes,      ///< 수평 직선 + 브레이크(그 속도 위면 감속)
        Booster      ///< 수평 직선 + 부스터(그 속도 아래면 가속)
    };

    /** @brief 트랙 점 하나에 붙는 구간 표시(비트)입니다. 열차 물리가 읽습니다. */
    struct CoasterSegmentFlag
    {
        static constexpr uint8 kNone    = 0;
        static constexpr uint8 kStation = 1u << 0;
        static constexpr uint8 kLift    = 1u << 1;
        static constexpr uint8 kBrake   = 1u << 2;
        static constexpr uint8 kBooster = 1u << 3;
    };

    /** @brief 조각 하나입니다. 종류마다 쓰는 값이 다르고, 쓰지 않는 값은 무시합니다. */
    struct CoasterTrackPiece
    {
        CoasterPieceType _type{ CoasterPieceType::Straight };
        float32          _length{ 10.0f }; ///< 수평 길이(m) — Station · Straight · LiftHill · Drop · Hill · Brakes · Booster
        float32          _height{ 0.0f };  ///< 높이 변화(m) — LiftHill(+) · Drop(−로 내려간다, 양수로 적는다) · Hill(꼭대기) · Turn(헬릭스, 부호대로)
        float32          _radius{ 10.0f }; ///< 반지름(m) — Turn, Loop 는 높이의 절반
        float32          _angle{ 90.0f };  ///< 회전각(도) — Turn
        float32          _bank{ 0.0f };    ///< 뱅크(도, 회전 안쪽으로 기운다) — Turn
        float32          _width{ 3.0f };   ///< 옆 비킴(m) — Loop
    };

    /** @brief 조각 이름을 열거로 읽습니다. 모르는 이름이면 false 이고 @p outType 은 그대로입니다. */
    [[nodiscard]] SW_GF_API bool parseCoasterPieceType( string_view text, CoasterPieceType& outType );
    /** @brief 조각 열거의 이름입니다. */
    SW_GF_API const utf8* toString( CoasterPieceType type );

    // ------------------------------------------------------------------------------
    // 2) 트랙 — 샘플 점 목록 + 거리 매개변수
    // ------------------------------------------------------------------------------
    /** @brief 트랙 위 한 자리의 좌표계입니다(탑승자 기준 — 앞 · 위 · 오른쪽은 서로 직교하는 단위 벡터). */
    struct CoasterTrackFrame
    {
        float3 _position{};
        float3 _forward{ 0.0f, 0.0f, 1.0f };
        float3 _up{ 0.0f, 1.0f, 0.0f };
        float3 _right{ 1.0f, 0.0f, 0.0f };
        uint8  _flags{ CoasterSegmentFlag::kNone };
    };

    /**
     * @class CoasterTrack
     * @brief 일정 간격(약 0.25 m)으로 샘플한 트랙 곡선입니다. 거리 `s`(m)로 위치 · 좌표계를 묻습니다.
     * @details 회로가 닫혔으면(`isClosed`) 거리는 한 바퀴 길이로 감깁니다(음수도). 열려 있으면 양 끝에서 잘립니다.
     */
    class SW_GF_API CoasterTrack
    {
    public:
        /** @brief 샘플 점 하나입니다. */
        struct Point
        {
            float3  _position{};
            float3  _up{ 0.0f, 1.0f, 0.0f };      ///< 탑승자의 위(접선과 직교하도록 정리된 값)
            float3  _tangent{ 0.0f, 0.0f, 1.0f }; ///< 단위 접선(이웃 점의 중심 차분)
            float32 _distance{ 0.0f };            ///< 시작점부터의 거리(m)
            uint8   _flags{ CoasterSegmentFlag::kNone };
        };

        CoasterTrack();

        /** @brief 점을 모두 지웁니다. */
        void clear();
        /** @brief 점을 덧붙입니다(빌더가 쓴다). 앞 점과 거의 같은 자리면 버립니다. */
        void appendPoint( const float3& position, const float3& up, uint8 flags );
        /** @brief 거리 · 접선 · 위를 다시 계산합니다. @p bClosed 면 마지막 점에서 첫 점으로 이어지는 회로로 봅니다. */
        void finalize( bool bClosed );

        /** @brief 거리 @p distance 의 좌표계입니다(점 사이는 보간). 점이 둘보다 적으면 기본 좌표계입니다. */
        CoasterTrackFrame sample( float32 distance ) const;
        /** @brief @p distance 를 트랙 위의 거리로 감습니다(닫힌 회로) 또는 자릅니다(열린 트랙). */
        float32 wrapDistance( float32 distance ) const;

        /** @brief 한 바퀴(닫힌 회로) 또는 처음부터 끝까지(열린 트랙)의 길이(m)입니다. */
        float32              getLength() const { return _length; }
        bool                 isClosed() const { return _bClosed == SW_TRUE; }
        float32              getMaxHeight() const { return _maxHeight; }
        float32              getMinHeight() const { return _minHeight; }
        const vector<Point>& getPoints() const { return _listPoint; }

    private:
        vector<Point> _listPoint;
        float32       _length;
        float32       _maxHeight;
        float32       _minHeight;
        uint8         _bClosed;
    };

    // ------------------------------------------------------------------------------
    // 3) 빌더 — 조각을 이어 트랙을 짓는다
    // ------------------------------------------------------------------------------
    /**
     * @class CoasterTrackBuilder
     * @brief 거북이처럼 자리(위치 · 진행 방향)를 들고 조각을 하나씩 이어 붙입니다.
     * @details `appendPiece` 마다 점이 쌓이고, `makeTrack( bCloseCircuit )` 이 트랙을 내줍니다. 닫으면 끝에서 시작점으로 에르미트 연결 곡선을 더합니다
     *          (끝이 이미 시작점과 같으면 더하지 않는다). 짓는 도중에도 `getPosition` · `getHeading` 으로 지금 자리를 물을 수 있습니다(에디터 미리보기).
     */
    class SW_GF_API CoasterTrackBuilder
    {
    public:
        /** @brief 점 사이 간격(m)입니다. G 를 차분으로 구하므로 너무 넓히지 않습니다. */
        static constexpr float32 kSampleSpacing = 0.25f;

        CoasterTrackBuilder();

        /** @brief 처음 자리 · 방향(요, 도)으로 되돌리고 점을 지웁니다. */
        void reset( const float3& startPosition, float32 startHeadingDegrees );
        /** @brief 조각 하나를 이어 붙입니다. */
        void appendPiece( const CoasterTrackPiece& piece );
        /** @brief 조각들을 차례로 이어 붙입니다. */
        void appendPieces( const vector<CoasterTrackPiece>& listPiece );
        /** @brief 지금까지의 점으로 트랙을 짓습니다. @p bCloseCircuit 면 시작점으로 이어 닫습니다. */
        CoasterTrack makeTrack( bool bCloseCircuit ) const;

        /** @brief 지금 자리입니다(다음 조각이 시작할 곳). */
        const float3& getPosition() const { return _position; }
        /** @brief 지금 진행 방향(요, 라디안)입니다. */
        float32 getHeading() const { return _heading; }

    private:
        /** @brief 진행 방향의 수평 단위 벡터입니다. */
        float3 computeForward() const;
        /** @brief 수평 길이 @p length 동안 높이를 @p heightAt( t ) 로 바꾸며 직진하는 조각입니다(t 는 0..1). */
        void appendProfile( float32 length, float32 height, CoasterPieceType type, uint8 flags );
        void appendTurn( const CoasterTrackPiece& piece, float32 directionSign );
        void appendLoop( const CoasterTrackPiece& piece );
        /** @brief 점 하나를 더합니다 — 위는 세계 위를 접선에 직교로 맞춘 것에 뱅크(라디안)를 접선 둘레로 돌린 것입니다. */
        void pushPoint( const float3& position, const float3& tangent, float32 bank, uint8 flags );

        vector<CoasterTrack::Point> _listPoint;
        float3                      _startPosition;
        float3                      _position;
        float32                     _startHeading;
        float32                     _heading;
    };

    // ------------------------------------------------------------------------------
    // 4) 레이아웃 카탈로그 — XML 의 조각 목록
    // ------------------------------------------------------------------------------
    /** @brief 이름 붙은 조각 목록 하나입니다. */
    struct CoasterLayoutDef
    {
        hashed_string             _id{};
        string                    _name{};
        vector<CoasterTrackPiece> _listPiece{};
        float32                   _startHeight{ 1.0f }; ///< 스테이션 높이(m)
    };

    /**
     * @class CoasterLayoutCatalog
     * @brief `<CoasterCatalog><Layout id="..." name="..." startHeight="1"><Piece type="LiftHill" length="30" height="25"/>...` 를 읽습니다.
     * @details 모르는 조각 이름은 경고하고 그 조각만 건너뜁니다. 같은 id 는 뒤의 것이 이깁니다.
     */
    class SW_GF_API CoasterLayoutCatalog
    {
    public:
        CoasterLayoutCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addLayout( const CoasterLayoutDef& layout );

        const CoasterLayoutDef*         findLayout( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<CoasterLayoutDef>& getLayouts() const { return _catalog.getAll(); }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<CoasterLayoutDef> _catalog; ///< 읽은 순서(게임이 차례로 바꿔 탄다)
    };
} // namespace sw
