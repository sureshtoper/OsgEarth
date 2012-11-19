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

#include <osgEarthSim/Simulation>
#include <osgEarthSim/IconFactory>
#include <osgEarthSim/KDISLiveEntityProvider>
#include <osgEarthSim/KDISLogEntityProvider>
#include <osgEarthSim/WebLVCEntityProvider>

#include <OpenThreads/Thread>

#include <KDIS/PDU/Entity_Info_Interaction/Entity_State_PDU.h>

using namespace KDIS;
using namespace DATA_TYPE;
using namespace PDU;
using namespace ENUMS;
using namespace UTILS;
using namespace NETWORK;

using namespace osgEarth;
using namespace osgEarth::Sim;
using namespace osgEarth::Util;
using namespace osgEarth::Util::Controls;
using namespace osgEarth::Annotation;
using namespace osgEarth::Symbology;

#define LC "[osgearth_stealthviewer] "

/**
 * Demonstrates use of the TrackNode to display entity track symbols.
 */

// globals for this demo
osg::StateSet*      g_declutterStateSet = 0L;
DeclutteringOptions g_dcOptions;


/** Prints an error message */
int
usage( const std::string& message )
{
    OE_WARN << LC << message << std::endl;
    return -1;
}


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

    std::string ip = "";
    int port = -1;

    std::string log;


    double entityTimeout = -1.0;
    bool loop = false;

    arguments.read( "--ip", ip);
    arguments.read( "--port", port );
    arguments.read( "--log", log );
    arguments.read( "--timeout", entityTimeout );
    arguments.read( "--log" , log );
    while (arguments.read( "--loop")) loop = true;    


    bool weblvc = false;
    while (arguments.read( "--weblvc")) weblvc = true;    

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
    
    osg::Group* tracks = new osg::Group();
    root->addChild( tracks );

    osg::ref_ptr< EntityProvider > entityProvider;    
    if (weblvc)
    {        
        if (ip.empty())
        {
            ip = "pubdemo.mak.com";
        }

        if (port < 0)
        {
            port = 80;
        }

        OE_NOTICE << "Connecting to WebLVC server at " << ip << ":" << port << std::endl;

        entityProvider = new WebLVCEntityProvider(ip, port, "/ws");
    }
    else if (!log.empty())
    {        

        //Connect to a log provider
        OE_NOTICE << "Opening log " << log << std::endl;
        entityProvider = new KDISLogEntityProvider( log );
        static_cast<KDISLogEntityProvider*>(entityProvider.get())->setLoop( loop );
    }
    else
    {
        //Connect to a local simulation
        if (ip.empty())
        {
            ip = "192.168.1.255";
        }

        if (port < 0)
        {
            port = 3000;
        }

        OE_NOTICE << "Connecting to live simulation on " << ip << ":" << port << std::endl;
        //Try to connect to a live sim
        entityProvider = new KDISLiveEntityProvider( ip, port );        
    }                

    //Connect a simulation to the entity provider
    osg::ref_ptr< Simulation > simulation = new Simulation(mapNode, tracks,  entityProvider );    
    simulation->setEntityTimeout( entityTimeout );
    //Start the entity provider
    entityProvider->start();


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
    
}
