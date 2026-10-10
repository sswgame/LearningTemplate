#include "pch.h"

#include "GameFramework/Base/Actor/AI/Blackboard.h"

namespace sw
{
    float32 BlackboardValue::computeNumeric() const
    {
        switch ( _type )
        {
            case BlackboardValueType::Float:
                return _float;
            case BlackboardValueType::Int:
            case BlackboardValueType::Bool:
                return static_cast<float32>( _int );
            case BlackboardValueType::Object:
                return _object != 0 ? 1.0f : 0.0f;
            case BlackboardValueType::Vector:
                return _vector.getLength();
            case BlackboardValueType::None:
                return 0.0f;
        }
        return 0.0f;
    }

    Blackboard::Blackboard()
        : _mapValue{}
        , _revision{ 0 }
    {
    }

    BlackboardValue& Blackboard::acquireValue( const hashed_string& key, BlackboardValueType type )
    {
        BlackboardValue& value = _mapValue[key];
        value._type            = type;
        ++_revision;
        return value;
    }

    void Blackboard::setFloat( const hashed_string& key, float32 value )
    {
        acquireValue( key, BlackboardValueType::Float )._float = value;
    }

    void Blackboard::setInt( const hashed_string& key, int32 value )
    {
        acquireValue( key, BlackboardValueType::Int )._int = value;
    }

    void Blackboard::setBool( const hashed_string& key, bool bValue )
    {
        acquireValue( key, BlackboardValueType::Bool )._int = bValue ? 1 : 0;
    }

    void Blackboard::setVector( const hashed_string& key, const float3& value )
    {
        acquireValue( key, BlackboardValueType::Vector )._vector = value;
    }

    void Blackboard::setObject( const hashed_string& key, uint64 objectID )
    {
        acquireValue( key, BlackboardValueType::Object )._object = objectID;
    }

    void Blackboard::clearValue( const hashed_string& key )
    {
        if ( _mapValue.erase( key ) > 0 )
            ++_revision;
    }

    void Blackboard::clear()
    {
        _mapValue.clear();
        ++_revision;
    }

    bool Blackboard::isSet( const hashed_string& key ) const
    {
        const BlackboardValue* pValue = findValue( key );
        if ( pValue == nullptr )
            return false;
        // 오브젝트는 0(없음)이면, 참거짓은 false 면 "없다" 로 본다 — 언리얼 Is Set 과 같다.
        if ( pValue->_type == BlackboardValueType::Object )
            return pValue->_object != 0;
        if ( pValue->_type == BlackboardValueType::Bool )
            return pValue->_int != 0;
        return pValue->_type != BlackboardValueType::None;
    }

    float32 Blackboard::getFloat( const hashed_string& key, float32 fallback ) const
    {
        const BlackboardValue* pValue = findValue( key );
        return pValue != nullptr && pValue->_type != BlackboardValueType::None ? pValue->computeNumeric() : fallback;
    }

    int32 Blackboard::getInt( const hashed_string& key, int32 fallback ) const
    {
        const BlackboardValue* pValue = findValue( key );
        if ( pValue == nullptr )
            return fallback;
        if ( pValue->_type == BlackboardValueType::Int || pValue->_type == BlackboardValueType::Bool )
            return pValue->_int;
        return pValue->_type == BlackboardValueType::Float ? static_cast<int32>( pValue->_float ) : fallback;
    }

    bool Blackboard::getBool( const hashed_string& key, bool bFallback ) const
    {
        const BlackboardValue* pValue = findValue( key );
        return pValue != nullptr && pValue->_type != BlackboardValueType::None ? pValue->computeNumeric() != 0.0f : bFallback;
    }

    float3 Blackboard::getVector( const hashed_string& key, const float3& fallback ) const
    {
        const BlackboardValue* pValue = findValue( key );
        return pValue != nullptr && pValue->_type == BlackboardValueType::Vector ? pValue->_vector : fallback;
    }

    uint64 Blackboard::getObject( const hashed_string& key, uint64 fallback ) const
    {
        const BlackboardValue* pValue = findValue( key );
        return pValue != nullptr && pValue->_type == BlackboardValueType::Object ? pValue->_object : fallback;
    }

    const BlackboardValue* Blackboard::findValue( const hashed_string& key ) const
    {
        const auto mapIter = _mapValue.find( key );
        return mapIter != _mapValue.end() ? &mapIter->second : nullptr;
    }
} // namespace sw
