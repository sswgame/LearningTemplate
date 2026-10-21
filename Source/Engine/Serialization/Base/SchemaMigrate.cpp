#include "pch.h"

#include "Engine/Serialization/Base/SchemaMigrate.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/VectorUtil.h"
#include "Core/String/TagID.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Base/ContainerVisitor.h"
#include "Engine/Serialization/Base/SchemaMigrationRegistry.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

namespace sw
{
    namespace
    {
        struct SchemaMigrateInternal
        {
            static bool constructWithDefaultCtor( void* pPtr, const TypeInfo& typeInfo )
            {
                const FunctionInfo* pCtor = typeInfo.findMethod( hashed_string( "$ctor" ) );
                if ( pCtor == nullptr || pCtor->_invoker.isBound() == false )
                    return false;
                pCtor->_invoker( pPtr, TaskArgs{} );
                return true;
            }

            static bool destroyIfDefaultConstructed( void* pPtr, const TypeInfo& typeInfo )
            {
                const FunctionInfo* pCtor          = typeInfo.findMethod( hashed_string( "$ctor" ) );
                const bool          bCannotDestroy = pCtor == nullptr || pCtor->_invoker.isBound() == false || typeInfo._destroyInstance == nullptr;
                if ( bCannotDestroy )
                    return false;
                typeInfo._destroyInstance( pPtr );
                return true;
            }

            static string_view stripJSONQuotes( string_view text )
            {
                const bool bQuotedJSON = text.size() >= 2 && text.front() == '"' && text.back() == '"';
                if ( bQuotedJSON )
                {
                    text.remove_prefix( 1 );
                    text.remove_suffix( 1 );
                }
                return text;
            }

            static bool isStringType( hashed_string typeName )
            {
                return typeName.isPredefinedType( PredefinedNameType::NameType_string ) ||
                       typeName.isPredefinedType( PredefinedNameType::NameType_hashed_string );
            }

            /**
             * @brief 스칼라(정수, 실수, bool) 타입 이름인지 확인합니다. `formatWirePodToString` 이 텍스트로 만들 수 있는 타입과 같습니다.
             * @details 미리 정의된 이름만 본다. 타입 레지스트리에 기대지 않으므로 원시 타입을 등록하기 전에도 같은 결과를 낸다.
             */
            static bool isScalarTypeName( hashed_string typeName )
            {
                static constexpr PredefinedNameType kArrScalarType[] = {
                    PredefinedNameType::NameType_float32, PredefinedNameType::NameType_float64, PredefinedNameType::NameType_bool,
                    PredefinedNameType::NameType_int8, PredefinedNameType::NameType_int16, PredefinedNameType::NameType_int32,
                    PredefinedNameType::NameType_int64, PredefinedNameType::NameType_uint8, PredefinedNameType::NameType_uint16,
                    PredefinedNameType::NameType_uint32, PredefinedNameType::NameType_uint64 };
                for ( const PredefinedNameType scalarType : kArrScalarType )
                {
                    if ( typeName.isPredefinedType( scalarType ) )
                        return true;
                }
                return false;
            }

            /** @brief 기록 타입이 대상과 **다른** enum 이면 그 EnumInfo 를 반환합니다. 같은 enum 을 다른 이름(FQN, 짧은 이름, 별칭)으로 부른 것이면 nullptr 입니다. */
            static const EnumInfo* findOtherWireEnum( hashed_string targetTypeName, hashed_string wireTypeName )
            {
                if ( wireTypeName.empty() )
                    return nullptr;
                const TypeRegistry& registry  = engine::getSubsystem<TypeRegistry>();
                const EnumInfo*     pWireEnum = registry.findEnum( wireTypeName );
                if ( pWireEnum == nullptr || registry.findEnum( targetTypeName ) == pWireEnum )
                    return nullptr;
                return pWireEnum;
            }

            /**
             * @brief 기록 enum 의 payload 를 열거자 이름(텍스트)으로 만듭니다. XML 이 그 필드에 적었을 텍스트입니다(비트플래그는 `A | B`).
             * @details payload 를 끝까지 읽지 못하거나 모르는 열거자면 false 다. enum 은 자기 크기만큼만 쓰이므로 0 으로 둔 int64 위치에 읽는다.
             */
            static bool formatWireEnumToString( const uint8* pPayload, size_t payloadSize, hashed_string wireTypeName, const SerializeContext& context,
                                                BinaryWireVersion wireVersion, string& outText )
            {
                int64  wireValue{ 0 };
                size_t offset{ 0 };
                if ( SerializerUtil::deserializeValueBinary( MutableValueAddress{ &wireValue }, wireTypeName, pPayload, payloadSize, offset, context, wireVersion ) != FieldReadResult::Read ||
                     offset != payloadSize )
                    return false;
                StringBuilder<constant::kMaxBuffer8192> ss;
                SerializerUtil::valueToText( ss, ValueAddress{ &wireValue }, wireTypeName, context );
                outText = ss.c_str();
                return true;
            }

