#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/SocketEditModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Character/CharacterIssue.h"
#include "Engine/Character/Fit/CharacterGeometry.h"

namespace sw::editor
{
    namespace
    {
        struct SocketEditModelInternal
        {
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
        , _state{ _sockets.saveToXMLText() }
        , _mirrorAxis{ MirrorAxis::X }
    {
    }

    bool SocketEditModel::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        SocketSet loaded;
        if ( loaded.loadFromXMLText( xmlText, sourceName, _kinds ) == false )
            return false;
        _sockets = std::move( loaded );
        _state.reset( _sockets.saveToXMLText() );
        return true;
    }

    bool SocketEditModel::loadFromFile( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_WARNING_ID( "SocketEditModel.SocketFileCouldNot", "Socket file could not be read: %#", string( absolutePath ).c_str() );
            return false;
        }
        return loadFromXMLText( text, absolutePath );
    }

    void SocketEditModel::clear()
    {
        _sockets.clear();
        _state.reset( _sockets.saveToXMLText() );
    }

    bool SocketEditModel::saveDocument( const AbsolutePath& absPath )
    {
        // 부모(`_base`)를 가리키던 파일은 부모와의 차이로 쓴다 — 실패 이유는 `SocketSet::saveToFile` 이 알린다.
        if ( _sockets.saveToFile( absPath, _kinds ) == false )
            return false;
        _state.markSaved( _sockets.saveToXMLText() ); // 더러움은 펼친 한 벌로 비교한다
        return true;
    }

    bool SocketEditModel::isDirty() const
    {
        return _state.isDirty( _sockets.saveToXMLText() );
    }

    bool SocketEditModel::validate( vector<string>& outListIssue ) const
    {
        // 검사는 엔진의 두 함수가 정한다(`SocketSet::validateReferences` · `validateBones`) — 게임이 읽을 때와 같은 규칙이다. 종류 표 · 스켈레톤을 아직 못 받았으면 그 검사는 하지 않는다.
        CharacterIssueList issues;
        if ( _kinds.getKinds().empty() == false )
            (void)_sockets.validateReferences( _kinds, &issues );
        if ( _listBoneName.empty() == false )
        {
            CharacterBoneArray bones;
            bones._listName = _listBoneName; // 이름만 본다(`CharacterBoneArray::findBone`)
            (void)_sockets.validateBones( bones, &issues );
        }
        for ( const CharacterIssue& issue : issues.getIssues() )
        {
            outListIssue.push_back( issue._text );
        }
        return issues.getIssues().empty();
    }

