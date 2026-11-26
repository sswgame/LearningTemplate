#include "pch.h"

#include "Engine/Serialization/Layout/ReplicatedLayout.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/PropertyRoleUtil.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    expected<const LayoutSchema*> ReplicatedLayout::findSchema( const TypeInfo& type )
    {
        const LayoutSchema* pCodec  = type._pLayoutSchema;
        const LayoutSchema* pSchema = pCodec != nullptr ? pCodec->_pReplicatedSchema : nullptr;
        if ( pSchema == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "type has no Replicated fields (or no generated layout schema)", type._fullyQualifiedName.c_str() } );

        // 스키마는 이 타입이 선언한 칸만이다. 상속분까지 센 `Replicated` 칸이 더 많으면 기반의 칸이 빠진다 — 조용히 복제하지 않는 대신 거절한다.
        uint32 ownCount = 0;
        for ( const PropertyInfo& prop : type._listProperty )
        {
            if ( prop._metadata->_bReplicated == SW_TRUE )
                ++ownCount;
        }
        vector<const PropertyInfo*> listWithBase;
        PropertyRoleUtil::collectReplicatedProperties( type, listWithBase );
        if ( listWithBase.size() != ownCount )
            return unexpected( Error{ ErrorKind::Unsupported, "a base type has Replicated fields - a replicated schema covers the fields its type declares", type._fullyQualifiedName.c_str() } );

        if ( expected<void> valid = LayoutSerializer::validateSchema( *pSchema ); valid.has_value() == false )
            return unexpected( valid.error() );
        return pSchema;
    }

    void ReplicatedLayout::stopForMissingSchema( const hashed_string& fqn, const Error& reason )
    {
        reportFatalError( "'%#' has no replicated schema: %# (%#)", fqn.c_str(), reason.message(), reason.detail() );
    }

    void ReplicatedLayout::collectChangedProperties( const LayoutSchema& schema, const void* pBefore, const void* pAfter, DynamicBitset& outChanged )
    {
        outChanged.resize( 0 );
        outChanged.resize( schema._fieldCount );
        for ( uint32 fieldIndex = 0; fieldIndex < schema._fieldCount; ++fieldIndex )
        {
            if ( LayoutSerializer::equalsFieldWithSchema( schema, fieldIndex, pBefore, pAfter ) == false )
                outChanged.set( fieldIndex );
        }
    }
} // namespace sw
