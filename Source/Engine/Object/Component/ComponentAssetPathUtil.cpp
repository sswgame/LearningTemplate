#include "pch.h"

#include "Engine/Object/Component/ComponentAssetPathUtil.h"

#include "Core/Foundation/Expected.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionContainers.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Serializer/AssetRefBase.h"

namespace sw
{
    namespace
    {
        struct ComponentAssetPathUtilInternal
        {
            /** @brief 형식 이름 @p typeName 이 에셋 경로 값의 형식이면 그 종류입니다(`AssetRef` · `string` · `hashed_string`). 아니면 nullopt 입니다. */
            static optional<AssetPathValueKind> findValueKind( const hashed_string& typeName )
            {
                static const hashed_string s_assetRefType( AssetRefBase::kTypeName );
                if ( typeName == s_assetRefType )
                    return AssetPathValueKind::AssetRef;
                // 읽을 C++ 타입과 같은 자리(`ReflectBuiltinOf<T>`)로 묻는다 — 이름 글과 캐스트 타입이 따로 놀지 않고 별칭도 같은 자리가 된다.
                const ReflectBuiltinIndex builtinIndex = ReflectedValueUtil::findBuiltinIndex( typeName );
                if ( builtinIndex == ReflectBuiltinOf<string>::kIndex )
                    return AssetPathValueKind::String;
                if ( builtinIndex == ReflectBuiltinOf<hashed_string>::kIndex )
                    return AssetPathValueKind::HashedString;
                return nullopt;
            }

            /** @brief 형식 이름 @p typeName 이 리플렉션 구조체(내장 값이 아닌 등록 타입)면 그 타입입니다. 아니면 nullptr 입니다. */
            static const TypeInfo* findNestedType( const hashed_string& typeName )
            {
                if ( typeName.empty() || ReflectedValueUtil::findBuiltinIndex( typeName ) != ReflectBuiltinIndex::kNone )
                    return nullptr;
                return engine::getSubsystem<TypeRegistry>().findType( typeName ); // 필수 서비스: 리플렉션 컴포넌트가 있으면 레지스트리가 있다
            }

            /** @brief 값 하나(@p pValue, 형식 @p typeName)를 돈다 — 경로 칸(@p bAssetPath)이면 그 값, 아니면 구조체 안으로 내려간다. */
            static void visitValue( void* pValue, const hashed_string& typeName, bool bAssetPath, const PropertyInfo& topProperty, const AssetPathVisitor& visit )
            {
                if ( pValue == nullptr )
                    return;
                if ( bAssetPath )
                {
                    const optional<AssetPathValueKind> kind = findValueKind( typeName );
                    if ( kind.has_value() )
                        visit( AssetPathValue{ pValue, *kind, &topProperty } );
                    return;
                }
                const TypeInfo* const pNested = findNestedType( typeName );
                if ( pNested != nullptr )
                    visitObject( *pNested, pValue, &topProperty, visit );
            }

            /** @brief @p type 인스턴스 @p pInstance 의 칸마다 돈다. @p pTopProperty 가 nullptr 이면 이 인스턴스가 컴포넌트다(칸 자신이 맨 위 칸). */
            static void visitObject( const TypeInfo& type, void* pInstance, const PropertyInfo* pTopProperty, const AssetPathVisitor& visit )
            {
                type.forEachProperty( [pInstance, pTopProperty, &visit]( const PropertyInfo& prop )
                {
                    if ( prop._bIsBitField == SW_TRUE || prop.hasValueAccessor() )
                        return; // 비트필드는 경로가 아니고, 접근자 칸은 값이 객체 밖이라 주소로 고칠 수 없다
                    const PropertyInfo& topProperty = pTopProperty != nullptr ? *pTopProperty : prop;
                    const bool          bAssetPath  = prop._metadata->_bAssetPath == SW_TRUE || prop._metadata->_assetType.empty() == false;
                    void* const         pValue      = prop.getValuePtr<void>( pInstance );
                    if ( prop._bIsContainer == SW_FALSE )
                    {
                        visitValue( pValue, prop._typeName, bAssetPath, topProperty, visit );
                        return;
                    }
                    const ISequenceContainerWrapper* const pSequence = prop.hasContainerWrapper() ? prop._containerWrapper->asSequence() : nullptr;
                    if ( pSequence == nullptr || pValue == nullptr || pSequence->allowsInPlaceElementWrite() == false )
                        return;
                    for ( size_t index = 0; index < pSequence->getSize( pValue ); ++index )
                        visitValue( pSequence->getElement( pValue, index ), prop._elementTypeName, bAssetPath, topProperty, visit );
                }, true ); // 상속 칸도 — 기반 타입의 경로 칸이 빠지지 않게
            }
        };
    } // namespace

    string_view AssetPathValue::getPath() const
    {
        switch ( _kind )
        {
            case AssetPathValueKind::AssetRef:
            {
                const AssetRefBase* const pValue = static_cast<const AssetRefBase*>( _pValue );
                return pValue->isBuiltin() ? string_view{} : string_view( pValue->path() ); // 내장 에셋(`Cube`)은 파일이 아니다 — 프리로드 · 핫 리로드가 파일로 찾지 않게
            }
            case AssetPathValueKind::String:
                return string_view( *static_cast<const string*>( _pValue ) );
            case AssetPathValueKind::HashedString:
                return string_view( static_cast<const hashed_string*>( _pValue )->c_str() );
        }
        return {};
    }

    bool AssetPathValue::assign( string_view path ) const
    {
        switch ( _kind )
        {
            case AssetPathValueKind::AssetRef:
                return static_cast<AssetRefBase*>( _pValue )->assign( path ); // 종류가 맞지 않으면 이유를 남긴다
            case AssetPathValueKind::String:
                *static_cast<string*>( _pValue ) = string{ path };
                return true;
            case AssetPathValueKind::HashedString:
                *static_cast<hashed_string*>( _pValue ) = hashed_string( path );
                return true;
        }
        return false;
    }

    void ComponentAssetPathUtil::forEachAssetPath( Component& component, const AssetPathVisitor& visit )
    {
        const TypeInfo* const pType = component.getTypeInfo();
        if ( pType != nullptr )
            forEachAssetPath( *pType, &component, visit );
    }

    void ComponentAssetPathUtil::forEachAssetPath( const TypeInfo& type, void* pInstance, const AssetPathVisitor& visit )
    {
        ComponentAssetPathUtilInternal::visitObject( type, pInstance, nullptr, visit );
    }
} // namespace sw
