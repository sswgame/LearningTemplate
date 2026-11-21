#include "pch.h"

#include "Engine/Reflection/ReflectValueBinding.h"

#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/TypeLookupCache.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    bool ReflectValueBinding::tryRead( const uint32 generation, const uint32 nameHash, ReflectValueFacts& outFacts ) const
    {
        const uint32 sequenceBefore = _sequence.load( std::memory_order_acquire );
        if ( sequenceBefore == 0 || ( sequenceBefore & 1u ) != 0 )
            return false;
        const uint32            recordedGeneration = _generation.load( std::memory_order_relaxed );
        const uint32            recordedNameHash   = _nameHash.load( std::memory_order_relaxed );
        const ReflectValueFacts facts{ _pType.load( std::memory_order_relaxed ), _pEnum.load( std::memory_order_relaxed ),
                                       static_cast<ReflectBuiltinIndex>( _builtinIndex.load( std::memory_order_relaxed ) ) };
        std::atomic_thread_fence( std::memory_order_acquire );
        if ( _sequence.load( std::memory_order_relaxed ) != sequenceBefore )
            return false;
        if ( recordedGeneration != generation || recordedNameHash != nameHash )
            return false;
        outFacts = facts;
        return true;
    }

    void ReflectValueBinding::write( const uint32 generation, const uint32 nameHash, const ReflectValueFacts& facts ) const
    {
        // 쓰는 쪽이 겹치면 진 쪽은 쓰지 않는다 — 같은 세대의 같은 사실이라 이긴 쪽의 것과 같다.
        uint32 sequence = _sequence.load( std::memory_order_relaxed );
        if ( ( sequence & 1u ) != 0 )
            return;
        if ( _sequence.compare_exchange_strong( sequence, sequence + 1, std::memory_order_relaxed, std::memory_order_relaxed ) == false )
            return;
        std::atomic_thread_fence( std::memory_order_release );
        _generation.store( generation, std::memory_order_relaxed );
        _nameHash.store( nameHash, std::memory_order_relaxed );
        _pType.store( facts._pType, std::memory_order_relaxed );
        _pEnum.store( facts._pEnum, std::memory_order_relaxed );
        _builtinIndex.store( static_cast<uint8>( facts._builtinIndex ), std::memory_order_relaxed );
        _sequence.store( sequence + 2, std::memory_order_release );
    }

    ReflectValueFacts ReflectValueBinding::resolve( const hashed_string& name, const TypeRegistry& registry ) const
    {
        const uint32      generation = getTypeTableGeneration();
        const uint32      nameHash   = name.empty() ? 0u : name.getHash();
        ReflectValueFacts facts{ nullptr, nullptr, ReflectBuiltinIndex::kNone };
        if ( tryRead( generation, nameHash, facts ) )
            return facts;
        facts._builtinIndex = ReflectedValueUtil::findBuiltinIndex( name );
        facts._pEnum        = registry.findEnum( name );
        facts._pType        = registry.findType( name );
        // 찾는 동안 세대가 올랐으면(다른 스레드의 등록) 묶지 않는다 — 다음 조회가 새 세대로 다시 찾는다.
        if ( getTypeTableGeneration() == generation )
            write( generation, nameHash, facts );
        return facts;
    }
} // namespace sw
