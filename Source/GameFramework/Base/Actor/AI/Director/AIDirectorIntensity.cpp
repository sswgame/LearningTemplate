#include "pch.h"

#include "GameFramework/Base/Actor/AI/Director/AIDirectorIntensity.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/AI/Director/AIDirectorProfile.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

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

    int32 AIDirectorIntensityModel::findSignalIndex( const hashed_string& signalId ) const
    {
        if ( _pDef == nullptr )
            return -1;
        for ( size_t index = 0; index < _pDef->_listSignal.size(); ++index )
        {
            if ( _pDef->_listSignal[index]._id == signalId )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void AIDirectorIntensityModel::noteCombat()
    {
        _calmSeconds = 0.0f;
    }

    bool AIDirectorIntensityModel::addSignal( const hashed_string& signalId, float32 amount )
    {
        const int32 signalIndex = findSignalIndex( signalId );
        if ( signalIndex < 0 )
            return false;
        const AIDirectorSignalDef& signal = _pDef->_listSignal[static_cast<size_t>( signalIndex )];
        if ( signal._kind != AIDirectorSignalKind::Impulse )
            return false;
        _listSignalValue[static_cast<size_t>( signalIndex )] = amount;
        const float32 added                                  = AIDirectorIntensityInternal::clampContribution( signal, MathUtil::max( 0.0f, amount ) * signal._scale );
        _stress                                              = MathUtil::clamp( _stress + added, 0.0f, _pDef->_max );
        if ( signal._bCombat == SW_TRUE )
            noteCombat();
        return true;
    }

    bool AIDirectorIntensityModel::setSignal( const hashed_string& signalId, float32 value )
    {
        const int32 signalIndex = findSignalIndex( signalId );
        if ( signalIndex < 0 )
            return false;
        const AIDirectorSignalDef& signal = _pDef->_listSignal[static_cast<size_t>( signalIndex )];
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
            bCombatNow = bCombatNow || signal._bCombat == SW_TRUE;
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

    float32 AIDirectorIntensityModel::getSignal( const hashed_string& signalId ) const
    {
        const int32 signalIndex = findSignalIndex( signalId );
        return signalIndex >= 0 ? _listSignalValue[static_cast<size_t>( signalIndex )] : 0.0f;
    }

    void AIDirectorIntensityModel::writeState( Archive& outArchive ) const
    {
        outArchive << _stress;
        outArchive << _calmSeconds;
        outArchive << static_cast<uint32>( _listSignalValue.size() );
        for ( const float32 value : _listSignalValue )
        {
            outArchive << value;
        }
    }

    bool AIDirectorIntensityModel::readState( Archive& archive )
    {
        float32 stress      = 0.0f;
        float32 calmSeconds = 0.0f;
        uint32  signalCount = 0;
        archive >> stress;
        archive >> calmSeconds;
        if ( StateArchiveUtil::readCount( archive, 4, signalCount ) == false || signalCount != _listSignalValue.size() )
            return false;
        vector<float32> listSignalValue( signalCount, 0.0f );
        for ( float32& value : listSignalValue )
        {
            archive >> value;
        }
        if ( archive.isError() )
            return false;
        _stress          = stress;
        _calmSeconds     = calmSeconds;
        _listSignalValue = std::move( listSignalValue );
        return true;
    }
} // namespace sw
