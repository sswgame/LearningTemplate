#include "pch.h"

#include "Engine/Reflection/ReflectedInstance.h"

#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionTypes.h"

namespace sw
{
    ReflectedInstance::ReflectedInstance( const TypeInfo& type, MemoryBlock&& block ) noexcept
        : _block{ std::move( block ) }
        , _pType{ &type }
    {
    }

    ReflectedInstance::ReflectedInstance( ReflectedInstance&& other ) noexcept
        : _block{ std::move( other._block ) }
        , _pType{ other._pType }
    {
        other._pType = nullptr;
    }

    ReflectedInstance& ReflectedInstance::operator=( ReflectedInstance&& other ) noexcept
    {
        if ( this == &other )
            return *this;
        reset(); // 지금 인스턴스의 소멸자부터 — 블록만 바꿔 넣으면 소멸자를 건너뛴다
        _block       = std::move( other._block );
        _pType       = other._pType;
        other._pType = nullptr;
        return *this;
    }

    ReflectedInstance ReflectedInstance::create( const TypeInfo& type )
    {
        const FunctionInfo* pConstructor = type.findMethod( hashed_string( "$ctor" ) );
        const bool          bHasCtor     = pConstructor != nullptr && pConstructor->_invoker.isBound();
        const bool          bHasDtor     = type._destroyInstance != nullptr && type._size != 0;
        if ( ( bHasCtor && bHasDtor ) == false )
            return {};
        MemoryBlock block = MemoryBlock::allocateAligned( type._size, alignof( std::max_align_t ) );
        if ( block.get() == nullptr )
            return {};
        (void)pConstructor->_invoker( block.get(), TaskArgs{} );
        return ReflectedInstance{ type, std::move( block ) };
    }

    void ReflectedInstance::reset() noexcept
    {
        if ( _block.get() != nullptr && _pType != nullptr )
            _pType->_destroyInstance( _block.get() );
        _block.reset();
        _pType = nullptr;
    }
} // namespace sw
