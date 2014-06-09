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
#include <osgDB/FileUtils>
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
using namespace osgEarth::Util::Controls;


/**
 * A collection of grib files at a given altitude
 */
struct Altitude
{
    std::string name;
    ImageLayerVector layers;
    osg::ref_ptr< CheckBoxControl > control;

    bool load(std::string directory)
    {
        name = osgDB::getSimpleFileName(directory);        
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

        OE_NOTICE << "Loaded " << layers.size() << " layers from " << directory << std::endl;

        return !layers.empty();
    }

    void addToMap(Map* map)
    {
        for (unsigned int i = 0; i < layers.size(); i++)
        {
            ImageLayer* layer = layers[i].get();
            layer->setVisible(false);
            map->addImageLayer(layer);
        }
    }

    void hideAll()
    {
        for (unsigned int i = 0; i < layers.size(); i++)
        {
            ImageLayer* layer = layers[i].get();
            layer->setVisible(false);         
        }
    }

    void show(int index)
    {
        hideAll();
        if (index >= 0 && index < layers.size())
        {
            layers[index]->setVisible(true);
        }
    }
};

typedef std::vector< Altitude > AltitudeVector;



static Grid* s_layerBox = NULL;
unsigned int s_selectedAltitude = 0;
AltitudeVector s_altitudes;
unsigned int s_index = 0;
bool s_autoPlay = true;
HSliderControl* s_timeSlider = 0;
LabelControl* s_timeLabel = 0;



osg::Node*
createControlPanel( osgViewer::View* view )
{
    ControlCanvas* canvas = ControlCanvas::get( view );

    // the outer container:
    s_layerBox = new Grid();
    s_layerBox->setBackColor(0,0,0,0.5);
    s_layerBox->setMargin( 10 );
    s_layerBox->setPadding( 10 );
    s_layerBox->setChildSpacing( 10 );
    s_layerBox->setChildVertAlign( Control::ALIGN_CENTER );
    s_layerBox->setAbsorbEvents( true );
    s_layerBox->setVertAlign( Control::ALIGN_TOP );

    canvas->addControl( s_layerBox );    
    return canvas;
}

struct SelectAltitudeHandler : public ControlEventHandler
{
    SelectAltitudeHandler( unsigned int index ) :
        _index(index)
        {
        }

    void onValueChanged( Control* control, bool value ) {
        if (value) {
            s_selectedAltitude = _index;            
        }

        for (unsigned int i = 0; i < s_altitudes.size(); i++)
        {
            s_altitudes[i].control->setValue( s_selectedAltitude == i );
        }
    }
    unsigned int _index;
};

struct TimeChangedHandler : public ControlEventHandler
{
    TimeChangedHandler()
    {
    }

    void onValueChanged( Control* control, float value ) {
        s_index = (int)value;
    }    
};

struct AutoPlayHandler : public ControlEventHandler
{
    AutoPlayHandler() 
    {
    }

    void onValueChanged( Control* control, bool value ) {
        s_autoPlay = value;
    }
};



