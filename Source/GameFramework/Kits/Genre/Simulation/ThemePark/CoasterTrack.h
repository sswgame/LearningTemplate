/**
 * @file CoasterTrack.h
 * @brief 롤러코스터 트랙을 구현합니다. 조각(스테이션, 리프트, 낙하, 언덕, 회전, 루프, 브레이크, 부스터)을 이어 만들고 거리로 샘플하는 곡선입니다.
 *
 * @details 좌표계는 엔진과 같다. +Y 가 위이고, 요 0 이 +Z 앞이며, 오른쪽은 `up.cross( forward )` 이다. 모든 조각은 **수평으로 들어와 수평으로 나간다**
 *          (기울기가 조각 경계에서 이어진다). 그래서 조각을 아무 순서로 이어도 곡선이 꺾이지 않고, 마지막에 `closeCircuit` 이 끝에서 시작으로
 *          에르미트 곡선을 이어 회로를 닫는다. 롤러코스터 타이쿤처럼 "조각을 이어 붙이는" 방식으로 만든다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    // ------------------------------------------------------------------------------
    // 1) 조각과 구간 표시
    // ------------------------------------------------------------------------------
    /** @brief 트랙 조각의 종류입니다. 데이터(XML)에는 열거자 이름 그대로 적습니다(대소문자 무시). */
    ENUM()
    enum class CoasterPieceType : uint8
    {
        Station = 0, ///< 수평 직선에 스테이션 표시가 붙는다(열차를 정해진 속도로 맞춘다)
        Straight,    ///< 수평 직선
        LiftHill,    ///< 체인 리프트로 올라간다(`_height` 만큼 올라가고 수평 길이는 `_length`)
        Drop,        ///< 내려간다(`_height` 만큼 내려가고 수평 길이는 `_length`)
        Hill,        ///< 올라갔다 내려오는 낙타 등 언덕이다(`_height`). 빠르면 에어타임이 생긴다
        TurnLeft,    ///< 왼쪽으로 `_angle` 도, 반지름 `_radius`, 뱅크 `_bank` 도, 높이 변화 `_height`(헬릭스)로 돈다
        TurnRight,   ///< 오른쪽으로 돌며, 나머지는 TurnLeft 와 같다
        Loop,        ///< 수직 클로소이드 루프이다(높이 2 x `_radius`, 아래는 1.4R, 위는 0.6R). 끝이 옆으로 `_width` 만큼 비켜 나가서 자기 자신과 겹치지 않는다
        Brakes,      ///< 수평 직선에 브레이크가 붙는다(그 속도 위이면 감속한다)
        Booster      ///< 수평 직선에 부스터가 붙는다(그 속도 아래이면 가속한다)
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
} // namespace sw

namespace sw
{
    /** @brief 조각 하나입니다. 종류마다 쓰는 값이 다르며, 쓰지 않는 값은 무시합니다. `<Piece>` 의 스키마이기도 합니다. */
    REFLECT()
    struct CoasterTrackPiece
    {
        REFLECT_BODY();

        PROPERTY( Required )
        CoasterPieceType _type{ CoasterPieceType::Straight };
        PROPERTY( Min = 0.1 )
        float32 _length{ 10.0f }; ///< 수평 길이(m)이다. Station, Straight, LiftHill, Drop, Hill, Brakes, Booster 가 쓴다
        PROPERTY()
        float32 _height{ 0.0f }; ///< 높이 변화(m)이다. LiftHill 은 +, Drop 은 양수로 적으면 내려가고, Hill 은 꼭대기 높이, Turn 은 헬릭스이며 부호를 그대로 따른다
        PROPERTY( Min = 1.0 )
        float32 _radius{ 10.0f }; ///< 반지름(m)이다. Turn 이 쓰며, Loop 는 높이의 절반이다
        PROPERTY()
        float32 _angle{ 90.0f }; ///< 회전각(도)이다. Turn 이 쓴다
        PROPERTY()
        float32 _bank{ 0.0f }; ///< 뱅크(도)이다. 회전 안쪽으로 기울며 Turn 이 쓴다
        PROPERTY( Min = 0.0 )
        float32 _width{ 3.0f }; ///< 옆 비킴(m)이다. Loop 가 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 조각 이름을 열거자로 읽습니다. 모르는 이름이면 false 를 반환하고 @p outType 은 그대로입니다. */
    [[nodiscard]] SW_KIT_API bool parseCoasterPieceType( string_view text, CoasterPieceType& outType );
    /** @brief 조각 열거자의 이름을 반환합니다. */
    SW_KIT_API const utf8* toString( CoasterPieceType type );

