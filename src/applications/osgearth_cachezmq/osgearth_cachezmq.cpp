/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
 * Copyright 2008-2013 Pelican Mapping
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

#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>

#include <osg/io_utils>

#include <osgEarth/Common>
#include <osgEarth/Map>
#include <osgEarth/MapFrame>
#include <osgEarth/Cache>
#include <osgEarth/CacheEstimator>
#include <osgEarth/CacheSeed>
#include <osgEarth/MapNode>
#include <osgEarth/Registry>
#include <osgEarthDrivers/feature_ogr/OGRFeatureOptions>

#include <iostream>
#include <sstream>
#include <iterator>

#include <zmq.h>

using namespace osgEarth;
using namespace osgEarth::Drivers;

#define LC "[osgearth_cache] "

//  Receive 0MQ string from socket and convert into C string
//  Caller must free returned string. Returns NULL if the context
//  is being terminated.
static char *
s_recv (void *socket) {
    char buffer [256];
    int size = zmq_recv (socket, buffer, 255, 0);
    if (size == -1)
        return NULL;
    if (size > 255)
        size = 255;
    buffer [size] = 0;
    return strdup (buffer);
}

//  Convert C string to 0MQ string and send to socket
static int
s_send (void *socket, char *string) {
    int size = zmq_send (socket, string, strlen (string), 0);
    return size;
}

void processKey( const TileKey& key, unsigned int minLevel, unsigned int maxLevel, const std::vector< GeoExtent > extents, void* sender )
{
    unsigned int x, y, lod;
    key.getTileXY(x, y);
    lod = key.getLevelOfDetail();

    bool gotData = true;

    if ( minLevel <= lod && maxLevel >= lod )
    {
        // Queue the task
        char string [256];        
        sprintf (string, "%d/%d/%d", key.getLevelOfDetail(), key.getTileX(), key.getTileY());        
        OE_NOTICE << "Sending " << string << std::endl;
        s_send (sender, string);

        /*
        gotData = cacheTile( mapf, key );
        if (gotData)
        {
        incrementCompleted( 1 );
        }

        if ( _progress.valid() && _progress->isCanceled() )
            return; // Task has been cancelled by user

        if ( _progress.valid() && gotData && _progress->reportProgress(_completed, _total, std::string("Cached tile: ") + key.str()) )
            return; // Canceled
            */
    }

    if ( gotData && lod <= maxLevel )
    {
        TileKey k0 = key.createChildKey(0);
        TileKey k1 = key.createChildKey(1);
        TileKey k2 = key.createChildKey(2);
        TileKey k3 = key.createChildKey(3); 

        bool intersectsKey = false;
        if (extents.empty()) intersectsKey = true;
        else
        {
            for (unsigned int i = 0; i < extents.size(); ++i)
            {
                if (extents[i].intersects( k0.getExtent() ) ||
                    extents[i].intersects( k1.getExtent() ) ||
                    extents[i].intersects( k2.getExtent() ) ||
                    extents[i].intersects( k3.getExtent() ))
                {
                    intersectsKey = true;
                }

            }
        }

        //Check to see if the bounds intersects ANY of the tile's children.  If it does, then process all of the children
        //for this level
        if (intersectsKey)
        {
            processKey( k0, minLevel, maxLevel, extents, sender );
            processKey( k1, minLevel, maxLevel, extents, sender );
            processKey( k2, minLevel, maxLevel, extents, sender );
            processKey( k3, minLevel, maxLevel, extents, sender );            
        }
    }
}

bool
cacheTile(const MapFrame& mapf, const TileKey& key )
{
    bool gotData = false;

    for( ImageLayerVector::const_iterator i = mapf.imageLayers().begin(); i != mapf.imageLayers().end(); i++ )
    {
        ImageLayer* layer = i->get();
        if ( layer->isKeyValid( key ) )
        {
            GeoImage image = layer->createImage( key );
            if ( image.valid() )
                gotData = true;
        }
    }

    if ( mapf.elevationLayers().size() > 0 )
    {
        osg::ref_ptr<osg::HeightField> hf;
        mapf.getHeightField( key, false, hf );
        if ( hf.valid() )
            gotData = true;
    }

    return gotData;
}


