#include "pch.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompileClaims.h"

namespace sw
{
    ShaderCompileClaims::Scope::Scope( ShaderCompileClaims& claims, uint64 textKey )
        : _claims{ claims }
        , _textKey{ textKey }
    {
        _claims.acquire( _textKey );
    }

    ShaderCompileClaims::Scope::~Scope()
    {
        _claims.release( _textKey );
    }

    ShaderCompileClaims::ShaderCompileClaims()
        : _mutex{}
        , _cvReleased{}
        , _uniqueTextKey{}
    {
    }

    void ShaderCompileClaims::acquire( uint64 textKey )
    {
        UniqueLock<mutex> lock{ _mutex };
        while ( _uniqueTextKey.insert( textKey ).second == false )
        {
            _cvReleased.wait( lock );
        }
    }

    void ShaderCompileClaims::release( uint64 textKey )
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            _uniqueTextKey.erase( textKey );
        }
        _cvReleased.notify_all();
    }
} // namespace sw
