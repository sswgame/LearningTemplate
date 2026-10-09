#include "pch.h"

#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Foundation/Utility/Math/OrientationUtil.h"

namespace sw
{
    namespace
    {
        struct ControlIntentInternal
        {
            static constexpr int32 kAxisMaxStep  = 127;   ///< `ControlIntent::kAxisSteps` 의 정수 — 축 −1..1
            static constexpr int32 kYawMaxStep   = 31416; ///< π × `kAngleStepsPerRadian` 을 올림 — 요 −π..π
            static constexpr int32 kPitchMaxStep = 15708; ///< π/2 × `kAngleStepsPerRadian` 을 올림 — 피치 −π/2..π/2

            /** @brief 값을 0 중심 정수 칸으로 바꿉니다(가까운 칸, ±@p maxStep 에서 자름). `write` 와 `quantize` 가 같은 칸을 쓴다. */
            static int32 computeStep( float32 value, float32 stepsPerUnit, int32 maxStep )
            {
                const int32 step = static_cast<int32>( MathUtil::round( value * stepsPerUnit ) );
                return MathUtil::clamp( step, -maxStep, maxStep );
            }

            static float32 computeValue( int32 step, float32 stepsPerUnit ) { return static_cast<float32>( step ) / stepsPerUnit; }

            static float32 quantizeValue( float32 value, float32 stepsPerUnit, int32 maxStep )
            {
                return computeValue( computeStep( value, stepsPerUnit, maxStep ), stepsPerUnit );
            }

            static void writeValue( BitWriter& writer, float32 value, float32 stepsPerUnit, int32 maxStep )
            {
                writer.writeInt( computeStep( value, stepsPerUnit, maxStep ), -maxStep, maxStep );
            }

            static float32 readValue( BitReader& reader, float32 stepsPerUnit, int32 maxStep )
            {
                return computeValue( reader.readInt( -maxStep, maxStep ), stepsPerUnit );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void ControlIntent::setButton( int32 buttonIndex, bool bDown, bool bTriggered )
    {
        if ( buttonIndex < 0 || kButtonCount <= buttonIndex )
            return;
        const uint32 bit = 1u << buttonIndex;
        _buttonDown      = bDown ? ( _buttonDown | bit ) : ( _buttonDown & ~bit );
        _buttonTriggered = bTriggered ? ( _buttonTriggered | bit ) : ( _buttonTriggered & ~bit );
    }

    float3 ControlIntent::computeWorldMove() const
    {
        // 요 0 이면 앞 +Z · 오른쪽 +X, 요 90° 면 앞 +X · 오른쪽 −Z(`FirstPersonLook` 과 같다).
        const float32 sinYaw = MathUtil::sin( _controlYaw );
        const float32 cosYaw = MathUtil::cos( _controlYaw );
        return float3{ _move._x * cosYaw + _move._y * sinYaw, _moveUp, -_move._x * sinYaw + _move._y * cosYaw };
    }

    void ControlIntent::setWorldMove( const float3& worldDirection )
    {
        const float32 sinYaw = MathUtil::sin( _controlYaw );
        const float32 cosYaw = MathUtil::cos( _controlYaw );
        float2        move{ worldDirection._x * cosYaw - worldDirection._z * sinYaw, worldDirection._x * sinYaw + worldDirection._z * cosYaw };
        const float32 length = MathUtil::sqrt( move._x * move._x + move._y * move._y );
        if ( length > 1.0f )
            move = float2{ move._x / length, move._y / length };
        _move = move;
    }

    void ControlIntent::write( BitWriter& writer ) const
    {
        using Internal = ControlIntentInternal;
        Internal::writeValue( writer, _move._x, kAxisSteps, Internal::kAxisMaxStep );
        Internal::writeValue( writer, _move._y, kAxisSteps, Internal::kAxisMaxStep );
        Internal::writeValue( writer, _moveUp, kAxisSteps, Internal::kAxisMaxStep );
        Internal::writeValue( writer, MathUtil::wrapAngle( _controlYaw ), kAngleStepsPerRadian, Internal::kYawMaxStep );
        Internal::writeValue( writer, _controlPitch, kAngleStepsPerRadian, Internal::kPitchMaxStep );
        for ( const float32 analog : _arrAnalog )
        {
            Internal::writeValue( writer, analog, kAxisSteps, Internal::kAxisMaxStep );
        }
        writer.writeVarUint( _buttonDown );
        writer.writeVarUint( _buttonTriggered );
    }

    bool ControlIntent::read( BitReader& reader )
    {
        using Internal = ControlIntentInternal;
        _move._x       = Internal::readValue( reader, kAxisSteps, Internal::kAxisMaxStep );
        _move._y       = Internal::readValue( reader, kAxisSteps, Internal::kAxisMaxStep );
        _moveUp        = Internal::readValue( reader, kAxisSteps, Internal::kAxisMaxStep );
        _controlYaw    = Internal::readValue( reader, kAngleStepsPerRadian, Internal::kYawMaxStep );
        _controlPitch  = Internal::readValue( reader, kAngleStepsPerRadian, Internal::kPitchMaxStep );
        for ( float32& analog : _arrAnalog )
        {
            analog = Internal::readValue( reader, kAxisSteps, Internal::kAxisMaxStep );
        }
        _buttonDown      = static_cast<uint32>( reader.readVarUint() );
        _buttonTriggered = static_cast<uint32>( reader.readVarUint() );
        if ( reader.hasOverflowed() )
        {
            *this = ControlIntent{};
            return false;
        }
        return true;
    }

    void ControlIntent::quantize()
    {
        using Internal = ControlIntentInternal;
        _move._x       = Internal::quantizeValue( _move._x, kAxisSteps, Internal::kAxisMaxStep );
        _move._y       = Internal::quantizeValue( _move._y, kAxisSteps, Internal::kAxisMaxStep );
        _moveUp        = Internal::quantizeValue( _moveUp, kAxisSteps, Internal::kAxisMaxStep );
        _controlYaw    = Internal::quantizeValue( MathUtil::wrapAngle( _controlYaw ), kAngleStepsPerRadian, Internal::kYawMaxStep );
        _controlPitch  = Internal::quantizeValue( _controlPitch, kAngleStepsPerRadian, Internal::kPitchMaxStep );
        for ( float32& analog : _arrAnalog )
        {
            analog = Internal::quantizeValue( analog, kAxisSteps, Internal::kAxisMaxStep );
        }
    }

    bool ControlIntent::operator==( const ControlIntent& other ) const
    {
        for ( int32 analogIndex = 0; analogIndex < kAnalogCount; ++analogIndex )
        {
            if ( _arrAnalog[analogIndex] != other._arrAnalog[analogIndex] )
                return false;
        }
        return _move._x == other._move._x && _move._y == other._move._y && _moveUp == other._moveUp && _controlYaw == other._controlYaw &&
               _controlPitch == other._controlPitch && _buttonDown == other._buttonDown && _buttonTriggered == other._buttonTriggered;
    }
} // namespace sw
