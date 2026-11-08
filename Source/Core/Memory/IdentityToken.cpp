#include "pch.h"

#include "Core/Memory/IdentityToken.h"

namespace sw
{
    IdentityToken IdentityToken::make()
    {
        return IdentityToken{ sw::make_shared<Block>() };
    }
} // namespace sw
