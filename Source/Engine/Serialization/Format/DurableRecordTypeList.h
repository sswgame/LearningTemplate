/**
 * @file DurableRecordTypeList.h
 * @brief 영속 레코드 형식(저장소 표 · 휘발 저장소 표가 싣는 `REFLECT` 형식)의 목록입니다. 배치 대조(`--write-state-layouts` → `py -3 -m Scripts format-release`)가 훑는다.
 * @details 표의 코덱(`ServiceRecordCodec<T>`)이 형식마다 `DurableRecordType` 하나를 정적으로 두어 스스로 오른다 — 손으로 적는 목록이 없어 표를 더하고 목록을 빠뜨릴 수 없다.
 *          모듈이 내려가면 그 형식도 빠진다(정적 소멸). 배포 빌드에는 대조가 없어 오르지 않는다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct TypeInfo;

    /** @brief 영속 레코드 형식 하나의 등록입니다. 정적 수명으로만 둔다(`ServiceRecordCodec`). */
    class SW_API DurableRecordType
    {
    public:
        using StaticTypeFn = const TypeInfo* (*)();

        explicit DurableRecordType( StaticTypeFn pfnStaticType );
        ~DurableRecordType();

        DurableRecordType( const DurableRecordType& )            = delete;
        DurableRecordType& operator=( const DurableRecordType& ) = delete;

        const TypeInfo* getType() const { return _pfnStaticType(); }

        /** @brief 올라온 형식마다 @p visitor( `const TypeInfo&` ) 를 부릅니다(올린 차례). */
        template <typename TVisitor>
        static void forEach( TVisitor&& visitor )
        {
            for ( uint32 index = 0; index < getCount(); ++index )
            {
                if ( const TypeInfo* pType = getAt( index )->getType() )
                    visitor( *pType );
            }
        }

    private:
        static uint32                   getCount();
        static const DurableRecordType* getAt( uint32 index );

        StaticTypeFn _pfnStaticType;
    };
} // namespace sw
