#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/SocketEditModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    namespace
    {
        struct SocketEditModelInternal
        {
            /** @brief 이름 목록에서 @p name 을 찾습니다(대소문자 무시, 해시 비교). */
            static bool containsName( const vector<hashed_string>& listName, const hashed_string& name )
            {
                for ( const hashed_string& candidate : listName )
                {
                    if ( candidate == name )
                        return true;
                }
                return false;
            }

            static hashed_string makeName( const string& text ) { return hashed_string( string_view( text ) ); }

            /** @brief 이름의 반대편이 있으면 그것을, 없으면 같은 이름을 반환합니다. */
            static hashed_string mirrorNameOrSame( const MirrorNameRules& rules, const hashed_string& name )
            {
                string mirrored;
                if ( name.empty() == false && rules.findMirrorName( name.view(), mirrored ) )
                    return makeName( mirrored );
                return name;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SocketEditModel::SocketEditModel()
        : _sockets{}
        , _kinds{}
        , _mirrorRules{}
        , _listBoneName{}
        , _savedText{ _sockets.saveToXMLText() }
        , _history{}
        , _mirrorAxis{ MirrorAxis::X }
    {
    }

    bool SocketEditModel::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        SocketSet loaded;
        if ( loaded.loadFromXMLText( xmlText, sourceName, _kinds ) == false )
            return false;
        _sockets   = std::move( loaded );
        _savedText = _sockets.saveToXMLText();
        _history.clear();
        return true;
    }

    bool SocketEditModel::loadFromFile( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_WARNING( "Socket file could not be read: %#", string( absolutePath ).c_str() );
            return false;
        }
        return loadFromXMLText( text, absolutePath );
    }

    void SocketEditModel::clear()
    {
        _sockets.clear();
        _savedText = _sockets.saveToXMLText();
        _history.clear();
    }

    bool SocketEditModel::saveToFile( string_view absolutePath )
    {
        const string text = _sockets.saveToXMLText();
        if ( FileUtil::writeTextFile( absolutePath, text ) == false )
        {
            SW_LOG_ERROR( "Socket file could not be written: %#", string( absolutePath ).c_str() );
            return false;
        }
        _savedText = text;
        return true;
    }

    bool SocketEditModel::isDirty() const
    {
        return ( _sockets.saveToXMLText() == _savedText ) == false;
    }

    bool SocketEditModel::validate( vector<string>& outListIssue ) const
    {
        const size_t issueCountBefore = outListIssue.size();
        const bool   bCheckKind       = _kinds.getKinds().empty() == false;
        const bool   bCheckBone       = _listBoneName.empty() == false;
        for ( const SocketDef& socket : _sockets.getSockets() )
        {
            const bool bKindToCheck = bCheckKind && socket._kind.empty() == false;
            if ( bKindToCheck && _kinds.hasKind( socket._kind ) == false )
                outListIssue.push_back( string( "Socket '" ) + socket._name.c_str() + "' has unknown kind '" + socket._kind.c_str() + "'" );
            const bool bParentToCheck = bCheckBone && socket._parent.empty() == false;
            if ( bParentToCheck && isParentAllowed( socket._parent ) == false )
                outListIssue.push_back( string( "Socket '" ) + socket._name.c_str() + "' has unknown parent '" + socket._parent.c_str() + "'" );
            for ( const hashed_string& fallback : socket._listFallback )
            {
                if ( _sockets.findSocket( fallback ) == nullptr )
                    outListIssue.push_back( string( "Socket '" ) + socket._name.c_str() + "' has unknown fallback '" + fallback.c_str() + "'" );
            }
        }
        return outListIssue.size() == issueCountBefore;
    }

    bool SocketEditModel::addSocket( const hashed_string& name, const hashed_string& parent, const hashed_string& kind, string& outError )
    {
        if ( name.empty() )
        {
            outError = "Socket name is empty";
            return false;
        }
        if ( _sockets.findSocket( name ) != nullptr )
        {
            outError = string( "Socket '" ) + name.c_str() + "' already exists";
            return false;
        }
        if ( isParentAllowed( parent ) == false )
        {
            outError = string( "Parent '" ) + parent.c_str() + "' is not a bone of this skeleton";
            return false;
        }
        if ( isKindAllowed( kind ) == false )
        {
            outError = string( "Kind '" ) + kind.c_str() + "' is not in the kind table";
            return false;
        }

        SocketDef socket;
        socket._name      = name;
        socket._parent    = parent;
        socket._kind      = kind;
        socket._fieldMask = SocketFieldBit::kAll;
        recordUndo();
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::removeSocket( const hashed_string& name, string& outError )
    {
        if ( _sockets.findSocket( name ) == nullptr )
        {
            outError = string( "Socket '" ) + name.c_str() + "' does not exist";
            return false;
        }
        recordUndo();
        vector<SocketDef> listKept;
        for ( const SocketDef& socket : _sockets.getSockets() )
        {
            if ( ( socket._name == name ) == false )
                listKept.push_back( socket );
        }
        replaceSockets( listKept );
        return true;
    }

    bool SocketEditModel::renameSocket( const hashed_string& oldName, const hashed_string& newName, string& outError )
    {
        if ( _sockets.findSocket( oldName ) == nullptr )
        {
            outError = string( "Socket '" ) + oldName.c_str() + "' does not exist";
            return false;
        }
        if ( newName.empty() )
        {
            outError = "Socket name is empty";
            return false;
        }
        // 대소문자만 바꾸는 이름 바꾸기는 해시가 같아 자기 자신으로 겹쳐 보이므로 자기 자신은 제외한다.
        const bool bSameSocket = oldName == newName;
        if ( bSameSocket == false && _sockets.findSocket( newName ) != nullptr )
        {
            outError = string( "Socket '" ) + newName.c_str() + "' already exists";
            return false;
        }

        recordUndo();
        vector<SocketDef> listRenamed = _sockets.getSockets();
        for ( SocketDef& socket : listRenamed )
        {
            if ( socket._name == oldName )
                socket._name = newName;
            for ( hashed_string& fallback : socket._listFallback )
            {
                if ( fallback == oldName )
                    fallback = newName;
            }
        }
        replaceSockets( listRenamed );
        return true;
    }

    bool SocketEditModel::setParent( const hashed_string& name, const hashed_string& parent, string& outError )
    {
        if ( isParentAllowed( parent ) == false )
        {
            outError = string( "Parent '" ) + parent.c_str() + "' is not a bone of this skeleton";
            return false;
        }
        SocketDef socket;
        if ( beginFieldEdit( name, SocketFieldBit::kParent, false, "parent", socket, outError ) == false )
            return false;
        socket._parent = parent;
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::setKind( const hashed_string& name, const hashed_string& kind, string& outError )
    {
        if ( isKindAllowed( kind ) == false )
        {
            outError = string( "Kind '" ) + kind.c_str() + "' is not in the kind table";
            return false;
        }
        SocketDef socket;
        if ( beginFieldEdit( name, SocketFieldBit::kKind, false, "kind", socket, outError ) == false )
            return false;
        socket._kind = kind;
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::setTranslation( const hashed_string& name, const float3& translation, bool bContinuous, string& outError )
    {
        SocketDef socket;
        if ( beginFieldEdit( name, SocketFieldBit::kTranslation, bContinuous, "translation", socket, outError ) == false )
            return false;
        socket._translation = translation;
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::setRotationDegrees( const hashed_string& name, const float3& degrees, bool bContinuous, string& outError )
    {
        SocketDef socket;
        if ( beginFieldEdit( name, SocketFieldBit::kRotation, bContinuous, "rotation", socket, outError ) == false )
            return false;
        socket._rotation = quaternion::makeFromYawPitchRoll( degrees * MathUtil::kDegreeToRadian );
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::setScale( const hashed_string& name, const float3& scale, bool bContinuous, string& outError )
    {
        SocketDef socket;
        if ( beginFieldEdit( name, SocketFieldBit::kScale, bContinuous, "scale", socket, outError ) == false )
            return false;
        socket._scale = scale;
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::setPreviewMesh( const hashed_string& name, string_view previewMesh, string& outError )
    {
        SocketDef socket;
        if ( beginFieldEdit( name, SocketFieldBit::kPreview, false, "preview", socket, outError ) == false )
            return false;
        socket._previewMesh = string( previewMesh );
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::setAnchor( const hashed_string& name, SocketAnchor anchor, string& outError )
    {
        SocketDef socket;
        if ( beginFieldEdit( name, SocketFieldBit::kAnchor, false, "anchor", socket, outError ) == false )
            return false;
        socket._anchor = anchor;
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::setFallback( const hashed_string& name, const vector<hashed_string>& listFallback, string& outError )
    {
        for ( const hashed_string& fallback : listFallback )
        {
            if ( fallback == name )
            {
                outError = "A socket cannot be its own fallback";
                return false;
            }
        }
        SocketDef socket;
        if ( beginFieldEdit( name, SocketFieldBit::kFallback, false, "fallback", socket, outError ) == false )
            return false;
        socket._listFallback = listFallback;
        _sockets.addSocket( socket );
        return true;
    }

    bool SocketEditModel::mirrorSocket( const hashed_string& name, hashed_string& outMirrorName, string& outError )
    {
        SocketDef source;
        if ( copySocket( name, source, outError ) == false )
            return false;

        string mirrorText;
        if ( _mirrorRules.findMirrorName( name.view(), mirrorText ) == false )
        {
            outError = string( "Socket '" ) + name.c_str() + "' has no left or right token in its name";
            return false;
        }

        SocketDef mirrored = source;
        mirrored._name     = SocketEditModelInternal::makeName( mirrorText );
        mirrored._parent   = SocketEditModelInternal::mirrorNameOrSame( _mirrorRules, source._parent );
        if ( isParentAllowed( mirrored._parent ) == false )
        {
            outError = string( "Mirrored parent '" ) + mirrored._parent.c_str() + "' is not a bone of this skeleton";
            return false;
        }
        mirrored._translation = CharacterMirrorUtil::mirrorTranslation( source._translation, _mirrorAxis );
        mirrored._rotation    = CharacterMirrorUtil::mirrorRotation( source._rotation, _mirrorAxis );
        for ( hashed_string& fallback : mirrored._listFallback )
        {
            fallback = SocketEditModelInternal::mirrorNameOrSame( _mirrorRules, fallback );
        }

        recordUndo();
        _sockets.addSocket( mirrored );
        outMirrorName = mirrored._name;
        return true;
    }

    bool SocketEditModel::undo()
    {
        return _history.undo( _sockets );
    }

    bool SocketEditModel::redo()
    {
        return _history.redo( _sockets );
    }

    void SocketEditModel::recordUndo( string_view coalesceKey )
    {
        _history.record( _sockets, coalesceKey );
    }

    bool SocketEditModel::copySocket( const hashed_string& name, SocketDef& outSocket, string& outError ) const
    {
        const SocketDef* pSocket = _sockets.findSocket( name );
        if ( pSocket == nullptr )
        {
            outError = string( "Socket '" ) + name.c_str() + "' does not exist";
            return false;
        }
        outSocket = *pSocket;
        return true;
    }

    void SocketEditModel::replaceSockets( const vector<SocketDef>& listSocket )
    {
        const vector<VirtualBoneDef> listVirtualBone = _sockets.getVirtualBones();
        _sockets.clear();
        for ( const VirtualBoneDef& virtualBone : listVirtualBone )
        {
            _sockets.addVirtualBone( virtualBone );
        }
        for ( const SocketDef& socket : listSocket )
        {
            _sockets.addSocket( socket );
        }
    }

    bool SocketEditModel::isParentAllowed( const hashed_string& parent ) const
    {
        // 비어 있으면 메시 루트이고, 본 목록이 없으면 검증하지 않는다. 가상 본도 부모가 될 수 있다.
        if ( parent.empty() || _listBoneName.empty() )
            return true;
        return SocketEditModelInternal::containsName( _listBoneName, parent ) || _sockets.findVirtualBone( parent ) != nullptr;
    }

    bool SocketEditModel::isKindAllowed( const hashed_string& kind ) const
    {
        if ( kind.empty() || _kinds.getKinds().empty() )
            return true;
        return _kinds.hasKind( kind );
    }

    bool SocketEditModel::beginFieldEdit( const hashed_string& name, uint16 fieldBit, bool bContinuous, string_view fieldName, SocketDef& outSocket, string& outError )
    {
        if ( copySocket( name, outSocket, outError ) == false )
            return false;
        string coalesceKey;
        if ( bContinuous )
        {
            coalesceKey = string( name.view() );
            coalesceKey += ":";
            coalesceKey += fieldName;
        }
        recordUndo( coalesceKey );
        outSocket._fieldMask |= fieldBit;
        return true;
    }
} // namespace sw::editor
