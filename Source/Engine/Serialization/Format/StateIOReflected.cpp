#include "pch.h"

#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/EnumInfo.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectVisit.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/FormatUpgrade.h"
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
            static constexpr uint32 kLayoutRuleVersion = 0;

            /**
             * @brief 잎 칸의 종류입니다. 칸마다 한 번 가른다.
             * @details 내장 타입은 `ReflectBuiltins.xxx` 의 State 역할 · Layout 이 정한다 — num 은 그 타입 그대로(`StateIO::value`), f32 · i32 는 성분마다,
             *          none 은 아래 손 처리기(bool · string · hashed_string)다. 바이트는 옛 이름 표 때와 같다(float 벡터 · int2 는 성분 순서 그대로).
             *          오브젝트 핸들은 싣지 않는다 — 이 바이트는 세이브에도 실리는데(`ComponentStateCodec`) 핸들은 런타임 id 라 다른 실행에서 남의 오브젝트를 가리킨다. 컴포넌트의
             *          핸들 PROPERTY 는 씬 오브젝트 스냅샷이 파일 id 로 옮기고(`ObjectStateBatch`), 상태 바이트에 들 참조는 `SceneObjectReference`(씬 GUID + 파일 id)로 적는다.
             */
            enum class LeafKind : uint8
            {
                Unsupported,
                Bool,
                Number,   ///< num Layout — `kArrValueNumber` 의 그 타입 칸
                Float32s, ///< f32 Layout — float32 를 성분 수만큼
                Int32s,   ///< i32 Layout — int32 를 성분 수만큼
                String,
                Name,
                Enum,
                Struct,
            };

            using ValueNumberFn = void ( * )( StateIO& io, void* pValue );

            template <typename T>
            static void valueNumber( StateIO& io, void* pValue )
            {
                io.value( *static_cast<T*>( pValue ) );
            }

            /** @brief State 역할인 num 줄이면 그 타입의 칸 함수, 아니면 nullptr 입니다. 산술이 아닌 타입은 `valueNumber<T>` 를 인스턴스화하지 않는다. */
            template <typename T, bool bNumber>
            static constexpr ValueNumberFn pickValueNumber()
            {
                if constexpr ( bNumber )
                    return &valueNumber<T>;
                else
                    return nullptr;
            }

            /** @brief State 역할인 num 줄이면 참입니다. */
            static constexpr bool isStateNumber( const ReflectBuiltinIndex builtinIndex )
            {
                const ReflectBuiltinTraits traits = ReflectBuiltinTraitTable::get( builtinIndex );
                return traits.hasRole( ReflectBuiltinRole::State ) && traits._layout == ReflectBuiltinLayout::Number;
            }

            /** @brief num 줄마다 칸 함수입니다. 순서는 `ReflectBuiltinIndex` 입니다. */
            static ValueNumberFn findValueNumber( const ReflectBuiltinIndex builtinIndex )
            {
                static constexpr ValueNumberFn kArrValueNumber[] = {
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, ... ) pickValueNumber<CppType, isStateNumber( ReflectBuiltinIndex::k##Canon )>(),
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
                };
                return builtinIndex < ReflectBuiltinIndex::kCount ? kArrValueNumber[static_cast<size_t>( builtinIndex )] : nullptr;
            }

            static LeafKind classifyBuiltin( const ReflectBuiltinIndex builtinIndex )
            {
                const ReflectBuiltinTraits traits = ReflectBuiltinTraitTable::get( builtinIndex );
                if ( traits.hasRole( ReflectBuiltinRole::State ) == false )
                    return LeafKind::Unsupported;
                switch ( traits._layout )
                {
                    case ReflectBuiltinLayout::Number:
                        return LeafKind::Number;
                    case ReflectBuiltinLayout::Float32:
                        return LeafKind::Float32s;
                    case ReflectBuiltinLayout::Int32:
                        return LeafKind::Int32s;
                    case ReflectBuiltinLayout::Angle: // State 가 아니다(`ReflectBuiltinTraitTable::isStateCarried`)
                        return LeafKind::Unsupported;
                    case ReflectBuiltinLayout::None:
                        break;
                }
                switch ( builtinIndex ) // 손 처리기 — `ReflectBuiltinTraitTable::hasStateHandHandler` 의 줄에서 오브젝트 핸들만 뺀다(상태 해시는 싣는다)
                {
                    case ReflectBuiltinIndex::kbool:
                        return LeafKind::Bool;
                    case ReflectBuiltinIndex::kstring:
                        return LeafKind::String;
                    case ReflectBuiltinIndex::khashed_string:
                        return LeafKind::Name;
                    default:
                        return LeafKind::Unsupported;
                }
            }

            static LeafKind classify( const ReflectValueFacts& facts )
            {
                if ( facts._builtinIndex != ReflectBuiltinIndex::kNone )
                    return classifyBuiltin( facts._builtinIndex );
                if ( facts._pEnum != nullptr )
                    return LeafKind::Enum;
                return facts.findStructType() != nullptr ? LeafKind::Struct : LeafKind::Unsupported;
            }

            /** @brief 칸의 잎(값, 시퀀스면 원소) 형식의 사실입니다. 프로퍼티의 묶음에서 읽는다 — 롤백 · 스냅샷마다 칸과 원소 수만큼 이름을 찾지 않는다. */
            static ReflectValueFacts resolveLeafFacts( const TypeRegistry& registry, const PropertyInfo& prop, const bool bSequence )
            {
                return bSequence ? prop.resolveElementFacts( registry ) : prop.resolveValueFacts( registry );
            }

            /** @brief 칸이 싣는 시퀀스 래퍼입니다. 컨테이너가 아니면 nullptr, State 역할 밖(맵 · 집합 · 중첩)은 실을 수 없어 멈춘다(`PropertyInfo::findStateSequence`). */
            static const ISequenceContainerWrapper* findSequence( const TypeInfo& owner, const PropertyInfo& prop )
            {
                if ( prop._bIsContainer == SW_FALSE )
                    return nullptr;
                const ISequenceContainerWrapper* pSequence = prop.findStateSequence();
                if ( pSequence == nullptr )
                    stopForUnsupportedField( owner, prop );
                return pSequence;
            }

            [[noreturn]] static void stopForUnsupportedField( const TypeInfo& owner, const PropertyInfo& prop )
            {
                reportFatalError( "state field '%#::%#' (%#) cannot be carried by StateIO::reflected - use a builtin with the State role (ReflectBuiltins.xxx), an enum, a reflected struct "
                                  "or a sequence of those, or take the field out of RuntimeState and carry it with io.value in serializeState; an object reference is a SceneObjectReference, "
                                  "not a runtime handle",
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
                    reportFatalError( "state type '%#' nests reflected structs deeper than %# - a struct contains itself", type._fullyQualifiedName.c_str(), constant::reflection::kMaxParentChainDepth );
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
                        appendLeafCanonical( registry, type, prop, resolveLeafFacts( registry, prop, pSequence != nullptr ), leafType, depth, inoutCanonical );
                    if ( pSequence != nullptr )
                        inoutCanonical += ']';
                    inoutCanonical += ';';
                }
            }

            static void appendLeafCanonical( const TypeRegistry& registry, const TypeInfo& owner, const PropertyInfo& prop, const ReflectValueFacts& facts, const hashed_string& leafType,
                                             const uint32 depth, string& inoutCanonical )
            {
                const LeafKind kind = classify( facts );
                if ( kind == LeafKind::Unsupported )
                    stopForUnsupportedField( owner, prop );
                inoutCanonical += leafType.c_str();
                if ( kind == LeafKind::Struct )
                {
                    inoutCanonical += '{';
                    appendCanonical( registry, *facts._pType, false, depth + 1, inoutCanonical );
                    inoutCanonical += '}';
                }
            }

            /** @brief f32 · i32 Layout 값 하나를 성분마다 싣습니다. 성분 수는 Layout 의 멤버 수다(`ReflectedValueUtil::getBuiltinComponentCount`). */
            template <typename TElement>
            static void valueComponents( StateIO& io, const ReflectBuiltinIndex builtinIndex, void* pValue )
            {
                const uint8 count      = ReflectedValueUtil::getBuiltinComponentCount( builtinIndex );
                TElement*   pComponent = static_cast<TElement*>( pValue );
                for ( uint8 index = 0; index < count; ++index )
                {
                    io.value( pComponent[index] );
                }
            }

            static void valueEnum( StateIO& io, const EnumInfo& info, void* pValue )
            {
                int64 stored = info.readValueFromMemory( pValue );
                io.value( stored );
                if ( io.isReading() && io.check( info.isDeclaredValue( stored ) ) ) // 열거자 밖 값은 거절한다(표식만 보는 isValidValue 로는 지나갔다)
                    info.writeValueToMemory( pValue, stored );
            }
        };
    } // namespace

    const TypeInfo& StateIO::requireReflectedType( const hashed_string& fqn )
    {
        // 레지스트리를 이름으로 바로 찾는다 — `StaticType()` 은 DLL 밖으로 내보내지 않은 키트 타입에서 링크가 깨진다(`FixedLayout::findCodec` 과 같은 까닭).
        const TypeInfo* pType = TypeLookupCache{}.find( fqn );
        if ( pType == nullptr )
            reportFatalError( "StateIO::reflected: '%#' is not a registered reflected type - add REFLECT() and REFLECT_BODY() or check the reflection generation", fqn.c_str() );
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

    void StateIO::writeReflectedHeader( const TypeInfo& type, Archive& outArchive )
    {
        outArchive << type._metadata->getVersion();
        outArchive << findReflectedLayoutHash( type );
    }

    void StateIO::reflectedObject( const TypeInfo& type, void* pObject, const uint64 layoutHash )
    {
        const uint32 version = type._metadata->getVersion();
        if ( isWriting() )
        {
            *_pArchive << version;
            *_pArchive << layoutHash; // 머리 모양은 writeReflectedHeader 와 같다(해시는 부른 쪽이 캐시에서 넘겼다)
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
            check( SerializerUtil::areValuesWithinDeclaredRanges( type, ObjectAddress{ pObject } ) ); // 손 io.check 처럼 로그 없이 거절한다
    }

    void StateIO::reflectedDerived( const TypeInfo& dynamicType, const hashed_string& staticFqn, void* pObject )
    {
        if ( dynamicType.isDerivedFrom( staticFqn ) == false )
            reportFatalError( "StateIO::reflectedAs: '%#' is not '%#' or derived from it - the fields would be read at another type's offsets", dynamicType._fullyQualifiedName.c_str(),
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

        // 잎 하나(칸 값 또는 시퀀스 원소)를 싣는다. 형식의 사실(@p facts)과 종류(@p kind)는 칸마다 한 번 푼다 — 시퀀스 원소마다 이름을 다시 맞추지 않는다.
        auto valueLeaf = [&]( const PropertyInfo& prop, const ReflectValueFacts& facts, const LeafKind kind, void* pValue )
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
                case LeafKind::Number:
                    Internal::findValueNumber( facts._builtinIndex )( *this, pValue );
                    return;
                case LeafKind::Float32s:
                    Internal::valueComponents<float32>( *this, facts._builtinIndex, pValue );
                    return;
                case LeafKind::Int32s:
                    Internal::valueComponents<int32>( *this, facts._builtinIndex, pValue );
                    return;
                case LeafKind::String:
                    value( *static_cast<string*>( pValue ) );
                    return;
                case LeafKind::Name:
                    value( *static_cast<hashed_string*>( pValue ) );
                    return;
                case LeafKind::Enum:
                    Internal::valueEnum( *this, *facts._pEnum, pValue );
                    return;
                case LeafKind::Struct:
                    reflectedProperties( *facts._pType, pValue, false, depth + 1 );
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
                        pSequence->clear( pContainer );
                }
                const ReflectValueFacts elementFacts = Internal::resolveLeafFacts( registry, prop, true );
                const LeafKind          elementKind  = Internal::classify( elementFacts );
                // 원소는 읽는 만큼만 만든다 — 개수를 먼저 믿고 다 만들면 망가진 개수 하나가 남은 바이트 × 원소 크기만큼 메모리를 잡는다(원소의 최소 바이트를 손으로 적던 자리).
                const bool bGrowWhileReading = isReading() && pSequence->isFixedSize() == false;
                for ( uint32 index = 0; index < elementCount && isOk(); ++index )
                {
                    if ( bGrowWhileReading )
                        pSequence->addElementDefault( pContainer );
                    valueLeaf( prop, elementFacts, elementKind, pSequence->getElement( pContainer, index ) );
                }
            }
            else
            {
                const ReflectValueFacts valueFacts = Internal::resolveLeafFacts( registry, prop, false );
                valueLeaf( prop, valueFacts, Internal::classify( valueFacts ), prop.getRawPtr( pObject ) );
            }
            if ( isOk() == false )
                return;
        }
    }

} // namespace sw
