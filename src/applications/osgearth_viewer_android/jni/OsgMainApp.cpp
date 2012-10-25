#include "OsgMainApp.hpp"

#include <osgDB/FileUtils>
#include <osgEarth/Capabilities>
#include <osgEarthUtil/AutoClipPlaneHandler>
#include <osgEarthUtil/ObjectLocator>
#include <osgEarthDrivers/tms/TMSOptions>

#include <osgDB/WriteFile>

#include "GLES2ShaderGenVisitor.h"

using namespace osgEarth;
using namespace osgEarth::Drivers;
using namespace osgEarth::Util;

class ClampObjectLocatorCallback : public osgEarth::TerrainCallback
{
public:
    ClampObjectLocatorCallback(ObjectLocatorNode* locator):
    _locator(locator),
    _maxLevel(-1),
    _minLevel(0)
    {
    }
    
    virtual void onTileAdded(const osgEarth::TileKey& tileKey, osg::Node* terrain, TerrainCallbackContext&)
    {
        if ((int)tileKey.getLevelOfDetail() > _minLevel && _maxLevel < (int)tileKey.getLevelOfDetail())
        {
            osg::Vec3d position = _locator->getLocator()->getPosition();
            
            if (tileKey.getExtent().contains(position.x(), position.y()))
            {
                //Compute our location in geocentric
                const osg::EllipsoidModel* ellipsoid = tileKey.getProfile()->getSRS()->getEllipsoid();
                double x, y, z;
                ellipsoid->convertLatLongHeightToXYZ(
                                                     osg::DegreesToRadians(position.y()), osg::DegreesToRadians(position.x()), 0,
                                                     x, y, z);
                //Compute the up vector
                osg::Vec3d up = ellipsoid->computeLocalUpVector(x, y, z );
                up.normalize();
                osg::Vec3d world(x, y, z);
                
                double segOffset = 50000;
                
                osg::Vec3d start = world + (up * segOffset);
                osg::Vec3d end = world - (up * segOffset);
                
                osgUtil::LineSegmentIntersector* i = new osgUtil::LineSegmentIntersector( start, end );
                
                osgUtil::IntersectionVisitor iv;
                iv.setIntersector( i );
                terrain->accept( iv );
                
                osgUtil::LineSegmentIntersector::Intersections& results = i->getIntersections();
                if ( !results.empty() )
                {
                    const osgUtil::LineSegmentIntersector::Intersection& result = *results.begin();
                    osg::Vec3d hit = result.getWorldIntersectPoint();
                    double lat, lon, height;
                    ellipsoid->convertXYZToLatLongHeight(hit.x(), hit.y(), hit.z(),
                                                         lat, lon, height);
                    position.z() = height;
                    //OE_NOTICE << "Got hit, setting new height to " << height << std::endl;
                    _maxLevel = tileKey.getLevelOfDetail();
                    _locator->getLocator()->setPosition( position );
                }
            }
        }
        
    }
    
    osg::ref_ptr< ObjectLocatorNode > _locator;
    int _maxLevel;
    int _minLevel;
};


OsgMainApp::OsgMainApp(){

    _initialized = false;

}
OsgMainApp::~OsgMainApp(){

}


