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

#include <osg/Texture2D>
#include <osg/ImageSequence>
#include <osgDB/ReadFile>
#include <osg/OperationThread>
#include <osgGA/StateSetManipulator>
#include <osgGA/TrackballManipulator>
#include <osgEarth/Profile>
#include <osgEarth/TileKey>
#include <osgEarth/Registry>
#include <osgEarth/ImageLayer>
#include <osgEarthDrivers/tms/TMSOptions>
#include <osgViewer/ViewerEventHandlers>

using namespace osgEarth;
using namespace osgEarth::Drivers;



osg::Image* getPlaceHolder()
{
    static osg::ref_ptr< osg::Image > placeholder;
    if (!placeholder.valid())
    {
        placeholder = osgDB::readImageFile("http://a.deviantart.net/avatars/l/o/loading-plz.gif");
    }
    return placeholder.get();
}

osg::ref_ptr< osg::OperationQueue > queue = new osg::OperationQueue;

class LoadImageOperation : public osg::Operation
{
public:
    LoadImageOperation(std::string& url):
      _url(url)
    {
    }

    void operator()(osg::Object*)
    {
        if (_before)
        {
            _before(_url);
        }
        _image = osgDB::readImageFile(_url);
        if (_after)
        {
            _after(_url);
        }
    }

    osg::ref_ptr< osg::Image > _image;
    std::string _url;

    std::function< void(const std::string&) > _before;
    std::function< void(const std::string&) > _after;
};

class LoadImageLayerOperation : public osg::Operation
{
public:
    LoadImageLayerOperation(ImageLayer* layer, const TileKey& key):      
      _layer(layer),
      _key(key)
    {
    }

    void operator()(osg::Object*)
    {
        GeoImage image = _layer->createImage(_key);
        _image = image.getImage();
    }

    osg::ref_ptr< osg::Image > _image;
    osg::ref_ptr< osgEarth::ImageLayer > _layer;
    TileKey _key;
};

class LoadLambdaOperation : public osg::Operation
{
public:
    LoadLambdaOperation(std::function< osg::Image*(const TileKey&) > callback, const TileKey& key):      
      _callback(callback),
      _key(key)
    {
    }

    void operator()(osg::Object*)
    {
        if (_callback)
        {
            _image = _callback(_key);
        }        
    }

    osg::ref_ptr< osg::Image > _image;    
    TileKey _key;
    std::function< osg::Image*(const TileKey&) > _callback;
};


static int currentFrame = 0;
static int numMerged = 0;
static int maxMergesPerFrame = 10;
static int numFrames = 0;

class AsyncImage : public osg::Image
{
public:
    AsyncImage(osg::Image* placeHolder, std::string& url):
          _placeHolder(placeHolder),
          _url(url)
      {
          assignToPlaceHolder();          
      }

      virtual bool requiresUpdateCall() const { return true; }

      virtual void update(osg::NodeVisitor* nv)
      {          
          const osg::FrameStamp* fs = nv->getFrameStamp();

          if (!_image.valid())
          {
              if (!_operation.valid())
              {
                  _operation = new LoadImageOperation(_url);
                  // Play with lambda expression.
                  _operation->_before = [](const std::string& url){ std::cout << "Before " << url << std::endl; };
                  _operation->_after = [](const std::string& url){ std::cout << "After " << url << std::endl; };
                  queue->add( _operation );
              }

              if (_operation->_image.valid())
              {
                  if (fs->getFrameNumber() != currentFrame)
                  {
                      currentFrame = fs->getFrameNumber();
                      numMerged = 0;
                  }

                  if (numMerged < maxMergesPerFrame)
                  {                  
                      _image = _operation->_image.get();
                      assignToImage();
                      numMerged++;
                  }

                  _operation = 0;
              }
          }
      }

