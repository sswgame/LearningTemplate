#include "pch.h"

#include "Engine/Serialization/Core/SchemaMigrate.h"

#include "Core/Concurrency/atomic.h"
#include "Core/String/TagID.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Serialization/Core/SerializerUtil.h"

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
                const FunctionInfo* pCtor = typeInfo.findMethod( hashed_string( "$ctor" ) );
                if ( pCtor == nullptr || pCtor->_invoker.isBound() == false || typeInfo._destroyInstance == nullptr )
                    return false;
                typeInfo._destroyInstance( pPtr );
                return true;
            }

            static string_view stripJsonQuotes( string_view text )
            {
                if ( text.size() >= 2 && text.front() == '"' && text.back() == '"' )
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
             * @brief 스칼라(정수 · 실수 · bool) 타입 이름인지 묻습니다. `formatWirePodToString` 이 텍스트로 만들 수 있는 타입과 같습니다.
             * @details 미리 정의된 이름만 봅니다. 타입 레지스트리에 기대지 않으므로 원시 타입 등록 전에도 같은 답을 냅니다.
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

            /** @brief 기록 타입이 대상과 **다른** enum 이면 그 EnumInfo 입니다. 같은 enum 을 다른 이름(FQN · 짧은 이름 · 별칭)으로 부른 것이면 nullptr 입니다. */
            static const EnumInfo* findOtherWireEnum( hashed_string targetTypeName, hashed_string wireTypeName )
            {
                if ( wireTypeName.empty() )
                    return nullptr;
                const TypeRegistry& registry  = engine::getTypeRegistry();
                const EnumInfo*     pWireEnum = registry.findEnum( wireTypeName );
                if ( pWireEnum == nullptr || registry.findEnum( targetTypeName ) == pWireEnum )
                    return nullptr;
                return pWireEnum;
            }

            /**
             * @brief 기록 enum 의 payload 를 열거자 이름(글)으로 만듭니다 — XML 이 그 칸에 적었을 글입니다(비트플래그는 `A | B`).
             * @details payload 를 끝까지 읽지 못하거나 모르는 열거자면 false 입니다. enum 은 제 크기만큼만 쓰이므로 0 으로 둔 int64 자리에 읽습니다.
             */
            static bool formatWireEnumToString( const uint8* pPayload, size_t payloadSize, hashed_string wireTypeName, const SerializeContext& ctx,
                                                BinaryWireVersion wireVersion, string& outText )
            {
                int64  wireValue{ 0 };
                size_t offset{ 0 };
                if ( SerializerUtil::deserializeValueBinary( &wireValue, wireTypeName, pPayload, payloadSize, offset, ctx, wireVersion ) == false ||
                     offset != payloadSize )
                    return false;
                StringBuilder<constant::kMaxBuffer8192> ss;
                SerializerUtil::valueToText( ss, &wireValue, wireTypeName, ctx );
                outText = ss.c_str();
                return true;
            }

            static bool isNumericTypeName( hashed_string typeName )
            {
                const TypeInfo* pInfo = engine::getTypeRegistry().findType( typeName );
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

            /** @brief `ReadType` 으로 읽어 `PrintType` 으로 넓혀 적습니다(`to_string` 오버로드가 넷뿐이기 때문입니다). */
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
             * @details **크기만으로는 타입을 가를 수 없습니다.** `sizeof(float32) == sizeof(int32)` 이고
             *          `sizeof(float64) == sizeof(int64)` 입니다. 그래서 크기로만 고르던 `formatPodToString`
             *          에서는 `float32` 분기가 **한 번도 돌지 않았고**(앞의 int32 분기가 먼저 걸립니다),
             *          `float32` 프로퍼티를 문자열로 바꾸는 스키마 이관이 `1.5f` 를 비트값
             *          `"1069547520"` 으로 적었습니다. 기록 타입은 바이너리 태그(`wireTypeHash`)와
             *          `SchemaOrphanValue._wireTypeHash` 가 **이미 들고 있었습니다.** 여기까지
             *          넘겨 주지 않았을 뿐입니다.
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
                    // 바이트로 읽어 0 · 1 로 접는다. bool 로 memcpy 하면 0 · 1 이 아닌 바이트가 정의되지 않은 값이 된다.
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
             * @brief orphan 의 바이너리 값을 프로퍼티 자리에 적용합니다. `applyOrphanTo` 와 `applyOrphanToPath` 가 함께 씁니다.
             * @details 기록 타입이 프로퍼티 타입과 **같을 때만** 제자리로 읽습니다. 다르면 곧바로 이관(`tryCoerceBinaryPayload`)으로
             *          보냅니다. 본 역직렬화 경로(`BinarySerializer` 의 태그 루프)와 같은 규칙입니다. 예전에는 기록 타입으로 프로퍼티
             *          자리에 먼저 읽었습니다. 모양이 다른 타입(int32 → int16 · string 등)이면 그 자리와 이웃 필드를 덮어썼습니다.
             * @param wireTypeName 기록 타입. 모르면 비웁니다(그때는 이관이 제 타입 읽기부터 합니다).
             */
            [[nodiscard]] static bool applyOrphanBinary( void* pPropPtr, hashed_string propTypeName, const SchemaOrphanValue& orphan,
                                                         hashed_string wireTypeName, const SerializeContext& ctx )
            {
                const uint8* pPayload    = orphan._listBinary.data();
                const size_t payloadSize = orphan._listBinary.size();
                // 적힌 판으로 읽는다 — 옛 스트림의 orphan 은 enum 을 값으로 들고 있다.
                if ( wireTypeName == propTypeName )
                {
                    size_t offset{ 0 };
                    if ( SerializerUtil::deserializeValueBinary( pPropPtr, propTypeName, pPayload, payloadSize, offset, ctx, orphan._wireVersion ) )
                        return true;
                }
                return tryCoerceBinaryPayload( pPropPtr, propTypeName, pPayload, payloadSize, ctx, wireTypeName, orphan._wireVersion );
            }

            /**
             * @brief orphan 값을 경로 `pPath` 의 프로퍼티에 적용합니다. `applyOrphanTo` 와 `applyOrphanToPath` 는 orphan 을 찾는 법만 다릅니다.
             * @details 텍스트가 있으면 텍스트로, 없으면 바이너리로 적용합니다. 바이너리의 기록 타입은 `wireTypeHint`, 비었으면 orphan 에
             *          남은 기록 타입 해시로 정합니다. 예전에는 경로 판이 힌트가 없을 때 프로퍼티 타입을 기록 타입으로 가정해,
             *          타입이 바뀐 orphan(int32 → float32 등)을 그 비트 그대로 제자리에 읽었습니다.
             */
            [[nodiscard]] static bool applyOrphanAt( void* pInstance, const TypeInfo& typeInfo, const utf8* pPath, const SchemaOrphanValue& orphan,
                                                     hashed_string wireTypeHint, const SerializeContext& ctx )
            {
                void*               pPtr{ nullptr };
                const PropertyInfo* pProp = nullptr;
                if ( resolvePropertyPath( pInstance, typeInfo, pPath, pPtr, pProp ) == false )
                    return false;

                if ( orphan._text.empty() == false )
                    return parseTextValueCoerced( pPtr, pProp->_typeName, orphan._text, ctx );
                if ( orphan._listBinary.empty() )
                    return false;

                hashed_string hint = wireTypeHint;
                if ( hint.empty() )
                    hint = findWireTypeName( orphan._wireTypeHash );
                // 기록 타입이 적혀 있는데 모르면(지운 enum · 타입) 바이트의 뜻을 모른다 — 크기로 짐작해 옮기지 않는다(바이너리 칸 읽기와 같은 규칙).
                if ( hint.empty() && orphan._wireTypeHash != 0 )
                    return false;
                return applyOrphanBinary( pPtr, pProp->_typeName, orphan, hint, ctx );
            }

            /** @brief orphan 의 이름 해시입니다. 이름을 intern 하지 않은 orphan 은 해시만 듭니다. */
            static uint32 getOrphanNameHash( const SchemaOrphanValue& orphan )
            {
                return orphan._nameHash != 0 ? orphan._nameHash : orphan._name.getHash();
            }

            /** @brief 타입(상속 포함)의 프로퍼티 이름 · 별칭 가운데 orphan 의 이름이 있는지 묻습니다. */
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
             * @brief 로드가 성공으로 끝난 뒤 아무 데도 가지 않은 orphan — 읽지 못한 값 · 타입에 없는 칸 — 을 **로드마다 한 번** 알립니다.
             * @details 이관 함수가 찾아 본 orphan(`_bClaimed`)은 그 함수가 처리했으므로 뺍니다. 옛 TypeInfo 를 스테이징했으면 같은 본문을
             *          두 타입으로 읽어 한쪽만 아는 칸은 반드시 다른 쪽의 orphan 이 되므로, 두 타입 중 하나라도 아는 이름은 스테이징된
             *          인스턴스가 실어 날랐다고 보고 뺍니다. 이름은 칸마다 한 번만 적습니다(컨테이너 원소가 여럿 실패해도 칸 하나).
             *          예전에는 JSON · XML 의 Ignore 정책이 이것을 말없이 버려, 숫자 칸의 "abc" 가 기본값으로 남은 것을 아무도 몰랐습니다.
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
                    const uint32 nameHash = getOrphanNameHash( orphan );
                    if ( std::find( listListedHash.begin(), listListedHash.end(), nameHash ) != listListedHash.end() )
                        continue;
                    listListedHash.push_back( nameHash );

                    if ( names.empty() == false )
                        names += ", ";
                    if ( orphan._name.empty() == false )
                        names += orphan._name.c_str();
                    else if ( orphan._writtenName.empty() == false )
                        names += orphan._writtenName; // 파일의 모르는 이름은 intern 하지 않는다 — 적힌 글 그대로
                    else
                        names += "#" + to_string( nameHash ); // 바이너리는 이름 없이 해시만 싣는다
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
                const TypeInfo* pNested = engine::getTypeRegistry().findType( prop._typeName );
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
                const TypeInfo* pNested = engine::getTypeRegistry().findType( prop._typeName );
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
            // 파일에서 읽은 고아는 이름을 intern 하지 않고 해시만 들 수 있다(XML · JSON 읽기) — 그때는 해시로 맞춰 본다.
            if ( orphanValue._name != name && ( orphanValue._name.empty() == false || orphanValue._nameHash != nameHash ) )
                continue;
            // 같은 이름은 모두 이관이 처리한 것이다 — 컨테이너 원소가 여럿 실패하면 같은 칸 이름의 orphan 이 여럿 생긴다.
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
        if ( pOrphan == nullptr || _pInstance == nullptr || _pTypeInfo == nullptr )
            return false;

        const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();
        return SchemaMigrateInternal::applyOrphanAt( _pInstance, *_pTypeInfo, propName.c_str(), *pOrphan, wireTypeHint, ctx );
    }

    bool SchemaMigrateContext::applyOrphanToPath( const utf8* pDottedPath, hashed_string wireTypeHint ) const
    {
        // `applyOrphanTo` 와 같은 검사. 예전에는 인스턴스 · 타입을 보지 않고 `*_pTypeInfo` 를 읽었다.
        if ( pDottedPath == nullptr || _pInstance == nullptr || _pTypeInfo == nullptr )
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

        const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();
        return SchemaMigrateInternal::applyOrphanAt( _pInstance, *_pTypeInfo, pDottedPath, *pOrphan, wireTypeHint, ctx );
    }

    bool SchemaMigrateContext::moveProperty( hashed_string fromProp, hashed_string toProp ) const
    {
        return movePropertyPath( fromProp.c_str(), toProp.c_str() );
    }

    bool SchemaMigrateContext::movePropertyPath( const utf8* pFromPath, const utf8* pToPath ) const
    {
        if ( _pInstance == nullptr || _pTypeInfo == nullptr || pFromPath == nullptr || pToPath == nullptr )
            return false;

        const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();

        void*               pSrcPtr{ nullptr };
        const PropertyInfo* pSrcProp = nullptr;
        void*               pSrcRoot = _pLegacyInstance != nullptr ? _pLegacyInstance : _pInstance;
        const TypeInfo*     pSrcType = _pLegacyTypeInfo != nullptr ? _pLegacyTypeInfo : _pTypeInfo;
        if ( resolvePropertyPath( pSrcRoot, *pSrcType, pFromPath, pSrcPtr, pSrcProp ) == false )
            return false;

        void*               pDstPtr{ nullptr };
        const PropertyInfo* pDstProp = nullptr;
        if ( resolvePropertyPath( _pInstance, *_pTypeInfo, pToPath, pDstPtr, pDstProp ) == false )
            return false;

        StringBuilder<constant::kMaxBuffer8192> ss;
        SerializerUtil::valueToText( ss, pSrcPtr, pSrcProp->_typeName, ctx );
        return parseTextValueCoerced( pDstPtr, pDstProp->_typeName, ss.view(), ctx );
    }

    bool SchemaMigrateContext::setPropertyFromText( hashed_string propName, string_view text ) const
    {
        if ( _pInstance == nullptr || _pTypeInfo == nullptr )
            return false;
        void*               pPtr{ nullptr };
        const PropertyInfo* pProp = nullptr;
        if ( resolvePropertyPath( _pInstance, *_pTypeInfo, propName.c_str(), pPtr, pProp ) == false )
            return false;
        const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();
        return parseTextValueCoerced( pPtr, pProp->_typeName, text, ctx );
    }

    bool isValueOnlyCoercion( hashed_string targetTypeName, hashed_string wireTypeName )
    {
        if ( wireTypeName.empty() || wireTypeName == targetTypeName )
            return false;
        // 기록 타입이 다른 enum 이면 열거자 이름으로만 옮긴다 — 제 타입으로 다시 읽으면 이름 해시가 수로 읽힌다.
        if ( SchemaMigrateInternal::findOtherWireEnum( targetTypeName, wireTypeName ) != nullptr )
            return true;
        if ( SchemaMigrateInternal::isScalarTypeName( wireTypeName ) == false )
            return false;
        return SchemaMigrateInternal::isScalarTypeName( targetTypeName ) || SchemaMigrateInternal::isStringType( targetTypeName );
    }

    hashed_string findWireTypeName( uint32 wireTypeHash )
    {
        const TypeRegistry& registry = engine::getTypeRegistry();
        const hashed_string typeName = registry.canonicalTypeNameByHash( wireTypeHash );
        if ( typeName.empty() == false )
            return typeName;
        const EnumInfo* pEnumInfo = registry.findEnumByNameHash( wireTypeHash );
        return ( pEnumInfo != nullptr ) ? pEnumInfo->_fullyQualifiedName : hashed_string{};
    }

    [[nodiscard]] bool tryCoerceBinaryPayload( void* pPropPtr, hashed_string targetTypeName, const uint8* pPayload, size_t payloadSize,
                                               const SerializeContext& ctx, hashed_string wireTypeName, BinaryWireVersion wireVersion )
    {
        if ( pPropPtr == nullptr || pPayload == nullptr )
            return false;

        // 값으로만 옮기는 쌍이면(`isValueOnlyCoercion`) 기록 값을 글로 만들어 대상 타입으로 다시 읽는다(JSON · XML 과 같은 규칙이다).
        //  - 기록 타입을 아는 스칼라 → 다른 스칼라 · 문자열. 아래의 제 타입 읽기를 먼저 하면 크기가 같은 스칼라는 비트가 그대로 재해석되고
        //    (int32 100 → float32 1.4e-43), 문자열은 int32 0 을 길이 0 으로 읽어 "" 가 됐다. 값으로 못 옮기면(float32 1.5 → int32) 실패다.
        //  - 기록 타입이 다른 enum → 열거자 이름. 문자열은 이름을, 같은 이름이 있는 enum 은 그 열거자를 받고, 수는 받지 않는다(XML 에 적힌 이름과 같다).
        //    제 타입 읽기로 가면 열거자 이름 해시(4 바이트)가 int32 · float32 로 그대로 읽혔다.
        // payload 크기가 기록 타입과 안 맞아도(손상) 재해석하지 않고 실패로 끝낸다.
        if ( isValueOnlyCoercion( targetTypeName, wireTypeName ) )
        {
            string     wireText;
            const bool bWireEnum  = SchemaMigrateInternal::findOtherWireEnum( targetTypeName, wireTypeName ) != nullptr;
            const bool bFormatted = bWireEnum ? SchemaMigrateInternal::formatWireEnumToString( pPayload, payloadSize, wireTypeName, ctx, wireVersion, wireText )
                                              : SchemaMigrateInternal::formatWirePodToString( pPayload, payloadSize, wireTypeName, wireText );
            if ( bFormatted == false )
                return false;
            return parseTextValueCoerced( pPropPtr, targetTypeName, wireText, ctx );
        }

        size_t offset{ 0 };
        if ( SerializerUtil::deserializeValueBinary( pPropPtr, targetTypeName, pPayload, payloadSize, offset, ctx, wireVersion ) &&
             offset == payloadSize )
            return true;

        // POD → 문자열. 기록 타입을 아는 스칼라는 위에서 끝났으므로 여기서는 크기로 짐작한다(정수와 실수를 가르지 못한다).
        if ( SchemaMigrateInternal::isStringType( targetTypeName ) )
        {
            string asText;
            if ( SchemaMigrateInternal::formatPodToString( pPayload, payloadSize, asText ) )
                return parseTextValueCoerced( pPropPtr, targetTypeName, asText, ctx );

            // 길이 접두 문자열 blob 은 맨 앞의 제 타입 읽기가 이미 다뤘다. 여기서는 길이가 맞으면 접두 뒤 바이트를 텍스트로 읽어 본다
            if ( payloadSize >= sizeof( uint32 ) )
            {
                uint32 len{ 0 };
                Memory::copy( &len, pPayload, sizeof( uint32 ) );
                if ( sizeof( uint32 ) + len == payloadSize )
                {
                    const string_view textValue{ reinterpret_cast<const utf8*>( pPayload + sizeof( uint32 ) ), len };
                    return parseTextValueCoerced( pPropPtr, targetTypeName, textValue, ctx );
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
                return parseTextValueCoerced( pPropPtr, targetTypeName, textValue, ctx );
            }
        }

        // 크기가 같은 POD 를 그대로 재해석한다(int32↔float32 등). 마지막 수단이다.
        offset                                        = 0;
        const SerializeContext::BinaryReadFn* pReader = ctx.findBinaryReader( targetTypeName );
        if ( pReader != nullptr )
        {
            if ( ( *pReader )( pPropPtr, pPayload, payloadSize, offset ) && offset == payloadSize )
                return true;
        }

        return false;
    }

    [[nodiscard]] bool parseTextValueCoerced( void* pValPtr, hashed_string typeName, string_view valStr,
                                              const SerializeContext& ctx )
    {
        if ( pValPtr == nullptr )
            return false;

        const string_view stripped = SchemaMigrateInternal::stripJsonQuotes( valStr );
        if ( SerializerUtil::parseTextValue( pValPtr, typeName, valStr, ctx ) )
            return true;
        if ( stripped.data() != valStr.data() || stripped.size() != valStr.size() )
        {
            if ( SerializerUtil::parseTextValue( pValPtr, typeName, stripped, ctx ) )
                return true;
        }

        // 숫자로 기록된 값 → 문자열. 대상이 문자열 타입이면 텍스트를 그대로 담는다.
        if ( SchemaMigrateInternal::isStringType( typeName ) )
        {
            if ( engine::getTypeRegistry().isType( typeName, "hashed_string" ) )
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

    bool resolvePropertyPath( void* pRoot, const TypeInfo& typeInfo, const utf8* pDottedPath, void*& pOutPtr,
                              const PropertyInfo*& pOutProp )
    {
        pOutPtr  = nullptr;
        pOutProp = nullptr;
        if ( pRoot == nullptr || pDottedPath == nullptr )
            return false;

        const vector<string> listPart = SchemaMigrateInternal::splitPath( pDottedPath );
        if ( listPart.empty() )
            return false;

        void*               pCur      = pRoot;
        const TypeInfo*     pCurType  = &typeInfo;
        const PropertyInfo* pLastProp = nullptr;

        for ( size_t partIndex = 0; partIndex < listPart.size(); ++partIndex )
        {
            const hashed_string name( listPart[partIndex].c_str() );
            const PropertyInfo* pProp = pCurType->findPropertyInHierarchy( name );
            if ( pProp == nullptr )
                return false;

            void* pPropPtr = pProp->getRawPtr( pCur );
            if ( partIndex + 1 == listPart.size() )
            {
                pOutPtr  = pPropPtr;
                pOutProp = pProp;
                return true;
            }

            const TypeInfo* pNested = engine::getTypeRegistry().findType( pProp->_typeName );
            if ( pNested == nullptr )
                return false;
            pCur      = pPropPtr;
            pCurType  = pNested;
            pLastProp = pProp;
            (void)pLastProp;
        }
        return false;
    }

    SW_LOG_CALLER( "SchemaMigrate" );

    bool runSchemaMigrateStep( uint32 fromVersion, uint32 currentVersion, void* pInstance, const TypeInfo& typeInfo,
                               void* pLegacyInstance, const TypeInfo* pLegacyTypeInfo,
                               const vector<SchemaOrphanValue>& listOrphan, SchemaMigrateFn migrate,
                               bool bWarnWhenNoMigrate, const SerializeContext& ctx )
    {
        const bool needsMigrate =
            migrate != nullptr && ( fromVersion != currentVersion || listOrphan.empty() == false || pLegacyInstance != nullptr );
        if ( needsMigrate )
        {
            SchemaMigrateContext migrateContext;
            migrateContext._fromVersion     = fromVersion;
            migrateContext._toVersion       = currentVersion;
            migrateContext._pInstance       = pInstance;
            migrateContext._pTypeInfo       = &typeInfo;
            migrateContext._pLegacyInstance = pLegacyInstance;
            migrateContext._pLegacyTypeInfo = pLegacyTypeInfo;
            migrateContext._pOrphans        = &listOrphan;
            migrateContext._pSerializeCtx   = &ctx;
            if ( migrate( migrateContext ) == false )
                return false;
            // 이관이 받아 준 로드다. 이관이 찾아 보지 않은 orphan 은 버려진다.
            SchemaMigrateInternal::warnDroppedOrphans( typeInfo, pLegacyTypeInfo, listOrphan );
            return true;
        }

        if ( migrate == nullptr && bWarnWhenNoMigrate )
        {
            SW_LOG_WARNING( "(%#) schema version %# -> %# with no migrate callback (%# orphans: %#)",
                            typeInfo._name.c_str(), fromVersion, currentVersion, static_cast<uint32>( listOrphan.size() ),
                            listOrphan.empty() ? "" : listOrphan[0]._name.c_str() );
            return false;
        }
        // 이관 없이 받아 주는 로드다(JSON · XML 의 Ignore 정책). orphan 은 모두 버려진다.
        SchemaMigrateInternal::warnDroppedOrphans( typeInfo, pLegacyTypeInfo, listOrphan );
        return true;
    }

} // namespace sw