    // ------------------------------------------------------------------------------
    // 2) 트랙: 샘플 점 목록과 거리 매개변수
    // ------------------------------------------------------------------------------
    /** @brief 트랙 위 한 지점의 좌표계입니다(탑승자 기준이며, 앞, 위, 오른쪽은 서로 직교하는 단위 벡터입니다). */
    struct CoasterTrackFrame
    {
        float3 _position{};
        float3 _forward{ 0.0f, 0.0f, 1.0f };
        float3 _up{ 0.0f, 1.0f, 0.0f };
        float3 _right{ 1.0f, 0.0f, 0.0f };
        uint8  _flags{ CoasterSegmentFlag::kNone };
    };
} // namespace sw

namespace sw
{
    /**
     * @class CoasterTrack
     * @brief 일정 간격(약 0.25 m)으로 샘플한 트랙 곡선입니다. 거리 `s`(m)로 위치와 좌표계를 묻습니다.
     * @details 회로가 닫혀 있으면(`isClosed`) 거리는 한 바퀴 길이로 감긴다(음수도 마찬가지이다). 열려 있으면 양 끝에서 잘린다.
     */
    class SW_KIT_API CoasterTrack
    {
    public:
        /** @brief 샘플 점 하나입니다. */
        struct Point
        {
            float3  _position{};
            float3  _up{ 0.0f, 1.0f, 0.0f };      ///< 탑승자의 위쪽 벡터이다(접선과 직교하도록 정리된 값)
            float3  _tangent{ 0.0f, 0.0f, 1.0f }; ///< 단위 접선이다(이웃 점의 중심 차분)
            float32 _distance{ 0.0f };            ///< 시작점부터의 거리(m)
            uint8   _flags{ CoasterSegmentFlag::kNone };
        };

        CoasterTrack();

        /** @brief 점을 모두 지웁니다. */
        void clear();
        /** @brief 점을 덧붙입니다(빌더가 씁니다). 앞 점과 거의 같은 위치이면 버립니다. */
        void appendPoint( const float3& position, const float3& up, uint8 flags );
        /** @brief 거리, 접선, 위쪽 벡터를 다시 계산합니다. @p bClosed 가 true 이면 마지막 점에서 첫 점으로 이어지는 회로로 봅니다. */
        void finalize( bool bClosed );

        /** @brief 거리 @p distance 의 좌표계를 반환합니다(점 사이는 보간합니다). 점이 둘보다 적으면 기본 좌표계를 반환합니다. */
        CoasterTrackFrame sample( float32 distance ) const;
        /** @brief @p distance 를 트랙 위의 거리로 바꿉니다. 닫힌 회로는 감고, 열린 트랙은 자릅니다. */
        float32 wrapDistance( float32 distance ) const;

        /** @brief 한 바퀴(닫힌 회로) 또는 처음부터 끝까지(열린 트랙)의 길이(m)를 반환합니다. */
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
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 3) 빌더: 조각을 이어 붙여 트랙을 만든다
    // ------------------------------------------------------------------------------
    /**
     * @class CoasterTrackBuilder
     * @brief 거북이처럼 현재 위치와 진행 방향을 가지고 조각을 하나씩 이어 붙입니다.
     * @details `appendPiece` 마다 점이 쌓이고, `makeTrack( bCloseCircuit )` 이 트랙을 만들어 준다. 닫으면 끝에서 시작점으로 에르미트 연결 곡선을 더한다
     *          (끝이 이미 시작점과 같으면 더하지 않는다). 만드는 도중에도 `getPosition` 과 `getHeading` 으로 지금 위치를 물을 수 있다(에디터 미리보기).
     */
    class SW_KIT_API CoasterTrackBuilder
    {
    public:
        /** @brief 점 사이의 간격(m)입니다. G 를 차분으로 구하므로 너무 넓히지 않습니다. */
        static constexpr float32 kSampleSpacing = 0.25f;

