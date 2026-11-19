#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/EnumInfo.h"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
#include "Engine/Serialization/Format/ReflectedStateUpgrade.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"

// `StateIO::reflected` — 리플렉션 상태 칸을 선언에서 싣는다. 바이트 약속은 `StateIO` 의 칸 함수 그대로라 손 본문과 섞여도 같은 아카이브다.
namespace sw
{
    namespace
    {
        struct StateIOReflectedInternal
        {
            /** @brief 칸 배치 해시의 규칙 판입니다. 잎 인코딩(열거를 int64 로, 시퀀스 개수를 uint32 로 …)을 바꾸면 올려 모든 배치 해시를 바꾼다. */
            static constexpr uint32 kLayoutRuleVersion = 1;

            /** @brief 잎 칸의 종류입니다. 이름으로 한 번 가른다. */
            enum class LeafKind : uint8
            {
                Unsupported,
                Bool,
                Int8,
                Int16,
                Int32,
                Int64,
                UInt8,
                UInt16,
                UInt32,
                UInt64,
                Float32,
                Float64,
                FloatVector, ///< float2 · float3 · float4 · quaternion — float32 를 `_size / 4` 개
                String,
                Name,
                Enum,
                Struct,
            };

            static LeafKind classify( const TypeRegistry& registry, const hashed_string& typeName )
            {
                struct NamedKind
                {
                    const utf8* _pName;
                    LeafKind    _kind;
                };
                static constexpr NamedKind kArrNamedKind[] = {
                    {         "bool",        LeafKind::Bool},
                    {         "int8",        LeafKind::Int8},
                    {        "int16",       LeafKind::Int16},
                    {        "int32",       LeafKind::Int32},
                    {        "int64",       LeafKind::Int64},
                    {        "uint8",       LeafKind::UInt8},
                    {       "uint16",      LeafKind::UInt16},
                    {       "uint32",      LeafKind::UInt32},
                    {       "uint64",      LeafKind::UInt64},
                    {      "float32",     LeafKind::Float32},
                    {      "float64",     LeafKind::Float64},
                    {       "float2", LeafKind::FloatVector},
                    {       "float3", LeafKind::FloatVector},
                    {       "float4", LeafKind::FloatVector},
                    {   "quaternion", LeafKind::FloatVector},
                    {       "string",      LeafKind::String},
                    {"hashed_string",        LeafKind::Name},
                };
                const string_view name = typeName.c_str();
                for ( const NamedKind& entry : kArrNamedKind )
                {
                    if ( name == entry._pName )
                        return entry._kind;
                }
                if ( registry.findEnum( typeName ) != nullptr )
                    return LeafKind::Enum;
                const TypeInfo* pType = registry.findType( typeName );
                if ( pType != nullptr && pType->isPrimitive() == false )
                    return LeafKind::Struct;
                return LeafKind::Unsupported;
            }

            /** @brief 칸이 싣는 시퀀스 래퍼입니다. 시퀀스가 아니면 nullptr, 맵 · 중첩 컨테이너는 실을 수 없어 멈춘다. */
            static const ISequenceContainerWrapper* findSequence( const TypeInfo& owner, const PropertyInfo& prop )
            {
                if ( prop._bIsContainer == SW_FALSE )
                    return nullptr;
                const bool                       bNested   = prop._nestedContainer != nullptr && prop._nestedContainer->_elementNested != nullptr;
                const ISequenceContainerWrapper* pSequence = prop._containerWrapper != nullptr && bNested == false ? prop._containerWrapper->asSequence() : nullptr;
                if ( pSequence == nullptr )
                    stopForUnsupportedField( owner, prop );
                return pSequence;
            }

            [[noreturn]] static void stopForUnsupportedField( const TypeInfo& owner, const PropertyInfo& prop )
            {
                SW_FATAL( "state field '%#::%#' (%#) cannot be carried by StateIO::reflected - use a number, bool, enum, string, hashed_string, float vector, reflected struct "
                          "or a sequence of those, or take the field out of RuntimeState and carry it with io.value in serializeState",
                          owner._fullyQualifiedName.c_str(), prop._name.c_str(), prop._typeName.c_str() );
            }

