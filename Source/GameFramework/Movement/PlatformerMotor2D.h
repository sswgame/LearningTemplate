/**
 * @file PlatformerMotor2D.h
 * @brief 2D 플랫포머 몸 — 칸 지형(벽 · 한쪽 발판 · 사다리) 위에서 달리기 · 점프(높이와 정점 시간으로) · 짧은 점프 · 코요테 시간 · 점프 미리 누르기 ·
 *        다단 점프 · 벽 미끄러짐 · 벽 점프 · 대시 · 사다리 · 발판 아래로 내려가기입니다.
 * @details 메트로배니아 · 액션 플랫포머 · 소울라이크 2D 가 같은 몸을 씁니다(셀레스테 · 할로우 나이트의 손맛을 내는 규칙들). 좌표는 +X 오른쪽, +Y 위, 단위는 칸이 아니라 월드 단위입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/Countdown.h"

namespace sw
{
    /** @brief 칸 종류입니다. */
    enum class PlatformTile : uint8
    {
        Empty = 0,
        Solid,
        OneWay, ///< 위에서만 밟힌다
        Ladder,
        Hazard ///< 닿으면 알린다(가시 · 용암 — 피해는 게임이)
    };

    /** @brief 칸 지형입니다. (0,0) 칸의 왼쪽 아래가 `_origin` 입니다. */
    class SW_GF_API PlatformTileMap
    {
    public:
        void initialize( int32 width, int32 height, float32 tileSize, const float2& origin );
        void setTile( int32 x, int32 y, PlatformTile tile );
        void fillTiles( int32 minX, int32 minY, int32 maxX, int32 maxY, PlatformTile tile );
        /** @brief 글자로 칠합니다 — 줄마다 한 행(첫 줄이 맨 위), '#' 벽 · '-' 한쪽 발판 · 'H' 사다리 · '^' 위험, 나머지 빈 칸. */
        void loadFromText( string_view text, float32 tileSize, const float2& origin );

        /** @brief 밖은 벽입니다(떨어져 나가지 않게 — 아래쪽 밖만 빈 칸이다: 구덩이). */
        PlatformTile getTile( int32 x, int32 y ) const;
        int32        computeTileX( float32 worldX ) const;
        int32        computeTileY( float32 worldY ) const;
        float32      getTileLeft( int32 x ) const { return _origin._x + static_cast<float32>( x ) * _tileSize; }
        float32      getTileBottom( int32 y ) const { return _origin._y + static_cast<float32>( y ) * _tileSize; }
        float32      getTileSize() const { return _tileSize; }
        int32        getWidth() const { return _width; }
        int32        getHeight() const { return _height; }

    private:
        vector<PlatformTile> _listTile{};
        float2               _origin{};
        float32              _tileSize{ 1.0f };
        int32                _width{ 0 };
        int32                _height{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 움직임 수치입니다. 점프는 높이와 정점까지 시간으로 정합니다(중력 · 초속은 거기서 나온다). */
    struct PlatformerSettings
    {
        float2  _halfExtents{ 0.35f, 0.45f };
        float32 _runSpeed{ 7.0f };
        float32 _groundAcceleration{ 70.0f };
        float32 _airAcceleration{ 40.0f };
        float32 _jumpHeight{ 3.2f };
        float32 _timeToApex{ 0.38f };
        float32 _fallGravityScale{ 1.6f }; ///< 내려올 때 더 빨리(떠 있는 느낌을 줄인다)
        float32 _jumpCutRatio{ 0.45f };    ///< 오르는 중 점프를 떼면 위 속도를 이만큼으로(짧은 점프)
        float32 _maxFallSpeed{ 18.0f };
        float32 _coyoteTime{ 0.1f };
        float32 _jumpBufferTime{ 0.12f };
        float32 _wallSlideSpeed{ 2.5f };
        float32 _wallJumpSpeedX{ 7.5f };
        float32 _wallJumpSpeedY{ 12.0f };
        float32 _wallJumpLockTime{ 0.15f }; ///< 벽 점프 직후 이 시간은 좌우 입력을 줄인다(같은 벽으로 바로 붙지 않게)
        float32 _dashSpeed{ 18.0f };
        float32 _dashDuration{ 0.15f };
        float32 _dashCooldown{ 0.3f };
        float32 _climbSpeed{ 4.0f };
        float32 _dropThroughTime{ 0.25f };
        int32   _extraJumpCount{ 0 }; ///< 공중 점프 수(2단 점프 = 1)
        int32   _airDashCount{ 1 };
        uint8   _bWallJump{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 틱의 입력입니다. */
    struct PlatformerInput
    {
        float2 _move{};                   ///< −1..1
        uint8  _bJumpPressed{ SW_FALSE }; ///< 이번 틱에 눌렀다
        uint8  _bJumpHeld{ SW_FALSE };
        uint8  _bDashPressed{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 틱에 일어난 일(비트)입니다. */
    struct PlatformerEvent
    {
        static constexpr uint32 kJumped        = 1u << 0;
        static constexpr uint32 kAirJumped     = 1u << 1;
        static constexpr uint32 kWallJumped    = 1u << 2;
        static constexpr uint32 kLanded        = 1u << 3;
        static constexpr uint32 kDashed        = 1u << 4;
        static constexpr uint32 kHitCeiling    = 1u << 5;
        static constexpr uint32 kTouchedHazard = 1u << 6;
    };
} // namespace sw

namespace sw
{
    /**
     * @class PlatformerMotor2D
     * @brief 고정 틱(권장 1/60)으로 부릅니다. 겹침은 축마다 따로 풀어(X → Y) 모서리에 걸리지 않게 하고, 한 틱 이동이 칸 반보다 크면 나눠 움직입니다.
     */
    class SW_GF_API PlatformerMotor2D
    {
    public:
        PlatformerMotor2D();

        void setSettings( const PlatformerSettings& settings );
        void setPosition( const float2& position );
        void setVelocity( const float2& velocity ) { _velocity = velocity; }
        /** @brief 넉백 · 튕기기 — 속도를 더하고 대시를 끊습니다. */
        void addImpulse( const float2& impulse );
        void update( const PlatformTileMap& map, const PlatformerInput& input, float32 deltaTime );

        const float2& getPosition() const { return _position; }
        const float2& getVelocity() const { return _velocity; }
        bool          isGrounded() const { return _bGrounded != SW_FALSE; }
        bool          isDashing() const { return _dash.isActive(); }
        bool          isClimbing() const { return _bClimbing != SW_FALSE; }
        /** @brief 붙은 벽 쪽(−1 왼쪽, 1 오른쪽, 0 없음)입니다 — 벽 미끄러짐 중. */
        int32                     getWallSide() const { return _wallSide; }
        int32                     getFacing() const { return _facing; }
        uint32                    getEvents() const { return _events; }
        bool                      hasEvent( uint32 event ) const { return ( _events & event ) != 0; }
        float32                   getGravity() const;
        float32                   getJumpSpeed() const;
        const PlatformerSettings& getSettings() const { return _settings; }

    private:
        bool isBlocked( const PlatformTileMap& map, const float2& center, bool bFromAbove, float32 previousBottom ) const;
        bool isTouching( const PlatformTileMap& map, PlatformTile tile ) const;
        void moveAxis( const PlatformTileMap& map, float32 delta, bool bVertical );
        void startJump( float32 speed, uint32 event );

        PlatformerSettings _settings;
        float2             _position;
        float2             _velocity;
        Countdown          _coyote;
        Countdown          _jumpBuffer;
        Countdown          _wallLock;
        Countdown          _dash;
        Countdown          _dashCooldown;
        Countdown          _dropThrough;
        int32              _extraJumpsLeft;
        int32              _airDashesLeft;
        int32              _wallSide;
        int32              _facing;
        uint32             _events;
        uint8              _bGrounded;
        uint8              _bClimbing;
        uint8              _bRising; ///< 점프로 오르는 중(짧은 점프 판정)
    };
} // namespace sw