      void assignToPlaceHolder()
      {
          if (_placeHolder.valid())
          {
              unsigned char* data = new unsigned char[ _placeHolder->getTotalSizeInBytes() ];
              memcpy(data, _placeHolder->data(), _placeHolder->getTotalSizeInBytes());
              Image::setImage(_placeHolder->s(), _placeHolder->t(), _placeHolder->r(), _placeHolder->getInternalTextureFormat(), _placeHolder->getPixelFormat(), _placeHolder->getDataType(), data, osg::Image::USE_NEW_DELETE, _placeHolder->getPacking());                            
          }
      }

      void assignToImage()
      {
          if (_image.valid())
          {
              unsigned char* data = new unsigned char[ _image->getTotalSizeInBytes() ];
              memcpy(data, _image->data(), _image->getTotalSizeInBytes());
              Image::setImage(_image->s(), _image->t(), _image->r(), _image->getInternalTextureFormat(), _image->getPixelFormat(), _image->getDataType(), data, osg::Image::USE_NEW_DELETE, _image->getPacking());                            
          }
      }

      osg::ref_ptr< osg::Image > _placeHolder;
      osg::ref_ptr< osg::Image > _image;
      std::string _url;
      osg::ref_ptr< LoadImageOperation > _operation;
};

class AsyncLayerImage : public osg::Image
{
public:
    AsyncLayerImage(osg::Image* placeHolder, ImageLayer* layer, const TileKey& key):
          _placeHolder(placeHolder),
          _layer(layer),
          _key(key)
      {
          assignToPlaceHolder();          
      }

      virtual bool requiresUpdateCall() const { return true; }

      /** update method for osg::Image subclasses that update themselves during the update traversal.*/
      virtual void update(osg::NodeVisitor* nv)
      {
          //OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
          //OE_NOTICE << "Update " << _url  << " time=" << fs->getSimulationTime() << std::endl;

          const osg::FrameStamp* fs = nv->getFrameStamp();

          if (!_image.valid())
          {
              if (!_operation.valid())
              {
                  _operation = new LoadImageLayerOperation(_layer, _key);
                  queue->add( _operation );
              }

              if (_operation->_image.valid())
              {
                  if (fs->getFrameNumber() != currentFrame)
                  {
                      currentFrame = fs->getFrameNumber();
                      numMerged = 0;
                  }

                  //if (numMerged < maxMergesPerFrame)
                  {                  
                      _image = _operation->_image.get();
                      assignToImage();
                      numMerged++;
                  }

                  _operation = 0;
              }
          }
      }

      void assignToPlaceHolder()
      {
          if (_placeHolder.valid())
          {
              unsigned char* data = new unsigned char[ _placeHolder->getTotalSizeInBytes() ];
              memcpy(data, _placeHolder->data(), _placeHolder->getTotalSizeInBytes());
              Image::setImage(_placeHolder->s(), _placeHolder->t(), _placeHolder->r(), _placeHolder->getInternalTextureFormat(), _placeHolder->getPixelFormat(), _placeHolder->getDataType(), data, osg::Image::USE_NEW_DELETE, _placeHolder->getPacking());                            
          }
      }

      void assignToImage()
      {
          if (_image.valid())
          {
              unsigned char* data = new unsigned char[ _image->getTotalSizeInBytes() ];
              memcpy(data, _image->data(), _image->getTotalSizeInBytes());
              Image::setImage(_image->s(), _image->t(), _image->r(), _image->getInternalTextureFormat(), _image->getPixelFormat(), _image->getDataType(), data, osg::Image::USE_NEW_DELETE, _image->getPacking());                            
          }
      }

      osg::ref_ptr< osg::Image > _placeHolder;
      osg::ref_ptr< osg::Image > _image;
      osg::ref_ptr< ImageLayer > _layer;
      TileKey _key;
      osg::ref_ptr< LoadImageLayerOperation > _operation;
};

static unsigned int LAYER_INDEX = 0;

class AsyncLayerSwitcherImage : public osg::Image
{
public:
    AsyncLayerSwitcherImage(osg::Image* placeHolder, ImageLayerVector& layers, const TileKey& key):
          _placeHolder(placeHolder),
          _layers(layers),
          _key(key),
          _imageIndex(-1),
          _requestIndex(-1)
      {
          assignToPlaceHolder();          
      }