            /** @brief 이 깊이에서 싣는 칸인지 봅니다. 최상위는 상태 역할 칸만, 안쪽 구조체는 Transient 가 아닌 칸 전부입니다. */
            static bool isCarried( const PropertyInfo& prop, const bool bStateRoleOnly )
            {
                if ( bStateRoleOnly )
                    return prop._metadata->_bRuntimeState == SW_TRUE;
                return prop._metadata->_bTransient == SW_FALSE;
            }

            /** @brief 배치 해시의 정규 글을 씁니다. 칸마다 `이름:형식;`, 시퀀스는 `[원소]`, 구조체는 `{안쪽}` 입니다. 열거는 늘 int64 로 실려 기반 폭이 배치에 들지 않는다. */
            static void appendCanonical( const TypeRegistry& registry, const TypeInfo& type, const bool bStateRoleOnly, const uint32 depth, string& inoutCanonical )
            {
                if ( depth > constant::reflection::kMaxParentChainDepth )
                {
                    SW_FATAL( "state type '%#' nests reflected structs deeper than %# - a struct contains itself", type._fullyQualifiedName.c_str(), constant::reflection::kMaxParentChainDepth );
                }
                for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
                {
                    if ( isCarried( prop, bStateRoleOnly ) == false )
                        continue;
                    inoutCanonical += prop._name.c_str();
                    inoutCanonical += ':';
                    const ISequenceContainerWrapper* pSequence = findSequence( type, prop );
                    const hashed_string&             leafType  = pSequence != nullptr ? prop._elementTypeName : prop._typeName;
                    if ( pSequence != nullptr )
                        inoutCanonical += pSequence->isFixedSize() ? "[fixed " : "[";
                    if ( prop._bIsBitField == SW_TRUE )
                        inoutCanonical += "bit";
                    else
                        appendLeafCanonical( registry, type, prop, leafType, depth, inoutCanonical );
                    if ( pSequence != nullptr )
                        inoutCanonical += ']';
                    inoutCanonical += ';';
                }
            }

            static void appendLeafCanonical( const TypeRegistry& registry, const TypeInfo& owner, const PropertyInfo& prop, const hashed_string& leafType, const uint32 depth,
                                             string& inoutCanonical )
            {
                const LeafKind kind = classify( registry, leafType );
                if ( kind == LeafKind::Unsupported )
                    stopForUnsupportedField( owner, prop );
                inoutCanonical += leafType.c_str();
                if ( kind == LeafKind::Struct )
                {
                    inoutCanonical += '{';
                    appendCanonical( registry, *registry.findType( leafType ), false, depth + 1, inoutCanonical );
                    inoutCanonical += '}';
                }
            }

            static void valueFloatVector( StateIO& io, const TypeRegistry& registry, const hashed_string& typeName, void* pValue )
            {
                const TypeInfo* pVector = registry.findType( typeName );
                const size_t    count   = pVector != nullptr ? pVector->_size / sizeof( float32 ) : 0;
                float32*        pFloat  = static_cast<float32*>( pValue );
                for ( size_t index = 0; index < count; ++index )
                {
                    io.value( pFloat[index] );
                }
            }

            static void valueEnum( StateIO& io, const EnumInfo& info, void* pValue )
            {
                int64 stored = info.readValueFromMemory( pValue );
                io.value( stored );
                if ( io.isReading() && io.check( info.isValidValue( stored ) ) )
                    info.writeValueToMemory( pValue, stored );
            }
        };
    } // namespace

    const TypeInfo& StateIO::requireReflectedType( const hashed_string& fqn )
    {
        // 레지스트리를 이름으로 바로 찾는다 — `StaticType()` 은 DLL 밖으로 내보내지 않은 키트 타입에서 링크가 깨진다(`FixedLayout::findCodec` 과 같은 까닭).
        const TypeInfo* pType = TypeLookupCache{}.find( fqn );
        if ( pType == nullptr )
            SW_FATAL( "StateIO::reflected: '%#' is not a registered reflected type - add REFLECT() and REFLECT_BODY() or check the reflection generation", fqn.c_str() );
        return *pType;
    }

    uint64 StateIO::computeReflectedLayoutHash( const TypeInfo& type )
    {
        string canonical;
        StateIOReflectedInternal::appendCanonical( engine::getSubsystem<TypeRegistry>(), type, true, 0, canonical );
        return FixedLayout::computeSchemaHash( StateIOReflectedInternal::kLayoutRuleVersion, canonical, {} );
    }

