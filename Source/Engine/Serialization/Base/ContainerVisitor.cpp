#include "pch.h"

#include "Engine/Serialization/Base/ContainerVisitor.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

namespace sw
{
    namespace
    {
        struct ContainerVisitorInternal
        {
            /** @brief 읽기 전에 잡는 원소 수의 상한입니다 — 파일의 개수 칸 하나로 큰 할당을 하지 않게. */
            static constexpr size_t kMaxReserveElementCount = MathUtil::kMaxUInt16;

            static void writeElement( const void* pElement, const NestedContainerInfo& nested, const ContainerElementPlan& plan, const ContainerSlot slot,
                                      IContainerWriter& writer, const SerializeContext& ctx )
            {
                switch ( plan._kind )
                {
                    case ContainerElementKind::NestedContainer:
                    {
                        writer.beginNestedContainer( slot );
                        ContainerVisitor::write( pElement, *nested._elementNested, writer, ctx );
                        writer.endNestedContainer( slot );
                        break;
                    }
                    case ContainerElementKind::OwnedPointer:
                    {
                        const void* const* ppObject = static_cast<const void* const*>( pElement );
                        writer.writeOwnedPointer( ppObject != nullptr ? *ppObject : nullptr );
                        break;
                    }
                    case ContainerElementKind::ValueObject:
                    {
                        writer.writeValueObject( pElement, nested._elementTypeName, *plan._pElementType, slot );
                        break;
                    }
                    case ContainerElementKind::Scalar:
                    {
                        writer.writeScalar( pElement, nested._elementTypeName, slot );
                        break;
                    }
                }
            }

            [[nodiscard]] static ContainerReadResult readElementValue( void* pElement, const NestedContainerInfo& nested, const ContainerElementPlan& plan,
                                                                       const ContainerSlot slot, IContainerReader& reader, const SerializeContext& ctx )
            {
                switch ( plan._kind )
                {
                    case ContainerElementKind::NestedContainer:
                        return ContainerVisitor::read( pElement, *nested._elementNested, reader, ctx );
                    case ContainerElementKind::OwnedPointer:
                        return ContainerReadResult::StreamBroken; // 시퀀스만 — readSequence 가 먼저 가른다(맵 값 계획은 이 모양을 만들지 않는다)
                    case ContainerElementKind::ValueObject:
                        return reader.readValueObject( pElement, nested._elementTypeName, *plan._pElementType, slot );
                    case ContainerElementKind::Scalar:
                        return reader.readScalar( pElement, nested._elementTypeName, slot );
                }
                return ContainerReadResult::StreamBroken;
            }

            [[nodiscard]] static ContainerReadResult readSequence( void* pContainer, const ISequenceContainerWrapper& sequence, const NestedContainerInfo& nested,
                                                                   IContainerReader& reader, const SerializeContext& ctx )
            {
                size_t                    countHint{ 0 };
                const ContainerReadResult opened = reader.beginSequence( countHint );
                if ( opened != ContainerReadResult::Read )
                    return opened;

                const ContainerElementPlan plan = ContainerElementPlan::make( nested, ContainerSlot::SequenceElement, ctx );
                if ( plan._kind == ContainerElementKind::OwnedPointer )
                {
                    return reader.forEachElement( SW_DELEGATE_LAMBDA( ContainerElementVisitDelegate, [&reader]( size_t ) -> ContainerReadResult
                    {
                        return reader.readOwnedPointer();
                    } ) );
                }

                sequence.clear( pContainer );
                if ( countHint > 0 )
                    sequence.reserve( pContainer, MathUtil::min( countHint, kMaxReserveElementCount ) );

                // 읽기는 형식이, 넣는 방법은 컨테이너가 정한다 — `set` 은 다 읽은 뒤 넣고, 고정 배열은 순번의 칸을 채운다.
                return reader.forEachElement( SW_DELEGATE_LAMBDA( ContainerElementVisitDelegate, [&]( size_t elementIndex ) -> ContainerReadResult
                {
                    ContainerReadResult result{ ContainerReadResult::Read };
                    bool                bFilled{ false };
                    const bool          bAppended = sequence.appendElement( pContainer, elementIndex, SW_DELEGATE_LAMBDA( ElementFillDelegate, [&]( void* pElement ) -> bool
                             {
                        bFilled = true;
                        result  = readElementValue( pElement, nested, plan, ContainerSlot::SequenceElement, reader, ctx );
                        return result == ContainerReadResult::Read;
                    } ) );
                    // 넣을 칸이 없었다(고정 배열보다 원소가 많다) — 형식이 그 원소를 지나간다.
                    if ( bAppended == false && bFilled == false )
                        return reader.skipElement( elementIndex );
                    return result;
                } ) );
            }