      virtual bool requiresUpdateCall() const { return true; }

      /** update method for osg::Image subclasses that update themselves during the update traversal.*/
      virtual void update(osg::NodeVisitor* nv)
      {
          const osg::FrameStamp* fs = nv->getFrameStamp();

          // We don't have an image or our existing image is dirty b/c the layer index changed.
          if (!_image.valid() ||
              _imageIndex != LAYER_INDEX) // Some criteria, this could be a callback or a separate function maybe.
          {
              // We don't have an operation or it's no longer valid.
              if (!_operation.valid() ||
                  _requestIndex != LAYER_INDEX)
              {
                  // Remove the existing operation if it's no longer valid.
                  if (_operation.valid())
                  {
                      queue->remove( _operation.get() );
                  }

                  _operation = new LoadImageLayerOperation(_layers[LAYER_INDEX], _key);
                  _requestIndex = LAYER_INDEX;
                  queue->add( _operation );
              }

              // The request is done, so set the image.
              if (_operation->_image.valid())
              {
                  _image = _operation->_image.get();
                  assignToImage();
                  // Record the image index as the request index.  We need this to determine if the image is valid or not.
                  _imageIndex = _requestIndex;
                  _operation = 0;
              }
          }
      }

      void assignToPlaceHolder()
      {
          if (_placeHolder.valid())
          {
              unsigned char* data = new unsigned char[ _placeHolder->getTotalSizeInBytes() ];
              memcpy(data, _placeHolder->data(), _placeHolder->getTotalSizeInBytes());
              Image::setImage(_placeHolder->s(), _placeHolder->t(), _placeHolder->r(), _placeHolder->getInternalTextureFormat(), _placeHolder->getPixelFormat(), _placeHolder->getDataType(), data, osg::Image::USE_NEW_DELETE, _placeHolder->getPacking());                            
          }
      }

      void assignToImage()
      {
          if (_image.valid())
          {
              unsigned char* data = new unsigned char[ _image->getTotalSizeInBytes() ];
              memcpy(data, _image->data(), _image->getTotalSizeInBytes());
              Image::setImage(_image->s(), _image->t(), _image->r(), _image->getInternalTextureFormat(), _image->getPixelFormat(), _image->getDataType(), data, osg::Image::USE_NEW_DELETE, _image->getPacking());                            
          }
      }

      osg::ref_ptr< osg::Image > _placeHolder;
      osg::ref_ptr< osg::Image > _image;

      ImageLayerVector& _layers;

      int _imageIndex;
      int _requestIndex;
      
      TileKey _key;
      osg::ref_ptr< LoadImageLayerOperation > _operation;
};

class AsyncLambdaImage : public osg::Image
{
public:
    AsyncLambdaImage(osg::Image* placeHolder, const TileKey& key, std::function< osg::Image*(const TileKey&) > callback):
          _placeHolder(placeHolder),          
          _key(key),
          _callback(callback)
      {
          assignToPlaceHolder();          
      }

      virtual bool requiresUpdateCall() const { return true; }

      /** update method for osg::Image subclasses that update themselves during the update traversal.*/
      virtual void update(osg::NodeVisitor* nv)
      {
          const osg::FrameStamp* fs = nv->getFrameStamp();

          if (!_image.valid())
          {
              if (!_operation.valid())
              {
                  _operation = new LoadLambdaOperation(_callback, _key);
                  queue->add( _operation );
              }

              if (_operation->_image.valid())
              {
                  if (fs->getFrameNumber() != currentFrame)
                  {
                      currentFrame = fs->getFrameNumber();
                      numMerged = 0;
                  }

                  //if (numMerged < maxMergesPerFrame)
                  {                  
                      _image = _operation->_image.get();
                      assignToImage();
                      numMerged++;
                  }

                  _operation = 0;
              }
          }
      }

