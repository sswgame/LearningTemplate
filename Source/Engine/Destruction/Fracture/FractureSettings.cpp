#include "pch.h"

#include "Engine/Destruction/Fracture/FractureSettings.h"

namespace sw
{
    FractureSettings::FractureSettings()
        : _listLevelCount{}
        , _interiorColor{ 0.62f, 0.58f, 0.52f, 1.0f }
        , _impactPoint{}
        , _clusterRadius{ 0.5f }
        , _clusterFraction{ 0.7f }
        , _interiorUvScale{ 1.0f }
        , _sliceJitter{ 0.15f }
        , _seed{ 1 }
        , _pieceCount{ 16 }
        , _arrSliceCount{ 4, 1, 1 }
        , _maxHullPoint{ 48 }
        , _pattern{ FracturePattern::Uniform }
        , _volume{ FractureVolume::Mesh }
    {
    }

    bool FractureSettings::parsePattern( string_view text, FracturePattern& outPattern )
    {
        if ( text == "uniform" )
            outPattern = FracturePattern::Uniform;
        else if ( text == "clustered" )
            outPattern = FracturePattern::Clustered;
        else if ( text == "slices" )
            outPattern = FracturePattern::Slices;
        else
            return false;
        return true;
    }

    string_view FractureSettings::getPatternName( FracturePattern pattern )
    {
        switch ( pattern )
        {
            case FracturePattern::Uniform:
                return "uniform";
            case FracturePattern::Clustered:
                return "clustered";
            case FracturePattern::Slices:
                return "slices";
        }
        return "uniform";
    }

    bool FractureSettings::parseVolume( string_view text, FractureVolume& outVolume )
    {
        if ( text == "mesh" )
            outVolume = FractureVolume::Mesh;
        else if ( text == "bounds" )
            outVolume = FractureVolume::Bounds;
        else if ( text == "hull" )
            outVolume = FractureVolume::Hull;
        else
            return false;
        return true;
    }

    string_view FractureSettings::getVolumeName( FractureVolume volume )
    {
        switch ( volume )
        {
            case FractureVolume::Mesh:
                return "mesh";
            case FractureVolume::Bounds:
                return "bounds";
            case FractureVolume::Hull:
                return "hull";
        }
        return "mesh";
    }
} // namespace sw
