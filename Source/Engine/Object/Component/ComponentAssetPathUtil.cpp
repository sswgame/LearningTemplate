#include "pch.h"

#include "Engine/Object/Component/ComponentAssetPathUtil.h"

#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    string_view ComponentAssetPathUtil::readAssetPath( const PropertyInfo& prop, const Component* pComponent )
    {
        if ( prop._metadata->_bAssetPath == SW_FALSE && prop._metadata->_assetType.empty() )
            return {};
        // 읽을 C++ 타입과 같은 자리(`ReflectBuiltinOf<T>`)로 묻는다 — 이름 글과 캐스트 타입이 따로 놀지 않고 별칭도 같은 자리가 된다.
        const int32 builtinIndex = ReflectValueUtil::findBuiltinIndex( prop._typeName );
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