      void assignToPlaceHolder()
      {
          if (_placeHolder.valid())
          {
              unsigned char* data = new unsigned char[ _placeHolder->getTotalSizeInBytes() ];
              memcpy(data, _placeHolder->data(), _placeHolder->getTotalSizeInBytes());
              Image::setImage(_placeHolder->s(), _placeHolder->t(), _placeHolder->r(), _placeHolder->getInternalTextureFormat(), _placeHolder->getPixelFormat(), _placeHolder->getDataType(), data, osg::Image::USE_NEW_DELETE, _placeHolder->getPacking());                            
          }
      }

      void assignToImage()
      {
          if (_image.valid())
          {
              unsigned char* data = new unsigned char[ _image->getTotalSizeInBytes() ];
              memcpy(data, _image->data(), _image->getTotalSizeInBytes());
              Image::setImage(_image->s(), _image->t(), _image->r(), _image->getInternalTextureFormat(), _image->getPixelFormat(), _image->getDataType(), data, osg::Image::USE_NEW_DELETE, _image->getPacking());                            
          }
      }

      osg::ref_ptr< osg::Image > _placeHolder;
      osg::ref_ptr< osg::Image > _image;
      TileKey _key;
      osg::ref_ptr< LoadLambdaOperation > _operation;
      std::function< osg::Image*(const TileKey&) > _callback;
};





osg::Node* makeURLTile(const TileKey& key)
{
    osg::Geometry* geometry = new osg::Geometry;

    osg::Vec3Array* verts = new osg::Vec3Array;
    verts->push_back(osg::Vec3(key.getTileX(), 0, key.getTileY()));
    verts->push_back(osg::Vec3(key.getTileX() + 1.0, 0, key.getTileY()));
    verts->push_back(osg::Vec3(key.getTileX() + 1.0, 0, key.getTileY() + 1.0));
    verts->push_back(osg::Vec3(key.getTileX(), 0, key.getTileY() + 1.0));
    geometry->setVertexArray(verts);

    osg::Vec2Array* texCoords = new osg::Vec2Array;
    texCoords->push_back(osg::Vec2f(0.0f, 0.0f));
    texCoords->push_back(osg::Vec2f(1.0f, 0.0f));
    texCoords->push_back(osg::Vec2f(1.0f, 1.0f));
    texCoords->push_back(osg::Vec2f(0.0f, 1.0f));
    geometry->setTexCoordArray(0, texCoords);
    geometry->setTexCoordArray(1, texCoords);

    std::stringstream buf;
    buf << "http://readymap.org/readymap/tiles/1.0.0/22/" << key.getLevelOfDetail() << "/" << key.getTileX() << "/" << key.getTileY() << ".jpg";
    std::string readymapURL = buf.str();
    
    osg::Texture2D *readymapTexture = new osg::Texture2D;
    readymapTexture->setResizeNonPowerOfTwoHint(false);    
    readymapTexture->setImage(new AsyncImage(getPlaceHolder(), readymapURL));
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, readymapTexture, osg::StateAttribute::ON);

    buf.str("");
    buf << "http://readymap.org/readymap/tiles/1.0.0/120/" << key.getLevelOfDetail() << "/" << key.getTileX() << "/" << key.getTileY() << ".png";
    std::string osmURL = buf.str();
    
    osg::Texture2D *osmTexture = new osg::Texture2D;
    osmTexture->setResizeNonPowerOfTwoHint(false);    
    osmTexture->setImage(new AsyncImage(getPlaceHolder(), osmURL));
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(1, osmTexture, osg::StateAttribute::ON);
    geometry->getOrCreateStateSet()->setTextureAttribute(1, new osg::TexEnv(osg::TexEnv::DECAL), osg::StateAttribute::ON);

    geometry->setUseVertexBufferObjects(true);
    geometry->setUseDisplayList(false);
    geometry->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, verts->size()));

    osg::Geode* geode =new osg::Geode;
    geode->addDrawable( geometry );
    return geode;
}