void initGUI()
{    
    unsigned int row = 0;

    //The overlay name
    LabelControl* header = new LabelControl( "Altitudes");      
    header->setVertAlign( Control::ALIGN_CENTER );
    header->setFontSize(22.0f);
    header->setForeColor(255,255,0,255);
    s_layerBox->setControl( 1, row, header );
    row++;

    for (unsigned int i = 0; i < s_altitudes.size(); i++)
    {
        Altitude& alt = s_altitudes[i];
        //Add some controls        
        CheckBoxControl* enabled = new CheckBoxControl( i == 0 );
        enabled->addEventHandler( new SelectAltitudeHandler(i) );
        enabled->setVertAlign( Control::ALIGN_CENTER );
        s_layerBox->setControl( 0, row, enabled );
        alt.control = enabled;

        //The overlay name
        LabelControl* name = new LabelControl( alt.name );      
        name->setVertAlign( Control::ALIGN_CENTER );
        s_layerBox->setControl( 1, row, name );
        row++;
    }

    row++;


    CheckBoxControl* autoPlay = new CheckBoxControl( s_autoPlay);
    autoPlay->addEventHandler( new AutoPlayHandler() );
    autoPlay->setVertAlign( Control::ALIGN_CENTER );
    s_layerBox->setControl( 0, row, autoPlay );        

    LabelControl* autoPlayLabel = new LabelControl( "Autoplay");      
    autoPlayLabel->setVertAlign( Control::ALIGN_CENTER );
    s_layerBox->setControl( 1, row, autoPlayLabel );


    // an opacity slider
    s_timeSlider = new HSliderControl( 0.0f, s_altitudes[0].layers.size()-1, 0.0f  );
    s_timeSlider->setWidth( 125 );
    s_timeSlider->setHeight( 12 );
    s_timeSlider->setVertAlign( Control::ALIGN_CENTER );
    s_timeSlider->addEventHandler( new TimeChangedHandler() );
    s_layerBox->setControl( 2, row, s_timeSlider );

    s_timeLabel = new LabelControl();
    autoPlayLabel->setVertAlign( Control::ALIGN_CENTER );
    s_layerBox->setControl( 3, row, s_timeLabel );
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

    std::string altitudesDir;
    arguments.read("--altitudes", altitudesDir);

    if (altitudesDir.empty())
    {
        OE_WARN << "Please provide an altitudes directory with --altitudes" << std::endl;
        return 1;
    }
    else
    {
        OE_NOTICE << "Read altitudes from " << altitudesDir << std::endl;
    }


    std::vector<std::string> directories;
    osgDB::DirectoryContents files = osgDB::getDirectoryContents(altitudesDir);
    for( osgDB::DirectoryContents::const_iterator f = files.begin(); f != files.end(); ++f )
    {
        if ( f->compare(".") == 0 || f->compare("..") == 0 )
            continue;

        std::string filepath = osgDB::concatPaths( altitudesDir, *f );
        directories.push_back( filepath );
    }

    unsigned int numLayers = 0;
    for (unsigned int i = 0; i < directories.size(); i++)
    {
        std::string filename = directories[i];
        if (osgDB::fileType(filename) == osgDB::DIRECTORY)
        {
            OE_NOTICE << "Loading altitude " << filename << std::endl;
            Altitude altitude;
            // Load up the altitudes
            altitude.load(filename);            
            if (s_altitudes.size() > 0 && numLayers != altitude.layers.size())
            {
                OE_NOTICE << "skipping altitude " << filename << " b/c it has " << altitude.layers.size() << " instead of " << numLayers << std::endl;
                continue;
            }
            altitude.addToMap( map );
            numLayers = altitude.layers.size();
            s_altitudes.push_back(altitude);
        }
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

    createControlPanel(&viewer);
    
    // Process cmdline args    
    MapNodeHelper helper;
    helper.configureView( &viewer );
    helper.parse(mapNode, arguments, &viewer, root, s_layerBox);        

    initGUI();

    while (!viewer.done())
    {
        // Animate the overlays
        if (viewer.getFrameStamp()->getFrameNumber() % 20 == 0)
        {
            // Increment the index if we're autoplaying
            if (s_autoPlay)
            {            
                s_index++;

                // Assume all the layers have the same size
                if (s_index >= s_altitudes[0].layers.size())
                {
                    s_index = 0;
                } 
                s_timeSlider->setValue(s_index, false);
            }

            std::stringstream buf;
            buf << "Time: " << s_index << std::endl;

            s_timeLabel->setText(buf.str());

            OE_NOTICE << "Showing layer " << s_index << " of altitude " << s_selectedAltitude << std::endl;

            // Only update the selected altitude
            for (unsigned int i = 0; i < s_altitudes.size(); i++)
            {
                if (i != s_selectedAltitude)
                {
                    s_altitudes[i].hideAll();
                }
                else
                {                    
                    s_altitudes[i].show(s_index);
                }
            } 
        }
        viewer.frame();
    }

    return 0;
}