    uint64 StateIO::findReflectedLayoutHash( const TypeInfo& type )
    {
        struct LayoutSlot
        {
            uint32 _generation{ 0 };
            uint64 _layoutHash{ 0 };
        };
        thread_local unordered_map<const TypeInfo*, LayoutSlot> t_mapLayoutSlot;
        const uint32                                            generation = TypeRegistry::getGeneration();
        LayoutSlot&                                             slot       = t_mapLayoutSlot[&type];
        if ( slot._generation != generation || generation == 0 )
        {
            slot._generation = generation;
            slot._layoutHash = computeReflectedLayoutHash( type );
        }
        return slot._layoutHash;
    }

    void StateIO::reflectedObject( const TypeInfo& type, void* pObject, const uint64 layoutHash )
    {
        const uint32 version = type._metadata->getVersion();
        if ( isWriting() )
        {
            *_pArchive << version;
            *_pArchive << layoutHash;
            reflectedProperties( type, pObject, true, 0 );
            return;
        }
        uint32 readVersion    = 0;
        uint64 readLayoutHash = 0;
        *_pArchive >> readVersion;
        *_pArchive >> readLayoutHash;
        if ( check( _pArchive->isOk() ) == false )
            return;
        if ( readVersion == version && readLayoutHash != layoutHash )
        {
            SW_LOG_ERROR_ID( "StateIO.ReflectedLayoutChanged",
                             "state of %# (version %#) was written with another field layout - a RuntimeState field was added, removed, renamed, retyped or moved without "
                             "raising REFLECT( Version ); refused instead of read wrong",
                             type._fullyQualifiedName.c_str(), version );
            fail();
            return;
        }
        if ( readVersion != version )
        {
            // 옛 판 — 머리 뒤 바이트를 그 타입의 패치 사슬로 올리고 거기서 이어 읽는다(`header` 와 같은 길).
            vector<uint8> bytes;
            readRemainingBytes( *_pArchive, bytes );
            FormatUpgradePayload payload{};
            payload._pBytes = &bytes;
            if ( check( FormatUpgradeStep::upgrade( FormatUpgradeDomain::ReflectedType, type._fullyQualifiedName.c_str(), readVersion, version, payload ) ) == false )
                return;
            unique_ptr<Archive> pUpgraded = make_unique<Archive>();
            pUpgraded->writeBytes( bytes.data(), bytes.size() );
            pUpgraded->setReadModeAndResetPosition( true );
            _pUpgradedArchive = std::move( pUpgraded );
            _pArchive         = _pUpgradedArchive.get();
        }
        reflectedProperties( type, pObject, true, 0 );
        if ( isOk() )
            check( SerializerUtil::isWithinDeclaredRanges( type, ObjectAddress{ pObject } ) );
    }

    void StateIO::reflectedDerived( const TypeInfo& dynamicType, const hashed_string& staticFqn, void* pObject )
    {
        if ( dynamicType.isDerivedFrom( staticFqn ) == false )
            SW_FATAL( "StateIO::reflectedAs: '%#' is not '%#' or derived from it - the fields would be read at another type's offsets", dynamicType._fullyQualifiedName.c_str(),
                      staticFqn.c_str() );
        reflectedObject( dynamicType, pObject, findReflectedLayoutHash( dynamicType ) );
    }