osg::Node* makeLayerTile(ImageLayer* layer, const TileKey& key)
{
    osg::Geometry* geometry = new osg::Geometry;

    unsigned int numRows, numCols;
    key.getProfile()->getNumTiles(key.getLevelOfDetail(), numCols, numRows);
    unsigned int y  = numRows - key.getTileY() - 1;
    

    osg::Vec3Array* verts = new osg::Vec3Array;
    verts->push_back(osg::Vec3(key.getTileX(), 0, y));
    verts->push_back(osg::Vec3(key.getTileX() + 1.0, 0, y));
    verts->push_back(osg::Vec3(key.getTileX() + 1.0, 0, y + 1.0));
    verts->push_back(osg::Vec3(key.getTileX(), 0, y + 1.0));
    geometry->setVertexArray(verts);

    osg::Vec2Array* texCoords = new osg::Vec2Array;
    texCoords->push_back(osg::Vec2f(0.0f, 0.0f));
    texCoords->push_back(osg::Vec2f(1.0f, 0.0f));
    texCoords->push_back(osg::Vec2f(1.0f, 1.0f));
    texCoords->push_back(osg::Vec2f(0.0f, 1.0f));
    geometry->setTexCoordArray(0, texCoords);

    osg::Texture2D *texture = new osg::Texture2D;
    texture->setResizeNonPowerOfTwoHint(false);    
    texture->setImage(new AsyncLayerImage(getPlaceHolder(), layer, key));
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);

    geometry->setUseVertexBufferObjects(true);
    geometry->setUseDisplayList(false);
    geometry->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, verts->size()));

    osg::Geode* geode =new osg::Geode;
    geode->addDrawable( geometry );
    return geode;
}

osg::Node* makeLayersTile(ImageLayerVector& layers, const TileKey& key)
{
    osg::Geometry* geometry = new osg::Geometry;

    unsigned int numRows, numCols;
    key.getProfile()->getNumTiles(key.getLevelOfDetail(), numCols, numRows);
    unsigned int y  = numRows - key.getTileY() - 1;
    

    osg::Vec3Array* verts = new osg::Vec3Array;
    verts->push_back(osg::Vec3(key.getTileX(), 0, y));
    verts->push_back(osg::Vec3(key.getTileX() + 1.0, 0, y));
    verts->push_back(osg::Vec3(key.getTileX() + 1.0, 0, y + 1.0));
    verts->push_back(osg::Vec3(key.getTileX(), 0, y + 1.0));
    geometry->setVertexArray(verts);

    osg::Vec2Array* texCoords = new osg::Vec2Array;
    texCoords->push_back(osg::Vec2f(0.0f, 0.0f));
    texCoords->push_back(osg::Vec2f(1.0f, 0.0f));
    texCoords->push_back(osg::Vec2f(1.0f, 1.0f));
    texCoords->push_back(osg::Vec2f(0.0f, 1.0f));
    geometry->setTexCoordArray(0, texCoords);

    osg::Texture2D *texture = new osg::Texture2D;
    texture->setResizeNonPowerOfTwoHint(false);    
    texture->setImage(new AsyncLayerSwitcherImage(getPlaceHolder(), layers, key));
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);

    geometry->setUseVertexBufferObjects(true);
    geometry->setUseDisplayList(false);
    geometry->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, verts->size()));

    osg::Geode* geode =new osg::Geode;
    geode->addDrawable( geometry );
    return geode;
}

