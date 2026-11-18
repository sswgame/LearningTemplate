#include "pch.h"

#include "Engine/Environment/Surface/SurfaceKind.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/MaterialParameterCollection.h"
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
            if ( kind._step._footprint.validate( path, kind._name.view() ) == false || kind._wetStep._footprint.validate( path, kind._name.view() ) == false )
                bOk = false;
        }
        if ( bOk && ( _wetPrintSize._x <= 0.0f || _wetPrintSize._y <= 0.0f ) )
        {
            SW_LOG_ERROR_ID( "Surface.WetPrintSizeMissing", "%#: _wetPrintSize needs a width and a length above 0 (wet prints would cover nothing)", path );
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

    float32 SurfaceKindTable::readGroundWetness() const
    {
        if ( _bWetnessRequested == false )
        {
            _bWetnessRequested  = true;
            _pWetnessCollection = MaterialParameterCollection::acquire( AssetID{ _wetnessCollection.get() } ); // 읽지 못하면 받기가 오류를 남긴다
        }
        const float4* pValue = _pWetnessCollection != nullptr ? _pWetnessCollection->findValue( _wetnessParameter ) : nullptr;
        if ( _pWetnessCollection != nullptr && pValue == nullptr )
            SW_LOG_ERROR_ONCE_ID( "Surface.WetnessParameterMissing", "Surface kinds: parameter '%#' is not in '%#' - footsteps treat the ground as dry", _wetnessParameter.c_str(),
                                  _wetnessCollection.c_str() );
        return pValue != nullptr ? MathUtil::clamp( pValue->_x, 0.0f, 1.0f ) : 0.0f;
    }

    shared_ptr<const SurfaceKindTable> SurfaceKindTable::findDefault()
    {
        const string&                      path   = engine::getSubsystem<EngineDefaultAssets>()._surfaceKinds.get();
        shared_ptr<const SurfaceKindTable> pTable = SharedAssetCache<SurfaceKindTable>::acquire( path ).snapshot();
        if ( pTable == nullptr )
            SW_LOG_ERROR_ID( "Surface.DefaultKindsCould", "Default surface kinds '%#' could not be loaded", path.c_str() );
        return pTable;
    }

    /** @brief 표면 종류 표 데이터 파일 종류입니다. */
    SW_SHARED_ASSET_CACHE( SurfaceKinds, SurfaceKindTable,
                           AssetTypeDef::makeDataFile( "surfacekinds", AssetPathRule{ {}, {}, ".surfacekinds.xml" }, AssetTypeLoader::of<SurfaceKindTable>( MemoryTag::Physics ) ) );
} // namespace sw