        CoasterTrackBuilder();

        /** @brief 처음 위치와 방향(요, 도)으로 되돌리고 점을 지웁니다. */
        void reset( const float3& startPosition, float32 startHeadingDegrees );
        /** @brief 조각 하나를 이어 붙입니다. */
        void appendPiece( const CoasterTrackPiece& piece );
        /** @brief 조각들을 차례로 이어 붙입니다. */
        void appendPieces( const vector<CoasterTrackPiece>& listPiece );
        /** @brief 지금까지의 점으로 트랙을 만듭니다. @p bCloseCircuit 가 true 이면 시작점으로 이어 닫습니다. */
        CoasterTrack makeTrack( bool bCloseCircuit ) const;

        /** @brief 지금 위치를 반환합니다(다음 조각이 시작할 곳입니다). */
        const float3& getPosition() const { return _position; }
        /** @brief 지금 진행 방향(요, 라디안)을 반환합니다. */
        float32 getHeading() const { return _heading; }

    private:
        /** @brief 진행 방향의 수평 단위 벡터를 반환합니다. */
        float3 computeForward() const;
        /** @brief 수평 길이 @p length 동안 높이를 @p heightAt( t ) 로 바꾸며 직진하는 조각을 붙입니다(t 는 0..1). */
        void appendProfile( float32 length, float32 height, CoasterPieceType type, uint8 flags );
        void appendTurn( const CoasterTrackPiece& piece, float32 directionSign );
        void appendLoop( const CoasterTrackPiece& piece );
        /** @brief 점 하나를 더합니다. 위쪽 벡터는 세계 위쪽을 접선에 직교로 맞춘 것에, 뱅크(라디안)만큼 접선 둘레로 돌린 값입니다. */
        void pushPoint( const float3& position, const float3& tangent, float32 bank, uint8 flags );

        vector<CoasterTrack::Point> _listPoint;
        float3                      _startPosition;
        float3                      _position;
        float32                     _startHeading;
        float32                     _heading;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 4) 레이아웃 카탈로그: XML 의 조각 목록
    // ------------------------------------------------------------------------------
    /** @brief 이름 붙은 조각 목록 하나입니다. */
    REFLECT()
    struct CoasterLayoutDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY()
        float32 _startHeight{ 1.0f }; ///< 스테이션 높이(m)

        vector<CoasterTrackPiece> _listPiece{}; ///< `<Piece>` 자식에서 읽는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class CoasterLayoutCatalog
     * @brief `<CoasterCatalog><Layout id="..." name="..." startHeight="1"><Piece type="LiftHill" length="30" height="25"/>...` 를 읽습니다.
     * @details 조각 하나라도 틀리면(모르는 종류 · 범위 밖 값) 그 레이아웃 전체를 넣지 않는 오류다 — 조각을 빼고 이으면 다른 트랙이 된다.
     */
    class SW_KIT_API CoasterLayoutCatalog : public XMLCatalog<CoasterLayoutCatalog>, public GameCatalog<CoasterLayoutDef>
    {
        friend class XMLCatalog<CoasterLayoutCatalog>;

    public:
        /** @brief 데이터 종류 이름입니다(`SW_RESOURCE_DATA_KIND` 등록 줄과 팩 경로 `GamePackDataUtil::tryMakeKindPath` 가 이것을 쓴다). */
        static constexpr const utf8* kDataKind = "coasters";

        CoasterLayoutCatalog();

    private:
        static constexpr const utf8* kXMLRootName = "CoasterCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Layout>` 과 그 `<Piece>` 를 스키마로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadLayout( const XMLNode& node, string_view sourceName );
    };
} // namespace sw
