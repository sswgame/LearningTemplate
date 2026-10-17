#include "pch.h"

#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct PairedAnimationDefInternal
        {
            static constexpr const utf8* kRootName                = "PairedAnimation";
            static constexpr const utf8* kArrRootAttribute[]      = { "duration" };
            static constexpr const utf8* kArrNotifyAttribute[]    = { "name", "time", "role" };
            static constexpr const utf8* kArrRoleAttribute[]      = { "name", "clip", "required", "anchor", "start", "enterBlend", "exitState",
                                                                      "exitBlend", "skeleton", "align", "offset", "yaw", "warpEnd", "maxAlignDistance" };
            static constexpr const utf8* kArrAlignOnlyAttribute[] = { "offset", "yaw", "maxAlignDistance" };

            /** @brief 유한하지 않으면(`nan` · `inf`) 오류를 남깁니다. */
            static void requireFinite( CharacterDataReader& reader, const XMLNode& node, const utf8* pName, float32 value )
            {
                if ( MathUtil::isFinite( value ) == false )
                    reader.addError( node, string( "attribute '" ) + pName + "' is not finite" );
            }

            /** @brief 유한하지 않거나 음수면 오류를 남깁니다. */
            static void requireNonNegative( CharacterDataReader& reader, const XMLNode& node, const utf8* pName, float32 value )
            {
                requireFinite( reader, node, pName, value );
                if ( value < 0.0f )
                    reader.addError( node, string( "attribute '" ) + pName + "' is negative" );
            }

            /**
             * @brief `align` 글을 풉니다. 없거나 모르는 값이면 오류를 남기고 `None` 입니다(읽기는 `finish` 에서 실패한다).
             * @details 글 그대로 견준다 — `hashed_string( "none" )` 은 빈 이름(None)과 같아 "적지 않음"과 가를 수 없다.
             */
            static PairedAnimationAlign parseAlign( CharacterDataReader& reader, const XMLNode& node )
            {
                const utf8* pText = node.findAttribute( "align" );
                if ( pText == nullptr )
                {
                    reader.addError( node, "needs attribute 'align'" );
                    return PairedAnimationAlign::None;
                }
                const string_view text( pText );
                if ( text == "none" )
                    return PairedAnimationAlign::None;
                if ( text == "snap" )
                    return PairedAnimationAlign::Snap;
                if ( text == "warp" )
                    return PairedAnimationAlign::Warp;
                reader.addError( node, string( "attribute 'align' must be none, snap or warp: '" ) + pText + "'" );
                return PairedAnimationAlign::None;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PairedAnimationDef::PairedAnimationDef()
        : _listRole{}
        , _listNotify{}
        , _duration{ 0.0f }
        , _anchorRoleIndex{ invalid_index::kUint32 }
    {
    }

    void PairedAnimationDef::clear()
    {
        _listRole.clear();
        _listNotify.clear();
        _duration        = 0.0f;
        _anchorRoleIndex = invalid_index::kUint32;
    }

    bool PairedAnimationDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        clear();
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, PairedAnimationDefInternal::kRootName, root ) )
            readRoot( root, reader );
        const bool bLoaded = reader.finish();
        if ( bLoaded == false )
            clear();
        return bLoaded;
    }

    bool PairedAnimationDef::loadFromResource( string_view path )
    {
        clear();
        CharacterDataReader reader( path );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, path, PairedAnimationDefInternal::kRootName, root ) )
            readRoot( root, reader );
        const bool bLoaded = reader.finish();
        if ( bLoaded == false )
            clear();
        return bLoaded;
    }

    uint32 PairedAnimationDef::findRoleIndex( const hashed_string& roleName ) const
    {
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            if ( _listRole[roleIndex]._name == roleName )
                return static_cast<uint32>( roleIndex );
        }
        return invalid_index::kUint32;
    }

    void PairedAnimationDef::readRoot( const XMLNode& root, CharacterDataReader& reader )
    {
        reader.reportUnknownAttributes( root, PairedAnimationDefInternal::kArrRootAttribute );
        _duration = reader.readFloat( root, "duration" );
        PairedAnimationDefInternal::requireFinite( reader, root, "duration", _duration );
        if ( _duration <= 0.0f )
            reader.addError( root, "attribute 'duration' must be positive" );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Role", true ) )
                readRole( child, reader );
            else if ( StringUtil::equals( child.getName(), "Notify", true ) )
                readNotify( child, reader );
            else
                reader.reportUnknownElement( child );
        }
        validate( reader );
    }

    void PairedAnimationDef::readRole( const XMLNode& node, CharacterDataReader& reader )
    {
        reader.reportUnknownAttributes( node, PairedAnimationDefInternal::kArrRoleAttribute );
        PairedAnimationRoleDef role;
        role._name      = reader.readName( node, "name", true );
        role._clip      = reader.readName( node, "clip", true );
        role._exitState = reader.readName( node, "exitState", true );
        // 경로는 글 그대로 둔다 — 빈 hashed_string 의 c_str() 은 "None" 이라 "적지 않음"이 경로 "None" 으로 바뀐다.
        const utf8* pSkeletonPath = node.findAttribute( "skeleton" );
        role._skeletonPath        = pSkeletonPath != nullptr ? pSkeletonPath : "";
        role._bRequired           = reader.readBool( node, "required" );
        role._bAnchor             = reader.readOptionalBool( node, "anchor", false );
        role._startSeconds        = reader.readOptionalFloat( node, "start", 0.0f );
        role._enterBlendSeconds   = reader.readFloat( node, "enterBlend" );
        role._exitBlendSeconds    = reader.readFloat( node, "exitBlend" );
        PairedAnimationDefInternal::requireNonNegative( reader, node, "start", role._startSeconds );
        PairedAnimationDefInternal::requireNonNegative( reader, node, "enterBlend", role._enterBlendSeconds );
        PairedAnimationDefInternal::requireNonNegative( reader, node, "exitBlend", role._exitBlendSeconds );

        // 앵커는 기준이라 정렬을 적지 않는다. 앵커 밖의 역할은 정렬 방식을 꼭 적는다(적지 않음 = 맞추지 않음으로 두지 않는다).
        const bool bHasAlign = CharacterDataReader::hasAttribute( node, "align" );
        if ( role._bAnchor && bHasAlign )
            reader.addError( node, "is the anchor and must not have 'align'" );
        else if ( role._bAnchor == false )
            role._align = PairedAnimationDefInternal::parseAlign( reader, node );

        const bool bAligned = role._align == PairedAnimationAlign::Snap || role._align == PairedAnimationAlign::Warp;
        if ( bAligned )
        {
            role._offset           = reader.readFloat3( node, "offset" );
            role._yawRadians       = reader.readOptionalFloat( node, "yaw", 0.0f ) * MathUtil::kDegreeToRadian;
            role._maxAlignDistance = reader.readFloat( node, "maxAlignDistance" );
            for ( uint32 axis = 0; axis < 3; ++axis )
            {
                PairedAnimationDefInternal::requireFinite( reader, node, "offset", role._offset.data()[axis] );
            }
            PairedAnimationDefInternal::requireFinite( reader, node, "yaw", role._yawRadians );
            PairedAnimationDefInternal::requireNonNegative( reader, node, "maxAlignDistance", role._maxAlignDistance );
        }
        else
        {
            for ( const utf8* pName : PairedAnimationDefInternal::kArrAlignOnlyAttribute )
            {
                if ( CharacterDataReader::hasAttribute( node, pName ) )
                    reader.addError( node, string( "has '" ) + pName + "' but does not align (align is snap or warp)" );
            }
        }
        if ( role._align == PairedAnimationAlign::Warp )
        {
            role._warpEndSeconds = reader.readFloat( node, "warpEnd" );
            PairedAnimationDefInternal::requireFinite( reader, node, "warpEnd", role._warpEndSeconds );
            // 크로스페이드가 끝나야 루트 모션 프레임의 클립이 이 역할의 클립이 된다 — 그 전에 창이 끝나면 한 번도 휘지 않는다.
            if ( role._warpEndSeconds <= role._enterBlendSeconds )
                reader.addError( node, "attribute 'warpEnd' must be after 'enterBlend'" );
        }
        else if ( CharacterDataReader::hasAttribute( node, "warpEnd" ) )
        {
            reader.addError( node, "has 'warpEnd' but align is not warp" );
        }
        if ( role._name.empty() == false && findRoleIndex( role._name ) != invalid_index::kUint32 )
        {
            reader.addError( node, string( "repeats role '" ) + role._name.c_str() + "'" );
            return;
        }
        _listRole.push_back( std::move( role ) );
    }

    void PairedAnimationDef::readNotify( const XMLNode& node, CharacterDataReader& reader )
    {
        reader.reportUnknownAttributes( node, PairedAnimationDefInternal::kArrNotifyAttribute );
        PairedAnimationNotifyDef notify;
        notify._name = reader.readName( node, "name", true );
        notify._role = reader.readName( node, "role", true );
        notify._time = reader.readFloat( node, "time" );
        PairedAnimationDefInternal::requireNonNegative( reader, node, "time", notify._time );
        _listNotify.push_back( notify );
    }

    void PairedAnimationDef::validate( CharacterDataReader& reader )
    {
        // 앵커는 정확히 하나 — 세션 시계가 앵커 플레이어의 시각이다.
        uint32 anchorCount = 0;
        for ( size_t roleIndex = 0; roleIndex < _listRole.size(); ++roleIndex )
        {
            const PairedAnimationRoleDef& role = _listRole[roleIndex];
            if ( role._bAnchor == false )
                continue;
            ++anchorCount;
            _anchorRoleIndex = static_cast<uint32>( roleIndex );
            if ( role._bRequired == false )
                reader.addError( string( "anchor role '" ) + role._name.c_str() + "' must be required" );
            if ( role._startSeconds != 0.0f )
                reader.addError( string( "anchor role '" ) + role._name.c_str() + "' must start at 0" );
        }
        if ( anchorCount != 1 )
            reader.addError( anchorCount == 0 ? "has no anchor role (exactly one role needs anchor=\"true\")" : "has more than one anchor role" );

        for ( const PairedAnimationRoleDef& role : _listRole )
        {
            if ( role._startSeconds >= _duration )
                reader.addError( string( "role '" ) + role._name.c_str() + "' starts at or after the end of the timeline" );
        }
        for ( const PairedAnimationNotifyDef& notify : _listNotify )
        {
            if ( notify._time > _duration )
                reader.addError( string( "notify '" ) + notify._name.c_str() + "' is after the end of the timeline" );
            if ( notify._role.empty() == false && findRoleIndex( notify._role ) == invalid_index::kUint32 )
                reader.addError( string( "notify '" ) + notify._name.c_str() + "' names unknown role '" + notify._role.c_str() + "'" );
        }
        // 세션이 시각 순서로 한 커서를 지나며 낸다.
        std::stable_sort( _listNotify.begin(), _listNotify.end(), []( const PairedAnimationNotifyDef& lhs, const PairedAnimationNotifyDef& rhs )
        { return lhs._time < rhs._time; } );
    }
} // namespace sw