//Initialization function
void OsgMainApp::initOsgWindow(int x,int y,int width,int height){

    __android_log_write(ANDROID_LOG_ERROR, "OSGANDROID",
            "Initializing geometry");

    //Pending
    _notifyHandler = new OsgAndroidNotifyHandler();
    _notifyHandler->setTag("Osg Viewer");
    osg::setNotifyHandler(_notifyHandler);
    osgEarth::setNotifyHandler(_notifyHandler);
    
    osg::setNotifyLevel(osg::FATAL);
    osgEarth::setNotifyLevel(osg::INFO);

    osg::notify(osg::ALWAYS)<<"Testing"<<std::endl;

    ::setenv("OSGEARTH_HTTP_DEBUG", "1", 1);
    ::setenv("OSGEARTH_DUMP_SHADERS", "1", 1);
    
    _viewer = new osgViewer::Viewer();
    _viewer->setUpViewerAsEmbeddedInWindow(x, y, width, height);
    _viewer->getCamera()->setViewport(new osg::Viewport(0, 0, width, height));
    _viewer->getCamera()->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    _viewer->getCamera()->setClearColor(osg::Vec4(1.0f,0.0f,0.0f,0.0f));
    //_viewer->getCamera()->setClearStencil(0);
    _viewer->getCamera()->setProjectionMatrixAsPerspective(45.0f,(float)width/height,
                                                           0.1f, 10000.0f);
    // configure the near/far so we don't clip things that are up close
    _viewer->getCamera()->setNearFarRatio(0.00002);
    _viewer->setThreadingModel(osgViewer::ViewerBase::SingleThreaded);


    // install our default manipulator (do this before calling load)
    _viewer->setCameraManipulator( new osgEarth::Util::EarthMultiTouchManipulator() );
    
    osg::Light* light = new osg::Light( 0 );
    light->setPosition( osg::Vec4(0, -1, 0, 0 ) );
    light->setAmbient( osg::Vec4(0.4f, 0.4f, 0.4f ,1.0) );
    light->setDiffuse( osg::Vec4(1,1,1,1) );
    light->setSpecular( osg::Vec4(0,0,0,1) );
    
    osg::Material* material = new osg::Material();
    material->setAmbient(osg::Material::FRONT, osg::Vec4(0.4,0.4,0.4,1.0));
    material->setDiffuse(osg::Material::FRONT, osg::Vec4(0.9,0.9,0.9,1.0));
    material->setSpecular(osg::Material::FRONT, osg::Vec4(0.4,0.4,0.4,1.0));
    
    
    osg::Node* node = osgDB::readNodeFile("/storage/sdcard0/Download/tests/gdal_tiff.earth");//readymap.earth");
    if ( !node )
    {
        OSG_ALWAYS << "Unable to load an earth file from the command line." << std::endl;
        return;
    }
    
    osg::ref_ptr<osgEarth::Util::MapNode> mapNode = osgEarth::Util::MapNode::findMapNode(node);
    if ( !mapNode.valid() )
    {
        OSG_ALWAYS << "Loaded scene graph does not contain a MapNode - aborting" << std::endl;
        return;
    }
    
    // warn about not having an earth manip
    osgEarth::Util::EarthManipulator* manip = dynamic_cast<osgEarth::Util::EarthManipulator*>(_viewer->getCameraManipulator());
    if ( manip == 0L )
    {
        OSG_ALWAYS << "Helper used before installing an EarthManipulator" << std::endl;
    }
    
    // a root node to hold everything:
    osg::Group* root = new osg::Group();
    root->addChild( mapNode.get() );
    //root->getOrCreateStateSet()->setAttribute(light);
    
    //have to add these
    root->getOrCreateStateSet()->setAttribute(material);
    //root->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE);
    
    /*double hours = 12.0f;
    float ambientBrightness = 0.4f;
    osgEarth::Util::SkyNode* sky = new osgEarth::Util::SkyNode( mapNode->getMap() );
    sky->setAmbientBrightness( ambientBrightness );
    sky->setDateTime( 1984, 11, 8, hours );
    sky->attach( _viewer, 0 );
    root->addChild( sky );*/
    
    
    
    //add model
    unsigned int numObjects = 2;
     osg::Group* treeGroup = new osg::Group();
     root->addChild(treeGroup);
     osg::Node* tree = osgDB::readNodeFile("/storage/sdcard0/Download/data/boxman.osg");
     osg::MatrixTransform* mt = new osg::MatrixTransform();
     mt->setMatrix(osg::Matrixd::scale(100000,100000,100000));
     mt->addChild( tree );
     //Create bound around mt rainer
     double centerLat =  46.840866;
     double centerLon = -121.769846;
     double gheight = 0.2;
     double gwidth = 0.2;
     double minLat = centerLat - (height/2.0);
     double minLon = centerLon - (width/2.0);
     
     OE_NOTICE << "Placing " << numObjects << " trees" << std::endl;
     
     for (unsigned int i = 0; i < numObjects; i++)
     {
     osgEarth::Util::ObjectLocatorNode* locator = new osgEarth::Util::ObjectLocatorNode( mapNode->getMap() );
     double lat = minLat + gheight * (rand() * 1.0)/(RAND_MAX-1);
     double lon = minLon + gwidth * (rand() * 1.0)/(RAND_MAX-1);
     //OE_NOTICE << "Placing tree at " << lat << ", " << lon << std::endl;
     locator->getLocator()->setPosition(osg::Vec3d(lon,  lat, 0 ) );
     locator->addChild( mt );
     treeGroup->addChild( locator );
     mapNode->getTerrain()->addTerrainCallback( new ClampObjectLocatorCallback(locator) );
     }
    //attach a UpdateLightingUniformsHelper to the model
    UpdateLightingUniformsHelper* updateLightInfo = new UpdateLightingUniformsHelper();
    treeGroup->setCullCallback(updateLightInfo);
    
    osgUtil::GLES2ShaderGenVisitor shaderGen;
    treeGroup->accept(shaderGen);
    root->accept(shaderGen);
    
    
    //for some reason we have to do this as global stateset doesn't
    //appear to be in the statesetstack
    root->getOrCreateStateSet()->setAttribute(_viewer->getLight());
    
    _viewer->setSceneData( root );
    
    // create the map.
    Map* map = new Map();
    
    // add a TMS imager layer:
    TMSOptions imagery;
    imagery.url() = "http://readymaps.org/readymap/tiles/1.0.0/7/";
    map->addImageLayer( new ImageLayer("Imagery", imagery) );
    /*
    // add a TMS elevation layer:
    TMSOptions elevation;
    elevation.url() = "http://readymap.org/readymap/tiles/1.0.0/9/";
    map->addElevationLayer( new ElevationLayer("Elevation", elevation) );
    
    */
    
    // make the map scene graph:
    MapNode* mapn = new MapNode( map );
    _viewer->setSceneData( mapn );

    osgDB::writeNodeFile(*mapn, "/storage/sdcard0/Download/mapnode.osgt");
    
    if(Registry::capabilities().supportsGLSL()){
        OSG_ALWAYS << "GLSL is Supported" << std::endl;
    }else{
        OSG_ALWAYS << "GLSL is NOT Supported" << std::endl;
    }
    
    _viewer->realize();

    _initialized = true;

}
//Draw
void OsgMainApp::draw(){

    _viewer->frame();
}
//Events
void OsgMainApp::mouseButtonPressEvent(float x,float y,int button){
    _viewer->getEventQueue()->mouseButtonPress(x, y, button);
}
void OsgMainApp::mouseButtonReleaseEvent(float x,float y,int button){
    _viewer->getEventQueue()->mouseButtonRelease(x, y, button);
}
void OsgMainApp::mouseMoveEvent(float x,float y){
    _viewer->getEventQueue()->mouseMotion(x, y);
}
void OsgMainApp::keyboardDown(int key){
    _viewer->getEventQueue()->keyPress(key);
}
void OsgMainApp::keyboardUp(int key){
    _viewer->getEventQueue()->keyRelease(key);
}
