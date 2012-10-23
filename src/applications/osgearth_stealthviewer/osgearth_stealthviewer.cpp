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
#include <osgEarth/MapNode>
#include <osgEarth/Random>
#include <osgEarth/StringUtils>
#include <osgEarth/ImageUtils>
#include <osgEarth/GeoMath>
#include <osgEarth/Units>
#include <osgEarth/StringUtils>
#include <osgEarthUtil/ExampleResources>
#include <osgEarthUtil/EarthManipulator>
#include <osgEarthUtil/MGRSFormatter>
#include <osgEarthUtil/Controls>
#include <osgEarthUtil/AnnotationEvents>
#include <osgEarthAnnotation/TrackNode>
#include <osgEarthAnnotation/Decluttering>
#include <osgEarthAnnotation/AnnotationData>
#include <osgEarthSymbology/Color>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgGA/StateSetManipulator>

#include <OpenThreads/Thread>

#include "KDIS/Extras/DIS_Logger_Record.h"
#include "KDIS/Network/Connection.h" // A cross platform connection class.
#include "KDIS/Extras/PDU_Factory.h"
#include "KDIS/PDU/Entity_Info_Interaction/Entity_State_PDU.h"

using namespace KDIS;
using namespace DATA_TYPE;
using namespace PDU;
using namespace ENUMS;
using namespace UTILS;
using namespace NETWORK;

using namespace osgEarth;
using namespace osgEarth::Util;
using namespace osgEarth::Util::Controls;
using namespace osgEarth::Annotation;
using namespace osgEarth::Symbology;

#define LC "[osgearth_stealthviewer] "

/**
 * Demonstrates use of the TrackNode to display entity track symbols.
 */

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
DeclutteringOptions g_dcOptions;


/** Prints an error message */
int
usage( const std::string& message )
{
    OE_WARN << LC << message << std::endl;
    return -1;
}

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

class EntityRecord : public osg::Referenced
{
public:
    EntityRecord(TrackNode* trackNode, Entity_State_PDU* state):    
    _trackNode( trackNode ),
    _state( state )
    {
        _time = osg::Timer::instance()->time_s();
        _state->InitDeadReckoning();
        _state->ResetDeadReckoning();
    }

    ~EntityRecord()
    {
        if (_state) delete _state;
    }

    double getTime() const
    {
        return _time;
    }

    void updateSimulation( )
    {
        double t = osg::Timer::instance()->time_s();
        if (_state && _trackNode)
        {
            double delta = t - _time;
            if (delta < 0) OE_NOTICE << "Bad delta!" << std::endl;
            if (delta > 10)
            {
//                OE_NOTICE << "Haven't got a real update from " << _state->GetEntityIdentifier().GetEntityID() << " in " << delta << "s" << std::endl;
            }
            _state->ApplyDeadReckoning( delta );            
            WorldCoordinates location = _state->GetEntityLocation();
            double lat, lon, alt;
            KDIS::UTILS::GeocentricToGeodetic(location.GetX(), location.GetY(), location.GetZ(), lat, lon, alt, WGS_1984);        
            GeoPoint position(SpatialReference::create("wgs84"), lon, lat, alt, ALTMODE_ABSOLUTE );
            _trackNode->setPosition( position );        
        }
    }

    void setEntityState(Entity_State_PDU* state)
    {
        if (_state) delete _state;
        _state = state;

        //Reset the time
        _time = osg::Timer::instance()->time_s();

        if (_state && _trackNode.valid())
        {
            WorldCoordinates location = _state->GetEntityLocation();
            double lat, lon, alt;
            KDIS::UTILS::GeocentricToGeodetic(location.GetX(), location.GetY(), location.GetZ(), lat, lon, alt, WGS_1984);        
            GeoPoint position(SpatialReference::create("wgs84"), lon, lat, alt, ALTMODE_ABSOLUTE );
            _trackNode->setPosition( position );
        }
    }

private:
    double _time;
    osg::ref_ptr< TrackNode > _trackNode;
    Entity_State_PDU* _state;
};


