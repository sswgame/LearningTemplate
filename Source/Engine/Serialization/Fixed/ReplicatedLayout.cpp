#include "pch.h"

#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "Core/Log/Logger.h"

#include "Engine/Reflection/PropertyInfo.h"
#include "Engine/Reflection/PropertyRoleUtil.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    expected<const FixedCodec*> ReplicatedLayout::findSchema( const TypeInfo& type )
    {
        const FixedCodec* pCodec  = type._pFixedCodec;
        const FixedCodec* pSchema = pCodec != nullptr ? pCodec->_pReplicated : nullptr;
        if ( pSchema == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "type has no Replicated fields (or no generated fixed layout codec)", type._fullyQualifiedName.c_str() } );

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

        if ( expected<void> valid = FixedLayout::validateCodec( *pSchema ); valid.has_value() == false )
            return unexpected( valid.error() );
        return pSchema;
    }

    void ReplicatedLayout::stopForMissingSchema( const hashed_string& fqn, const Error& reason )
    {
        SW_FATAL( "'%#' has no replicated schema: %# (%#)", fqn.c_str(), reason.message(), reason.detail() );
    }

    void ReplicatedLayout::collectChangedProperties( const FixedCodec& schema, const void* pBefore, const void* pAfter, DynamicBitset& outChanged )
    {
        outChanged.resize( 0 );
        outChanged.resize( schema._fieldCount );
        for ( uint32 fieldIndex = 0; fieldIndex < schema._fieldCount; ++fieldIndex )
        {
            if ( FixedLayout::equalsFieldWithCodec( schema, fieldIndex, pBefore, pAfter ) == false )
                outChanged.set( fieldIndex );
        }
    }
} // namespace sw