osg::Node* makeLambdaTile(std::function< osg::Image*(const TileKey&) > callback, const TileKey& key)
{
    osg::Geometry* geometry = new osg::Geometry;

    unsigned int numRows, numCols;
    key.getProfile()->getNumTiles(key.getLevelOfDetail(), numCols, numRows);
    unsigned int y  = numRows - key.getTileY() - 1;
    

    osg::Vec3Array* verts = new osg::Vec3Array;
    verts->push_back(osg::Vec3(key.getTileX(), 0, y));
    verts->push_back(osg::Vec3(key.getTileX() + 1.0, 0, y));
    verts->push_back(osg::Vec3(key.getTileX() + 1.0, 0, y + 1.0));
    verts->push_back(osg::Vec3(key.getTileX(), 0, y + 1.0));
    geometry->setVertexArray(verts);

    osg::Vec2Array* texCoords = new osg::Vec2Array;
    texCoords->push_back(osg::Vec2f(0.0f, 0.0f));
    texCoords->push_back(osg::Vec2f(1.0f, 0.0f));
    texCoords->push_back(osg::Vec2f(1.0f, 1.0f));
    texCoords->push_back(osg::Vec2f(0.0f, 1.0f));
    geometry->setTexCoordArray(0, texCoords);
    geometry->setTexCoordArray(1, texCoords);

    osg::Texture2D *texture = new osg::Texture2D;
    texture->setResizeNonPowerOfTwoHint(false);    
    texture->setImage(new AsyncLambdaImage(getPlaceHolder(), key, callback));
    geometry->getOrCreateStateSet()->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);

    geometry->setUseVertexBufferObjects(true);
    geometry->setUseDisplayList(false);
    geometry->addPrimitiveSet(new osg::DrawArrays(GL_QUADS, 0, verts->size()));

    osg::Geode* geode =new osg::Geode;
    geode->addDrawable( geometry );
    return geode;
}

int
main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc,argv);

    // create a viewer:
    osgViewer::Viewer viewer(arguments);
    osg::Group* root = new osg::Group;
    root->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

    unsigned int numThreads = 16;
    std::vector< osg::ref_ptr< osg::OperationsThread > > threads;
    for (unsigned int i = 0; i < numThreads; ++i)
    {
        osg::OperationsThread* thread = new osg::OperationsThread();
        thread->setOperationQueue(queue.get());
        thread->start();
        threads.push_back( thread );
    }

    viewer.addEventHandler(new osgViewer::StatsHandler());
    viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));

    // ReadyMap Imagery
    TMSOptions rmImagery;
    rmImagery.url() = "http://readymap.org/readymap/tiles/1.0.0/22/";
    osg::ref_ptr< ImageLayer > readymapLayer = new ImageLayer("ReadyMap imagery", rmImagery);
    readymapLayer->open();

    // OSM Imagery
    TMSOptions osmImagery;
    osmImagery.url() = "http://readymap.org/readymap/tiles/1.0.0/120/";
    osg::ref_ptr< ImageLayer > osmLayer = new ImageLayer("OSM imagery", osmImagery);
    osmLayer->open();

    ImageLayerVector layers;
    layers.push_back( readymapLayer.get() );
    layers.push_back( osmLayer.get() );


    
    unsigned int lod = 3;
    const Profile* profile = osgEarth::Registry::instance()->getGlobalGeodeticProfile();
    unsigned int wide, high;
    profile->getNumTiles(lod, wide, high);
    for (unsigned int c = 0; c < wide; c++)
    {
        for (unsigned int r = 0; r < high; r++)
        {
            TileKey key(lod, c, r, profile);
            //root->addChild(makeURLTile(key));
            //root->addChild(makeLayerTile(layer, key));
            root->addChild(makeLayersTile(layers, key));

            /*
            root->addChild(makeLambdaTile([layer](const TileKey& key) -> osg::Image* {
                GeoImage img = layer->createImage(key);
                return img.getImage();
            } , key));
            */
        }
    }
    OE_NOTICE << "Added " << wide * high << " tiles" << std::endl;

    viewer.setSceneData( root );
    viewer.setCameraManipulator(new osgGA::TrackballManipulator());

    // create sample data
    std::vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);
    vec.push_back(4);
    vec.push_back(5);

    // for_each using lambda
    std::cout << "All numbers" << std::endl;
    std::for_each(vec.begin(), vec.end(), [](int n) { std::cout << n << std::endl; });

 

    while (!viewer.done())
    {
        viewer.frame();
        numFrames++;     
        if (numFrames == 200)
        {
            // Selecting the layer index is an example of some external critera an application could use to 
            // trigger an imagery change.
            LAYER_INDEX++;
            if (LAYER_INDEX >= layers.size())
            {
                LAYER_INDEX = 0;
            }
            OE_NOTICE << "Selecting layer " << LAYER_INDEX << ": " << layers[LAYER_INDEX]->getName() << std::endl;
            numFrames = 0;
        }
    }
    return 0;
}