class DISListener : public OpenThreads::Thread
{       
public:
    DISListener():
      _done(false)
      {
      }

      virtual ~DISListener()
      {
          cancel();
      }

      virtual void run()
      {                    
          // Note this address will probably be different for your network.
          //Connection conn( "224.0.0.1", 3000, true, false );
          Connection conn( "192.168.1.255", 3000);

          KOCTET cBuffer[MAX_PDU_SIZE]; // Somewhere to store the data we receive.

          PDU_Factory factory;
          //factory.AddFilter( new FactoryFilterExerciseID( 1 ) );


          while (!_done)
          {
              try
              {                  
                  KINT32 i32Recv = 0;

                  i32Recv = conn.Receive( cBuffer, MAX_PDU_SIZE );
                  if (i32Recv > 0)
                  {                      
                      KDataStream s( cBuffer, i32Recv );

                      //If the PDU didn't match our filters, then we 
                      auto_ptr< Header > pHeader = factory.Decode( s );
                      if (!pHeader.get())
                      {                          
                          continue;
                      }

                      //Take owner ship of this PDU                      
                      Entity_State_PDU* entityState = dynamic_cast<Entity_State_PDU*>(pHeader.get());
                      if (entityState)
                      {
                          //Take ownership of this pdu
                          pHeader.release();
                          onEntityStateChanged( entityState );
                      }                      
                  }                  
              }
              catch( std::exception & e )
              {
                  std::cout << e.what() << std::endl;
              } 
          }
      }

      virtual int
          cancel()
      {
          if ( isRunning() )
          {
              _done = true;  

              while( isRunning() )
              {        
                  OpenThreads::Thread::YieldCurrentThread();
              }
          }
          return 0;
      }
      
      virtual void onEntityStateChanged( Entity_State_PDU* entityState )
      {
          //OE_NOTICE << "Entity state changed " << id << ":   " << lat << ", " << lon << ", " << alt << " marker=" << marker << " force=" << forceId << std::endl;
      }

private:

    volatile bool _done;
};

typedef std::map< int, osg::ref_ptr< TrackNode > > TrackNodeMap;

typedef std::map< int, osg::ref_ptr< EntityRecord >> EntityRecords;

class IconFactory
{
public:
    IconFactory()
    {        
        osg::ref_ptr< osg::Image > img = osgDB::readImageFile( "../data/airtrack_friendly.png" );
        ImageUtils::resizeImage( img.get(), ICON_SIZE, ICON_SIZE, _friendly );

        img = osgDB::readImageFile( "../data/airtrack_opposing.png" );        
        ImageUtils::resizeImage( img.get(), ICON_SIZE, ICON_SIZE, _opposing );

        img  = osgDB::readImageFile( "../data/airtrack_neutral.png" );        
        ImageUtils::resizeImage( img.get(), ICON_SIZE, ICON_SIZE, _neutral );
        
        img  = osgDB::readImageFile( "../data/airtrack_unknown.png" );        
        ImageUtils::resizeImage( img.get(), ICON_SIZE, ICON_SIZE, _unknown );
    }

    osg::Image* getIcon(ForceID forceId )
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

    osg::ref_ptr< osg::Image > _friendly;
    osg::ref_ptr< osg::Image > _opposing;
    osg::ref_ptr< osg::Image > _neutral;
    osg::ref_ptr< osg::Image > _unknown;
};

IconFactory s_iconFactory;



static int numMessages = 0;


class Simulation : public DISListener, public osg::Referenced
{
public:
    Simulation(MapNode* mapNode, osg::Group* entityGroup):
      _mapNode( mapNode ),
      _entityGroup( entityGroup )
    {        
    }

    virtual ~Simulation()
    {
    }

