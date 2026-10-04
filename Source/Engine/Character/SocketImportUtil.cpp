#include "pch.h"

#include "Engine/Character/SocketImportUtil.h"

#include "Core/File/FileUtil.h"

#include "Engine/Character/SocketSet.h"

namespace sw
{
    void SocketImportUtil::createSocketSet( vector_reference<const SocketImportNode> listNode, SocketSet& outSockets )
    {
        outSockets.clear();
        for ( const SocketImportNode& node : listNode )
        {
            SocketDef socket;
            socket._name        = node._name;
            socket._parent      = node._parentBone;
            socket._kind        = node._kind;
            socket._previewMesh = node._previewMesh;
            (void)node._localTransform.decompose( socket._scale, socket._rotation, socket._translation );
            socket._fieldMask = SocketFieldBit::kParent | SocketFieldBit::kTranslation | SocketFieldBit::kRotation | SocketFieldBit::kScale;
            if ( socket._kind.empty() == false )
                socket._fieldMask |= SocketFieldBit::kKind;
            if ( socket._previewMesh.empty() == false )
                socket._fieldMask |= SocketFieldBit::kPreview;
            outSockets.addSocket( socket );
        }
    }

    bool SocketImportUtil::writeIfMissing( string_view absolutePath, const SocketSet& sockets, bool& outWritten )
    {
        outWritten = false;
        if ( FileUtil::fileExists( absolutePath ) )
            return true;
        if ( FileUtil::ensureParentDirectoryExists( absolutePath ) == false )
            return false;
        if ( FileUtil::writeTextFile( absolutePath, sockets.saveToXmlText() ) == false )
            return false;
        outWritten = true;
        return true;
    }
} // namespace sw
