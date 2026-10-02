#include "pch.h"

#include "Engine/Object/Component/MissingComponent.h"

namespace sw
{
    MissingComponent::MissingComponent()
        : _originalTypeName{}
        , _originalFormat{ 0 }
        , _originalText{}
        , _originalBytes{}
    {
        setCanEverTick( false );
    }

    void MissingComponent::setOriginalElement( const SerializeContext::OpaqueElementView& element )
    {
        _originalTypeName = string( element._typeName );
        _originalFormat   = static_cast<uint8>( element._format );
        _originalText     = string( element._text );
        _originalBytes.clear();
        if ( element._pBytes != nullptr && element._byteCount > 0 )
            _originalBytes.assign( element._pBytes, element._pBytes + element._byteCount );
    }

    bool MissingComponent::tryGetOriginalElement( SerializeContext::OpaqueElementView& outElement ) const
    {
        if ( _originalTypeName.empty() )
            return false;
        outElement._typeName  = _originalTypeName;
        outElement._format    = static_cast<SerializeContext::OpaqueFormat>( _originalFormat );
        outElement._text      = _originalText;
        outElement._pBytes    = _originalBytes.empty() ? nullptr : _originalBytes.data();
        outElement._byteCount = _originalBytes.size();
        return true;
    }
} // namespace sw