            [[nodiscard]] static ContainerReadResult readMap( void* pContainer, const IMapContainerWrapper& map, const NestedContainerInfo& nested,
                                                              IContainerReader& reader, const SerializeContext& ctx )
            {
                size_t                    countHint{ 0 };
                const ContainerReadResult opened = reader.beginMap( countHint );
                if ( opened != ContainerReadResult::Read )
                    return opened;

                map.clear( pContainer );
                const ContainerElementPlan plan = ContainerElementPlan::make( nested, ContainerSlot::MapValue, ctx );
                vector<uint8>              keyBytes( map.getKeySize() );
                vector<uint8>              valueBytes( map.getValueSize() );
                return reader.forEachElement( SW_DELEGATE_LAMBDA( ContainerElementVisitDelegate, [&]( size_t ) -> ContainerReadResult
                {
                    map.defaultConstructKey( keyBytes.data() );
                    map.defaultConstructValue( valueBytes.data() );
                    const ContainerReadResult keyResult = reader.readMapKey( keyBytes.data(), nested._keyTypeName );
                    // 키를 못 읽어도 자리를 알면 값을 읽는다 — 바이너리는 값을 지나야 다음 항목 자리에 선다.
                    ContainerReadResult valueResult{ ContainerReadResult::StreamBroken };
                    if ( keyResult != ContainerReadResult::StreamBroken )
                        valueResult = readElementValue( valueBytes.data(), nested, plan, ContainerSlot::MapValue, reader, ctx );

                    const bool bInsert = keyResult == ContainerReadResult::Read && valueResult == ContainerReadResult::Read;
                    if ( bInsert )
                        map.insertKeyValue( pContainer, keyBytes.data(), valueBytes.data() );
                    map.destroyKey( keyBytes.data() );
                    map.destroyValue( valueBytes.data() );
                    if ( bInsert )
                        return ContainerReadResult::Read;
                    return ContainerVisitor::mergeResult( ContainerVisitor::mergeResult( keyResult, valueResult ), ContainerReadResult::FieldFailed );
                } ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ContainerElementPlan ContainerElementPlan::make( const NestedContainerInfo& nested, const ContainerSlot slot, const SerializeContext& ctx )
    {
        ContainerElementPlan plan{};
        if ( nested._elementNested != nullptr )
        {
            plan._kind = ContainerElementKind::NestedContainer;
        }
        else if ( slot == ContainerSlot::SequenceElement && SerializerUtil::isOwnedPointerElementType( nested._elementTypeName ) )
        {
            plan._kind = ContainerElementKind::OwnedPointer;
        }
        else
        {
            plan._pElementType = SerializerUtil::findNestedObjectType( nested._elementTypeName, ctx );
            plan._kind         = ( plan._pElementType != nullptr ) ? ContainerElementKind::ValueObject : ContainerElementKind::Scalar;
        }
        return plan;
    }

    void ContainerVisitor::write( const void* pContainer, const NestedContainerInfo& nested, IContainerWriter& writer, const SerializeContext& ctx )
    {
        if ( pContainer == nullptr || nested._wrapper == nullptr )
            return;

        const ISequenceContainerWrapper* pSequence = nested._wrapper->asSequence();
        if ( pSequence != nullptr )
        {
            const ContainerElementPlan plan         = ContainerElementPlan::make( nested, ContainerSlot::SequenceElement, ctx );
            const size_t               elementCount = pSequence->getSize( pContainer );
            writer.beginSequence( elementCount );
            for ( size_t elementIndex = 0; elementIndex < elementCount; ++elementIndex )
            {
                ContainerVisitorInternal::writeElement( pSequence->getElementConst( pContainer, elementIndex ), nested, plan, ContainerSlot::SequenceElement, writer, ctx );
            }
            writer.endSequence();
            return;
        }

        const IMapContainerWrapper* pMap = nested._wrapper->asMap();
        if ( pMap == nullptr )
            return;
        const ContainerElementPlan plan = ContainerElementPlan::make( nested, ContainerSlot::MapValue, ctx );
        writer.beginMap( pMap->getSize( pContainer ) );
        pMap->forEach( pContainer, [&]( const void* pKey, const void* pValue )
        {
            writer.beginMapEntry( pKey, nested._keyTypeName );
            ContainerVisitorInternal::writeElement( pValue, nested, plan, ContainerSlot::MapValue, writer, ctx );
            writer.endMapEntry();
        } );
        writer.endMap();
    }

    ContainerReadResult ContainerVisitor::read( void* pContainer, const NestedContainerInfo& nested, IContainerReader& reader, const SerializeContext& ctx )
    {
        if ( pContainer == nullptr || nested._wrapper == nullptr )
            return ContainerReadResult::StreamBroken;

        const ISequenceContainerWrapper* pSequence = nested._wrapper->asSequence();
        if ( pSequence != nullptr )
            return ContainerVisitorInternal::readSequence( pContainer, *pSequence, nested, reader, ctx );
        const IMapContainerWrapper* pMap = nested._wrapper->asMap();
        if ( pMap != nullptr )
            return ContainerVisitorInternal::readMap( pContainer, *pMap, nested, reader, ctx );
        return ContainerReadResult::StreamBroken;
    }

    ContainerReadResult ContainerVisitor::mergeResult( const ContainerReadResult lhs, const ContainerReadResult rhs ) noexcept
    {
        return ( static_cast<uint8>( rhs ) > static_cast<uint8>( lhs ) ) ? rhs : lhs;
    }
} // namespace sw
