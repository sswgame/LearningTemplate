/**
 * @file ActionPlatformerBody.h
 * @brief 기반 `PlatformerMotor2D` 위의 이동 모드 — 우산 활공(검브렐라) · 갈고리(갈고리 지점에 걸어 진자 운동, 놓을 때 속도 유지) ·
 *        드릴(페퍼 그라인더 — 흙 칸 안을 파고 다니다 튀어나올 때 점프)입니다.
 * @details 흙 · 갈고리 지점은 키트 자체 격자(`ActionTerrainGrid`)가 듭니다. 흙은 기반 칸 지형에는 벽(Solid)으로 칠해 보통 몸은 그 위를 걷고,
 *          드릴 모드만 그 안을 지납니다. 보통 · 활공 모드는 기반 몸이 움직이고, 갈고리 · 드릴 모드는 이 몸이 직접 움직인 뒤 놓을 때 위치와 속도를
 *          기반 몸에 돌려줍니다. 고정 틱(권장 1/60)으로 부르면 결정적입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Movement/PlatformerMotor2D.h"
#include "GameFramework/Base/Utility/Countdown.h"
#include "GameFramework/Base/Utility/GridTopology.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Action/ActionPlatformer/ActionPlatformerCatalog.h"

namespace sw
{
    class Archive;

    /**
     * @class ActionTerrainGrid
     * @brief 키트의 칸 정보 — 흙(드릴로 파는 칸)과 갈고리 지점입니다. 칸 크기 · 원점은 기반 칸 지형과 같습니다.
     */
    class SW_GF_API ActionTerrainGrid
    {
    public:
        ActionTerrainGrid();

        /**
         * @brief 글자로 칠합니다 — 기반 `PlatformTileMap::loadFromText` 의 글자에 더해 'D' 흙(기반 지형에는 벽), 'O' 갈고리 지점(칸 가운데, 기반 지형에는 빈 칸).
         * @param outMap 기반 몸이 쓸 칸 지형(이 글로 함께 채운다)
         */
        void loadFromText( string_view text, float32 tileSize, const float2& origin, PlatformTileMap& outMap );
        void addGrapplePoint( const float2& point ) { _listGrapplePoint.push_back( point ); }

        bool isDirt( int32 x, int32 y ) const;
        bool isDirtAt( const float2& worldPosition ) const;
        /** @brief @p position 에서 가장 가까운 갈고리 지점(거리 [@p minDistance, @p maxDistance])의 자리입니다. 같은 거리면 앞 자리. 없으면 −1 입니다. */
        int32 findGrapplePoint( const float2& position, float32 minDistance, float32 maxDistance ) const;

        const vector<float2>& getGrapplePoints() const { return _listGrapplePoint; }
        int32                 getWidth() const { return _topology._width; }
        int32                 getHeight() const { return _topology._height; }

    private:
        int32 computeTileX( float32 worldX ) const;
        int32 computeTileY( float32 worldY ) const;

        vector<uint8>  _listDirt; ///< 칸마다 SW_TRUE/SW_FALSE(`_topology` 의 칸 번호)
        vector<float2> _listGrapplePoint;
        float2         _origin;
        float32        _tileSize;
        GridTopology   _topology;
    };
} // namespace sw

namespace sw
{
    /** @brief 이동 모드입니다. */
    enum class ActionMoveMode : uint8
    {
        Normal = 0, ///< 기반 몸 그대로
        Glide,      ///< 우산 — 떨어지는 속도 상한 · 중력이 준다
        Grapple,    ///< 갈고리 진자
        Drill       ///< 흙 속 굴착
    };

    /** @brief 한 틱의 입력입니다. */
    struct ActionBodyInput
    {
        PlatformerInput _motor{};
        uint8           _bGlideHeld{ SW_FALSE };
        uint8           _bGrapplePressed{ SW_FALSE }; ///< 이번 틱에 눌렀다 — 가까운 지점에 건다
        uint8           _bGrappleHeld{ SW_FALSE };    ///< 떼면 놓는다
        uint8           _bDrillHeld{ SW_FALSE };      ///< 누른 채 흙 쪽으로 가면 파고든다
    };
} // namespace sw