int
producer( osg::ArgumentParser& args )
{   
    //Read the min level
    unsigned int minLevel = 0;
    while (args.read("--min-level", minLevel));
    
    //Read the max level
    unsigned int maxLevel = 5;
    while (args.read("--max-level", maxLevel));

    bool estimate = args.read("--estimate");        
    

    std::vector< Bounds > bounds;
    // restrict packaging to user-specified bounds.    
    double xmin=DBL_MAX, ymin=DBL_MAX, xmax=DBL_MIN, ymax=DBL_MIN;
    while (args.read("--bounds", xmin, ymin, xmax, ymax ))
    {        
        Bounds b;
        b.xMin() = xmin, b.yMin() = ymin, b.xMax() = xmax, b.yMax() = ymax;
        bounds.push_back( b );
    }    

    //Read the cache override directory
    std::string cachePath;
    while (args.read("--cache-path", cachePath));

    //Read the cache type
    std::string cacheType;
    while (args.read("--cache-type", cacheType));

    bool verbose = args.read("--verbose");

    //Read in the earth file.
    osg::ref_ptr<osg::Node> node = osgDB::readNodeFiles( args );
    if ( !node.valid() )
    {
        OE_NOTICE << "Failed to read .earth file." << std::endl;
        return 1;
    }

    MapNode* mapNode = MapNode::findMapNode( node.get() );
    if ( !mapNode )
    {
        OE_NOTICE << "Input file was not a .earth file" << std::endl;
        return 1;
    }    

    std::vector<TileKey> keys;
    mapNode->getMap()->getProfile()->getRootKeys(keys);

    std::vector< GeoExtent > extents;
    for (unsigned int i = 0; i < bounds.size(); i++)
    {
        GeoExtent extent(mapNode->getMapSRS(), bounds[i]);
        extents.push_back( extent );
    }
       

    void *context = zmq_ctx_new ();

    //  Socket to send messages on
    void *sender = zmq_socket (context, ZMQ_PUSH);
    int hwm = 10000;
    zmq_setsockopt( sender, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_bind (sender, "tcp://*:5557");

    printf ("Press Enter when the workers are ready: ");
    getchar ();
    printf ("Sending tasks to workers…\n");

    // Get the root keys    
    for (unsigned int i = 0; i < keys.size(); i++)
    {
        processKey( keys[i], minLevel, maxLevel, extents, sender );
    }

    unsigned int numWorkers = 6;
    for (unsigned int i = 0; i < numWorkers; i++)
    {
        s_send( sender, "DIE");
    }    
    
    // Don't close so we don't destroy the socket and context so all the messages will get processed
    //zmq_close (sender);
    //zmq_ctx_destroy (context);
    return 0;
}

int
consumer( osg::ArgumentParser& args )
{
     //Read the min level
    unsigned int minLevel = 0;
    while (args.read("--min-level", minLevel));
    
    //Read the max level
    unsigned int maxLevel = 5;
    while (args.read("--max-level", maxLevel));

    bool estimate = args.read("--estimate");        
    

    std::vector< Bounds > bounds;
    // restrict packaging to user-specified bounds.    
    double xmin=DBL_MAX, ymin=DBL_MAX, xmax=DBL_MIN, ymax=DBL_MIN;
    while (args.read("--bounds", xmin, ymin, xmax, ymax ))
    {        
        Bounds b;
        b.xMin() = xmin, b.yMin() = ymin, b.xMax() = xmax, b.yMax() = ymax;
        bounds.push_back( b );
    }    

    //Read the cache override directory
    std::string cachePath;
    while (args.read("--cache-path", cachePath));

    //Read the cache type
    std::string cacheType;
    while (args.read("--cache-type", cacheType));

    bool verbose = args.read("--verbose");

    //Read in the earth file.
    osg::ref_ptr<osg::Node> node = osgDB::readNodeFiles( args );
    if ( !node.valid() )
    {
        OE_NOTICE << "Failed to read .earth file." << std::endl;
        return 1;
    }

    MapNode* mapNode = MapNode::findMapNode( node.get() );
    if ( !mapNode )
    {
        OE_NOTICE << "Input file was not a .earth file" << std::endl;
        return 1;
    }

    MapFrame mapf( mapNode->getMap(), Map::TERRAIN_LAYERS, "CacheSeed::seed" );

    CacheSeed seeder;    

    //  Socket to receive messages on
    void *context = zmq_ctx_new ();
    void *receiver = zmq_socket (context, ZMQ_PULL);
    zmq_connect (receiver, "tcp://localhost:5557");    
     
    
    //  Process tasks forever
    while (1) {
        char *string = s_recv (receiver);
        unsigned int lod, x, y;
        if (strcmp(string, "DIE") == 0)
        {
            std::cout << "Got poison pill" << std::endl;
            break;
        }

        sscanf(string, "%d/%d/%d.%d", &lod, &x, &y);
        free (string);        

        TileKey key( lod, x, y, mapf.getProfile());        

        OE_NOTICE << "Processing tile " << key.str() << std::endl;

        cacheTile( mapf, key );        
    }
    zmq_close (receiver);    
    zmq_ctx_destroy (context);
    return 0;    
}

int
main(int argc, char** argv)
{
    osg::ArgumentParser args(&argc,argv);

    if ( args.read( "--producer") )
        return producer( args );
    else if ( args.read( "--consumer" ) )
        return consumer( args );    
}