    /*virtual void onEntityStateChanged(int id,
                                      double lat, double lon, double alt,
                                      const std::string& marker, ForceID forceId)
                                      */
    virtual void onEntityStateChanged( Entity_State_PDU* entityState )
    {
        OpenThreads::ScopedLock< OpenThreads::Mutex > lk( _mutex );
        numMessages++;
        double t = osg::Timer::instance()->time_s();
        //Initialize the dead reckoning on the entity
        entityState->InitDeadReckoning();        
        OE_NOTICE << "Messages=" << numMessages << " Rate=" << (double)numMessages / t << " msg/s" << std::endl;

        int id = entityState->GetEntityIdentifier().GetEntityID();
        ForceID forceId = entityState->GetForceID();


        //Compute the location
        WorldCoordinates location = entityState->GetEntityLocation();
        double lat, lon, alt;
        KDIS::UTILS::GeocentricToGeodetic(location.GetX(), location.GetY(), location.GetZ(), lat, lon, alt, WGS_1984);        
        GeoPoint position(SpatialReference::create("wgs84"), lon, lat, alt, ALTMODE_ABSOLUTE );

        //Try to get the existing record
        EntityRecords::iterator itr = _entities.find( id );
        EntityRecord* record = 0;
        if (itr == _entities.end())
        {
            //Create a new entity record

            OE_NOTICE << "Adding new entity " << id << std::endl;
            // build a track field schema.
            TrackNodeFieldSchema schema;
            createFieldSchema( schema );

            osg::Image* image = s_iconFactory.getIcon( forceId );            

            TrackNode* trackNode = new TrackNode(_mapNode.get(), position, image, schema);
            trackNode->setFieldValue(FIELD_NAME, entityState->GetEntityMarking().GetEntityMarkingString());
            record = new EntityRecord( trackNode, entityState );
            _entities[id] = record;
            _entityGroup->addChild( trackNode );
        }
        else
        {   
            record = itr->second.get();
            record->setEntityState( entityState );
        }     
    }

    void updateSim()
    {                
        OpenThreads::ScopedLock< OpenThreads::Mutex > lk( _mutex );
        for (EntityRecords::iterator itr = _entities.begin(); itr != _entities.end(); itr++)
        {
            itr->second->updateSimulation();
        }
    }
    
    EntityRecords _entities;

    osg::ref_ptr< osg::Group > _entityGroup;
    osg::ref_ptr< MapNode > _mapNode;    
    OpenThreads::Mutex _mutex;
};