            static bool isNumericTypeName( hashed_string typeName )
            {
                const TypeInfo* pInfo = engine::getSubsystem<TypeRegistry>().findType( typeName );
                if ( pInfo == nullptr || pInfo->isPrimitive() == false )
                    return false;
                return typeName.isPredefinedType( PredefinedNameType::NameType_string ) == false &&
                       typeName.isPredefinedType( PredefinedNameType::NameType_hashed_string ) == false &&
                       typeName.isPredefinedType( PredefinedNameType::NameType_float2 ) == false &&
                       typeName.isPredefinedType( PredefinedNameType::NameType_float3 ) == false &&
                       typeName.isPredefinedType( PredefinedNameType::NameType_float4 ) == false &&
                       typeName.isPredefinedType( PredefinedNameType::NameType_float4x4 ) == false &&
                       typeName.isPredefinedType( PredefinedNameType::NameType_quaternion ) == false;
            }

            template <typename T>
            [[nodiscard]] static bool readPod( const uint8* pPayload, size_t payloadSize, T& out )
            {
                if ( payloadSize != sizeof( T ) )
                    return false;
                Memory::copy( &out, pPayload, sizeof( T ) );
                return true;
            }

            /** @brief `ReadType` 으로 읽어 `PrintType` 으로 넓혀 적습니다(`to_string` 오버로드가 네 개뿐이기 때문입니다). */
            template <typename ReadType, typename PrintType>
            static bool formatPodAs( const uint8* pPayload, size_t payloadSize, string& out )
            {
                ReadType value{};
                if ( readPod( pPayload, payloadSize, value ) == false )
                    return false;
                out = sw::to_string( static_cast<PrintType>( value ) );
                return true;
            }

