#include "pch.h"

#include "GameFramework/Runtime/Character/AI/Blackboard.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    float32 BlackboardValue::computeNumeric() const
    {
        switch ( _type )
        {
            case BlackboardValueKind::Float:
                return _float;
            case BlackboardValueKind::Int:
            case BlackboardValueKind::Bool:
                return static_cast<float32>( _int );
            case BlackboardValueKind::Object:
                return _object != 0 ? 1.0f : 0.0f;
            case BlackboardValueKind::Vector:
                return _vector.getLength();
            case BlackboardValueKind::None:
                return 0.0f;
        }
        return 0.0f;
    }

    string BlackboardValue::formatText() const
    {
        switch ( _type )
        {
            case BlackboardValueKind::Float:
                return to_string( _float );
            case BlackboardValueKind::Int:
                return to_string( _int );
            case BlackboardValueKind::Bool:
                return _int != 0 ? string( "true" ) : string( "false" );
            case BlackboardValueKind::Vector:
            {
                StringBuilder<constant::buffer::kMax128> text;
                text.appendFormat( "(%#, %#, %#)", _vector._x, _vector._y, _vector._z );
                return string( text.view() );
            }
            case BlackboardValueKind::Object:
            {
                StringBuilder<constant::buffer::kMax32> text;
                text.appendFormat( "#%#", _object );
                return string( text.view() );
            }
            case BlackboardValueKind::None:
                return string( "-" );
        }
        return string( "-" );
    }

    void Blackboard::collectKeys( vector<hashed_string>& outListKey ) const
    {
        outListKey.clear();
        for ( const auto& [key, value] : _mapValue )
        {
            (void)value;
            outListKey.push_back( key );
        }
        VectorUtil::sort( outListKey, []( const hashed_string& left, const hashed_string& right )
        { return string_view( left.c_str() ) < string_view( right.c_str() ); } );
    }

    Blackboard::Blackboard()
        : _mapValue{}
        , _revision{ 0 }
    {
    }

    BlackboardValue& Blackboard::acquireValue( const hashed_string& key, BlackboardValueKind type )
    {
        BlackboardValue& value = _mapValue[key];
        value._type            = type;
        ++_revision;
        return value;
    }

    void Blackboard::setFloat( const hashed_string& key, float32 value )
    {
        acquireValue( key, BlackboardValueKind::Float )._float = value;
    }

    void Blackboard::setInt( const hashed_string& key, int32 value )
    {
        acquireValue( key, BlackboardValueKind::Int )._int = value;
    }

    void Blackboard::setBool( const hashed_string& key, bool bValue )
    {
        acquireValue( key, BlackboardValueKind::Bool )._int = bValue ? 1 : 0;
    }

    void Blackboard::setVector( const hashed_string& key, const float3& value )
    {
        acquireValue( key, BlackboardValueKind::Vector )._vector = value;
    }

    void Blackboard::setObject( const hashed_string& key, uint64 objectID )
    {
        acquireValue( key, BlackboardValueKind::Object )._object = objectID;
    }

    bool Blackboard::copyValue( const hashed_string& from, const hashed_string& to )
    {
        const BlackboardValue* pSource = findValue( from );
        if ( pSource == nullptr || pSource->_type == BlackboardValueKind::None )
            return false;
        const BlackboardValue copied     = *pSource; // 대상 칸을 만들며 표가 다시 자리를 잡아도 원본을 잃지 않게 먼저 베낀다
        acquireValue( to, copied._type ) = copied;
        return true;
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
        if ( pValue->_type == BlackboardValueKind::Object )
            return pValue->_object != 0;
        if ( pValue->_type == BlackboardValueKind::Bool )
            return pValue->_int != 0;
        return pValue->_type != BlackboardValueKind::None;
    }

    optional<float32> Blackboard::getFloat( const hashed_string& key ) const
    {
        const BlackboardValue* pValue = findValue( key );
        if ( pValue == nullptr || pValue->_type == BlackboardValueKind::None )
            return nullopt;
        return pValue->computeNumeric();
    }

    optional<int32> Blackboard::getInt( const hashed_string& key ) const
    {
        const BlackboardValue* pValue = findValue( key );
        if ( pValue == nullptr )
            return nullopt;
        if ( pValue->_type == BlackboardValueKind::Int || pValue->_type == BlackboardValueKind::Bool )
            return pValue->_int;
        if ( pValue->_type == BlackboardValueKind::Float )
            return static_cast<int32>( pValue->_float );
        return nullopt;
    }

    optional<bool> Blackboard::getBool( const hashed_string& key ) const
    {
        const BlackboardValue* pValue = findValue( key );
        if ( pValue == nullptr || pValue->_type == BlackboardValueKind::None )
            return nullopt;
        return pValue->computeNumeric() != 0.0f;
    }

    optional<float3> Blackboard::getVector( const hashed_string& key ) const
    {
        const BlackboardValue* pValue = findValue( key );
        if ( pValue == nullptr || pValue->_type != BlackboardValueKind::Vector )
            return nullopt;
        return pValue->_vector;
    }

    optional<uint64> Blackboard::getObject( const hashed_string& key ) const
    {
        const BlackboardValue* pValue = findValue( key );
        if ( pValue == nullptr || pValue->_type != BlackboardValueKind::Object )
            return nullopt;
        return pValue->_object;
    }

    const BlackboardValue* Blackboard::findValue( const hashed_string& key ) const
    {
        const auto mapIter = _mapValue.find( key );
        return mapIter != _mapValue.end() ? &mapIter->second : nullptr;
    }
} // namespace sw