    void StateIO::reflectedProperties( const TypeInfo& type, void* pObject, const bool bStateRoleOnly, const uint32 depth )
    {
        using Internal               = StateIOReflectedInternal;
        using LeafKind               = Internal::LeafKind;
        const TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
        if ( depth > constant::reflection::kMaxParentChainDepth )
        {
            fail();
            return;
        }

        // 잎 하나(칸 값 또는 시퀀스 원소)를 싣는다. 종류(@p kind)는 칸마다 한 번 가른다 — 시퀀스 원소마다 이름을 다시 맞추지 않는다.
        auto valueLeaf = [&]( const PropertyInfo& prop, const hashed_string& leafType, const LeafKind kind, void* pValue )
        {
            switch ( kind )
            {
                case LeafKind::Bool:
                {
                    uint8 stored = *static_cast<bool*>( pValue ) ? SW_TRUE : SW_FALSE;
                    flag( stored );
                    *static_cast<bool*>( pValue ) = stored == SW_TRUE;
                    return;
                }
                case LeafKind::Int8:
                    value( *static_cast<int8*>( pValue ) );
                    return;
                case LeafKind::Int16:
                    value( *static_cast<int16*>( pValue ) );
                    return;
                case LeafKind::Int32:
                    value( *static_cast<int32*>( pValue ) );
                    return;
                case LeafKind::Int64:
                    value( *static_cast<int64*>( pValue ) );
                    return;
                case LeafKind::UInt8:
                    value( *static_cast<uint8*>( pValue ) );
                    return;
                case LeafKind::UInt16:
                    value( *static_cast<uint16*>( pValue ) );
                    return;
                case LeafKind::UInt32:
                    value( *static_cast<uint32*>( pValue ) );
                    return;
                case LeafKind::UInt64:
                    value( *static_cast<uint64*>( pValue ) );
                    return;
                case LeafKind::Float32:
                    value( *static_cast<float32*>( pValue ) );
                    return;
                case LeafKind::Float64:
                    value( *static_cast<float64*>( pValue ) );
                    return;
                case LeafKind::FloatVector:
                    Internal::valueFloatVector( *this, registry, leafType, pValue );
                    return;
                case LeafKind::String:
                    value( *static_cast<string*>( pValue ) );
                    return;
                case LeafKind::Name:
                    value( *static_cast<hashed_string*>( pValue ) );
                    return;
                case LeafKind::Enum:
                    Internal::valueEnum( *this, *registry.findEnum( leafType ), pValue );
                    return;
                case LeafKind::Struct:
                    reflectedProperties( *registry.findType( leafType ), pValue, false, depth + 1 );
                    return;
                case LeafKind::Unsupported:
                    break;
            }
            Internal::stopForUnsupportedField( type, prop );
        };

        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            if ( Internal::isCarried( prop, bStateRoleOnly ) == false )
                continue;
            if ( prop._bIsBitField == SW_TRUE )
            {
                uint8 stored = prop.getValue<bool>( pObject ) ? SW_TRUE : SW_FALSE;
                flag( stored );
                if ( isReading() )
                    prop.setValue<bool>( pObject, stored == SW_TRUE );
            }
            else if ( const ISequenceContainerWrapper* pSequence = Internal::findSequence( type, prop ) )
            {
                void*  pContainer   = prop.getRawPtr( pObject );
                uint32 elementCount = static_cast<uint32>( pSequence->getSize( pContainer ) );
                if ( pSequence->isFixedSize() )
                {
                    expect( elementCount ); // 고정 배열은 자라지 않는다 — 다른 길이는 다른 배치다
                }
                else
                {
                    count( 1, elementCount );
                    if ( isOk() && isReading() )
                    {
                        pSequence->clear( pContainer );
                        pSequence->reserve( pContainer, elementCount );
                        for ( uint32 index = 0; index < elementCount; ++index )
                        {
                            pSequence->addElementDefault( pContainer );
                        }
                    }
                }
                const LeafKind elementKind = Internal::classify( registry, prop._elementTypeName );
                for ( uint32 index = 0; index < elementCount && isOk(); ++index )
                {
                    valueLeaf( prop, prop._elementTypeName, elementKind, pSequence->getElement( pContainer, index ) );
                }
            }
            else
            {
                valueLeaf( prop, prop._typeName, Internal::classify( registry, prop._typeName ), prop.getRawPtr( pObject ) );
            }
            if ( isOk() == false )
                return;
        }
    }

    bool ReflectedStateUpgrade::writeFirstVersionHead( const hashed_string& typeFqn, Archive& outNewBody )
    {
        const TypeInfo* pType = engine::getSubsystem<TypeRegistry>().findType( typeFqn );
        if ( pType == nullptr )
            return false;
        constexpr uint32 kFirstVersion = 1; // 손 본문은 리플렉션 판이 생기기 전이다
        outNewBody << kFirstVersion;
        outNewBody << StateIO::computeReflectedLayoutHash( *pType );
        return true;
    }
} // namespace sw