/** creates some UI controls for adjusting the decluttering parameters. */
void
createControls( osgViewer::View* view )
{
    ControlCanvas* canvas = ControlCanvas::get(view, true);
    
    // title bar
    VBox* vbox = canvas->addControl(new VBox(Control::ALIGN_NONE, Control::ALIGN_BOTTOM, 2, 1 ));
    vbox->setBackColor( Color(Color::Black, 0.5) );
    vbox->addControl( new LabelControl("osgEarth StealthViewer", Color::Yellow) );
    
    // checkbox that toggles decluttering of tracks
    struct ToggleDecluttering : public ControlEventHandler {
        void onValueChanged( Control* c, bool on ) {
            Decluttering::setEnabled( g_declutterStateSet, on );
        }
    };
    HBox* dcToggle = vbox->addControl( new HBox() );
    dcToggle->addControl( new CheckBoxControl(true, new ToggleDecluttering()) );
    dcToggle->addControl( new LabelControl("Declutter") );

    // checkbox that toggles the coordinate display
    struct ToggleCoords : public ControlEventHandler {
        void onValueChanged( Control* c, bool on ) {
            g_showCoords = on;
        }
    };
    HBox* coordsToggle = vbox->addControl( new HBox() );
    coordsToggle->addControl( new CheckBoxControl(true, new ToggleCoords()) );
    coordsToggle->addControl( new LabelControl("Show locations") );

    // grid for the slider controls so they look nice
    Grid* grid = vbox->addControl( new Grid() );
    grid->setHorizFill( true );
    grid->setChildHorizAlign( Control::ALIGN_LEFT );
    grid->setChildSpacing( 6 );

    unsigned r=0;

    // event handler for changing decluttering options
    struct ChangeFloatOption : public ControlEventHandler {
        optional<float>& _param;
        LabelControl* _label;
        ChangeFloatOption( optional<float>& param, LabelControl* label ) : _param(param), _label(label) { }
        void onValueChanged( Control* c, float value ) {
            _param = value;
            _label->setText( Stringify() << std::fixed << std::setprecision(1) << value );
            Decluttering::setOptions( g_dcOptions );
        }
    };    

    grid->setControl( 0, ++r, new LabelControl("Min scale:") );
    LabelControl* minAnimationScaleLabel = grid->setControl( 2, r, new LabelControl(Stringify() << std::fixed << std::setprecision(1) << *g_dcOptions.minAnimationScale()) );
    grid->setControl( 1, r, new HSliderControl( 
        0.0, 1.0, *g_dcOptions.minAnimationScale(), new ChangeFloatOption(g_dcOptions.minAnimationScale(), minAnimationScaleLabel) ) );

    grid->setControl( 0, ++r, new LabelControl("Min alpha:") );
    LabelControl* alphaLabel = grid->setControl( 2, r, new LabelControl(Stringify() << std::fixed << std::setprecision(1) << *g_dcOptions.minAnimationAlpha()) );
    grid->setControl( 1, r, new HSliderControl( 
        0.0, 1.0, *g_dcOptions.minAnimationAlpha(), new ChangeFloatOption(g_dcOptions.minAnimationAlpha(), alphaLabel) ) );

    grid->setControl( 0, ++r, new LabelControl("Activate time (s):") );
    LabelControl* actLabel = grid->setControl( 2, r, new LabelControl(Stringify() << std::fixed << std::setprecision(1) << *g_dcOptions.inAnimationTime()) );
    grid->setControl( 1, r, new HSliderControl( 
        0.0, 2.0, *g_dcOptions.inAnimationTime(), new ChangeFloatOption(g_dcOptions.inAnimationTime(), actLabel) ) );

    grid->setControl( 0, ++r, new LabelControl("Deactivate time (s):") );
    LabelControl* deactLabel = grid->setControl( 2, r, new LabelControl(Stringify() << std::fixed << std::setprecision(1) << *g_dcOptions.outAnimationTime()) );
    grid->setControl( 1, r, new HSliderControl( 
        0.0, 2.0, *g_dcOptions.outAnimationTime(), new ChangeFloatOption(g_dcOptions.outAnimationTime(), deactLabel) ) );
}


/**
 * Main application.
 */
int
main(int argc, char** argv)
{    
    osg::ArgumentParser arguments(&argc,argv);

    // initialize a viewer.
    osgViewer::Viewer viewer( arguments );
    viewer.setCameraManipulator( new EarthManipulator );

    // load a map from an earth file.
    osg::Node* earth = MapNodeHelper().load(arguments, &viewer);
    MapNode* mapNode = MapNode::findMapNode(earth);
    if ( !mapNode )
        return usage("Missing required .earth file" );

    osg::Group* root = new osg::Group();
    root->addChild( earth );
    viewer.setSceneData( root );

    // build a track field schema.    
    TrackNodeFieldSchema schema;
    createFieldSchema( schema );
    
    osg::Group* tracks = new osg::Group();
    root->addChild( tracks );

    osg::ref_ptr< Simulation > simulation = new Simulation(mapNode, tracks );
    simulation->start();    


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
    viewer.setRunFrameScheme( viewer.CONTINUOUS );

    // configure a UI for controlling the demo
    createControls( &viewer );

    while (!viewer.done())
    {
        simulation->updateSim();
        viewer.frame();
    }

    //viewer.run();
}
