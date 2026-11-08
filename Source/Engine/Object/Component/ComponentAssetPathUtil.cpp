#include "pch.h"

#include "Engine/Object/Component/ComponentAssetPathUtil.h"

#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Serializer/AssetRefBase.h"

namespace sw
{
    string_view ComponentAssetPathUtil::readAssetPath( const PropertyInfo& prop, const Component* pComponent )
    {
        if ( prop._metadata->_bAssetPath == SW_FALSE && prop._metadata->_assetType.empty() )
            return {};
        static const hashed_string s_assetRefType( AssetRefBase::kTypeName );
        if ( prop._typeName == s_assetRefType )
        {
            // 내장 에셋(`Cube`)은 파일이 아니다 — 프리로드 · 핫 리로드가 파일로 찾지 않게 빈 값이다.
            const AssetRefBase* pValue = prop.getValuePtr<AssetRefBase>( pComponent );
            return pValue != nullptr && pValue->isBuiltin() == false ? string_view( pValue->path() ) : string_view{};
        }
        // 읽을 C++ 타입과 같은 자리(`ReflectBuiltinOf<T>`)로 묻는다 — 이름 글과 캐스트 타입이 따로 놀지 않고 별칭도 같은 자리가 된다.
        const optional<int32> builtinIndex = ReflectedValueUtil::findBuiltinIndex( prop._typeName );
        if ( builtinIndex == ReflectBuiltinOf<string>::kIndex )
        {
            const string* pValue = prop.getValuePtr<string>( pComponent );
            return pValue != nullptr ? string_view( *pValue ) : string_view{};
        }
        if ( builtinIndex == ReflectBuiltinOf<hashed_string>::kIndex )
        {
            const hashed_string* pValue = prop.getValuePtr<hashed_string>( pComponent );
            return pValue != nullptr ? string_view( pValue->c_str() ) : string_view{};
        }
        return {};
    }
} // namespace sw
