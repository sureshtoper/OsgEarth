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
#include <osgEarthUtil/MGRSFormatter>
#include <osgEarthUtil/Controls>
#include <osgEarthUtil/AnnotationEvents>
#include <osgEarthUtil/HTM>
#include <osgEarthAnnotation/TrackNode>
#include <osgEarthAnnotation/AnnotationData>
#include <osgEarth/Random>
#include <osgEarth/StringUtils>
#include <osgEarth/ImageUtils>
#include <osgEarth/GeoMath>
#include <osgEarth/Units>
#include <osgEarth/StringUtils>
#include <osgEarth/Decluttering>
#include <osgEarthSymbology/Color>

using namespace osgEarth;
using namespace osgEarth::Drivers;
using namespace osgEarth::Util;
using namespace osgEarth::Util::Controls;
using namespace osgEarth::Annotation;
using namespace osgEarth::Symbology;


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

// field names for the track labels
#define FIELD_NAME     "name"
#define FIELD_POSITION "position"
#define FIELD_NUMBER   "number"

// icon to use, and size in pixels
#define ICON_URL       "../data/m2525_air.png"
#define ICON_SIZE      40

// format coordinates as MGRS
static MGRSFormatter s_format(MGRSFormatter::PRECISION_10000M);

// globals for this demo
osg::StateSet*      g_declutterStateSet = 0L;
bool                g_showCoords        = true;
optional<float>     g_duration          = 60.0;
unsigned            g_numTracks         = 500;
DeclutteringOptions g_dcOptions;



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
            s_index = 0;
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

    unsigned int maxLayers = 0;

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

        if (alt.layers.size() > maxLayers) maxLayers = alt.layers.size();
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
    s_timeSlider = new HSliderControl( 0.0f, maxLayers-1, 0.0f  );    
    s_timeSlider->setWidth( 125 );
    s_timeSlider->setHeight( 12 );
    s_timeSlider->setVertAlign( Control::ALIGN_CENTER );
    s_timeSlider->addEventHandler( new TimeChangedHandler() );
    s_layerBox->setControl( 2, row, s_timeSlider );

    s_timeLabel = new LabelControl();
    autoPlayLabel->setVertAlign( Control::ALIGN_CENTER );
    s_layerBox->setControl( 3, row, s_timeLabel );
}


/** A little track simulator that goes a simple great circle interpolation */
struct TrackSim : public osg::Referenced
{
    TrackNode* _track;
    Angular _startLat, _startLon, _endLat, _endLon;

    void update( double t )
    {
        osg::Vec3d pos;
        GeoMath::interpolate(
            _startLat.as(Units::RADIANS), _startLon.as(Units::RADIANS),
            _endLat.as(Units::RADIANS), _endLon.as(Units::RADIANS),
            t,
            pos.y(), pos.x() );

        GeoPoint geo(
            _track->getMapNode()->getMapSRS(),
            osg::RadiansToDegrees(pos.x()),
            osg::RadiansToDegrees(pos.y()),
            10000.0,
            ALTMODE_ABSOLUTE);

        // update the position label.
        _track->setPosition(geo);

        if ( g_showCoords )
        {
            _track->setFieldValue( FIELD_POSITION, s_format(geo) );
        }
        else
            _track->setFieldValue( FIELD_POSITION, "" );
    }
};
typedef std::list< osg::ref_ptr<TrackSim> > TrackSims;


/** Update operation that runs the simulators. */
struct TrackSimUpdate : public osg::Operation
{
    TrackSimUpdate(TrackSims& sims) : osg::Operation( "tasksim", true ), _sims(sims) { }

    void operator()( osg::Object* obj ) {
        osg::View* view = dynamic_cast<osg::View*>(obj);
        double t = fmod(view->getFrameStamp()->getSimulationTime(), (double)g_duration.get()) / (double)g_duration.get();
        for( TrackSims::iterator i = _sims.begin(); i != _sims.end(); ++i )
            i->get()->update( t );
    }

    TrackSims& _sims;
};


/**
 * Creates a field schema that we'll later use as a labeling template for
 * TrackNode instances.
 */
void
createFieldSchema( TrackNodeFieldSchema& schema )
{
    // draw the track name above the icon:
    TextSymbol* nameSymbol = new TextSymbol();
    nameSymbol->pixelOffset()->set( 0, 2+ICON_SIZE/2 );
    nameSymbol->alignment() = TextSymbol::ALIGN_CENTER_BOTTOM;
    nameSymbol->halo()->color() = Color::Black;
    nameSymbol->size() = nameSymbol->size().value() + 2.0f;
    schema[FIELD_NAME] = TrackNodeField(nameSymbol, false); // false => static label (won't change after set)

    // draw the track coordinates below the icon:
    TextSymbol* posSymbol = new TextSymbol();
    posSymbol->pixelOffset()->set( 0, -2-ICON_SIZE/2 );
    posSymbol->alignment() = TextSymbol::ALIGN_CENTER_TOP;
    posSymbol->fill()->color() = Color::Yellow;
    posSymbol->size() = posSymbol->size().value() - 2.0f;
    schema[FIELD_POSITION] = TrackNodeField(posSymbol, true); // true => may change at runtime

    // draw some other field to the left:
    TextSymbol* numberSymbol = new TextSymbol();
    numberSymbol->pixelOffset()->set( -2-ICON_SIZE/2, 0 );
    numberSymbol->alignment() = TextSymbol::ALIGN_RIGHT_CENTER;
    schema[FIELD_NUMBER] = TrackNodeField(numberSymbol, false);
}


