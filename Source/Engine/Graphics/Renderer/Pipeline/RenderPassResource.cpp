#include "pch.h"

#include "Engine/Graphics/Renderer/Pipeline/RenderPassResource.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "RenderPassResource" );

    namespace
    {
    } // namespace

    bool RenderPassResource::loadFromXmlFile( string_view assetRelativePath )
    {
        const TypeInfo* pTypeInfo = engine::getTypeRegistry().findType<RenderPassDesc>();
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_ERROR( "RenderPassDesc TypeInfo 를 찾을 수 없습니다 — 리플렉션 생성이 빠졌습니다" );
            return false;
        }

        _desc = {};

        // PROPERTY 그래프를 그대로 읽는다. 예전엔 필드마다 손으로 childText 를 뒤졌는데, 필드를
        // 하나 추가할 때마다 파서와 라이터를 같이 고쳐야 했고 하나만 빠뜨리면 조용히 빈 값이 됐다.
        if ( XmlSerializer::loadFile( assetRelativePath, &_desc, *pTypeInfo ) == false )
        {
            SW_LOG_ERROR( "RenderPass XML 로드 실패: %#", assetRelativePath );
            return false;
        }

        SW_LOG_INFO( "Loaded '%#' (Attachments: %#)", _desc._name, _desc._listAttachment.size() );
        return true;
    }

    bool RenderPassResource::saveToXmlFile( string_view assetRelativePath ) const
    {
        const TypeInfo* pTypeInfo = engine::getTypeRegistry().findType<RenderPassDesc>();
        if ( pTypeInfo == nullptr )
            return false;

        string absPath = ResourceUtil::getResourcePath( assetRelativePath );
        if ( absPath.empty() )
            absPath = assetRelativePath;

        if ( XmlSerializer::saveFile( absPath, &_desc, *pTypeInfo ) == false )
        {
            SW_LOG_ERROR( "Failed to write XML file: %#", absPath );
            return false;
        }

        SW_LOG_INFO( "Saved RenderPass '%#' to: %#", _desc._name, absPath );
        return true;
    }

    TaskHandle RenderPassResource::loadFromXmlFileAsync( string_view assetRelativePath )
    {
        TaskHandle handle = engine::getTaskManager().emplaceTask(
            "LoadRenderPassAsync",
            SW_DELEGATE_FUNCTION( TaskArgsDelegate, RenderPassResource::loadFromXmlFileAsyncJob ),
            MakeTaskArgs( this, string( assetRelativePath ) ) );
        handle.submit();
        return handle;
    }

    void RenderPassResource::loadFromXmlFileAsyncJob( const TaskArgs& args )
    {
        RenderPassResource* pResource = args.get<RenderPassResource*>( 0 );
        if ( pResource == nullptr )
            return;
        pResource->loadFromXmlFile( args.get<string>( 1 ) );
    }

} // namespace sw
