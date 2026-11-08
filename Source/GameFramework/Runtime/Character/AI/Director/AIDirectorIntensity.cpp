#include "pch.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirectorIntensity.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/AI/Director/AIDirectorProfile.h"

namespace sw
{
    namespace
    {
        struct AIDirectorIntensityInternal
        {
            /** @brief 싸움을 한 번도 하지 않았을 때의 "싸움 뒤 지난 시간" 입니다 — 처음부터 식은 상태로 시작한다. */
            static constexpr float32 kNeverFought = 1.0e6f;

            static float32 clampContribution( const AIDirectorSignalDef& signal, float32 value ) { return signal._max >= 0.0f ? MathUtil::min( signal._max, value ) : value; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AIDirectorIntensityModel::AIDirectorIntensityModel()
        : _listSignalValue{}
        , _pDef{ nullptr }
        , _stress{ 0.0f }
        , _calmSeconds{ AIDirectorIntensityInternal::kNeverFought }
    {
    }

    void AIDirectorIntensityModel::initialize( const AIDirectorIntensityDef* pDef )
    {
        _pDef = pDef;
        reset();
    }

    void AIDirectorIntensityModel::reset()
    {
        _listSignalValue.assign( _pDef != nullptr ? _pDef->_listSignal.size() : 0, 0.0f );
        _stress      = 0.0f;
        _calmSeconds = AIDirectorIntensityInternal::kNeverFought;
    }

    optional<int32> AIDirectorIntensityModel::findSignalIndex( const hashed_string& signalID ) const
    {
        if ( _pDef == nullptr )
            return nullopt;
        for ( size_t index = 0; index < _pDef->_listSignal.size(); ++index )
        {
            if ( _pDef->_listSignal[index]._id == signalID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    void AIDirectorIntensityModel::recordCombat()
    {
        _calmSeconds = 0.0f;
    }

    bool AIDirectorIntensityModel::addSignal( const hashed_string& signalID, float32 amount )
    {
        const optional<int32> foundSignal = findSignalIndex( signalID );
        if ( foundSignal.has_value() == false )
            return false;
        const int32                signalIndex = *foundSignal;
        const AIDirectorSignalDef& signal      = _pDef->_listSignal[static_cast<size_t>( signalIndex )];
        if ( signal._kind != AIDirectorSignalKind::Impulse )
            return false;
        _listSignalValue[static_cast<size_t>( signalIndex )] = amount;
        const float32 added                                  = AIDirectorIntensityInternal::clampContribution( signal, MathUtil::max( 0.0f, amount ) * signal._scale );
        _stress                                              = MathUtil::clamp( _stress + added, 0.0f, _pDef->_max );
        if ( signal._bCombat )
            recordCombat();
        return true;
    }

    bool AIDirectorIntensityModel::setSignal( const hashed_string& signalID, float32 value )
    {
        const optional<int32> foundSignal = findSignalIndex( signalID );
        if ( foundSignal.has_value() == false )
            return false;
        const int32                signalIndex = *foundSignal;
        const AIDirectorSignalDef& signal      = _pDef->_listSignal[static_cast<size_t>( signalIndex )];
        if ( signal._kind == AIDirectorSignalKind::Impulse )
            return false;
        _listSignalValue[static_cast<size_t>( signalIndex )] = value;
        return true;
    }

    void AIDirectorIntensityModel::update( float32 deltaTime )
    {
        if ( _pDef == nullptr || deltaTime <= 0.0f )
            return;
        bool bCombatNow = false;
        for ( size_t index = 0; index < _pDef->_listSignal.size(); ++index )
        {
            const AIDirectorSignalDef& signal = _pDef->_listSignal[index];
            if ( signal._kind != AIDirectorSignalKind::Rate || _listSignalValue[index] <= 0.0f )
                continue;
            const float32 perSecond = AIDirectorIntensityInternal::clampContribution( signal, _listSignalValue[index] * signal._scale );
            _stress += perSecond * deltaTime;
            bCombatNow = bCombatNow || signal._bCombat;
        }
        _calmSeconds = bCombatNow ? 0.0f : _calmSeconds + deltaTime;
        if ( _calmSeconds > _pDef->_decayDelay )
        {
            // 식기는 지연이 끝난 순간부터 센다 — 이번 프레임에 지연을 넘었으면 넘은 만큼만 식는다.
            const float32 decayTime = MathUtil::min( deltaTime, _calmSeconds - _pDef->_decayDelay );
            _stress -= _pDef->_decayPerSecond * decayTime;
        }
        _stress = MathUtil::clamp( _stress, 0.0f, _pDef->_max );
    }

    float32 AIDirectorIntensityModel::computeLevelFloor() const
    {
        float32 floor = 0.0f;
        for ( size_t index = 0; index < _pDef->_listSignal.size(); ++index )
        {
            const AIDirectorSignalDef& signal = _pDef->_listSignal[index];
            if ( signal._kind == AIDirectorSignalKind::Level )
                floor += AIDirectorIntensityInternal::clampContribution( signal, MathUtil::max( 0.0f, _listSignalValue[index] ) * signal._scale );
        }
        return floor;
    }

    float32 AIDirectorIntensityModel::getIntensity() const
    {
        if ( _pDef == nullptr )
            return 0.0f;
        return MathUtil::min( _pDef->_max, MathUtil::max( _stress, computeLevelFloor() ) );
    }

    float32 AIDirectorIntensityModel::getSignal( const hashed_string& signalID ) const
    {
        const optional<int32> signalIndex = findSignalIndex( signalID );
        return signalIndex.has_value() ? _listSignalValue[static_cast<size_t>( *signalIndex )] : 0.0f;
    }

    void AIDirectorIntensityModel::serializeState( StateIO& io )
    {
        io.value( _stress );
        io.value( _calmSeconds );
        io.expect( static_cast<uint32>( _listSignalValue.size() ) );
        if ( io.isOk() == false )
            return;
        for ( float32& value : _listSignalValue )
        {
            io.value( value );
        }
        // 스트레스는 0 ~ max, 평온 시간은 음수가 아니다 - 쓰는 쪽은 이 범위만 쓴다.
        const float32 maxStress = _pDef != nullptr ? _pDef->_max : 0.0f;
        io.check( 0.0f <= _stress && _stress <= maxStress && 0.0f <= _calmSeconds );
    }

} // namespace sw