namespace sw
{
    /** @brief 한 틱에 일어난 일(비트)입니다. 기반 몸의 일은 `getMotor().getEvents()` 입니다. */
    struct ActionBodyEvent
    {
        static constexpr uint32 kGlideStarted    = 1u << 0;
        static constexpr uint32 kGlideEnded      = 1u << 1;
        static constexpr uint32 kGrappleAttached = 1u << 2;
        static constexpr uint32 kGrappleReleased = 1u << 3;
        static constexpr uint32 kDrillEntered    = 1u << 4;
        static constexpr uint32 kDrillExited     = 1u << 5;
        static constexpr uint32 kDrillJumped     = 1u << 6; ///< 튀어나오며 점프했다
        static constexpr uint32 kDrillAborted    = 1u << 7; ///< 흙에 닿지 못해 그만뒀다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ActionPlatformerBody
     * @brief 기반 몸 하나와 그 위의 이동 모드입니다.
     */
    class SW_GF_API ActionPlatformerBody
    {
    public:
        static constexpr uint32 kStateTag     = 0x44425041u; ///< 'APBD'
        static constexpr uint32 kStateVersion = 1;

        ActionPlatformerBody();

        void initialize( const PlatformerSettings& motorSettings, const ActionBodySettings& settings );
        void setPosition( const float2& position );
        void update( const PlatformTileMap& map, const ActionTerrainGrid& terrain, const ActionBodyInput& input, float32 deltaTime );

        ActionMoveMode           getMode() const { return _mode; }
        float2                   getPosition() const;
        float2                   getVelocity() const;
        uint32                   getEvents() const { return _events; }
        bool                     hasEvent( uint32 event ) const { return ( _events & event ) != 0; }
        const float2&            getAnchor() const { return _anchor; }
        float32                  getRopeLength() const { return _ropeLength; }
        const PlatformerMotor2D& getMotor() const { return _motor; }
        PlatformerMotor2D&       getMotor() { return _motor; }

        /**
         * @brief 이동 모드 · 기반 몸(`PlatformerMotor2D`) · 갈고리 · 드릴의 자리 · 속도 · 밧줄 끝 · 길이 · 드릴 찾기 시간 · 흙 안 표시를 씁니다.
         *        설정은 `initialize` 의 것(활공이면 활공 설정을 다시 건다), 지형은 `update` 의 인자, 이번 틱의 사건 비트는 싣지 않습니다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 기반 몸에 걸 설정입니다 — 활공이면 낙하 상한 · 중력을 줄인 것입니다. */
        PlatformerSettings makeMotorSettings( bool bGlide ) const;
        void               setGlide( bool bGlide );
        [[nodiscard]] bool tryAttachGrapple( const ActionTerrainGrid& terrain );
        [[nodiscard]] bool tryEnterDrill( const ActionTerrainGrid& terrain, const ActionBodyInput& input );
        void               updateGrapple( const PlatformTileMap& map, const ActionBodyInput& input, float32 deltaTime );
        void               updateDrill( const PlatformTileMap& map, const ActionTerrainGrid& terrain, const ActionBodyInput& input, float32 deltaTime );
        /** @brief 직접 움직이던 위치 · 속도를 기반 몸에 돌려주고 보통 모드로 갑니다. */
        void returnToMotor( const float2& velocity );

        PlatformerMotor2D  _motor;
        PlatformerSettings _motorSettings;
        ActionBodySettings _settings;
        float2             _position; ///< 갈고리 · 드릴 모드의 위치
        float2             _velocity; ///< 갈고리 · 드릴 모드의 속도
        float2             _anchor;
        float32            _ropeLength;
        Countdown          _drillSearchTimer;
        uint32             _events;
        ActionMoveMode     _mode;
        uint8              _bInsideDirt;
    };
} // namespace sw
