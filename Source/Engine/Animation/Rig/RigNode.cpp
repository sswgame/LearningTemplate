#include "pch.h"

#include "Engine/Animation/Rig/RigNode.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Animation/Rig/RigIkSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Serialization/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "Rig" );

    namespace
    {
        struct RigNodeInternal
        {
            /** @brief "key '<키>' <설명>" 꼴의 오류 문장입니다. */
            static string makeKeyMessage( string_view key, string_view what )
            {
                string message = "key '";
                message += key;
                message += "' ";
                message += what;
                return message;
            }
        };
    } // namespace

    RigJsonReader::RigJsonReader( const JsonValue& object, string_view context )
        : _object{ object }
        , _listUsedKey{}
        , _context{ context }
        , _bOk{ SW_TRUE }
    {
        if ( _object.isObject() == false )
            fail( "expected an object" );
    }

    void RigJsonReader::fail( string_view message )
    {
        SW_LOG_ERROR( "Rig '%#': %#", _context.c_str(), message );
        _bOk = SW_FALSE;
    }

    JsonValue RigJsonReader::findMember( string_view key, bool bRequired )
    {
        _listUsedKey.push_back( string{ key } );
        if ( _object.isObject() == false )
            return JsonValue{};
        const JsonValue value = _object.get( key, false );
        if ( value.isValid() == false && bRequired )
        {
            string message = "missing required key '";
            message += key;
            message += "'";
            fail( message );
        }
        return value;
    }

    bool RigJsonReader::readName( string_view key, hashed_string& outValue, bool bRequired )
    {
        const JsonValue value = findMember( key, bRequired );
        if ( value.isValid() == false )
            return bRequired == false;
        if ( value.isString() == false || value.asString().empty() )
        {
            fail( RigNodeInternal::makeKeyMessage( key, "must be a non-empty string" ) );
            return false;
        }
        outValue = hashed_string( value.asString() );
        return true;
    }

    bool RigJsonReader::readFloat( string_view key, float32& outValue, bool bRequired )
    {
        const JsonValue value = findMember( key, bRequired );
        if ( value.isValid() == false )
            return bRequired == false;
        if ( value.isNumber() == false )
        {
            fail( RigNodeInternal::makeKeyMessage( key, "must be a number" ) );
            return false;
        }
        outValue = static_cast<float32>( value.asFloat() );
        return true;
    }

    bool RigJsonReader::readUint( string_view key, uint32& outValue, bool bRequired )
    {
        float32 number = 0.0f;
        if ( readFloat( key, number, bRequired ) == false )
            return false;
        if ( _object.get( key, false ).isValid() == false )
            return true;
        if ( number < 0.0f || MathUtil::floor( number ) != number )
        {
            fail( RigNodeInternal::makeKeyMessage( key, "must be a non-negative integer" ) );
            return false;
        }
        outValue = static_cast<uint32>( number );
        return true;
    }

    bool RigJsonReader::readBool( string_view key, bool& outValue, bool bRequired )
    {
        const JsonValue value = findMember( key, bRequired );
        if ( value.isValid() == false )
            return bRequired == false;
        if ( value.isBool() == false )
        {
            fail( RigNodeInternal::makeKeyMessage( key, "must be true or false" ) );
            return false;
        }
        outValue = value.asBool();
        return true;
    }

    bool RigJsonReader::readFloat3( string_view key, float3& outValue, bool bRequired )
    {
        const JsonValue value = findMember( key, bRequired );
        if ( value.isValid() == false )
            return bRequired == false;
        float32 arrValue[3]{};
        if ( AnimJsonUtil::readFloats( value, arrValue, 3 ) == false )
        {
            fail( RigNodeInternal::makeKeyMessage( key, "must be an array of 3 numbers" ) );
            return false;
        }
        outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
        return true;
    }

    bool RigJsonReader::readRotationDegrees( string_view key, quaternion& outValue, bool bRequired )
    {
        float3 degrees{};
        if ( readFloat3( key, degrees, bRequired ) == false )
            return false;
        outValue = quaternion::createFromYawPitchRoll( degrees * MathUtil::kDegreeToRadian );
        return true;
    }

    bool RigJsonReader::readNameList( string_view key, vector<hashed_string>& outListValue, bool bRequired )
    {
        const JsonValue value = findMember( key, bRequired );
        if ( value.isValid() == false )
            return bRequired == false;
        if ( value.isArray() == false || value.size() == 0 )
        {
            fail( RigNodeInternal::makeKeyMessage( key, "must be a non-empty array of names" ) );
            return false;
        }
        outListValue.clear();
        for ( size_t index = 0; index < value.size(); ++index )
        {
            const JsonValue element = value.at( index );
            if ( element.isString() == false || element.asString().empty() )
            {
                fail( RigNodeInternal::makeKeyMessage( key, "must hold only non-empty strings" ) );
                return false;
            }
            outListValue.push_back( hashed_string( element.asString() ) );
        }
        return true;
    }

    JsonValue RigJsonReader::readArray( string_view key, bool bRequired )
    {
        const JsonValue value = findMember( key, bRequired );
        if ( value.isValid() && value.isArray() == false )
        {
            fail( RigNodeInternal::makeKeyMessage( key, "must be an array" ) );
            return JsonValue{};
        }
        return value;
    }

    JsonValue RigJsonReader::readObject( string_view key, bool bRequired )
    {
        const JsonValue value = findMember( key, bRequired );
        if ( value.isValid() && value.isObject() == false )
        {
            fail( RigNodeInternal::makeKeyMessage( key, "must be an object" ) );
            return JsonValue{};
        }
        return value;
    }

    bool RigJsonReader::readChoice( string_view key, const utf8* const* ppChoice, uint32 choiceCount, uint32& outIndex, bool bRequired )
    {
        const JsonValue value = findMember( key, bRequired );
        if ( value.isValid() == false )
            return bRequired == false;
        const string text = value.isString() ? value.asString() : string{};
        for ( uint32 index = 0; index < choiceCount; ++index )
        {
            if ( text == ppChoice[index] )
            {
                outIndex = index;
                return true;
            }
        }
        fail( RigNodeInternal::makeKeyMessage( key, "has unknown value '" ) + text + "'" );
        return false;
    }

    bool RigJsonReader::finish()
    {
        if ( _object.isObject() )
        {
            for ( const string& member : _object.getMemberNames() )
            {
                if ( std::find( _listUsedKey.begin(), _listUsedKey.end(), member ) == _listUsedKey.end() )
                {
                    string message = "unknown key '";
                    message += member;
                    message += "'";
                    fail( message );
                }
            }
        }
        return isOk();
    }

    bool RigBindContext::findBone( const hashed_string& name, uint32& outBone ) const
    {
        const int32 boneIndex = ( _pSkeleton != nullptr ) ? _pSkeleton->findBoneIndex( name ) : -1;
        if ( boneIndex < 0 )
        {
            SW_LOG_ERROR( "Rig '%#': unknown bone '%#'", _label, name.c_str() );
            return false;
        }
        outBone = static_cast<uint32>( boneIndex );
        return true;
    }

    bool RigBindContext::findTarget( const hashed_string& name, uint32& outTarget ) const
    {
        const int32 targetIndex = ( _pInstance != nullptr ) ? _pInstance->findTargetIndex( name ) : -1;
        if ( targetIndex < 0 )
        {
            SW_LOG_ERROR( "Rig '%#': unknown target '%#'", _label, name.c_str() );
            return false;
        }
        outTarget = static_cast<uint32>( targetIndex );
        return true;
    }

    bool RigBindContext::findBones( const vector<hashed_string>& listName, vector<uint32>& outListBone, bool bChain ) const
    {
        outListBone.clear();
        for ( const hashed_string& name : listName )
        {
            uint32 bone = 0;
            if ( findBone( name, bone ) == false )
                return false;
            outListBone.push_back( bone );
        }
        if ( bChain == false )
            return true;
        for ( size_t index = 1; index < outListBone.size(); ++index )
        {
            // 사슬은 각 본이 다음 본의 조상이어야 한다(사이에 본이 끼어도 된다 — 팔뚝 → 손목 → 손).
            int32 current = static_cast<int32>( outListBone[index] );
            while ( current >= 0 && current != static_cast<int32>( outListBone[index - 1] ) )
            {
                current = _pSkeleton->getBone( static_cast<uint32>( current ) )._parentIndex;
            }
            if ( current < 0 )
            {
                SW_LOG_ERROR( "Rig '%#': chain bone '%#' is not a descendant of '%#'", _label, listName[index].c_str(), listName[index - 1].c_str() );
                return false;
            }
        }
        return true;
    }

    RigNode::RigNode()
        : _name{}
        , _weightCurve{}
        , _weightSlot{}
        , _weight{ 1.0f }
    {
    }

    bool RigNode::parseCommon( RigJsonReader& reader )
    {
        const bool bOk = reader.readName( "name", _name, true ) && reader.readFloat( "weight", _weight, false ) &&
                         reader.readName( "weight_curve", _weightCurve, false ) && reader.readName( "weight_slot", _weightSlot, false );
        if ( bOk && ( _weight < 0.0f || 1.0f < _weight ) )
        {
            reader.fail( "'weight' must be in [0, 1]" );
            return false;
        }
        return bOk;
    }
} // namespace sw