/** Builds a bunch of tracks. */
void
createTrackNodes( MapNode* mapNode, osg::Group* parent, const TrackNodeFieldSchema& schema, TrackSims& sims )
{
    // load an icon to use:
    osg::ref_ptr<osg::Image> srcImage = osgDB::readImageFile( ICON_URL );
    osg::ref_ptr<osg::Image> image;
    ImageUtils::resizeImage( srcImage.get(), ICON_SIZE, ICON_SIZE, image );

    // make some tracks, choosing a random simulation for each.
    Random prng;
    const SpatialReference* geoSRS = mapNode->getMapSRS()->getGeographicSRS();

    for( unsigned i=0; i<g_numTracks; ++i )
    {
        double lon0 = -180.0 + prng.next() * 360.0;
        double lat0 = -80.0 + prng.next() * 160.0;

        GeoPoint pos(geoSRS, lon0, lat0);

        TrackNode* track = new TrackNode(mapNode, pos, image, schema);

        track->setFieldValue( FIELD_NAME,     Stringify() << "Track:" << i );
        track->setFieldValue( FIELD_POSITION, Stringify() << s_format(pos) );
        track->setFieldValue( FIELD_NUMBER,   Stringify() << (1 + prng.next(9)) );

        // add a priority
        AnnotationData* data = new AnnotationData();
        data->setPriority( float(i) );
        track->setAnnotationData( data );

        parent->addChild( track );

        // add a simulator for this guy
        double lon1 = -180.0 + prng.next() * 360.0;
        double lat1 = -80.0 + prng.next() * 160.0;
        TrackSim* sim = new TrackSim();
        sim->_track = track;        
        sim->_startLat = lat0; sim->_startLon = lon0;
        sim->_endLat = lat1; sim->_endLon = lon1;
        sims.push_back( sim );
    }
}


int  
usage( const std::string& msg )
{
    OE_NOTICE << msg << std::endl;
    OE_NOTICE << "USAGE: osgearth_weather" << std::endl;
    OE_NOTICE << "   --altitudes <directory> Path to directory of exported altitudes full of grib files" << std::endl;                    
    return -1;
}




int
main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc,argv);

	// initialize a viewer:
    osgViewer::Viewer viewer(arguments);
    EarthManipulator* manip = new EarthManipulator();
    viewer.setCameraManipulator( manip );

	createControlPanel(&viewer);

	osg::Node* node = MapNodeHelper().load(arguments, &viewer, s_layerBox);

	MapNode* mapNode = MapNode::findMapNode(node);

	// make the map scene graph:
    osg::Group* root = new osg::Group();
    viewer.setSceneData( root );   
	root->addChild( node );

    std::string altitudesDir;
    arguments.read("--altitudes", altitudesDir);

    if (altitudesDir.empty())
    {
        return usage("Please provide an altitudes directory with --altitudes");        
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
            if (altitude.layers.size() > 0)
            {
                altitude.addToMap( mapNode->getMap() );
                numLayers = altitude.layers.size();
                s_altitudes.push_back(altitude);
            }
        }
    }

    if (s_altitudes.size() == 0)
    {
        return usage("Failed to load any altitudes");
    }

    initGUI();


	//Initialize the tracks
	// build a track field schema.
    TrackNodeFieldSchema schema;
    createFieldSchema( schema );

    // create some track nodes.
    TrackSims trackSims;
    osg::Group* tracks = new osg::Group();
    //HTMGroup* tracks = new HTMGroup();
    createTrackNodes( mapNode, tracks, schema, trackSims );
    root->addChild( tracks );

    // Set up the automatic decluttering. setEnabled() activates decluttering for
    // all drawables under that state set. We are also activating priority-based
    // sorting, which looks at the AnnotationData::priority field for each drawable.
    // (By default, objects are sorted by disatnce-to-camera.) Finally, we customize 
    // a couple of the decluttering options to get the animation effects we want.
    g_declutterStateSet = tracks->getOrCreateStateSet();
    Decluttering::setEnabled( g_declutterStateSet, true );
    g_dcOptions = Decluttering::getOptions();
    g_dcOptions.inAnimationTime()  = 1.0f;
    g_dcOptions.outAnimationTime() = 1.0f;
    g_dcOptions.sortByPriority()   = true;
    Decluttering::setOptions( g_dcOptions );

    // attach the simulator to the viewer.
    viewer.addUpdateOperation( new TrackSimUpdate(trackSims) );

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
                if (s_index >= s_altitudes[s_selectedAltitude].layers.size())
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
                    s_altitudes[i].show(osg::minimum(s_index, s_altitudes[i].layers.size()-1));
                }
            } 
        }
        viewer.frame();
    }

    return 0;
}
