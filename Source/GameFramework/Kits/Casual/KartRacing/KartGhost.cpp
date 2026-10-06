#include "pch.h"

#include "GameFramework/Kits/Casual/KartRacing/KartGhost.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Kits/Casual/KartRacing/KartTrack.h"

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

    void KartGhost::writeFrame( BitWriter& writer, const KartGhostFrame& frame )
    {
        writer.writeInt( frame._throttle, -127, 127 );
        writer.writeInt( frame._steer, -127, 127 );
        writer.writeBits( frame._buttons, KartGhostInternal::kButtonBits );
    }

    KartGhostFrame KartGhost::readFrame( BitReader& reader )
    {
        KartGhostFrame frame;
        frame._throttle = static_cast<int8>( reader.readInt( -127, 127 ) );
        frame._steer    = static_cast<int8>( reader.readInt( -127, 127 ) );
        frame._buttons  = static_cast<uint8>( reader.readBits( KartGhostInternal::kButtonBits ) );
        return frame;
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

    void KartGhost::serialize( vector<uint8>& outBuffer ) const
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
        for ( const KartGhostFrame& frame : _listFrame )
            writeFrame( writer, frame );
        outBuffer = writer.releaseBytes();
    }

    bool KartGhost::deserialize( const uint8* pData, int32 byteCount )
    {
        if ( pData == nullptr || byteCount <= 0 )
            return false;
        BitReader reader( pData, byteCount );
        if ( reader.readUint32() != KartGhostInternal::kMagic || reader.readBits( 8 ) != kVersion )
            return false;
        float3 startPosition;
        startPosition._x         = reader.readFloat();
        startPosition._y         = reader.readFloat();
        startPosition._z         = reader.readFloat();
        const float32 startYaw   = reader.readFloat();
        const float32 step       = reader.readFloat();
        const float32 finishTime = reader.readFloat();
        const uint64  frameCount = reader.readVarUint();
        const int32   frameBits  = 8 + 8 + KartGhostInternal::kButtonBits;
        const bool    bCountFits = frameCount <= static_cast<uint64>( reader.getBitsRemaining() / frameBits );
        if ( reader.hasOverflowed() || bCountFits == false || step <= 0.0f )
            return false;
        vector<KartGhostFrame> listFrame;
        listFrame.reserve( static_cast<size_t>( frameCount ) );
        for ( uint64 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            listFrame.push_back( readFrame( reader ) );
        if ( reader.hasOverflowed() )
            return false;
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
            (void)_pTrack->applyBoostPad( _motor, _lastBoostPad );
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
