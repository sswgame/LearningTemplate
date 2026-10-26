#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartGhost.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Rule/KartTrack.h"

namespace sw
{
    namespace
    {
        struct KartGhostInternal
        {
            static constexpr float32 kAxisScale  = 127.0f;
            static constexpr int32   kButtonBits = 4;
            static constexpr uint32  kMagic      = FourCcUtil::make( "KGHT" );

            static int8 encodeAxis( float32 value )
            {
                const float32 clamped = MathUtil::clamp( value, -1.0f, 1.0f );
                return static_cast<int8>( MathUtil::round( clamped * kAxisScale ) );
            }

            static float32 decodeAxis( int8 value ) { return static_cast<float32>( value ) / kAxisScale; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    KartGhost::KartGhost()
        : _listFrame{}
        , _startPosition{}
        , _startYaw{ 0.0f }
        , _step{ 1.0f / 60.0f }
        , _finishTime{ 0.0f }
    {
    }

    KartGhostFrame KartGhost::encodeInput( const ArcadeVehicleInput& input, bool bUseItem )
    {
        KartGhostFrame frame;
        frame._throttle = KartGhostInternal::encodeAxis( input._throttle );
        frame._steer    = KartGhostInternal::encodeAxis( input._steer );
        if ( input._bDriftHeld != SW_FALSE )
            frame._buttons |= kButtonDrift;
        if ( input._bBoostPressed != SW_FALSE )
            frame._buttons |= kButtonBoost;
        if ( input._bJumpPressed != SW_FALSE )
            frame._buttons |= kButtonJump;
        if ( bUseItem )
            frame._buttons |= kButtonUseItem;
        return frame;
    }

    ArcadeVehicleInput KartGhost::decodeInput( const KartGhostFrame& frame )
    {
        ArcadeVehicleInput input;
        input._throttle      = KartGhostInternal::decodeAxis( frame._throttle );
        input._steer         = KartGhostInternal::decodeAxis( frame._steer );
        input._bDriftHeld    = ( frame._buttons & kButtonDrift ) != 0 ? SW_TRUE : SW_FALSE;
        input._bBoostPressed = ( frame._buttons & kButtonBoost ) != 0 ? SW_TRUE : SW_FALSE;
        input._bJumpPressed  = ( frame._buttons & kButtonJump ) != 0 ? SW_TRUE : SW_FALSE;
        return input;
    }

    bool KartGhost::writeFrame( BitWriter& writer, const KartGhostFrame& frame )
    {
        const bool bAxesWritten = writer.writeInt( frame._throttle, -127, 127 ) && writer.writeInt( frame._steer, -127, 127 );
        if ( bAxesWritten == false )
            return false;
        writer.writeBits( frame._buttons, KartGhostInternal::kButtonBits );
        return true;
    }

    bool KartGhost::readFrame( BitReader& reader, KartGhostFrame& outFrame )
    {
        int32 throttle{ 0 };
        int32 steer{ 0 };
        if ( reader.readInt( -127, 127, throttle ) == false || reader.readInt( -127, 127, steer ) == false )
            return false;
        const uint8 buttons = static_cast<uint8>( reader.readBits( KartGhostInternal::kButtonBits ) );
        if ( reader.hasOverflowed() )
            return false;
        outFrame._throttle = static_cast<int8>( throttle );
        outFrame._steer    = static_cast<int8>( steer );
        outFrame._buttons  = buttons;
        return true;
    }

    void KartGhost::begin( const float3& startPosition, float32 startYaw, float32 step )
    {
        _listFrame.clear();
        _startPosition = startPosition;
        _startYaw      = startYaw;
        _step          = step > 0.0f ? step : 1.0f / 60.0f;
        _finishTime    = 0.0f;
    }

    void KartGhost::record( const ArcadeVehicleInput& input, bool bUseItem )
    {
        _listFrame.push_back( encodeInput( input, bUseItem ) );
    }

    bool KartGhost::serialize( vector<uint8>& outBuffer ) const
    {
        BitWriter writer{ std::move( outBuffer ) };
        writer.writeUint32( KartGhostInternal::kMagic );
        writer.writeBits( kVersion, 8 );
        writer.writeFloat( _startPosition._x );
        writer.writeFloat( _startPosition._y );
        writer.writeFloat( _startPosition._z );
        writer.writeFloat( _startYaw );
        writer.writeFloat( _step );
        writer.writeFloat( _finishTime );
        writer.writeVarUint( static_cast<uint64>( _listFrame.size() ) );
        bool bFramesWritten = true;
        for ( const KartGhostFrame& frame : _listFrame )
        {
            bFramesWritten = bFramesWritten && writeFrame( writer, frame );
        }
        outBuffer = writer.releaseBytes();
        return bFramesWritten;
    }

    bool KartGhost::deserialize( const uint8* pData, int32 byteCount )
    {
        if ( pData == nullptr || byteCount <= 0 )
            return false;
        BitReader reader( pData, byteCount );
        if ( reader.readUint32() != KartGhostInternal::kMagic || reader.readBits( 8 ) != kVersion )
            return false;
        float3  startPosition;
        float32 startYaw{ 0.0f };
        float32 step{ 0.0f };
        float32 finishTime{ 0.0f };
        // NaN 간격은 아래 `step <= 0` 을 빠져나간다 — 유한한 값만 받는다.
        const bool bFinite = reader.readFiniteFloat( startPosition._x ) && reader.readFiniteFloat( startPosition._y ) &&
                             reader.readFiniteFloat( startPosition._z ) && reader.readFiniteFloat( startYaw ) && reader.readFiniteFloat( step ) &&
                             reader.readFiniteFloat( finishTime );
        if ( bFinite == false )
            return false;
        const uint64 frameCount        = reader.readVarUint();
        const int32  frameBits         = 8 + 8 + KartGhostInternal::kButtonBits;
        const bool   bCountFits        = frameCount <= static_cast<uint64>( reader.getBitsRemaining() / frameBits );
        const bool   bInvalidGhostData = reader.hasOverflowed() || bCountFits == false || step <= 0.0f;
        if ( bInvalidGhostData )
            return false;
        vector<KartGhostFrame> listFrame;
        listFrame.reserve( static_cast<size_t>( frameCount ) );
        for ( uint64 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            KartGhostFrame frame;
            if ( readFrame( reader, frame ) == false )
                return false;
            listFrame.push_back( frame );
        }
        _listFrame.swap( listFrame );
        _startPosition = startPosition;
        _startYaw      = startYaw;
        _step          = step;
        _finishTime    = finishTime;
        return true;
    }

    // ------------------------------------------------------------------------------
    // KartGhostPlayer
    // ------------------------------------------------------------------------------
    KartGhostPlayer::KartGhostPlayer()
        : _motor{}
        , _timer{}
        , _pGhost{ nullptr }
        , _pTrack{ nullptr }
        , _frameIndex{ 0 }
        , _lastBoostPad{ -1 }
    {
    }

    void KartGhostPlayer::initialize( const KartGhost* pGhost, const ArcadeVehicleSettings& settings, const KartTrack* pTrack )
    {
        _pGhost       = pGhost;
        _pTrack       = pTrack;
        _frameIndex   = 0;
        _lastBoostPad = -1;
        _motor.setSettings( settings );
        _motor.setGround( pTrack );
        if ( pGhost != nullptr )
        {
            _motor.reset( pGhost->getStartPosition(), pGhost->getStartYaw() );
            _timer = FixedStepTimer{ pGhost->getStep(), 0.25f };
        }
    }

    bool KartGhostPlayer::step()
    {
        if ( isFinished() )
            return false;
        const KartGhostFrame& frame = _pGhost->getFrames()[static_cast<size_t>( _frameIndex )];
        _motor.update( KartGhost::decodeInput( frame ), _pGhost->getStep() );
        if ( _pTrack != nullptr )
            (void)_pTrack->applyBoostPad( _motor, _lastBoostPad ); // 걸었는지는 쓰지 않는다. 모터가 상태를 가지고 있다
        ++_frameIndex;
        return true;
    }

    int32 KartGhostPlayer::advance( float32 frameTime )
    {
        const int32 stepCount = _timer.consume( frameTime );
        int32       stepped   = 0;
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            if ( step() == false )
                break;
            ++stepped;
        }
        return stepped;
    }

    bool KartGhostPlayer::isFinished() const
    {
        return _pGhost == nullptr || _frameIndex >= _pGhost->getFrameCount();
    }
} // namespace sw
