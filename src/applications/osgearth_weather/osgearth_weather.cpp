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
#include <osg/Notify>
#include <osgDB/FileNameUtils>
#include <osgGA/GUIEventHandler>
#include <osgGA/StateSetManipulator>
#include <osgViewer/Viewer>
#include <osgEarth/MapNode>
#include <osgEarth/FileUtils>
#include <osgEarthUtil/ExampleResources>
#include <osgEarthUtil/EarthManipulator>
#include <osgEarthDrivers/tms/TMSOptions>
#include <osgEarthDrivers/gdal/GDALOptions>
#include <osgEarthDrivers/colorramp/ColorRampOptions>

using namespace osgEarth;
using namespace osgEarth::Drivers;
using namespace osgEarth::Util;

void loadAltitude(std::string directory, ImageLayerVector &layers)
{
    CollectFilesVisitor v;
    v.traverse(directory);

    for (unsigned int i = 0; i < v.filenames.size(); i++)
    {
        std::string filename = v.filenames[i];
        if (osgDB::getFileExtension(filename) == "gr1")
        {
            ColorRampOptions colorOpts;
            colorOpts.ramp() = "../data/colorramps/temperature_c.clr";

            GDALOptions gdalOpt;
            gdalOpt.url() = filename;

            
            ElevationLayerOptions elevationOpt("", gdalOpt);
            colorOpts.elevationLayer() = elevationOpt;

            layers.push_back( new ImageLayer(colorOpts) );
        }
    }
}

int
main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc,argv);

    // create the map.
    Map* map = new Map();

    // add a TMS imagery layer:
    TMSOptions imagery;
    imagery.url() = "http://readymap.org/readymap/tiles/1.0.0/22/";
    map->addImageLayer( new ImageLayer("ReadyMap imagery", imagery) );


    // Load up the altitudes
    ImageLayerVector gribs;
    loadAltitude("C:/geodata/NextGenFed/WeatherData/AirTemp5f", gribs);
    for (unsigned int i = 0; i < gribs.size(); i++)
    {
        ImageLayer* layer = gribs[i].get();
        layer->setVisible(i == 0);
        map->addImageLayer(layer);
    }

    // initialize a viewer:
    osgViewer::Viewer viewer(arguments);
    EarthManipulator* manip = new EarthManipulator();
    viewer.setCameraManipulator( manip );

    // make the map scene graph:
    osg::Group* root = new osg::Group();
    viewer.setSceneData( root );

    MapNode* mapNode = new MapNode( map );
    root->addChild( mapNode );
    
    // Process cmdline args
    MapNodeHelper helper;
    helper.configureView( &viewer );
    helper.parse(mapNode, arguments, &viewer, root, new LabelControl("Weather Explorer"));    

    unsigned int index = 0;

    while (!viewer.done())
    {
        // Animate the overlays
        if (viewer.getFrameStamp()->getFrameNumber() % 20 == 0)
        {
            // Increment the index
            index++;
            if (index >= gribs.size())
            {
                index = 0;
            }
            
            // Now enable the correct layer
            for (unsigned int i = 0; i < gribs.size(); i++)
            {
                ImageLayer* layer = gribs[i].get();
                layer->setVisible(i == index );
            }            
        }
        viewer.frame();
    }

    return 0;
}
