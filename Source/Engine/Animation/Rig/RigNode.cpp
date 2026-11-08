#include "pch.h"

#include "Engine/Animation/Rig/RigNode.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
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
        SW_LOG_ERROR_ID( "Rig.Rig", "Rig '%#': %#", _context.c_str(), message );
        _bOk = SW_FALSE;
    }

    const utf8* RigXMLReader::readAttribute( string_view key, bool bRequired )
    {
        _listUsedKey.push_back( string{ key } );
        if ( _element.isValid() == false )
            return nullptr;
        const string      keyText{ key };
        const utf8* const pValue = _element.findAttribute( keyText.c_str() );
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
        const XMLNode child = _element.findChild( keyText.c_str() );
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
        const hashed_string name( string_view{ pValue } );
        // 빈 이름의 철자(`none`, 대소문자 무시)는 이름이 될 수 없다 — 담으면 빈 이름이 되어 필수 이름이 조용히 비게 된다(`XMLReadUtil::readOptionalName` 과 같은 규칙).
        if ( name.empty() )
        {
            fail( makeKeyMessage( key, "must not spell the empty name (none)" ) );
            return false;
        }
        outValue = name;
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
        if ( _element.findAttribute( keyText.c_str() ) == nullptr )
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
        // 쓰는 쪽(`XMLNode::appendAttribute( bool )` 의 1/0)과 같은 문법 하나로 읽는다 — 리그만의 true/false 문법을 두면 엔진이 쓴 파일을 못 읽는다.
        if ( StringUtil::tryParseBool( string_view{ pValue }, outValue ) == false )
        {
            fail( makeKeyMessage( key, "must be a boolean (true/false/1/0)" ) );
            return false;
        }
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
            const hashed_string name( string_view{ element.getText() } );
            if ( name.empty() )
            {
                fail( makeKeyMessage( key, "must not hold the empty name (none)" ) );
                return false;
            }
            outListValue.push_back( name );
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

    bool RigXMLReader::readFields( void* pInstance, const TypeInfo& typeInfo )
    {
        for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
        {
            _listUsedKey.push_back( XMLFields::getAttributeName( prop ) );
        }
        if ( _element.isValid() == false )
            return false;
        // 모르는 키는 `finish` 가 손으로 읽은 키(이름 목록 등)와 함께 낸다 — 칸 읽기는 그 밖의 오류만 이 리더의 오류로 옮긴다.
        const XMLFieldsIssueSink sink{ []( void* pContext, const utf8* pID, string_view text )
        {
            if ( StringUtil::equals( pID, "XMLFields.UnknownField" ) == false )
                static_cast<RigXMLReader*>( pContext )->fail( text );
        },
                                       this };
        // 판정은 리더 상태로 한다 — `XMLFields::readFields` 의 반환은 모르는 키에도 거짓이지만, 모르는 키는 아직 다른 칸 읽기가 맡을 수 있다.
        (void)XMLFields::readFields( _element, pInstance, typeInfo, _context, {}, nullptr, sink );
        return isOk();
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
        const optional<uint32> boneIndex = ( _pSkeleton != nullptr ) ? _pSkeleton->findBoneIndex( name ) : nullopt;
        if ( boneIndex.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Rig.RigUnknownBone", "Rig '%#': unknown bone '%#'", _label, name.c_str() );
            return false;
        }
        outBone = *boneIndex;
        return true;
    }

    bool RigBindContext::findTarget( const hashed_string& name, uint32& outTarget ) const
    {
        const optional<uint32> targetIndex = ( _pInstance != nullptr ) ? _pInstance->findTargetIndex( name ) : nullopt;
        if ( targetIndex.has_value() == false )
        {
            SW_LOG_ERROR_ID( "Rig.RigUnknownTarget", "Rig '%#': unknown target '%#'", _label, name.c_str() );
            return false;
        }
        outTarget = *targetIndex;
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
                SW_LOG_ERROR_ID( "Rig.RigChainBoneNot", "Rig '%#': chain bone '%#' is not a descendant of '%#'", _label, listName[index].c_str(), listName[index - 1].c_str() );
                return false;
            }
        }
        return true;
    }

    RigNode::RigNode()
        : _common{}
    {
    }

    bool RigNode::parseCommon( RigXMLReader& reader )
    {
        return reader.readFields( _common ); // 키 · 기본값 · 범위(가중치 0..1, 화면 크기 >= 0)는 RigNodeCommonParams 의 PROPERTY
    }
} // namespace sw
