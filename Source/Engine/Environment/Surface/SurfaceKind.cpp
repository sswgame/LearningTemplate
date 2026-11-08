#include "pch.h"

#include "Engine/Environment/Surface/SurfaceKind.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"

namespace sw
{
    bool SurfaceKindTable::loadFromAsset( const AssetID& assetID )
    {
        *this = SurfaceKindTable{};
        return finishLoad( assetID.view(), ReflectedXMLFile::loadDesc( assetID, *this ) );
    }

    bool SurfaceKindTable::loadFromDiskFile( string_view diskPath )
    {
        *this = SurfaceKindTable{};
        return finishLoad( diskPath, ReflectedXMLFile::loadDescFromDiskFile( diskPath, *this ) );
    }

    bool SurfaceKindTable::finishLoad( string_view path, bool bRead )
    {
        bool bOk = bRead;
        if ( bOk && _listKind.empty() )
        {
            SW_LOG_ERROR_ID( "Surface.KindTableEmpty", "%#: no surface kinds in <_listKind>", path );
            bOk = false;
        }
        // 칸 · 범위(이동 계수 0..1, 도장 칸 >= 0, 변형 격자 크기)는 PROPERTY 가 정본이고 `loadDesc` 가 본다. 여기서는 칸 사이의 규칙만 본다.
        for ( size_t kindIndex = 0; kindIndex < _listKind.size(); ++kindIndex )
        {
            const SurfaceKindDef& kind = _listKind[kindIndex];
            for ( size_t otherIndex = 0; otherIndex < kindIndex; ++otherIndex )
            {
                if ( _listKind[otherIndex]._name == kind._name )
                {
                    SW_LOG_ERROR_ID( "Surface.KindDuplicate", "%#: surface kind '%#' appears twice", path, kind._name.c_str() );
                    bOk = false;
                }
            }
            if ( kind._footprint.validate( path, kind._name.view() ) == false )
                bOk = false;
        }
        if ( bOk && findKind( _waterKind ) == nullptr )
        {
            SW_LOG_ERROR_ID( "Surface.WaterKindUnknown", "%#: _waterKind '%#' is not a surface kind of the table", path, _waterKind.c_str() );
            bOk = false;
        }
        if ( bOk == false )
            *this = SurfaceKindTable{};
        return bOk;
    }

    const SurfaceKindDef* SurfaceKindTable::findKind( const hashed_string& name ) const
    {
        return VectorUtil::findBy<&SurfaceKindDef::_name>( _listKind, name );
    }

    const SurfaceKindDef& SurfaceKindTable::getWaterKind() const
    {
        const SurfaceKindDef* pWater = findKind( _waterKind );
        SW_LOG_ASSERT( pWater != nullptr, "The surface kind table was read without its water kind '%#'", _waterKind.c_str() );
        return *pWater;
    }

    shared_ptr<const SurfaceKindTable> SurfaceKindTable::findDefault()
    {
        const string&                      path   = engine::getSubsystem<EngineDefaultAssets>()._surfaceKinds.get();
        shared_ptr<const SurfaceKindTable> pTable = SharedAssetCache<SurfaceKindTable>::acquire( path );
        if ( pTable == nullptr )
            SW_LOG_ERROR_ID( "Surface.DefaultKindsCould", "Default surface kinds '%#' could not be loaded", path.c_str() );
        return pTable;
    }

    /** @brief 표면 종류 표 데이터 파일 종류입니다. */
    SW_SHARED_ASSET_CACHE( SurfaceKinds, SurfaceKindTable,
                           AssetTypeDef::makeDataFile( "surfacekinds", AssetPathRule{ {}, {}, ".surfacekinds.xml" }, AssetTypeLoader::of<SurfaceKindTable>( MemoryTag::Physics ) ) );
} // namespace sw
