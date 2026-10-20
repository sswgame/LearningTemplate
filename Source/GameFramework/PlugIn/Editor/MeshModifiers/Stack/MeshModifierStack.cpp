/**
 * @file MeshModifierStack.cpp
 * @brief 모디파이어 스택의 순서 편집 · 적용 · 저장 문서 읽기와 쓰기를 구현합니다.
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierStack.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct MeshModifierStackInternal
        {
            static constexpr string_view kMeshExtension = ".mesh";

            static void writeVector3( const JSONValue& parent, string_view key, const float3& value )
            {
                const JSONValue array = parent.set( key );
                array.setArray();
                array.pushBack().setFloat( static_cast<float64>( value._x ) );
                array.pushBack().setFloat( static_cast<float64>( value._y ) );
                array.pushBack().setFloat( static_cast<float64>( value._z ) );
            }

            /** @brief 숫자 셋의 배열 @p array 를 읽습니다. 모양이 다르거나 숫자가 아닌 칸이 있으면 false 입니다(0 으로 두지 않는다). */
            [[nodiscard]] static bool readFloat3( const JSONValue& array, float3& outValue )
            {
                if ( array.isArray() == false || array.size() != 3 )
                    return false;
                const std::optional<float32> x        = array.at( 0 ).read<float32>();
                const std::optional<float32> y        = array.at( 1 ).read<float32>();
                const std::optional<float32> z        = array.at( 2 ).read<float32>();
                const bool                   bAllRead = x.has_value() && y.has_value() && z.has_value();
                if ( bAllRead == false )
                    return false;
                outValue = float3{ *x, *y, *z };
                return true;
            }

            /** @brief 없어도 되는 벡터 @p key 를 읽습니다. 없으면 @p inoutValue 를 그대로 두고(기즈모의 초기값), 숫자 셋이 아니면 false 입니다. */
            [[nodiscard]] static bool readVector3( const JSONValue& parent, string_view key, float3& inoutValue )
            {
                const JSONValue array = parent.get( key );
                return array.isValid() == false || readFloat3( array, inoutValue );
            }

            /**
             * @brief 곡선 키 하나를 배열로 씁니다. 선형 키는 [시간, 값](옛 문서와 같다), 상수 · 자동 접선 큐빅은 [시간, 값, 보간],
             *        접선을 손으로 둔 큐빅은 [시간, 값, 보간, 들어오는 접선, 나가는 접선]입니다.
             */
            static void writeCurveKey( const JSONValue& array, const FloatCurveKey& key )
            {
                const JSONValue pair = array.pushBack();
                pair.setArray();
                pair.pushBack().setFloat( static_cast<float64>( key._time ) );
                pair.pushBack().setFloat( static_cast<float64>( key._value ) );
                if ( key._interpolation == CurveInterpolation::Linear )
                    return;
                pair.pushBack().setUint( static_cast<uint64>( key._interpolation ) );
                if ( key._interpolation == CurveInterpolation::Cubic && key._bAutoTangent == false )
                {
                    pair.pushBack().setFloat( static_cast<float64>( key._arriveTangent ) );
                    pair.pushBack().setFloat( static_cast<float64>( key._leaveTangent ) );
                }
            }

            static void writeCurve( const JSONValue& array, const FloatCurve& curve )
            {
                array.setArray();
                for ( const FloatCurveKey& key : curve._listKey )
                {
                    writeCurveKey( array, key );
                }
            }

            /**
             * @brief `writeCurveKey` 의 배열을 읽습니다. 선형이 아닌 키의 자동 접선은 `setParamValue` 의 다듬기가 다시 센다.
             * @return 모양이 틀린 키(칸 수 · 숫자가 아닌 칸 · 모르는 보간)가 있으면 false 입니다. 그 키만 버리지 않는다(곡선 모양이 말없이 바뀐다).
             */
            [[nodiscard]] static bool readCurve( const JSONValue& source, FloatCurve& outCurve )
            {
                outCurve._listKey.clear();
                for ( size_t index = 0; index < source.size(); ++index )
                {
                    const JSONValue pair = source.at( index );
                    if ( pair.isArray() == false || ( pair.size() != 2 && pair.size() != 3 && pair.size() != 5 ) )
                        return false;
                    const std::optional<float32> time          = pair.at( 0 ).read<float32>();
                    const std::optional<float32> value         = pair.at( 1 ).read<float32>();
                    const std::optional<uint32>  interpolation = pair.size() >= 3 ? pair.at( 2 ).read<uint32>() : std::optional<uint32>{ 0u };
                    const std::optional<float32> arrive        = pair.size() == 5 ? pair.at( 3 ).read<float32>() : std::optional<float32>{ 0.0f };
                    const std::optional<float32> leave         = pair.size() == 5 ? pair.at( 4 ).read<float32>() : std::optional<float32>{ 0.0f };
                    const bool                   bRead         = time.has_value() && value.has_value() && interpolation.has_value() && arrive.has_value() && leave.has_value();
                    if ( bRead == false || *interpolation > static_cast<uint32>( CurveInterpolation::Cubic ) )
                        return false;
                    FloatCurveKey key{};
                    key._time          = *time;
                    key._value         = *value;
                    key._interpolation = pair.size() >= 3 ? static_cast<CurveInterpolation>( *interpolation ) : CurveInterpolation::Linear;
                    key._bAutoTangent  = pair.size() == 3;
                    if ( pair.size() == 5 )
                    {
                        key._arriveTangent = *arrive;
                        key._leaveTangent  = *leave;
                    }
                    outCurve._listKey.push_back( key );
                }
                return true;
            }

            static void writeParam( const MeshModifier& modifier, uint32 paramIndex, const JSONValue& params )
            {
                const MeshModifierParamDef&   def   = modifier.getParamDefs()[paramIndex];
                const MeshModifierParamValue& value = modifier.getParamValue( paramIndex );
                switch ( def._kind )
                {
                    case MeshModifierParamKind::Float:
                    case MeshModifierParamKind::Int:
                    case MeshModifierParamKind::Bool:
                    case MeshModifierParamKind::Axis:
                    {
                        params.set( def._pName ).setFloat( static_cast<float64>( value._value._x ) );
                        break;
                    }
                    case MeshModifierParamKind::Vector3:
                    {
                        writeVector3( params, def._pName, value._value );
                        break;
                    }
                    case MeshModifierParamKind::Curve:
                    {
                        writeCurve( params.set( def._pName ), value._curve );
                        break;
                    }
                    case MeshModifierParamKind::CurveList:
                    {
                        const JSONValue array = params.set( def._pName );
                        array.setArray();
                        for ( const FloatCurve& curve : value._listCurve )
                        {
                            writeCurve( array.pushBack(), curve );
                        }
                        break;
                    }
                    case MeshModifierParamKind::ReferenceList:
                    {
                        const JSONValue array = params.set( def._pName );
                        array.setArray();
                        for ( const string& text : value._listText )
                        {
                            array.pushBack().setString( text );
                        }
                        break;
                    }
                    case MeshModifierParamKind::NumberList:
                    {
                        const JSONValue array = params.set( def._pName );
                        array.setArray();
                        for ( const float32 number : value._listNumber )
                        {
                            array.pushBack().setFloat( static_cast<float64>( number ) );
                        }
                        break;
                    }
                    case MeshModifierParamKind::Reference:
                    case MeshModifierParamKind::AssetPath:
                    {
                        params.set( def._pName ).setString( value._text );
                        break;
                    }
                }
            }

            /** @brief 매개변수 하나를 읽습니다. 값의 모양이 매개변수 종류와 다르면 false 입니다(그 매개변수를 기본값으로 두지 않는다). */
            [[nodiscard]] static bool readParam( MeshModifier& modifier, uint32 paramIndex, const JSONValue& source )
            {
                const MeshModifierParamDef& def = modifier.getParamDefs()[paramIndex];
                switch ( def._kind )
                {
                    case MeshModifierParamKind::Float:
                    case MeshModifierParamKind::Int:
                    case MeshModifierParamKind::Bool:
                    case MeshModifierParamKind::Axis:
                    {
                        const std::optional<bool>    flag   = source.isBool() ? source.read<bool>() : std::nullopt;
                        const std::optional<float32> number = source.isNumber() ? source.read<float32>() : std::nullopt;
                        if ( flag.has_value() == false && number.has_value() == false )
                            return false;
                        modifier.setScalar( paramIndex, flag.has_value() ? ( *flag ? 1.0f : 0.0f ) : *number );
                        return true;
                    }
                    case MeshModifierParamKind::Vector3:
                    {
                        float3 value{};
                        if ( readFloat3( source, value ) == false )
                            return false;
                        modifier.setVector3( paramIndex, value );
                        return true;
                    }
                    case MeshModifierParamKind::Curve:
                    {
                        MeshModifierParamValue value{};
                        if ( source.isArray() == false || readCurve( source, value._curve ) == false )
                            return false;
                        modifier.setParamValue( paramIndex, value );
                        return true;
                    }
                    case MeshModifierParamKind::CurveList:
                    {
                        if ( source.isArray() == false )
                            return false;
                        MeshModifierParamValue value{};
                        value._listCurve.resize( source.size() );
                        for ( size_t index = 0; index < source.size(); ++index )
                        {
                            const JSONValue item = source.at( index );
                            if ( item.isArray() == false || readCurve( item, value._listCurve[index] ) == false )
                                return false;
                        }
                        modifier.setParamValue( paramIndex, value );
                        return true;
                    }
                    case MeshModifierParamKind::ReferenceList:
                    {
                        if ( source.isArray() == false )
                            return false;
                        MeshModifierParamValue value{};
                        for ( size_t index = 0; index < source.size(); ++index )
                        {
                            const JSONValue item = source.at( index );
                            if ( item.isString() == false )
                                return false;
                            value._listText.push_back( item.asString() );
                        }
                        modifier.setParamValue( paramIndex, value );
                        return true;
                    }
                    case MeshModifierParamKind::NumberList:
                    {
                        if ( source.isArray() == false )
                            return false;
                        vector<float32> listNumber;
                        listNumber.reserve( source.size() );
                        for ( size_t index = 0; index < source.size(); ++index )
                        {
                            const std::optional<float32> number = source.at( index ).read<float32>();
                            if ( number.has_value() == false )
                                return false;
                            listNumber.push_back( *number );
                        }
                        modifier.setNumberList( paramIndex, listNumber );
                        return true;
                    }
                    case MeshModifierParamKind::Reference:
                    case MeshModifierParamKind::AssetPath:
                    {
                        if ( source.isString() == false )
                            return false;
                        modifier.setText( paramIndex, source.asString() );
                        return true;
                    }
                }
                return false;
            }

            static void writeModifier( const MeshModifier& modifier, const JSONValue& object )
            {
                object.setObject();
                object.set( "type" ).setString( modifier.getTypeID() );
                object.set( "name" ).setString( modifier.getName() );
                object.set( "enabled" ).setBool( modifier.isEnabled() );
                object.set( "worldSpace" ).setBool( modifier.isWorldSpace() );
                object.set( "spaceReference" ).setString( modifier.getSpaceReference() );
                const MeshModifierGizmo& gizmo = modifier.getGizmo();
                writeVector3( object, "center", gizmo._center );
                writeVector3( object, "rotation", gizmo._rotationDegree );
                object.set( "axis" ).setUint( gizmo._axisIndex );
                const JSONValue params = object.set( "params" );
                params.setObject();
                for ( uint32 paramIndex = 0; paramIndex < modifier.getParamCount(); ++paramIndex )
                {
                    writeParam( modifier, paramIndex, params );
                }
                const JSONValue extra = object.set( "extra" );
                extra.setObject();
                modifier.writeExtraJSON( extra );
            }

            /**
             * @brief 모디파이어 하나를 읽습니다. 모르는 종류는 경고하고 @p outModifier 가 비고 true 입니다(그 모듈이 빠진 실행).
             * @return 값의 모양이 틀리면(켬 · 끔 · 벡터 · 축 · 매개변수 · 추가 상태) 오류를 남기고 false 입니다. 그 값을 기본값으로 두지 않는다.
             */
            [[nodiscard]] static bool readModifier( const JSONValue& object, unique_ptr<MeshModifier>& outModifier )
            {
                const string typeID = object.get( "type" ).asString();
                outModifier         = MeshModifierRegistry::create( typeID );
                if ( outModifier == nullptr )
                {
                    SW_LOG_WARNING( "Unknown mesh modifier type '%#' is skipped", typeID.c_str() );
                    return true;
                }
                MeshModifier& modifier = *outModifier;
                if ( object.has( "name" ) )
                    modifier.setName( object.get( "name" ).asString() );
                // 적지 않은 칸은 모디파이어의 초기값이다.
                bool               bEnabled    = modifier.isEnabled();
                bool               bWorldSpace = modifier.isWorldSpace();
                MeshModifierGizmo& gizmo       = modifier.getGizmo();
                const bool         bFieldsRead = object.readOptionalInto( "enabled", bEnabled ) && object.readOptionalInto( "worldSpace", bWorldSpace ) &&
                                         readVector3( object, "center", gizmo._center ) && readVector3( object, "rotation", gizmo._rotationDegree ) &&
                                         object.readOptionalInto( "axis", gizmo._axisIndex ) && gizmo._axisIndex <= 2;
                if ( bFieldsRead == false )
                {
                    SW_LOG_ERROR( "Mesh modifier '%#' has a broken enabled, worldSpace, center, rotation or axis (0 to 2)", typeID.c_str() );
                    return false;
                }
                modifier.setEnabled( bEnabled );
                modifier.setWorldSpace( bWorldSpace );
                modifier.setSpaceReference( object.get( "spaceReference" ).asString() );

                const JSONValue params = object.get( "params" );
                if ( params.isObject() )
                {
                    for ( uint32 paramIndex = 0; paramIndex < modifier.getParamCount(); ++paramIndex )
                    {
                        const utf8* pName = modifier.getParamDefs()[paramIndex]._pName;
                        if ( params.has( pName, false ) && readParam( modifier, paramIndex, params.get( pName, false ) ) == false )
                        {
                            SW_LOG_ERROR( "Mesh modifier '%#' parameter '%#' does not have the shape of its kind: %#", typeID.c_str(), pName, params.get( pName, false ).dump() );
                            return false;
                        }
                    }
                }
                modifier.onParamsChanged();
                const JSONValue extra = object.get( "extra" );
                if ( extra.isObject() && modifier.readExtraJSON( extra ) == false )
                {
                    SW_LOG_ERROR( "Mesh modifier '%#' has broken extra state", typeID.c_str() );
                    return false;
                }
                return true;
            }

            static string_view stripMeshExtension( string_view meshPath )
            {
                const bool bHasExtension = meshPath.size() >= kMeshExtension.size() &&
                                           meshPath.substr( meshPath.size() - kMeshExtension.size() ) == kMeshExtension;
                return bHasExtension ? meshPath.substr( 0, meshPath.size() - kMeshExtension.size() ) : meshPath;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    MeshModifierStack::MeshModifierStack()
        : _listModifier{}
        , _bRecomputeNormals{ false }
    {
    }

    int32 MeshModifierStack::addModifier( string_view typeID )
    {
        unique_ptr<MeshModifier> modifier = MeshModifierRegistry::create( typeID );
        if ( modifier == nullptr )
            return -1;
        return addModifier( std::move( modifier ) );
    }

    int32 MeshModifierStack::addModifier( unique_ptr<MeshModifier> modifier )
    {
        if ( modifier == nullptr )
            return -1;
        _listModifier.push_back( std::move( modifier ) );
        return static_cast<int32>( _listModifier.size() - 1 );
    }

    bool MeshModifierStack::removeModifier( int32 index )
    {
        if ( index < 0 || getModifierCount() <= index )
            return false;
        _listModifier.erase( _listModifier.begin() + index );
        return true;
    }

    bool MeshModifierStack::moveUp( int32 index )
    {
        if ( index <= 0 || getModifierCount() <= index )
            return false;
        std::swap( _listModifier[static_cast<size_t>( index )], _listModifier[static_cast<size_t>( index - 1 )] );
        return true;
    }

    bool MeshModifierStack::moveDown( int32 index )
    {
        if ( index < 0 || getModifierCount() - 1 <= index )
            return false;
        std::swap( _listModifier[static_cast<size_t>( index )], _listModifier[static_cast<size_t>( index + 1 )] );
        return true;
    }

    void MeshModifierStack::clear()
    {
        _listModifier.clear();
    }

    void MeshModifierStack::apply( MeshModifierMesh& inoutMesh, const MeshModifierEvaluation& evaluation ) const
    {
        // 원본 위치가 필요한 모디파이어가 있을 때만 스택 입력을 복사해 넘긴다(바깥 스택이 이미 넘겼으면 그것을 쓴다)
        vector<float3>         listStackInputPosition;
        MeshModifierEvaluation stackEvaluation = evaluation;
        if ( needsStackInputPosition() && stackEvaluation._pListStackInputPosition == nullptr )
        {
            listStackInputPosition                   = inoutMesh._listPosition;
            stackEvaluation._pListStackInputPosition = &listStackInputPosition;
        }
        applyRange( inoutMesh, stackEvaluation, 0, getModifierCount() );
        finishApply( inoutMesh );
    }

    void MeshModifierStack::applyRange( MeshModifierMesh& inoutMesh, const MeshModifierEvaluation& evaluation, int32 firstIndex, int32 endIndex ) const
    {
        const int32 first = MathUtil::max( firstIndex, 0 );
        const int32 end   = MathUtil::min( endIndex, getModifierCount() );
        for ( int32 index = first; index < end; ++index )
        {
            _listModifier[static_cast<size_t>( index )]->apply( inoutMesh, evaluation );
        }
    }

    bool MeshModifierStack::needsStackInputPosition() const
    {
        bool bNeedsOriginal = false;
        for ( const unique_ptr<MeshModifier>& modifier : _listModifier )
        {
            bNeedsOriginal = bNeedsOriginal || ( modifier->isEnabled() && modifier->needsOriginalPosition() );
        }
        return bNeedsOriginal;
    }

    void MeshModifierStack::finishApply( MeshModifierMesh& inoutMesh ) const
    {
        if ( _bRecomputeNormals )
            MeshModifierUtil::recomputeFaceNormals( inoutMesh._listPosition, inoutMesh._listNormal );
    }

    string MeshModifierStack::makeJSONText( int32 indent ) const
    {
        JSONDocument    document;
        const JSONValue root = document.makeObject();
        root.set( "version" ).setUint( kVersion );
        root.set( "recomputeNormals" ).setBool( _bRecomputeNormals );
        const JSONValue modifiers = root.set( "modifiers" );
        modifiers.setArray();
        for ( const unique_ptr<MeshModifier>& modifier : _listModifier )
        {
            MeshModifierStackInternal::writeModifier( *modifier, modifiers.pushBack() );
        }
        return document.dump( indent );
    }

    string MeshModifierStack::makePrefixJSONText( int32 endIndex ) const
    {
        JSONDocument    document;
        const JSONValue modifiers = document.makeArray();
        const int32     end       = MathUtil::min( endIndex, getModifierCount() );
        for ( int32 index = 0; index < end; ++index )
        {
            MeshModifierStackInternal::writeModifier( *_listModifier[static_cast<size_t>( index )], modifiers.pushBack() );
        }
        return document.dump( -1 );
    }

    bool MeshModifierStack::readJSONText( string_view jsonText )
    {
        clear();
        JSONDocument document;
        if ( document.parse( jsonText, "modifiers.json" ) == false )
            return false;
        const JSONValue             root    = document.getRoot();
        const std::optional<uint32> version = root.isObject() ? root.readRequired<uint32>( "version" ) : std::nullopt;
        if ( version.has_value() == false || *version != kVersion )
            return false;
        bool            bRecomputeNormals = false; // 없으면 스택의 초기값(다시 세지 않음)이다
        const JSONValue modifiers         = root.get( "modifiers" );
        if ( root.readOptionalInto( "recomputeNormals", bRecomputeNormals ) == false || modifiers.isArray() == false )
            return false;
        _bRecomputeNormals = bRecomputeNormals;
        for ( size_t index = 0; index < modifiers.size(); ++index )
        {
            const JSONValue          object = modifiers.at( index );
            unique_ptr<MeshModifier> modifier;
            if ( object.isObject() == false || MeshModifierStackInternal::readModifier( object, modifier ) == false )
            {
                clear();
                return false;
            }
            if ( modifier != nullptr )                      // 모르는 종류는 readModifier 가 경고하고 건너뛰었다
                (void)addModifier( std::move( modifier ) ); // 넘긴 것이 nullptr 이 아니므로 번호는 늘 유효하다
        }
        return true;
    }

    bool MeshModifierStack::saveFile( string_view absolutePath ) const
    {
        const string text = makeJSONText( 2 );
        return FileUtil::writeFile( absolutePath, reinterpret_cast<const uint8*>( text.data() ), text.size() );
    }

    bool MeshModifierStack::loadFile( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
            return false;
        return readJSONText( text );
    }

    string MeshModifierStack::makeSidecarPath( string_view meshPath )
    {
        string path( MeshModifierStackInternal::stripMeshExtension( meshPath ) );
        path.append( kSidecarExtension );
        return path;
    }

    string MeshModifierStack::makeBakedMeshPath( string_view meshPath )
    {
        string path( MeshModifierStackInternal::stripMeshExtension( meshPath ) );
        path.append( kBakedSuffix );
        path.append( MeshModifierStackInternal::kMeshExtension );
        return path;
    }

    MeshModifier* MeshModifierStack::findModifier( int32 index )
    {
        if ( index < 0 || getModifierCount() <= index )
            return nullptr;
        return _listModifier[static_cast<size_t>( index )].get();
    }

    const MeshModifier* MeshModifierStack::findModifier( int32 index ) const
    {
        if ( index < 0 || getModifierCount() <= index )
            return nullptr;
        return _listModifier[static_cast<size_t>( index )].get();
    }
} // namespace sw::editor
