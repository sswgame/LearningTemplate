#include "pch.h"

#include "GameFramework/Base/Character/Control/Intent/ControlIntent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

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

            [[nodiscard]] static bool writeValue( BitWriter& writer, float32 value, float32 stepsPerUnit, int32 maxStep )
            {
                return writer.writeInt( computeStep( value, stepsPerUnit, maxStep ), -maxStep, maxStep );
            }

            /** @brief 칸 하나를 읽어 값으로 바꿉니다. 실패하면 @p outValue 는 그대로입니다. */
            [[nodiscard]] static bool readValue( BitReader& reader, float32 stepsPerUnit, int32 maxStep, float32& outValue )
            {
                int32 step{ 0 };
                if ( reader.readInt( -maxStep, maxStep, step ) == false )
                    return false;
                outValue = computeValue( step, stepsPerUnit );
                return true;
            }

            /** @brief 이동 칸 둘이 길이 1 안(칸으로 x² + y² ≤ 127²)인가. 축마다 ±127 만 보면 대각선이 √2 까지 나온다. */
            static bool isMoveStepInside( int32 stepX, int32 stepY ) { return stepX * stepX + stepY * stepY <= kAxisMaxStep * kAxisMaxStep; }

            /** @brief 이동 두 축을 칸으로 바꿉니다. 길면 길이 1 로 줄이고, 반올림이 원 밖으로 밀면(0.7071 → 90 칸 둘) 0 쪽으로 버린다. */
            static void computeMoveSteps( const float2& move, int32& outStepX, int32& outStepY )
            {
                float2        clamped = move;
                const float32 length  = MathUtil::sqrt( move._x * move._x + move._y * move._y );
                if ( length > 1.0f )
                    clamped = float2{ move._x / length, move._y / length };
                outStepX = computeStep( clamped._x, ControlIntent::kAxisSteps, kAxisMaxStep );
                outStepY = computeStep( clamped._y, ControlIntent::kAxisSteps, kAxisMaxStep );
                if ( isMoveStepInside( outStepX, outStepY ) )
                    return;
                // 0 쪽으로 버린 칸은 길이가 늘지 않으므로 길이 1 이하 입력이면 늘 원 안이다.
                outStepX = static_cast<int32>( clamped._x * ControlIntent::kAxisSteps );
                outStepY = static_cast<int32>( clamped._y * ControlIntent::kAxisSteps );
            }

            /** @brief 이동 두 칸을 읽습니다. 축마다 범위 밖이거나 길이가 1 을 넘으면 false 이고 @p outMove 는 그대로입니다. */
            [[nodiscard]] static bool readMove( BitReader& reader, float2& outMove )
            {
                int32 stepX{ 0 };
                int32 stepY{ 0 };
                if ( reader.readInt( -kAxisMaxStep, kAxisMaxStep, stepX ) == false || reader.readInt( -kAxisMaxStep, kAxisMaxStep, stepY ) == false )
                    return false;
                if ( isMoveStepInside( stepX, stepY ) == false )
                    return false;
                outMove = float2{ computeValue( stepX, ControlIntent::kAxisSteps ), computeValue( stepY, ControlIntent::kAxisSteps ) };
                return true;
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

    void ControlIntent::setTriggerLook( float32 yaw, float32 pitch )
    {
        _bTriggerLook = true;
        _triggerYaw   = MathUtil::wrapAngle( yaw );
        _triggerPitch = pitch;
    }

    bool ControlIntent::write( BitWriter& writer ) const
    {
        using Internal = ControlIntentInternal;
        // 칸은 `computeStep` 이 ±maxStep 으로 자른 뒤라 범위 밖이 없다. 그래도 실패하면 스트림이 어긋나므로 멈춘다.
        int32 moveStepX{ 0 };
        int32 moveStepY{ 0 };
        Internal::computeMoveSteps( _move, moveStepX, moveStepY );
        bool bWritten = writer.writeInt( moveStepX, -Internal::kAxisMaxStep, Internal::kAxisMaxStep ) &&
                        writer.writeInt( moveStepY, -Internal::kAxisMaxStep, Internal::kAxisMaxStep ) &&
                        Internal::writeValue( writer, _moveUp, kAxisSteps, Internal::kAxisMaxStep ) &&
                        Internal::writeValue( writer, MathUtil::wrapAngle( _controlYaw ), kAngleStepsPerRadian, Internal::kYawMaxStep ) &&
                        Internal::writeValue( writer, _controlPitch, kAngleStepsPerRadian, Internal::kPitchMaxStep );
        for ( const float32 analog : _arrAnalog )
        {
            bWritten = bWritten && Internal::writeValue( writer, analog, kAxisSteps, Internal::kAxisMaxStep );
        }
        if ( bWritten == false )
            return false;
        writer.writeVarUint( _buttonDown );
        writer.writeVarUint( _buttonTriggered );
        writer.writeBool( _bTriggerLook );
        if ( _bTriggerLook == false )
            return true;
        return Internal::writeValue( writer, MathUtil::wrapAngle( _triggerYaw ), kAngleStepsPerRadian, Internal::kYawMaxStep ) &&
               Internal::writeValue( writer, _triggerPitch, kAngleStepsPerRadian, Internal::kPitchMaxStep );
    }

    bool ControlIntent::read( BitReader& reader )
    {
        using Internal = ControlIntentInternal;
        bool bRead     = Internal::readMove( reader, _move ) &&
                     Internal::readValue( reader, kAxisSteps, Internal::kAxisMaxStep, _moveUp ) &&
                     Internal::readValue( reader, kAngleStepsPerRadian, Internal::kYawMaxStep, _controlYaw ) &&
                     Internal::readValue( reader, kAngleStepsPerRadian, Internal::kPitchMaxStep, _controlPitch );
        for ( float32& analog : _arrAnalog )
        {
            bRead = bRead && Internal::readValue( reader, kAxisSteps, Internal::kAxisMaxStep, analog );
        }
        _buttonDown      = reader.readVarUint32();
        _buttonTriggered = reader.readVarUint32();
        _bTriggerLook    = reader.readBool();
        _triggerYaw      = 0.0f;
        _triggerPitch    = 0.0f;
        if ( _bTriggerLook )
        {
            bRead = bRead && Internal::readValue( reader, kAngleStepsPerRadian, Internal::kYawMaxStep, _triggerYaw ) &&
                    Internal::readValue( reader, kAngleStepsPerRadian, Internal::kPitchMaxStep, _triggerPitch );
        }
        if ( bRead == false || reader.hasOverflowed() )
        {
            *this = ControlIntent{};
            return false;
        }
        return true;
    }

    void ControlIntent::quantize()
    {
        using Internal = ControlIntentInternal;
        int32 moveStepX{ 0 };
        int32 moveStepY{ 0 };
        Internal::computeMoveSteps( _move, moveStepX, moveStepY );
        _move         = float2{ Internal::computeValue( moveStepX, kAxisSteps ), Internal::computeValue( moveStepY, kAxisSteps ) };
        _moveUp       = Internal::quantizeValue( _moveUp, kAxisSteps, Internal::kAxisMaxStep );
        _controlYaw   = Internal::quantizeValue( MathUtil::wrapAngle( _controlYaw ), kAngleStepsPerRadian, Internal::kYawMaxStep );
        _controlPitch = Internal::quantizeValue( _controlPitch, kAngleStepsPerRadian, Internal::kPitchMaxStep );
        for ( float32& analog : _arrAnalog )
        {
            analog = Internal::quantizeValue( analog, kAxisSteps, Internal::kAxisMaxStep );
        }
        _triggerYaw   = _bTriggerLook ? Internal::quantizeValue( MathUtil::wrapAngle( _triggerYaw ), kAngleStepsPerRadian, Internal::kYawMaxStep ) : 0.0f;
        _triggerPitch = _bTriggerLook ? Internal::quantizeValue( _triggerPitch, kAngleStepsPerRadian, Internal::kPitchMaxStep ) : 0.0f;
    }

    bool ControlIntent::operator==( const ControlIntent& other ) const
    {
        for ( int32 analogIndex = 0; analogIndex < kAnalogCount; ++analogIndex )
        {
            if ( _arrAnalog[analogIndex] != other._arrAnalog[analogIndex] )
                return false;
        }
        return _move._x == other._move._x && _move._y == other._move._y && _moveUp == other._moveUp && _controlYaw == other._controlYaw &&
               _controlPitch == other._controlPitch && _buttonDown == other._buttonDown && _buttonTriggered == other._buttonTriggered &&
               _bTriggerLook == other._bTriggerLook && _triggerYaw == other._triggerYaw && _triggerPitch == other._triggerPitch;
    }
} // namespace sw
