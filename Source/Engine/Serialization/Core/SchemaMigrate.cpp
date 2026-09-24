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

            static hashed_string resolveWireTypeHash( uint32 wireTypeHash )
            {
                return engine::getTypeRegistry().canonicalTypeNameByHash( wireTypeHash );
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
            static bool readPod( const uint8* pPayload, size_t payloadSize, T& out )
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
                    return formatPodAs<bool, int32>( pPayload, payloadSize, out );
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
            static bool applyOrphanBinary( void* pPropPtr, hashed_string propTypeName, const SchemaOrphanValue& orphan,
                                           hashed_string wireTypeName, const SerializeContext& ctx )
            {
                const uint8* pPayload    = orphan._listBinary.data();
                const size_t payloadSize = orphan._listBinary.size();
                if ( wireTypeName == propTypeName )
                {
                    size_t offset{ 0 };
                    if ( SerializerUtil::deserializeValueBinary( pPropPtr, propTypeName, pPayload, payloadSize, offset, ctx ) )
                        return true;
                }
                return tryCoerceBinaryPayload( pPropPtr, propTypeName, pPayload, payloadSize, ctx, wireTypeName );
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
        for ( const SchemaOrphanValue& orphanValue : *_pOrphans )
        {
            if ( orphanValue._name == name )
                return &orphanValue;
        }
        return nullptr;
    }

    const SchemaOrphanValue* SchemaMigrateContext::findOrphanHash( uint32 nameHash ) const
    {
        if ( _pOrphans == nullptr || nameHash == 0 )
            return nullptr;
        for ( const SchemaOrphanValue& orphanValue : *_pOrphans )
        {
            if ( orphanValue._nameHash == nameHash )
                return &orphanValue;
        }
        return nullptr;
    }

    bool SchemaMigrateContext::applyOrphanTo( hashed_string propName, hashed_string wireTypeHint ) const
    {
        const SchemaOrphanValue* pOrphan = findOrphan( propName );
        if ( pOrphan == nullptr )
            pOrphan = findOrphanHash( propName.getHash() );
        if ( pOrphan == nullptr || _pInstance == nullptr || _pTypeInfo == nullptr )
            return false;

        void*               pPtr{ nullptr };
        const PropertyInfo* pProp = nullptr;
        if ( resolvePropertyPath( _pInstance, *_pTypeInfo, propName.c_str(), pPtr, pProp ) == false )
            return false;

        const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();

        if ( pOrphan->_text.empty() == false )
            return parseTextValueCoerced( pPtr, pProp->_typeName, pOrphan->_text, ctx );

        if ( pOrphan->_listBinary.empty() == false )
        {
            hashed_string hint = wireTypeHint;
            if ( hint.empty() )
                hint = SchemaMigrateInternal::resolveWireTypeHash( pOrphan->_wireTypeHash );
            return SchemaMigrateInternal::applyOrphanBinary( pPtr, pProp->_typeName, *pOrphan, hint, ctx );
        }
        return false;
    }

    bool SchemaMigrateContext::applyOrphanToPath( const utf8* pDottedPath, hashed_string wireTypeHint ) const
    {
        if ( pDottedPath == nullptr )
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

        void*               pPtr{ nullptr };
        const PropertyInfo* pProp = nullptr;
        if ( resolvePropertyPath( _pInstance, *_pTypeInfo, pDottedPath, pPtr, pProp ) == false )
            return false;

        const SerializeContext& ctx = _pSerializeCtx != nullptr ? *_pSerializeCtx : SerializeContext::getDefault();
        if ( pOrphan->_text.empty() == false )
            return parseTextValueCoerced( pPtr, pProp->_typeName, pOrphan->_text, ctx );
        if ( pOrphan->_listBinary.empty() == false )
        {
            // 기록 타입은 `applyOrphanTo` 와 같게 정한다. 예전에는 힌트가 없으면 프로퍼티 타입을 기록 타입으로 가정해,
            // 타입이 바뀐 orphan(int32 → float32 등)을 그 비트 그대로 제자리에 읽었다.
            hashed_string hint = wireTypeHint;
            if ( hint.empty() )
                hint = SchemaMigrateInternal::resolveWireTypeHash( pOrphan->_wireTypeHash );
            return SchemaMigrateInternal::applyOrphanBinary( pPtr, pProp->_typeName, *pOrphan, hint, ctx );
        }
        return false;
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

    bool isScalarValueCoercion( hashed_string targetTypeName, hashed_string wireTypeName )
    {
        if ( wireTypeName.empty() || wireTypeName == targetTypeName || SchemaMigrateInternal::isScalarTypeName( wireTypeName ) == false )
            return false;
        return SchemaMigrateInternal::isScalarTypeName( targetTypeName ) || SchemaMigrateInternal::isStringType( targetTypeName );
    }

    bool tryCoerceBinaryPayload( void* pPropPtr, hashed_string targetTypeName, const uint8* pPayload, size_t payloadSize,
                                 const SerializeContext& ctx, hashed_string wireTypeName )
    {
        if ( pPropPtr == nullptr || pPayload == nullptr )
            return false;

        // 기록 타입을 아는 스칼라가 다른 스칼라 · 문자열로 바뀌었으면 **값으로** 옮긴다. 기록 타입의 텍스트를 대상 타입으로 다시
        // 읽는다(JSON · XML 과 같은 규칙이다). 아래의 제 타입 읽기를 먼저 하면 크기가 같은 스칼라는 비트가 그대로 재해석되고
        // (int32 100 → float32 1.4e-43), 문자열은 int32 0 을 길이 0 으로 읽어 "" 가 됐다. 값으로 못 옮기면(float32 1.5 → int32)
        // 실패다. payload 크기가 기록 타입과 안 맞아도(손상) 재해석하지 않고 실패로 끝낸다.
        if ( isScalarValueCoercion( targetTypeName, wireTypeName ) )
        {
            string wireText;
            if ( SchemaMigrateInternal::formatWirePodToString( pPayload, payloadSize, wireTypeName, wireText ) == false )
                return false;
            return parseTextValueCoerced( pPropPtr, targetTypeName, wireText, ctx );
        }

        size_t offset{ 0 };
        if ( SerializerUtil::deserializeValueBinary( pPropPtr, targetTypeName, pPayload, payloadSize, offset, ctx ) &&
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

    bool parseTextValueCoerced( void* pValPtr, hashed_string typeName, string_view valStr,
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
            SchemaMigrateContext mctx;
            mctx._fromVersion     = fromVersion;
            mctx._toVersion       = currentVersion;
            mctx._pInstance       = pInstance;
            mctx._pTypeInfo       = &typeInfo;
            mctx._pLegacyInstance = pLegacyInstance;
            mctx._pLegacyTypeInfo = pLegacyTypeInfo;
            mctx._pOrphans        = &listOrphan;
            mctx._pSerializeCtx   = &ctx;
            return migrate( mctx );
        }

        if ( migrate == nullptr && bWarnWhenNoMigrate )
        {
            SW_LOG_WARNING( "(%#) schema version %# -> %# with no migrate callback (%# orphans: %#)",
                            typeInfo._name.c_str(), fromVersion, currentVersion, static_cast<uint32>( listOrphan.size() ),
                            listOrphan.empty() ? "" : listOrphan[0]._name.c_str() );
            return false;
        }
        return true;
    }

} // namespace sw
