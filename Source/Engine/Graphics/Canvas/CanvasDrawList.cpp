#include "pch.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"

namespace sw
{
    bool CanvasTextureRef::isEqual( const CanvasTextureRef& other ) const
    {
        return _texture == other._texture && _atlasPage == other._atlasPage;
    }

    void CanvasDrawList::clear()
    {
        _listQuad.clear();
        _listBatch.clear();
        _targetSize = float2{};
    }

    void CanvasFrameData::clear()
    {
        _mainOutput.clear();
        _listAtlasUpload.clear();
        _contentRevision = 0;
    }
} // namespace sw
