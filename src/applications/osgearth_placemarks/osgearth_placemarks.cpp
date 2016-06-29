/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2015 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include <osgViewer/Viewer>
#include <osgEarth/MapNode>
#include <osgEarth/NodeUtils>
#include <osgEarthUtil/ExampleResources>

#include <osgEarthUtil/EarthManipulator>

#include <osgEarthAnnotation/PlaceNode>
#include <osgEarthSymbology/BBoxSymbol>

using namespace osgEarth;
using namespace osgEarth::Annotation;
using namespace osgEarth::Util;

//------------------------------------------------------------------

int
usage( char** argv )
{
    OE_WARN << "Usage: " << argv[0] << " <earthfile>" << std::endl;
    return -1;
}


/**
 * A predicate functor that takes a PlaceNode and returns true or false.
 */
class PlaceNodeFunctor
{
public:
    virtual bool operator()(PlaceNode& placeNode) = 0;
};

/**
 * A simple PlaceNodeFunctor that tries to search for a string within the text of the PlaceNode.
 */
class NameFunctor : public PlaceNodeFunctor
{
public:
    NameFunctor(const std::string& match):
      _match(match)
    {
    }

    virtual bool operator()(PlaceNode& placeNode)
    {
        std::string text = toLower(placeNode.getText());
        return text.find( toLower(_match) ) != std::string::npos;
    }

    std::string _match;
};


/**
 * The PlaceNodeManager keeps track of all of the PlaceNodes that are loaded by feature layers and applies styling based on the PlaceNodeFunctor.
 */
class PlaceNodeManager : public osgEarth::NodeOperation
{
public:

    PlaceNodeManager():
      _functor(0)
    {
    }

    /**
     * Called as new nodes are added, potentially in a background pager thread.
     */
    virtual void operator()( osg::Node* node )
    {
        OpenThreads::ScopedLock< OpenThreads::Mutex > lk(_mutex);

        // Get rid of any PlaceNodes that have expired.
        expireNodes();

        // Find all the placenodes in the incoming node.
        FindNodesVisitor< PlaceNode > visitor;
        node->accept( visitor );     

        for (std::vector< PlaceNode* >::iterator itr = visitor._results.begin(); itr != visitor._results.end(); ++itr)
        {
            // Make the placenode dynamic so we can change it safely.
            (*itr)->setDynamic(true);
            // Apply the functor.
            applyFunctor(*itr);

            // Keep track of the placenode.
            _placeNodes.push_back( *itr );
        }
    }

    /**
     * Get rid of any nodes that have been deleted.
     */
    void expireNodes()
    {     
        for (std::vector< osg::observer_ptr< PlaceNode> >::iterator itr = _placeNodes.begin(); itr != _placeNodes.end();)
        {
            osg::ref_ptr< PlaceNode > place;
            if ( itr->lock(place) )
            {
                ++itr;
            }
            else
            {                
                itr = _placeNodes.erase(itr);
            }
        }
    }

    /**
     * Set the PlaceNodeFunctor.
     */
    void setFunctor(PlaceNodeFunctor* functor)
    {        
        _functor = functor;
        applyFunctor();
    }

    /**
     * Applies the functor to all of the tracked PlaceNodes.
     */
    void applyFunctor()
    {        
        OpenThreads::ScopedLock< OpenThreads::Mutex > lk(_mutex);
        expireNodes();
        for (std::vector< osg::observer_ptr< PlaceNode> >::iterator itr = _placeNodes.begin(); itr != _placeNodes.end(); ++itr)
        {
            osg::ref_ptr< PlaceNode > place;
            if ( itr->lock(place) )
            {
                applyFunctor(place.get());
            }
        }
    }

    /*
     * Apply the functor to a single placenode.
     */
    void applyFunctor(PlaceNode* placeNode)
    {
        if (!_functor) return;

        Style style = placeNode->getStyle();
        BBoxSymbol* bbox = style.getOrCreateSymbol<BBoxSymbol>();

        // If the placenode passes the functor, color it Cyan with a thick outline.
        if ((*_functor)(*placeNode))
        {            
            bbox->border()->color() = Color::Cyan;
            bbox->border()->width() = 2.0;
        }
        else
        {
            // Otherwise color it white with a thin outline.
            bbox->border()->color() = Color::White;
            bbox->border()->width() = 1.0;
        }
        // Update the style.
        placeNode->setStyle(style);
    }


    PlaceNodeFunctor *_functor;
    
    OpenThreads::Mutex _mutex;

    std::vector< osg::observer_ptr< PlaceNode> > _placeNodes;
};

//------------------------------------------------------------------

int
main(int argc, char** argv)
{
    osg::Group* root = new osg::Group();

    // try to load an earth file.
    osg::ArgumentParser arguments(&argc,argv);

    osgViewer::Viewer viewer(arguments);
    viewer.setCameraManipulator( new EarthManipulator() );

    // load an earth file and parse demo arguments
    osg::Node* node = MapNodeHelper().load(arguments, &viewer);
    if ( !node )
        return usage(argv);

    root->addChild( node );

    // find the map node that we loaded.
    MapNode* mapNode = MapNode::findMapNode(node);
    if ( !mapNode )
        return usage(argv);

    // Create a list of functors
    std::vector< NameFunctor > functors;
    functors.push_back(NameFunctor("New"));
    functors.push_back(NameFunctor("Chica"));
    functors.push_back(NameFunctor("San"));
    functors.push_back(NameFunctor("S"));
    int functorIndex = 0;

    // Create a PlaceNodeManager and initialize it's functor. 
    osg::ref_ptr< PlaceNodeManager > placeNodeManager = new PlaceNodeManager();
    placeNodeManager->setFunctor(&functors[0]);

    // Add our PlaceNodeManager to all model layers.  You could just add it to the ones that meet some criteria instead of all of them if you'd like.
    ModelLayerVector modelLayers;
    mapNode->getMap()->getModelLayers( modelLayers );
    for (ModelLayerVector::iterator itr = modelLayers.begin(); itr != modelLayers.end(); ++itr)
    {
        itr->get()->getModelSource()->addPreMergeOperation(placeNodeManager);
    }

    // initialize the viewer:    
    viewer.setSceneData( root );    
    viewer.getCamera()->setSmallFeatureCullingPixelSize(-1.0f);

    while (!viewer.done())
    {
        // Switch functions every 100 frames.
        if (viewer.getFrameStamp()->getFrameNumber() % 100 == 0)
        {
            functorIndex++;
            if (functorIndex >= functors.size()) functorIndex = 0;        
            placeNodeManager->setFunctor(&functors[functorIndex]);
            OE_NOTICE << "Matching: " << functors[functorIndex]._match << std::endl;
        }

        viewer.frame();
    }
    return 0;
}
