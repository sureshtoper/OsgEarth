/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2008-2012 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <osgEarthSim/IconFactory>
#include <osgDB/ReadFile>
#include <osgEarth/ImageUtils>

#include <osgEarth/Notify>

using namespace osgEarth::Sim;

IconFactory::IconFactory():
_iconSize( 40 )
{        
    load("../data/airtrack_friendly.png",
         "../data/airtrack_opposing.png",
         "../data/airtrack_neutral.png",
         "../data/airtrack_unknown.png"
        );
}

IconFactory::IconFactory(int iconSize,
                    const std::string& friendly,
                    const std::string& opposing,
                    const std::string& neutral,
                    const std::string& unknown
                    ):
_iconSize( iconSize )
{
    load( friendly, opposing, neutral, unknown);
}

void IconFactory::load(const std::string& friendly,
                       const std::string& opposing,
                       const std::string& neutral,
                       const std::string& unknown)
{
    osg::ref_ptr< osg::Image > img = osgDB::readImageFile( friendly  );
    if (img.valid())
    {
        ImageUtils::resizeImage( img.get(), _iconSize, _iconSize, _friendly );
    }

    img = osgDB::readImageFile( opposing );        
    if (img.valid())
    {
        ImageUtils::resizeImage( img.get(), _iconSize, _iconSize, _opposing );
    }

    img  = osgDB::readImageFile( neutral );        
    if (img.valid())
    {
        ImageUtils::resizeImage( img.get(), _iconSize, _iconSize, _neutral );
    }

    img  = osgDB::readImageFile( unknown );  
    if (img.valid())
    {
        ImageUtils::resizeImage( img.get(), _iconSize, _iconSize, _unknown );
    }
}


osg::Image* IconFactory::getIcon(ForceID forceId )
{
    if (forceId == Friendly)
    {
        return _friendly.get();
    }
    else if (forceId == Opposing)
    {
        return _opposing.get();
    }
    else if (forceId == Neutral)
    {
        return _neutral.get();
    }
    return _unknown.get();
}

int IconFactory::getIconSize() const
{
    return _iconSize;
}