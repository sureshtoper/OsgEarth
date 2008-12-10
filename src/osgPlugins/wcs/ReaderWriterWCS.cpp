/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
 * Copyright 2008-2009 Pelican Ventures, Inc.
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

#include <osgEarth/PlateCarre>
#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
#include <osgDB/Registry>
#include <osgDB/ReadFile>
#include "WCS11Source.h"
#include <sstream>
#include <stdlib.h>

using namespace osgEarth;

class ReaderWriterWCS : public osgDB::ReaderWriter
{
    public:
        ReaderWriterWCS() {}

        virtual const char* className()
        {
            return "WCS 1.1.0 Reader";
        }
        
        virtual bool acceptsExtension(const std::string& extension) const
        {
            return osgDB::equalCaseInsensitive( extension, "wcs" );
        }

        virtual ReadResult readObject(const std::string& file_name, const Options* opt) const
        {
            return readNode( file_name, opt );
        }

        virtual ReadResult readNode(const std::string& file_name, const Options* opt) const
        {
            return ReadResult::FILE_NOT_HANDLED;
            //return ReadResult( "WCS: illegal usage (readNode); please use readImage/readHeightField" );
        }

        virtual ReadResult readImage(const std::string& file_name, const Options* opt) const
        {
            return ReadResult::FILE_NOT_HANDLED;
        }

        virtual ReadResult readHeightField(const std::string& file_name, const Options* opt) const
        {            
            std::string ext = osgDB::getFileExtension( file_name );
            if ( !acceptsExtension( ext ) )
            {
                return ReadResult::FILE_NOT_HANDLED;
            }

            std::string keystr = file_name.substr( 0, file_name.find_first_of( '.' ) );
            osg::ref_ptr<TileKey> key = TileKeyFactory::createFromName( keystr );

            osg::ref_ptr<WCS11Source> source = new WCS11Source(); //TODO: config/cache it
            osg::HeightField* field = source->createHeightField( key.get() );
            return field? ReadResult( field ) : ReadResult( "Unable to load WCS height field" );
        }
};

REGISTER_OSGPLUGIN(wcs, ReaderWriterWCS)