            /**
             * @brief 기록 타입(wire type)을 아는 POD payload 를 **그 타입의** 텍스트로 만듭니다.
             * @details **크기만으로는 타입을 가를 수 없다.** `sizeof(float32) == sizeof(int32)` 이고
             *          `sizeof(float64) == sizeof(int64)` 이다. 크기로만 고르면 `float32` 분기가 돌지 않아(앞의 int32 분기가
             *          먼저 걸린다) `float32` 프로퍼티를 문자열로 바꾸는 스키마 이관이 `1.5f` 를 비트값 `"1069547520"` 으로 적는다.
             *          기록 타입은 바이너리 태그(`wireTypeHash`)와 `SchemaOrphanValue._wireTypeHash` 가 가지고 있다.
             * @return 기록 타입을 모르거나, 스칼라가 아니거나, payload 크기가 그 타입과 다르면 false 입니다.
             */
            static bool formatWirePodToString( const uint8* pPayload, size_t payloadSize, hashed_string wireTypeName, string& out )
            {
                if ( wireTypeName.empty() )
                    return false;

                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_float32 ) )
                    return formatPodAs<float32, float32>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_float64 ) )
                    return formatPodAs<float64, float64>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_bool ) )
                {
                    // 바이트로 읽어 0 과 1 로 접는다. bool 로 memcpy 하면 0 과 1 이 아닌 바이트가 정의되지 않은 값이 된다.
                    uint8 byteValue{ 0 };
                    if ( readPod( pPayload, payloadSize, byteValue ) == false )
                        return false;
                    out = sw::to_string( static_cast<int32>( byteValue != 0 ? 1 : 0 ) );
                    return true;
                }
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_int8 ) )
                    return formatPodAs<int8, int32>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_int16 ) )
                    return formatPodAs<int16, int32>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_int32 ) )
                    return formatPodAs<int32, int32>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_int64 ) )
                    return formatPodAs<int64, int64>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_uint8 ) )
                    return formatPodAs<uint8, uint32>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_uint16 ) )
                    return formatPodAs<uint16, uint32>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_uint32 ) )
                    return formatPodAs<uint32, uint32>( pPayload, payloadSize, out );
                if ( wireTypeName.isPredefinedType( PredefinedNameType::NameType_uint64 ) )
                    return formatPodAs<uint64, uint64>( pPayload, payloadSize, out );
                return false;
            }

            /**
             * @brief 기록 타입을 모를 때 크기로 짐작합니다.
             * @warning 정수와 실수를 **가를 수 없습니다**(크기가 같습니다). 정수로 읽습니다. 기록 타입을 아는
             *          스칼라는 `formatWirePodToString` 이 먼저 처리하므로, 여기까지 오는 것은 기록 타입을 모르거나
             *          스칼라가 아닌(열거형 등) payload 뿐입니다.
             */
            static bool formatPodToString( const uint8* pPayload, size_t payloadSize, string& out )
            {
                if ( payloadSize == sizeof( int32 ) )
                    return formatPodAs<int32, int32>( pPayload, payloadSize, out );
                if ( payloadSize == sizeof( int64 ) )
                    return formatPodAs<int64, int64>( pPayload, payloadSize, out );
                return false;
            }

            static vector<string> splitPath( const utf8* pDottedPath )
            {
                vector<string> listPart;
                if ( StringUtil::isNullOrEmpty( pDottedPath ) )
                    return listPart;
                string_splitter splitter( pDottedPath, { "." } );
                for ( string_view token : splitter.getSplitList() )
                {
                    string_view trimmedToken = StringUtil::trim( token );
                    if ( trimmedToken.empty() == false )
                        listPart.push_back( string{ trimmedToken } );
                }
                return listPart;
            }

            /**
             * @brief orphan 의 바이너리 값을 프로퍼티 위치에 적용합니다. `applyOrphanTo` 와 `applyOrphanToPath` 가 함께 씁니다.
             * @details 기록 타입이 프로퍼티 타입과 **같을 때만** 제자리로 읽는다. 다르면 곧바로 이관(`tryCoerceBinaryPayload`)으로
             *          보낸다. 본 역직렬화 경로(`BinarySerializer` 의 태그 루프)와 같은 규칙이다. 기록 타입으로 프로퍼티 위치에
             *          먼저 읽으면 구조가 다른 타입(int32 → int16, string 등)에서 그 위치와 이웃 필드를 덮어쓴다.
             *          프로퍼티 단위로 쓴다. 비트필드는 그 비트만 쓴다(값 주소로 쓰면 같은 바이트의 다른 플래그를 덮는다).
             * @param wireTypeName 기록 타입. 모르면 비웁니다(그때는 이관이 제 타입 읽기부터 합니다).
             */
            [[nodiscard]] static bool applyOrphanBinary( const ResolvedPropertyPath& target, const SchemaOrphanValue& orphan, hashed_string wireTypeName,
                                                         const SerializeContext& context )
            {
                const PropertyInfo& prop        = *target._pProp;
                const uint8*        pPayload    = orphan._listBinary.data();
                const size_t        payloadSize = orphan._listBinary.size();
                // 기록된 버전으로 읽는다. orphan 은 그것을 읽은 스트림의 버전을 가지고 있다. 끝까지 읽혀야 받는다(`SerializerUtil::applyPropertyBinary`).
                // 이관 함수가 bool 로 받으므로 객체째 실패도 적용 실패다(이관이 실패하면 읽기 전체가 실패한다).
                if ( wireTypeName == prop._typeName &&
                     SerializerUtil::applyPropertyBinary( prop, target._owner, pPayload, payloadSize, context, orphan._wireVersion ) == FieldReadResult::Read )
                    return true;
                return tryCoercePropertyBinary( prop, target._owner, pPayload, payloadSize, context, wireTypeName, orphan._wireVersion ) == FieldReadResult::Read;
            }

            /**
             * @brief 프로퍼티 하나의 값을 텍스트로 씁니다(`SerializerUtil::valueToText` 의 프로퍼티 판). 비트필드는 그 비트를 bool 로 씁니다.
             * @return 값 주소가 없으면(접근자만 있는 프로퍼티) false 입니다.
             */
            [[nodiscard]] static bool propertyValueToText( StringBuilder<constant::kMaxBuffer8192>& ss, const ResolvedPropertyPath& source, const SerializeContext& context )
            {
                const PropertyInfo& prop   = *source._pProp;
                void* const         pOwner = source._owner.get();
                if ( prop._bIsBitField == SW_TRUE )
                {
                    const bool bValue = prop.getValue<bool>( pOwner );
                    SerializerUtil::valueToText( ss, ValueAddress{ &bValue }, hashed_string( "bool" ), context );
                    return true;
                }
                const void* const pValue = prop.getRawPtr( pOwner );
                if ( pValue == nullptr )
                    return false;
                SerializerUtil::valueToText( ss, ValueAddress{ pValue }, prop._typeName, context );
                return true;
            }

            /**
             * @brief orphan 값을 경로 `pPath` 의 프로퍼티에 적용합니다. `applyOrphanTo` 와 `applyOrphanToPath` 는 orphan 을 찾는 법만 다릅니다.
             * @details 텍스트가 있으면 텍스트로, 없으면 바이너리로 적용한다. 바이너리의 기록 타입은 `wireTypeHint` 로 정하고, 비었으면 orphan 에
             *          남은 기록 타입 해시로 정한다. 프로퍼티 타입을 기록 타입으로 가정하면 타입이 바뀐 orphan(int32 → float32 등)을
             *          그 비트 그대로 제자리에 읽게 된다.
             */
            [[nodiscard]] static bool applyOrphanAt( const MutableObjectAddress instance, const TypeInfo& typeInfo, const utf8* pPath, const SchemaOrphanValue& orphan,
                                                     hashed_string wireTypeHint, const SerializeContext& context )
            {
                ResolvedPropertyPath target;
                if ( resolvePropertyPath( instance, typeInfo, pPath, target ) == false )
                    return false;

                if ( orphan._text.empty() == false )
                    return applyPropertyTextCoerced( *target._pProp, target._owner, orphan._text, context );
                if ( orphan._listBinary.empty() )
                    return false;

                hashed_string hint = wireTypeHint;
                if ( hint.empty() )
                    hint = findWireTypeName( orphan._wireTypeHash );
                // 기록 타입이 적혀 있는데 모르면(지운 enum, 지운 타입) 바이트의 뜻을 모른다. 크기로 짐작해 옮기지 않는다(바이너리 필드 읽기와 같은 규칙이다).
                if ( hint.empty() && orphan._wireTypeHash != 0 )
                    return false;
                return applyOrphanBinary( target, orphan, hint, context );
            }

            /** @brief orphan 의 이름 해시입니다. 이름을 intern 하지 않은 orphan 은 해시만 가집니다. */
            static uint32 getOrphanNameHash( const SchemaOrphanValue& orphan )
            {
                return orphan._nameHash != 0 ? orphan._nameHash : orphan._name.getHash();
            }

            /** @brief 타입(상속 포함)의 프로퍼티 이름과 별칭 가운데 orphan 의 이름이 있는지 확인합니다. */
            static bool isOrphanNameKnownTo( const TypeInfo& typeInfo, const SchemaOrphanValue& orphan )
            {
                const uint32 nameHash = getOrphanNameHash( orphan );
                for ( const PropertyInfo& prop : typeInfo.getPropertiesWithBase() )
                {
                    if ( prop.matchesNameHash( nameHash ) )
                        return true;
                }
                return false;
            }

            /**
             * @brief 로드가 성공으로 끝난 뒤 아무 데도 쓰이지 않은 orphan(읽지 못한 값, 타입에 없는 필드)을 **로드마다 한 번** 알립니다.
             * @details 이관 함수가 찾아 본 orphan(`_bClaimed`)은 그 함수가 처리했으므로 뺀다. 옛 TypeInfo 를 스테이징했으면 같은 본문을
             *          두 타입으로 읽어 한쪽만 아는 필드는 반드시 다른 쪽의 orphan 이 되므로, 두 타입 중 하나라도 아는 이름은 스테이징된
             *          인스턴스가 처리했다고 보고 뺀다. 이름은 필드마다 한 번만 적는다(컨테이너 원소가 여럿 실패해도 필드 하나로 센다).
             *          JSON, XML 의 Ignore 정책도 여기서 알린다. 숫자 필드의 "abc" 가 말없이 기본값으로 남지 않게 하려는 것이다.
             */
            static void warnDroppedOrphans( const TypeInfo& typeInfo, const TypeInfo* pLegacyTypeInfo, const vector<SchemaOrphanValue>& listOrphan )
            {
                vector<uint32> listListedHash;
                string         names;
                for ( const SchemaOrphanValue& orphan : listOrphan )
                {
                    if ( orphan._bClaimed )
                        continue;
                    const bool bStagedElsewhere =
                        pLegacyTypeInfo != nullptr && ( isOrphanNameKnownTo( typeInfo, orphan ) || isOrphanNameKnownTo( *pLegacyTypeInfo, orphan ) );
                    if ( bStagedElsewhere )
                        continue;
                    // 적힌 텍스트가 먼저다. 파일의 모르는 이름은 intern 하지 않고, 안쪽 원소의 이름은 타입 이름이 붙어 있다(`SpriteComponent._x`).
                    const uint32 nameHash = getOrphanNameHash( orphan );
                    string       shownName;
                    if ( orphan._writtenName.empty() == false )
                        shownName = orphan._writtenName;
                    else if ( orphan._name.empty() == false )
                        shownName = orphan._name.c_str();
                    else
                        shownName = "#" + to_string( nameHash ); // 바이너리는 이름 없이 해시만 싣는다
                    const uint32 shownHash = static_cast<uint32>( hashed_string::computeHash( string_view{ shownName } ) );
                    if ( VectorUtil::contains( listListedHash, shownHash ) )
                        continue;
                    listListedHash.push_back( shownHash );

                    if ( names.empty() == false )
                        names += ", ";
                    names += shownName;
                }
                if ( listListedHash.empty() )
                    return;
                SW_LOG_WARNING( "%#: dropped %# saved field(s) that the type does not have or could not read: %#", typeInfo._name.c_str(),
                                static_cast<uint32>( listListedHash.size() ), names );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void* createScratchInstance( const TypeInfo& typeInfo, vector<uint8>& listStorage )
    {
        if ( typeInfo._size == 0 )
            return nullptr;
        listStorage.assign( typeInfo._size, 0 );
        void* pBase = listStorage.data();

        if ( SchemaMigrateInternal::constructWithDefaultCtor( pBase, typeInfo ) )
            return pBase;

        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            void* pPropPtr = prop.getRawPtr( pBase );
            if ( pPropPtr == nullptr )
                return;
            NestedContainerInfo shape = prop.getContainerShape();
            if ( shape._wrapper != nullptr )
            {
                shape._wrapper->constructEmpty( pPropPtr );
            }
            else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_string ) )
                sw_placement_new( pPropPtr ) string();
            else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_hashed_string ) )
                sw_placement_new( pPropPtr ) hashed_string();
            else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_atomic_bool ) )
                sw_placement_new( pPropPtr ) atomic<bool>();
            else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_TagID ) )
                sw_placement_new( pPropPtr ) TagID();
            else
            {
                const TypeInfo* pNested = engine::getSubsystem<TypeRegistry>().findType( prop._typeName );
                if ( pNested != nullptr )
                    SchemaMigrateInternal::constructWithDefaultCtor( pPropPtr, *pNested );
            }
        }, true /* 상속 PROPERTY 포함 */ );
        return pBase;
    }

    void destroyScratchInstance( void* pInstance, const TypeInfo& typeInfo )
    {
        if ( pInstance == nullptr )
            return;
        if ( SchemaMigrateInternal::destroyIfDefaultConstructed( pInstance, typeInfo ) )
            return;
        typeInfo.forEachProperty( [&]( const PropertyInfo& prop )
        {
            void* pPropPtr = prop.getRawPtr( pInstance );
            if ( pPropPtr == nullptr )
                return;
            NestedContainerInfo shape = prop.getContainerShape();
            if ( shape._wrapper != nullptr )
            {
                shape._wrapper->destroyContainer( pPropPtr );
            }
            else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_string ) )
                std::destroy_at( static_cast<string*>( pPropPtr ) );
            else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_hashed_string ) )
                std::destroy_at( static_cast<hashed_string*>( pPropPtr ) );
            else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_atomic_bool ) )
                std::destroy_at( static_cast<atomic<bool>*>( pPropPtr ) );
            else if ( prop._typeName.isPredefinedType( PredefinedNameType::NameType_TagID ) )
                std::destroy_at( static_cast<TagID*>( pPropPtr ) );
            else
            {
                const TypeInfo* pNested = engine::getSubsystem<TypeRegistry>().findType( prop._typeName );
                if ( pNested != nullptr )
                    SchemaMigrateInternal::destroyIfDefaultConstructed( pPropPtr, *pNested );
            }
        }, true /* 상속 PROPERTY 포함 */ );
    }

    const SchemaOrphanValue* SchemaMigrateContext::findOrphan( hashed_string name ) const
    {
        if ( _pOrphans == nullptr )
            return nullptr;
        const uint32             nameHash = name.getHash();
        const SchemaOrphanValue* pFirst   = nullptr;
        for ( const SchemaOrphanValue& orphanValue : *_pOrphans )
        {
            // 파일에서 읽은 orphan 은 이름을 intern 하지 않고 해시만 가질 수 있다(XML, JSON 읽기). 그때는 해시로 맞춰 본다.
            if ( orphanValue._name != name && ( orphanValue._name.empty() == false || orphanValue._nameHash != nameHash ) )
                continue;
            // 같은 이름은 모두 이관이 처리한 것이다. 컨테이너 원소가 여럿 실패하면 같은 필드 이름의 orphan 이 여럿 생긴다.
            orphanValue._bClaimed = true;
            if ( pFirst == nullptr )
                pFirst = &orphanValue;
        }
        return pFirst;
    }

    const SchemaOrphanValue* SchemaMigrateContext::findOrphanHash( uint32 nameHash ) const
    {
        if ( _pOrphans == nullptr || nameHash == 0 )
            return nullptr;
        const SchemaOrphanValue* pFirst = nullptr;
        for ( const SchemaOrphanValue& orphanValue : *_pOrphans )
        {
            if ( orphanValue._nameHash != nameHash )
                continue;
            orphanValue._bClaimed = true;
            if ( pFirst == nullptr )
                pFirst = &orphanValue;
        }
        return pFirst;
    }

    bool SchemaMigrateContext::applyOrphanTo( hashed_string propName, hashed_string wireTypeHint ) const
    {
        const SchemaOrphanValue* pOrphan = findOrphan( propName );
        if ( pOrphan == nullptr )
            pOrphan = findOrphanHash( propName.getHash() );
        const bool bCannotApplyOrphan = pOrphan == nullptr || _pInstance == nullptr || _pTypeInfo == nullptr;
        if ( bCannotApplyOrphan )
            return false;

        const SerializeContext& context = _serializeContext;
        return SchemaMigrateInternal::applyOrphanAt( MutableObjectAddress{ _pInstance }, *_pTypeInfo, propName.c_str(), *pOrphan, wireTypeHint, context );
    }

    bool SchemaMigrateContext::applyOrphanToPath( const utf8* pDottedPath, hashed_string wireTypeHint ) const
    {
        // `applyOrphanTo` 와 같은 검사.
        const bool bInvalidDottedPath = pDottedPath == nullptr || _pInstance == nullptr || _pTypeInfo == nullptr;
        if ( bInvalidDottedPath )
            return false;
        const vector<string> listPart = SchemaMigrateInternal::splitPath( pDottedPath );
        if ( listPart.empty() )
            return false;

        // orphan 이름은 보통 리프 이름이거나 옛 키 전체다
        const hashed_string      leaf( listPart.back().c_str() );
        const SchemaOrphanValue* pOrphan = findOrphan( leaf );
        if ( pOrphan == nullptr )
            pOrphan = findOrphan( hashed_string( pDottedPath ) );
        if ( pOrphan == nullptr )
            return false;

        const SerializeContext& context = _serializeContext;
        return SchemaMigrateInternal::applyOrphanAt( MutableObjectAddress{ _pInstance }, *_pTypeInfo, pDottedPath, *pOrphan, wireTypeHint, context );
    }

    bool SchemaMigrateContext::moveProperty( hashed_string fromProp, hashed_string toProp ) const
    {
        return movePropertyPath( fromProp.c_str(), toProp.c_str() );
    }

    bool SchemaMigrateContext::movePropertyPath( const utf8* pFromPath, const utf8* pToPath ) const
    {
        const bool bCannotMovePath = _pInstance == nullptr || _pTypeInfo == nullptr || pFromPath == nullptr || pToPath == nullptr;
        if ( bCannotMovePath )
            return false;

        const SerializeContext& context = _serializeContext;

        ResolvedPropertyPath       source;
        const MutableObjectAddress srcRoot{ _pLegacyInstance != nullptr ? _pLegacyInstance : _pInstance };
        const TypeInfo*            pSrcType = _pLegacyTypeInfo != nullptr ? _pLegacyTypeInfo : _pTypeInfo;
        if ( resolvePropertyPath( srcRoot, *pSrcType, pFromPath, source ) == false )
            return false;

        ResolvedPropertyPath target;
        if ( resolvePropertyPath( MutableObjectAddress{ _pInstance }, *_pTypeInfo, pToPath, target ) == false )
            return false;

        StringBuilder<constant::kMaxBuffer8192> ss;
        if ( SchemaMigrateInternal::propertyValueToText( ss, source, context ) == false )
            return false;
        return applyPropertyTextCoerced( *target._pProp, target._owner, ss.view(), context );
    }

    bool SchemaMigrateContext::setPropertyFromText( hashed_string propName, string_view text ) const
    {
        if ( _pInstance == nullptr || _pTypeInfo == nullptr )
            return false;
        ResolvedPropertyPath target;
        if ( resolvePropertyPath( MutableObjectAddress{ _pInstance }, *_pTypeInfo, propName.c_str(), target ) == false )
            return false;
        return applyPropertyTextCoerced( *target._pProp, target._owner, text, _serializeContext );
    }

    bool isValueOnlyCoercion( hashed_string targetTypeName, hashed_string wireTypeName )
    {
        if ( wireTypeName.empty() || wireTypeName == targetTypeName )
            return false;
        // 기록 타입이 다른 enum 이면 열거자 이름으로만 옮긴다. 제 타입으로 다시 읽으면 이름 해시가 수로 읽힌다.
        if ( SchemaMigrateInternal::findOtherWireEnum( targetTypeName, wireTypeName ) != nullptr )
            return true;
        if ( SchemaMigrateInternal::isScalarTypeName( wireTypeName ) == false )
            return false;
        return SchemaMigrateInternal::isScalarTypeName( targetTypeName ) || SchemaMigrateInternal::isStringType( targetTypeName );
    }

    hashed_string findWireTypeName( uint32 wireTypeHash )
    {
        const TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
        hashed_string       typeName = registry.canonicalTypeNameByHash( wireTypeHash );
        if ( typeName.empty() == false )
            return typeName;
        const EnumInfo* pEnumInfo = registry.findEnumByNameHash( wireTypeHash );
        typeName                  = ( pEnumInfo != nullptr ) ? pEnumInfo->_fullyQualifiedName : hashed_string{};
        return typeName;
    }

    FieldReadResult tryCoerceBinaryPayload( MutableValueAddress value, hashed_string targetTypeName, const uint8* pPayload, size_t payloadSize,
                                            const SerializeContext& context, hashed_string wireTypeName, BinaryWireVersion wireVersion )
    {
        void* const pValPtr = value.get();
        if ( pValPtr == nullptr || pPayload == nullptr )
            return FieldReadResult::FieldFailed;

        // 값으로만 옮기는 쌍이면(`isValueOnlyCoercion`) 기록 값을 텍스트로 만들어 대상 타입으로 다시 읽는다(JSON, XML 과 같은 규칙이다).
        //  - 기록 타입을 아는 스칼라 → 다른 스칼라나 문자열. 아래의 제 타입 읽기를 먼저 하면 크기가 같은 스칼라는 비트가 그대로 재해석되고
        //    (int32 100 → float32 1.4e-43), 문자열은 int32 0 을 길이 0 으로 읽어 "" 가 된다. 값으로 못 옮기면(float32 1.5 → int32) 실패다.
        //  - 기록 타입이 다른 enum → 열거자 이름. 문자열은 이름을, 같은 이름이 있는 enum 은 그 열거자를 받고, 수는 받지 않는다(XML 에 적힌 이름과 같다).
        //    제 타입 읽기로 가면 열거자 이름 해시(4 바이트)가 int32, float32 로 그대로 읽힌다.
        // payload 크기가 기록 타입과 안 맞아도(손상) 재해석하지 않고 실패로 끝낸다.
        if ( isValueOnlyCoercion( targetTypeName, wireTypeName ) )
        {
            string     wireText;
            const bool bWireEnum  = SchemaMigrateInternal::findOtherWireEnum( targetTypeName, wireTypeName ) != nullptr;
            const bool bFormatted = bWireEnum ? SchemaMigrateInternal::formatWireEnumToString( pPayload, payloadSize, wireTypeName, context, wireVersion, wireText )
                                              : SchemaMigrateInternal::formatWirePodToString( pPayload, payloadSize, wireTypeName, wireText );
            if ( bFormatted == false )
                return FieldReadResult::FieldFailed;
            return ContainerVisitor::fieldResultOf( parseTextValueCoerced( value, targetTypeName, wireText, context ) );
        }

        // 값 하나로 읽을 수 없는 대상(컨테이너 프로퍼티의 `map` · `vector` 같은 이름, 처리기 없는 타입)은 이관 대상이 아니다. 값 읽기를 시도하지 않는다.
        if ( SerializerUtil::hasBinaryValueCodec( targetTypeName, context ) == false )
            return FieldReadResult::FieldFailed;

        size_t offset{ 0 };
        switch ( SerializerUtil::deserializeValueBinary( value, targetTypeName, pPayload, payloadSize, offset, context, wireVersion ) )
        {
            case FieldReadResult::Read:
            {
                if ( offset == payloadSize )
                    return FieldReadResult::Read;
                break;
            }
            case FieldReadResult::FieldFailed:
            {
                break;
            }
            case FieldReadResult::ObjectRejected:
            {
                // 구조체 안의 금지된 필드다. 다른 해석을 더 해 보면 그것이 칸 실패로 낮아진다.
                return FieldReadResult::ObjectRejected;
            }
        }

        // POD → 문자열. 기록 타입을 아는 스칼라는 위에서 끝났으므로 여기서는 크기로 짐작한다(정수와 실수를 가르지 못한다).
        if ( SchemaMigrateInternal::isStringType( targetTypeName ) )
        {
            string asText;
            if ( SchemaMigrateInternal::formatPodToString( pPayload, payloadSize, asText ) )
                return ContainerVisitor::fieldResultOf( parseTextValueCoerced( value, targetTypeName, asText, context ) );

            // 길이 접두 문자열 blob 은 맨 앞의 제 타입 읽기가 이미 다뤘다. 여기서는 길이가 맞으면 접두 뒤 바이트를 텍스트로 읽어 본다
            if ( payloadSize >= sizeof( uint32 ) )
            {
                uint32 len{ 0 };
                Memory::copy( &len, pPayload, sizeof( uint32 ) );
                if ( sizeof( uint32 ) + len == payloadSize )
                {
                    const string_view textValue{ reinterpret_cast<const utf8*>( pPayload + sizeof( uint32 ) ), len };
                    return ContainerVisitor::fieldResultOf( parseTextValueCoerced( value, targetTypeName, textValue, context ) );
                }
            }
        }

        // 문자열 blob → 숫자
        if ( SchemaMigrateInternal::isNumericTypeName( targetTypeName ) && payloadSize >= sizeof( uint32 ) )
        {
            uint32 len{ 0 };
            Memory::copy( &len, pPayload, sizeof( uint32 ) );
            if ( sizeof( uint32 ) + len == payloadSize )
            {
                const string_view textValue{ reinterpret_cast<const utf8*>( pPayload + sizeof( uint32 ) ), len };
                return ContainerVisitor::fieldResultOf( parseTextValueCoerced( value, targetTypeName, textValue, context ) );
            }
        }

        // 크기가 같은 POD 를 그대로 재해석한다(int32↔float32 등). 마지막 수단이다.
        offset                                        = 0;
        const SerializeContext::BinaryReadFn* pReader = context.findBinaryReader( targetTypeName );
        if ( pReader != nullptr )
        {
            if ( ( *pReader )( pValPtr, pPayload, payloadSize, offset ) && offset == payloadSize )
                return FieldReadResult::Read;
        }

        return FieldReadResult::FieldFailed;
    }

    FieldReadResult tryCoercePropertyBinary( const PropertyInfo& prop, const MutableObjectAddress instance, const uint8* pPayload, const size_t payloadSize,
                                             const SerializeContext& context, const hashed_string wireTypeName, const BinaryWireVersion wireVersion )
    {
        void* const pInstance = instance.get();
        if ( pInstance == nullptr )
            return FieldReadResult::FieldFailed;
        if ( prop._bIsBitField == SW_TRUE )
        {
            // 비트필드는 주소를 가질 수 없어 임시 bool 에 옮기고 그 비트만 쓴다. 실패하면 비트는 그대로다.
            bool                  bValue = prop.getValue<bool>( pInstance );
            const FieldReadResult result = tryCoerceBinaryPayload( MutableValueAddress{ &bValue }, hashed_string( "bool" ), pPayload, payloadSize, context, wireTypeName, wireVersion );
            switch ( result )
            {
                case FieldReadResult::Read:
                {
                    prop.setValue<bool>( pInstance, bValue );
                    return FieldReadResult::Read;
                }
                case FieldReadResult::FieldFailed:
                case FieldReadResult::ObjectRejected:
                {
                    return result;
                }
            }
            return FieldReadResult::ObjectRejected;
        }
        void* const pValue = prop.getRawPtr( pInstance );
        if ( pValue == nullptr )
            return FieldReadResult::FieldFailed;
        return tryCoerceBinaryPayload( MutableValueAddress{ pValue }, prop._typeName, pPayload, payloadSize, context, wireTypeName, wireVersion );
    }

    bool applyPropertyTextCoerced( const PropertyInfo& prop, const MutableObjectAddress instance, const string_view valStr, const SerializeContext& context )
    {
        void* const pInstance = instance.get();
        if ( pInstance == nullptr )
            return false;
        if ( prop._bIsBitField == SW_TRUE )
        {
            bool bValue = prop.getValue<bool>( pInstance );
            if ( parseTextValueCoerced( MutableValueAddress{ &bValue }, hashed_string( "bool" ), valStr, context ) == false )
                return false;
            prop.setValue<bool>( pInstance, bValue );
            return true;
        }
        void* const pValue = prop.getRawPtr( pInstance );
        return pValue != nullptr && parseTextValueCoerced( MutableValueAddress{ pValue }, prop._typeName, valStr, context );
    }

    bool parseTextValueCoerced( MutableValueAddress value, hashed_string typeName, string_view valStr,
                                const SerializeContext& context )
    {
        void* const pValPtr = value.get();
        if ( pValPtr == nullptr )
            return false;

        const string_view stripped = SchemaMigrateInternal::stripJSONQuotes( valStr );
        if ( SerializerUtil::parseTextValue( value, typeName, valStr, context ) )
            return true;
        if ( stripped.data() != valStr.data() || stripped.size() != valStr.size() )
        {
            if ( SerializerUtil::parseTextValue( value, typeName, stripped, context ) )
                return true;
        }

        // 숫자로 기록된 값 → 문자열. 대상이 문자열 타입이면 텍스트를 그대로 담는다.
        if ( SchemaMigrateInternal::isStringType( typeName ) )
        {
            if ( engine::getSubsystem<TypeRegistry>().isType( typeName, "hashed_string" ) )
            {
                *static_cast<hashed_string*>( pValPtr ) =
                    hashed_string( stripped.data(), static_cast<uint32>( stripped.size() ) );
            }
            else
                *static_cast<string*>( pValPtr ) = string( stripped );
            return true;
        }

        return false;
    }

    bool resolvePropertyPath( const MutableObjectAddress root, const TypeInfo& typeInfo, const utf8* pDottedPath, ResolvedPropertyPath& outResolved )
    {
        outResolved = ResolvedPropertyPath{};
        if ( root.get() == nullptr || pDottedPath == nullptr )
            return false;

        const vector<string> listPart = SchemaMigrateInternal::splitPath( pDottedPath );
        if ( listPart.empty() )
            return false;

        // 경로 중간의 구조체 값은 다음 부분의 객체 주소다. 끝의 프로퍼티는 주소를 꺼내지 않는다(비트필드는 값 주소가 없다).
        void*           pCur     = root.get();
        const TypeInfo* pCurType = &typeInfo;

        for ( size_t partIndex = 0; partIndex < listPart.size(); ++partIndex )
        {
            const hashed_string name( listPart[partIndex].c_str() );
            const PropertyInfo* pProp = pCurType->findPropertyInHierarchy( name );
            if ( pProp == nullptr )
                return false;

            if ( partIndex + 1 == listPart.size() )
            {
                outResolved = ResolvedPropertyPath{ MutableObjectAddress{ pCur }, pProp };
                return true;
            }

            const TypeInfo* pNested = engine::getSubsystem<TypeRegistry>().findType( pProp->_typeName );
            void* const     pNext   = pProp->getRawPtr( pCur );
            if ( pNested == nullptr || pNext == nullptr )
                return false;
            pCur     = pNext;
            pCurType = pNested;
        }
        return false;
    }

    SW_LOG_CALLER( "SchemaMigrate" );

    bool runSchemaMigrateStep( uint32 fromVersion, uint32 currentVersion, void* pInstance, const TypeInfo& typeInfo,
                               void* pLegacyInstance, const TypeInfo* pLegacyTypeInfo,
                               const vector<SchemaOrphanValue>& listOrphan,
                               bool bMigrateRequired, const SerializeContext& context )
    {
        const bool bMigrateApplies = fromVersion != currentVersion || listOrphan.empty() == false || pLegacyInstance != nullptr;
        // 찾기는 이관이 쓰일 수 있을 때만 한다. 버전이 같고 남은 값이 없는 읽기(거의 모든 읽기)는 표를 보지 않는다.
        const SchemaMigrateFn migrate = bMigrateApplies ? SchemaMigrationRegistry::find( typeInfo, fromVersion ) : nullptr;
        if ( migrate != nullptr )
        {
            SchemaMigrateContext migrateContext{ context };
            migrateContext._fromVersion     = fromVersion;
            migrateContext._toVersion       = currentVersion;
            migrateContext._pInstance       = pInstance;
            migrateContext._pTypeInfo       = &typeInfo;
            migrateContext._pLegacyInstance = pLegacyInstance;
            migrateContext._pLegacyTypeInfo = pLegacyTypeInfo;
            migrateContext._pOrphans        = &listOrphan;
            if ( migrate( migrateContext ) == false )
                return false;
            // 이관이 받아 준 로드다. 이관이 찾아 보지 않은 orphan 은 버려진다.
            SchemaMigrateInternal::warnDroppedOrphans( typeInfo, pLegacyTypeInfo, listOrphan );
            return true;
        }

        if ( bMigrateRequired )
        {
            SW_LOG_ERROR( "(%#) schema version %# -> %# needs a migration and none is registered for (%#, %#) - rewrite the data, or register SW_SCHEMA_MIGRATION only when a value must be computed (%# orphans: %#)",
                          typeInfo._name.c_str(), fromVersion, currentVersion, typeInfo._fullyQualifiedName.c_str(), fromVersion,
                          static_cast<uint32>( listOrphan.size() ), listOrphan.empty() ? "" : listOrphan[0]._name.c_str() );
            return false;
        }
        // 이관 없이 받아 주는 로드다(JSON, XML 의 Ignore 정책). orphan 은 모두 버려진다.
        SchemaMigrateInternal::warnDroppedOrphans( typeInfo, pLegacyTypeInfo, listOrphan );
        return true;
    }

} // namespace sw
