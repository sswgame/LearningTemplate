#include "pch.h"

#include "GameFramework/Base/Progression/LevelProgress.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    ExperienceCurve::ExperienceCurve()
        : _listXpToNext{}
        , _base{ 100.0f }
        , _exponent{ 1.5f }
        , _linear{ 0.0f }
        , _maxLevel{ 99 }
    {
    }

    void ExperienceCurve::setFormula( float32 base, float32 exponent, float32 linear, int32 maxLevel )
    {
        _listXpToNext.clear();
        _base     = MathUtil::max( 0.0f, base );
        _exponent = exponent;
        _linear   = linear;
        _maxLevel = MathUtil::max( 1, maxLevel );
    }

    void ExperienceCurve::setTable( const vector<int64>& listXpToNext )
    {
        _listXpToNext = listXpToNext;
        _maxLevel     = static_cast<int32>( listXpToNext.size() ) + 1;
    }

    bool ExperienceCurve::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        if ( GameDataXml::parseRoot( doc, xmlText, sourceName, "ExperienceCurve", root ) == false )
            return false;
        loadFromNode( root );
        return true;
    }

    void ExperienceCurve::loadFromNode( const XmlNode& node )
    {
        vector<int64> listXpToNext;
        for ( XmlNode child = node.findChild( "Level" ); child; child = child.findNextSibling( "Level" ) )
        {
            listXpToNext.push_back( MathUtil::max( 1, child.getAttributeInt( "xp", 1 ) ) );
        }
        if ( listXpToNext.empty() == false )
        {
            setTable( listXpToNext );
            return;
        }
        setFormula( node.getAttributeFloat( "base", _base ), node.getAttributeFloat( "exponent", _exponent ), node.getAttributeFloat( "linear", _linear ),
                    node.getAttributeInt( "maxLevel", _maxLevel ) );
    }

    int64 ExperienceCurve::getXpToNext( int32 level ) const
    {
        if ( level < 1 || level >= _maxLevel )
            return 0;
        if ( _listXpToNext.empty() == false )
            return _listXpToNext[static_cast<size_t>( level - 1 )];
        const float32 value = _base * MathUtil::pow( static_cast<float32>( level ), _exponent ) + _linear * static_cast<float32>( level );
        return MathUtil::max<int64>( 1, static_cast<int64>( value + 0.5f ) );
    }

    int64 ExperienceCurve::computeTotalXp( int32 level ) const
    {
        int64 total = 0;
        for ( int32 current = 1; current < MathUtil::min( level, _maxLevel ); ++current )
        {
            total += getXpToNext( current );
        }
        return total;
    }

    int32 LevelProgress::addXp( const ExperienceCurve& curve, int64 amount )
    {
        if ( amount <= 0 || _level >= curve.getMaxLevel() )
            return 0;
        _totalXp += amount;
        _xp += amount;
        int32 gained = 0;
        while ( _level < curve.getMaxLevel() && _xp >= curve.getXpToNext( _level ) )
        {
            _xp -= curve.getXpToNext( _level );
            ++_level;
            ++gained;
        }
        if ( _level >= curve.getMaxLevel() )
        {
            _totalXp -= _xp; // 넘친 것은 버린다
            _xp = 0;
        }
        return gained;
    }

    void LevelProgress::setLevel( const ExperienceCurve& curve, int32 level )
    {
        _level   = MathUtil::clamp( level, 1, curve.getMaxLevel() );
        _xp      = 0;
        _totalXp = curve.computeTotalXp( _level );
    }

    float32 LevelProgress::computeRatio( const ExperienceCurve& curve ) const
    {
        const int64 toNext = curve.getXpToNext( _level );
        return toNext <= 0 ? 1.0f : static_cast<float32>( _xp ) / static_cast<float32>( toNext );
    }

    void LevelProgress::writeState( Archive& outArchive ) const
    {
        outArchive << _xp;
        outArchive << _totalXp;
        outArchive << _level;
    }

    bool LevelProgress::readState( Archive& archive )
    {
        int64 xp      = 0;
        int64 totalXp = 0;
        int32 level   = 1;
        archive >> xp;
        archive >> totalXp;
        archive >> level;
        const bool bValid = archive.isOk() && 0 <= xp && xp <= totalXp && 1 <= level;
        if ( bValid == false )
            return false;
        _xp      = xp;
        _totalXp = totalXp;
        _level   = level;
        return true;
    }
} // namespace sw