    expected<void> SocketEditModel::addSocket( const hashed_string& name, const hashed_string& parent, const hashed_string& kind )
    {
        if ( name.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Socket name is empty" } );
        if ( _sockets.findSocket( name ) != nullptr )
            return unexpected( Error{ ErrorKind::AlreadyExists, "Socket already exists", name.view() } );
        if ( isParentAllowed( parent ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "Parent is not a bone of this skeleton", parent.view() } );
        if ( isKindAllowed( kind ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "Kind is not in the kind table", kind.view() } );

        SocketDef socket;
        socket._name      = name;
        socket._parent    = parent;
        socket._kind      = kind;
        socket._fieldMask = SocketFieldBit::kAll;
        recordUndo();
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::removeSocket( const hashed_string& name )
    {
        if ( _sockets.findSocket( name ) == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "Socket does not exist", name.view() } );
        recordUndo();
        vector<SocketDef> listKept;
        for ( const SocketDef& socket : _sockets.getSockets() )
        {
            if ( ( socket._name == name ) == false )
                listKept.push_back( socket );
        }
        replaceSockets( listKept );
        return {};
    }

    expected<void> SocketEditModel::renameSocket( const hashed_string& oldName, const hashed_string& newName )
    {
        if ( _sockets.findSocket( oldName ) == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "Socket does not exist", oldName.view() } );
        if ( newName.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Socket name is empty" } );
        // 대소문자만 바꾸는 이름 바꾸기는 해시가 같아 자기 자신으로 겹쳐 보이므로 자기 자신은 제외한다.
        const bool bSameSocket = oldName == newName;
        if ( bSameSocket == false && _sockets.findSocket( newName ) != nullptr )
            return unexpected( Error{ ErrorKind::AlreadyExists, "Socket already exists", newName.view() } );

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
        return {};
    }

    expected<void> SocketEditModel::setParent( const hashed_string& name, const hashed_string& parent )
    {
        if ( isParentAllowed( parent ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "Parent is not a bone of this skeleton", parent.view() } );
        expected<SocketDef> edit = beginFieldEdit( name, SocketFieldBit::kParent, false, "parent" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket = *edit;
        socket._parent    = parent;
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::setKind( const hashed_string& name, const hashed_string& kind )
    {
        if ( isKindAllowed( kind ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "Kind is not in the kind table", kind.view() } );
        expected<SocketDef> edit = beginFieldEdit( name, SocketFieldBit::kKind, false, "kind" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket = *edit;
        socket._kind      = kind;
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::setTranslation( const hashed_string& name, const float3& translation, bool bContinuous )
    {
        expected<SocketDef> edit = beginFieldEdit( name, SocketFieldBit::kTranslation, bContinuous, "translation" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket   = *edit;
        socket._translation = translation;
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::setRotationDegrees( const hashed_string& name, const float3& degrees, bool bContinuous )
    {
        expected<SocketDef> edit = beginFieldEdit( name, SocketFieldBit::kRotation, bContinuous, "rotation" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket = *edit;
        socket._rotation  = quaternion::makeFromYawPitchRoll( degrees * MathUtil::kDegreeToRadian );
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::setScale( const hashed_string& name, const float3& scale, bool bContinuous )
    {
        expected<SocketDef> edit = beginFieldEdit( name, SocketFieldBit::kScale, bContinuous, "scale" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket = *edit;
        socket._scale     = scale;
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::setUnitTransform( const hashed_string& name, const float4x4& unitTransform, const float4x4& parentUnitTransform, bool bContinuous )
    {
        // 소켓의 유닛 변환은 로컬 × 부모(행벡터)다 — 로컬 = 유닛 × 부모⁻¹.
        float3     translation;
        quaternion rotation;
        float3     scale;
        if ( ( unitTransform * parentUnitTransform.invert() ).decompose( scale, rotation, translation ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Socket got a transform with no scale to take apart", name.view() } );
        const SocketDef* pSocket = _sockets.findSocket( name );
        if ( pSocket == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "Socket does not exist", name.view() } );
        constexpr float32 kScaleTolerance = 1.0e-4f; // 끌기의 부동소수 오차로 크기 칸을 적지 않는다
        const float3      scaleDelta      = scale - pSocket->_scale;
        const bool        bScaleChanged   = MathUtil::abs( scaleDelta._x ) > kScaleTolerance || MathUtil::abs( scaleDelta._y ) > kScaleTolerance ||
                                   MathUtil::abs( scaleDelta._z ) > kScaleTolerance;
        const uint16        fieldBits = static_cast<uint16>( SocketFieldBit::kTranslation | SocketFieldBit::kRotation | ( bScaleChanged ? SocketFieldBit::kScale : 0u ) );
        expected<SocketDef> edit      = beginFieldEdit( name, fieldBits, bContinuous, "transform" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket   = *edit;
        socket._translation = translation;
        socket._rotation    = rotation;
        if ( bScaleChanged )
            socket._scale = scale;
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::setPreviewMesh( const hashed_string& name, string_view previewMesh )
    {
        expected<SocketDef> edit = beginFieldEdit( name, SocketFieldBit::kPreview, false, "preview" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket   = *edit;
        socket._previewMesh = string( previewMesh );
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::setAnchor( const hashed_string& name, SocketAnchor anchor )
    {
        expected<SocketDef> edit = beginFieldEdit( name, SocketFieldBit::kAnchor, false, "anchor" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket = *edit;
        socket._anchor    = anchor;
        _sockets.addSocket( socket );
        return {};
    }

    expected<void> SocketEditModel::setFallback( const hashed_string& name, const vector<hashed_string>& listFallback )
    {
        for ( const hashed_string& fallback : listFallback )
        {
            if ( fallback == name )
                return unexpected( Error{ ErrorKind::InvalidArgument, "A socket cannot be its own fallback", name.view() } );
        }
        expected<SocketDef> edit = beginFieldEdit( name, SocketFieldBit::kFallback, false, "fallback" );
        if ( edit.has_value() == false )
            return unexpected( edit.error() );
        SocketDef& socket    = *edit;
        socket._listFallback = listFallback;
        _sockets.addSocket( socket );
        return {};
    }

    expected<hashed_string> SocketEditModel::mirrorSocket( const hashed_string& name )
    {
        const expected<SocketDef> copied = copySocket( name );
        if ( copied.has_value() == false )
            return unexpected( copied.error() );
        const SocketDef& source = *copied;

        string mirrorText;
        if ( _mirrorRules.findMirrorName( name.view(), mirrorText ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Socket has no left or right token in its name", name.view() } );

        SocketDef mirrored = source;
        mirrored._name     = SocketEditModelInternal::makeName( mirrorText );
        mirrored._parent   = SocketEditModelInternal::mirrorNameOrSame( _mirrorRules, source._parent );
        if ( isParentAllowed( mirrored._parent ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "Mirrored parent is not a bone of this skeleton", mirrored._parent.view() } );
        mirrored._translation = CharacterMirrorUtil::mirrorTranslation( source._translation, _mirrorAxis );
        mirrored._rotation    = CharacterMirrorUtil::mirrorRotation( source._rotation, _mirrorAxis );
        for ( hashed_string& fallback : mirrored._listFallback )
        {
            fallback = SocketEditModelInternal::mirrorNameOrSame( _mirrorRules, fallback );
        }

        recordUndo();
        _sockets.addSocket( mirrored );
        return mirrored._name;
    }

    bool SocketEditModel::undo()
    {
        return _state.undo( _sockets );
    }

    bool SocketEditModel::redo()
    {
        return _state.redo( _sockets );
    }

    void SocketEditModel::recordUndo( string_view coalesceKey )
    {
        _state.record( _sockets, coalesceKey );
    }

    expected<SocketDef> SocketEditModel::copySocket( const hashed_string& name ) const
    {
        const SocketDef* pSocket = _sockets.findSocket( name );
        if ( pSocket == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "Socket does not exist", name.view() } );
        return *pSocket;
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
        return VectorUtil::contains( _listBoneName, parent ) || _sockets.findVirtualBone( parent ) != nullptr;
    }

    bool SocketEditModel::isKindAllowed( const hashed_string& kind ) const
    {
        if ( kind.empty() || _kinds.getKinds().empty() )
            return true;
        return _kinds.hasKind( kind );
    }

    expected<SocketDef> SocketEditModel::beginFieldEdit( const hashed_string& name, uint16 fieldBit, bool bContinuous, string_view fieldName )
    {
        expected<SocketDef> socket = copySocket( name );
        if ( socket.has_value() == false )
            return socket;
        string coalesceKey;
        if ( bContinuous )
        {
            coalesceKey = string( name.view() );
            coalesceKey += ":";
            coalesceKey += fieldName;
        }
        recordUndo( coalesceKey );
        socket->_fieldMask |= fieldBit;
        return socket;
    }
} // namespace sw::editor
