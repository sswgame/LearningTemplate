#include "pch.h"

#include "Engine/Animation/Rig/RigNode.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "Rig" );

    namespace
    {
        /** @brief "key '<키>' <설명>" 형태의 오류 문장입니다. */
        string makeKeyMessage( string_view key, string_view what )
        {
            string message = "key '";
            message += key;
            message += "' ";
            message += what;
            return message;
        }

        /** @brief 목록 원소 요소의 이름입니다. */
        constexpr string_view kItemTag = "item";
    } // namespace

    size_t RigXMLList::size() const
    {
        size_t count{ 0 };
        for ( XMLNode item = _listNode.findChild(); item.isValid(); item = item.findNextSibling() )
        {
            ++count;
        }
        return count;
    }

    XMLNode RigXMLList::at( size_t index ) const
    {
        XMLNode item = _listNode.findChild();
        for ( size_t skipped = 0; item.isValid() && skipped < index; ++skipped )
        {
            item = item.findNextSibling();
        }
        return item;
    }

    RigXMLReader::RigXMLReader( const XMLNode& element, string_view context )
        : _element{ element }
        , _listUsedKey{}
        , _context{ context }
        , _bOk{ SW_TRUE }
    {
        if ( _element.isValid() == false )
            fail( "expected an element" );
    }

    void RigXMLReader::fail( string_view message )
    {
        SW_LOG_ERROR( "Rig '%#': %#", _context.c_str(), message );
        _bOk = SW_FALSE;
    }

    const utf8* RigXMLReader::readAttribute( string_view key, bool bRequired )
    {
        _listUsedKey.push_back( string{ key } );
        if ( _element.isValid() == false )
            return nullptr;
        const string      keyText{ key };
        const utf8* const pValue = _element.findAttribute( keyText.c_str(), false );
        if ( pValue == nullptr && bRequired )
        {
            string message = "missing required key '";
            message += key;
            message += "'";
            fail( message );
        }
        return pValue;
    }

    XMLNode RigXMLReader::readChild( string_view key, bool bRequired )
    {
        _listUsedKey.push_back( string{ key } );
        if ( _element.isValid() == false )
            return XMLNode{};
        const string  keyText{ key };
        const XMLNode child = _element.findChild( keyText.c_str(), false );
        if ( child.isValid() == false && bRequired )
        {
            string message = "missing required key '";
            message += key;
            message += "'";
            fail( message );
        }
        return child;
    }

    bool RigXMLReader::readName( string_view key, hashed_string& outValue, bool bRequired )
    {
        const utf8* const pValue = readAttribute( key, bRequired );
        if ( pValue == nullptr )
            return bRequired == false;
        if ( string_view{ pValue }.empty() )
        {
            fail( makeKeyMessage( key, "must be a non-empty string" ) );
            return false;
        }
        outValue = hashed_string( string_view{ pValue } );
        return true;
    }

    bool RigXMLReader::readFloat( string_view key, float32& outValue, bool bRequired )
    {
        const utf8* const pValue = readAttribute( key, bRequired );
        if ( pValue == nullptr )
            return bRequired == false;
        float32 number{ 0.0f };
        if ( StringUtil::parseFloat( string_view{ pValue }, number ) == false )
        {
            fail( makeKeyMessage( key, "must be a number" ) );
            return false;
        }
        outValue = number;
        return true;
    }

    bool RigXMLReader::readUint( string_view key, uint32& outValue, bool bRequired )
    {
        float32 number = 0.0f;
        if ( readFloat( key, number, bRequired ) == false )
            return false;
        const string keyText{ key };
        if ( _element.findAttribute( keyText.c_str(), false ) == nullptr )
            return true;
        if ( number < 0.0f || MathUtil::floor( number ) != number )
        {
            fail( makeKeyMessage( key, "must be a non-negative integer" ) );
            return false;
        }
        outValue = static_cast<uint32>( number );
        return true;
    }

    bool RigXMLReader::readBool( string_view key, bool& outValue, bool bRequired )
    {
        const utf8* const pValue = readAttribute( key, bRequired );
        if ( pValue == nullptr )
            return bRequired == false;
        const string_view text{ pValue };
        if ( text != "true" && text != "false" )
        {
            fail( makeKeyMessage( key, "must be true or false" ) );
            return false;
        }
        outValue = text == "true";
        return true;
    }

    bool RigXMLReader::readFloat3( string_view key, float3& outValue, bool bRequired )
    {
        const utf8* const pValue = readAttribute( key, bRequired );
        if ( pValue == nullptr )
            return bRequired == false;
        float32 arrValue[3]{};
        if ( XMLReadUtil::readFloatList( string_view{ pValue }, arrValue, 3 ) == false )
        {
            fail( makeKeyMessage( key, "must be 3 numbers (x,y,z)" ) );
            return false;
        }
        outValue = float3{ arrValue[0], arrValue[1], arrValue[2] };
        return true;
    }

    bool RigXMLReader::readRotationDegrees( string_view key, quaternion& outValue, bool bRequired )
    {
        float3 degrees{};
        if ( readFloat3( key, degrees, bRequired ) == false )
            return false;
        outValue = quaternion::makeFromYawPitchRoll( degrees * MathUtil::kDegreeToRadian );
        return true;
    }

    bool RigXMLReader::readNameList( string_view key, vector<hashed_string>& outListValue, bool bRequired )
    {
        const XMLNode listNode = readChild( key, bRequired );
        if ( listNode.isValid() == false )
            return bRequired == false;
        if ( listNode.findChild().isValid() == false )
        {
            fail( makeKeyMessage( key, "must be a non-empty list of names" ) );
            return false;
        }
        outListValue.clear();
        for ( XMLNode element = listNode.findChild(); element.isValid(); element = element.findNextSibling() )
        {
            if ( string_view{ element.getName() } != kItemTag || string_view{ element.getText() }.empty() )
            {
                fail( makeKeyMessage( key, "must hold only non-empty <item> names" ) );
                return false;
            }
            outListValue.push_back( hashed_string( string_view{ element.getText() } ) );
        }
        return true;
    }

    RigXMLList RigXMLReader::readArray( string_view key, bool bRequired )
    {
        const XMLNode listNode = readChild( key, bRequired );
        for ( XMLNode element = listNode.findChild(); element.isValid(); element = element.findNextSibling() )
        {
            if ( string_view{ element.getName() } != kItemTag )
            {
                fail( makeKeyMessage( key, "must hold only <item> elements" ) );
                return RigXMLList{};
            }
        }
        return RigXMLList{ listNode };
    }

    XMLNode RigXMLReader::readObject( string_view key, bool bRequired )
    {
        return readChild( key, bRequired );
    }

    bool RigXMLReader::readChoice( string_view key, const utf8* const* ppChoice, uint32 choiceCount, uint32& outIndex, bool bRequired )
    {
        const utf8* const pValue = readAttribute( key, bRequired );
        if ( pValue == nullptr )
            return bRequired == false;
        const string text{ pValue };
        for ( uint32 index = 0; index < choiceCount; ++index )
        {
            if ( text == ppChoice[index] )
            {
                outIndex = index;
                return true;
            }
        }
        fail( makeKeyMessage( key, "has unknown value '" ) + text + "'" );
        return false;
    }

    bool RigXMLReader::finish()
    {
        if ( _element.isValid() )
        {
            for ( XMLAttribute attribute = _element.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
            {
                if ( VectorUtil::contains( _listUsedKey, string{ attribute.getName() } ) == false )
                    fail( string{ "unknown key '" } + attribute.getName() + "'" );
            }
            for ( XMLNode child = _element.findChild(); child.isValid(); child = child.findNextSibling() )
            {
                if ( VectorUtil::contains( _listUsedKey, string{ child.getName() } ) == false )
                    fail( string{ "unknown key '" } + child.getName() + "'" );
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
            // 사슬은 각 본이 다음 본의 조상이어야 한다(사이에 본이 끼어도 된다. 예를 들어 팔뚝, 손목, 손 순서다).
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
        , _minScreenSize{ 0.0f }
        , _bEnabled{ SW_TRUE }
    {
    }

    bool RigNode::parseCommon( RigXMLReader& reader )
    {
        bool       bEnabled = true; // 적지 않으면 켜짐 — 형식이 정한 "적지 않음" 값이다(에디터가 끈 노드만 `enabled="false"` 를 쓴다)
        const bool bOk      = reader.readName( "name", _name, true ) && reader.readFloat( "weight", _weight, false ) &&
                         reader.readName( "weight_curve", _weightCurve, false ) && reader.readName( "weight_slot", _weightSlot, false ) &&
                         reader.readFloat( "min_screen_size", _minScreenSize, false ) && reader.readBool( "enabled", bEnabled, false );
        _bEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        if ( bOk && ( _weight < 0.0f || 1.0f < _weight ) )
        {
            reader.fail( "'weight' must be in [0, 1]" );
            return false;
        }
        if ( bOk && _minScreenSize < 0.0f )
        {
            reader.fail( "'min_screen_size' must not be negative" );
            return false;
        }
        return bOk;
    }
} // namespace sw
