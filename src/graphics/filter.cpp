#include "./graphics/filter.h"

#include "./core/scene.h"
#include "./graphics/camera.h"
#include "./graphics/texture_image.h"
#include "./utils/cgutils.h"
#include "./utils/log.h"
#include "./utils/timer.h"

using namespace std;

namespace Splash
{

/*************/
Filter::Filter(RootObject* root)
    : Texture(root)
{
    init();
}

/*************/
void Filter::init()
{
    _type = "filter";
    _renderingPriority = Priority::FILTER;
    registerAttributes();

    // This is used for getting documentation "offline"
    if (!_root)
        return;

    // Intialize FBO, textures and everything OpenGL
    setOutput();
}

/*************/
Filter::~Filter()
{
    if (!_root)
        return;

#ifdef DEBUG
    Log::get() << Log::DEBUGGING << "Filter::~Filter - Destructor" << Log::endl;
#endif
}

/*************/
void Filter::bind()
{
    _fbo->getColorTexture()->bind();
}

/*************/
unordered_map<string, Values> Filter::getShaderUniforms() const
{
    unordered_map<string, Values> uniforms;
    uniforms["size"] = {static_cast<float>(_fbo->getColorTexture()->getSpec().width), static_cast<float>(_fbo->getColorTexture()->getSpec().height)};
    return uniforms;
}

/*************/
bool Filter::linkIt(const std::shared_ptr<GraphObject>& obj)
{
    if (!obj)
        return false;

    if (dynamic_pointer_cast<Texture>(obj))
    {
        if (!_inTextures.empty() && _inTextures[_inTextures.size() - 1].expired())
            _screen->removeTexture(_inTextures[_inTextures.size() - 1].lock());

        auto tex = dynamic_pointer_cast<Texture>(obj);
        _screen->addTexture(tex);
        _inTextures.push_back(tex);

        return true;
    }
    else if (dynamic_pointer_cast<Image>(obj))
    {
        auto tex = dynamic_pointer_cast<Texture_Image>(_root->createObject("texture_image", getName() + "_" + obj->getName() + "_tex").lock());
        if (tex->linkTo(obj))
            return linkTo(tex);
        else
            return false;
    }
    else if (dynamic_pointer_cast<Camera>(obj).get())
    {
        auto cam = dynamic_pointer_cast<Camera>(obj).get();
        auto tex = cam->getTexture();
        return linkTo(tex);
    }

    return false;
}

/*************/
void Filter::unbind()
{
    _fbo->getColorTexture()->unbind();
}

/*************/
void Filter::unlinkIt(const std::shared_ptr<GraphObject>& obj)
{
    if (dynamic_pointer_cast<Texture>(obj).get())
    {
        for (uint32_t i = 0; i < _inTextures.size();)
        {
            if (_inTextures[i].expired())
                continue;

            auto inTex = _inTextures[i].lock();
            auto tex = dynamic_pointer_cast<Texture>(obj);
            if (inTex == tex)
            {
                _screen->removeTexture(tex);
                _inTextures.erase(_inTextures.begin() + i);
            }
            else
            {
                ++i;
            }
        }
    }
    else if (dynamic_pointer_cast<Image>(obj).get())
    {
        auto textureName = getName() + "_" + obj->getName() + "_tex";

        if (auto tex = _root->getObject(textureName))
        {
            tex->unlinkFrom(obj);
            unlinkFrom(tex);
        }

        _root->disposeObject(textureName);
    }
    else if (dynamic_pointer_cast<Camera>(obj).get())
    {
        auto cam = dynamic_pointer_cast<Camera>(obj);
        auto tex = cam->getTexture();
        unlinkFrom(tex);
    }
}

/*************/
void Filter::setKeepRatio(bool keepRatio)
{
    if (keepRatio == _keepRatio)
        return;

    _keepRatio = keepRatio;
    updateSizeWrtRatio();
}

/*************/
void Filter::updateSizeWrtRatio()
{
    if (_keepRatio && (_sizeOverride[0] || _sizeOverride[1]))
    {
        auto inputSpec = _inTextures[0].lock()->getSpec();

        float ratio = static_cast<float>(inputSpec.width) / static_cast<float>(inputSpec.height);
        ratio = ratio != 0.f ? ratio : 1.f;

        if (_sizeOverride[0] > _sizeOverride[1])
            _sizeOverride[1] = static_cast<int>(_sizeOverride[0] / ratio);
        else
            _sizeOverride[0] = static_cast<int>(_sizeOverride[1] * ratio);
    }
}

/*************/
void Filter::render()
{
    if (_inTextures.empty() || _inTextures[0].expired())
        return;

    auto input = _inTextures[0].lock();
    auto inputSpec = input->getSpec();

    if (inputSpec != _spec || (_sizeOverride[0] > 0 && _sizeOverride[1] > 0))
    {
        auto newOutTextureSpec = inputSpec;
        if (_sizeOverride[0] > 0 || _sizeOverride[1] > 0)
        {
            updateSizeWrtRatio();
            newOutTextureSpec.width = _sizeOverride[0] ? _sizeOverride[0] : _sizeOverride[1];
            newOutTextureSpec.height = _sizeOverride[1] ? _sizeOverride[1] : _sizeOverride[0];
        }

        if (_spec != newOutTextureSpec)
        {
            _spec = newOutTextureSpec;
            _fbo->setSize(_spec.width, _spec.height);
        }
    }

    // Update the timestamp to the latest from all input textures
    int64_t timestamp{0};
    for (const auto& texture : _inTextures)
    {
        auto texturePtr = texture.lock();
        if (!texturePtr)
            continue;
        timestamp = std::max(timestamp, texturePtr->getTimestamp());
    }
    _spec.timestamp = timestamp;

    _fbo->bindDraw();
    glViewport(0, 0, _spec.width, _spec.height);

    _screen->activate();
    updateUniforms();
    _screen->draw();
    _screen->deactivate();

    _fbo->unbindDraw();

    _fbo->getColorTexture()->generateMipmap();
    if (_grabMipmapLevel >= 0)
    {
        auto colorTexture = _fbo->getColorTexture();
        _mipmapBuffer = colorTexture->grabMipmap(_grabMipmapLevel).getRawBuffer();
        auto spec = colorTexture->getSpec();
        _mipmapBufferSpec = {spec.width, spec.height, spec.channels, spec.bpp, spec.format};
    }

    // Automatic black level stuff
    if (_autoBlackLevelTargetValue != 0.f)
    {
        auto luminance = _fbo->getColorTexture()->getMeanValue().luminance();
        auto deltaLuminance = _autoBlackLevelTargetValue - luminance;
        auto newBlackLevel = _autoBlackLevel + deltaLuminance / 2.f;

        auto currentTime = Timer::getTime() / 1000;
        auto deltaT = _previousTime == 0 ? 0.f : static_cast<float>((currentTime - _previousTime) / 1e3);
        _previousTime = currentTime;

        if (deltaT != 0.f)
        {
            auto blackLevelProgress = std::min(1.f, deltaT / _autoBlackLevelSpeed); // Limit to 1.f, otherwise the black level resonates
            newBlackLevel = min(_autoBlackLevelTargetValue, max(0.f, newBlackLevel));
            _autoBlackLevel = newBlackLevel * blackLevelProgress + _autoBlackLevel * (1.f - blackLevelProgress);
            _filterUniforms["_blackLevel"] = {_autoBlackLevel / 255.0};
        }
    }
}

/*************/
void Filter::updateUniforms()
{
    auto shader = _screen->getShader();

    // Built-in uniforms
    _filterUniforms["_time"] = {static_cast<int>(Timer::getTime() / 1000)};

    int64_t masterClock;
    bool paused;
    if (Timer::get().getMasterClock<chrono::milliseconds>(masterClock, paused))
        _filterUniforms["_clock"] = {static_cast<int>(masterClock)};

    if (!_colorCurves.empty())
    {
        Values tmpCurves;
        for (uint32_t i = 0; i < _colorCurves[0].size(); ++i)
            for (uint32_t j = 0; j < _colorCurves.size(); ++j)
                tmpCurves.push_back(_colorCurves[j][i].as<float>());
        Values curves;
        curves.push_back(tmpCurves);
        shader->setAttribute("uniform", {"_colorCurves", curves});
    }

    // Update generic uniforms
    for (auto& weakObject : _linkedObjects)
    {
        auto obj = weakObject.lock();
        if (!obj)
            continue;

        if (obj->getType() == "image")
        {
            Values remainingTime, duration;
            obj->getAttribute("duration", duration);
            obj->getAttribute("remaining", remainingTime);
            if (remainingTime.size() == 1)
                shader->setAttribute("uniform", {"_filmRemaining", remainingTime[0].as<float>()});
            if (duration.size() == 1)
                shader->setAttribute("uniform", {"_filmDuration", duration[0].as<float>()});
        }
    }

    // Update uniforms specific to the current filtering shader
    for (auto& uniform : _filterUniforms)
    {
        Values param;
        param.push_back(uniform.first);
        for (auto& v : uniform.second)
            param.push_back(v);
        shader->setAttribute("uniform", param);
    }
}

/*************/
void Filter::setOutput()
{
    _fbo = make_unique<Framebuffer>(_root);
    _fbo->getColorTexture()->setAttribute("filtering", {1});
    _fbo->setParameters(false, true);

    // Setup the virtual screen
    _screen = make_shared<Object>(_root);
    _screen->setAttribute("fill", {"filter"});
    auto virtualScreen = make_shared<Geometry>(_root);
    _screen->addGeometry(virtualScreen);

    // Some attributes are only meant to be with the default shader
    registerDefaultShaderAttributes();
}

/*************/
void Filter::updateShaderParameters()
{
    if (!_shaderSource.empty() || !_shaderSourceFile.empty())
        return;

    if (!_colorCurves.empty()) // Validity of color curve has been checked earlier
        _screen->setAttribute("fill", {"filter", "COLOR_CURVE_COUNT " + to_string(static_cast<int>(_colorCurves[0].size()))});

    // This is a trick to force the shader compilation
    _screen->activate();
    _screen->deactivate();
}

/*************/
bool Filter::setFilterSource(const string& source)
{
    auto shader = make_shared<Shader>();
    // Save the value for all existing uniforms
    auto uniformValues = _filterUniforms;

    map<Shader::ShaderType, string> shaderSources;
    shaderSources[Shader::ShaderType::fragment] = source;
    if (!shader->setSource(shaderSources))
    {
        Log::get() << Log::WARNING << "Filter::" << __FUNCTION__ << " - Could not apply shader filter" << Log::endl;
        return false;
    }
    _screen->setShader(shader);

    // This is a trick to force the shader compilation
    _screen->activate();
    _screen->deactivate();

    // Unregister previous automatically added uniforms
    for (const auto& uniform : _filterUniforms)
        _attribFunctions.erase(uniform.first);

    // Register the attributes corresponding to the shader uniforms
    auto uniforms = shader->getUniforms();
    auto uniformsDocumentation = shader->getUniformsDocumentation();
    for (const auto& u : uniforms)
    {
        // Uniforms starting with a underscore are kept hidden
        if (u.first.empty() || u.first[0] == '_')
            continue;

        vector<char> types;
        for (auto& v : u.second)
            types.push_back(v.getTypeAsChar());

        _filterUniforms[u.first] = u.second;
        addAttribute(u.first,
            [=](const Values& args) {
                _filterUniforms[u.first] = args;
                return true;
            },
            [=]() -> Values { return _filterUniforms[u.first]; },
            types);

        auto documentation = uniformsDocumentation.find(u.first);
        if (documentation != uniformsDocumentation.end())
            setAttributeDescription(u.first, documentation->second);

        // Reset the value if this uniform already existed
        auto uniformValueIt = uniformValues.find(u.first);
        if (uniformValueIt != uniformValues.end())
            setAttribute(u.first, uniformValueIt->second);
    }

    return true;
}

/*************/
void Filter::registerAttributes()
{
    Texture::registerAttributes();

    addAttribute("filterSource",
        [&](const Values& args) {
            auto src = args[0].as<string>();
            if (src.empty())
                return true; // No shader specified
            _shaderSource = src;
            _shaderSourceFile = "";
            addTask([=]() { setFilterSource(src); });
            return true;
        },
        [&]() -> Values { return {_shaderSource}; },
        {'s'});
    setAttributeDescription("filterSource", "Set the fragment shader source for the filter");

    addAttribute("fileFilterSource",
        [&](const Values& args) {
            auto srcFile = args[0].as<string>();
            if (srcFile.empty())
                return true; // No shader specified

            ifstream in(srcFile, ios::in | ios::binary);
            if (in)
            {
                string contents;
                in.seekg(0, ios::end);
                contents.resize(in.tellg());
                in.seekg(0, ios::beg);
                in.read(&contents[0], contents.size());
                in.close();

                _shaderSourceFile = srcFile;
                _shaderSource = "";
                addTask([=]() { setFilterSource(contents); });
                return true;
            }
            else
            {
                Log::get() << Log::WARNING << __FUNCTION__ << " - Unable to load file " << srcFile << Log::endl;
                return false;
            }
        },
        [&]() -> Values { return {_shaderSourceFile}; },
        {'s'});
    setAttributeDescription("fileFilterSource", "Set the fragment shader source for the filter from a file");

    addAttribute("watchShaderFile",
        [&](const Values& args) {
            _watchShaderFile = args[0].as<bool>();

            if (_watchShaderFile)
            {
                addPeriodicTask("watchShader",
                    [=]() {
                        if (_shaderSourceFile.empty())
                            return;

                        std::filesystem::path sourcePath(_shaderSourceFile);
                        try
                        {
                            auto lastWriteTime = std::filesystem::last_write_time(sourcePath);
                            if (lastWriteTime != _lastShaderSourceWrite)
                            {
                                _lastShaderSourceWrite = lastWriteTime;
                                setAttribute("fileFilterSource", {_shaderSourceFile});
                            }
                        }
                        catch (...)
                        {
                        }
                    },
                    500);
            }
            else
            {
                removePeriodicTask("watchShader");
            }

            return true;
        },
        [&]() -> Values { return {_watchShaderFile}; },
        {'n'});
    setAttributeDescription("watchShaderFile", "If true, automatically updates the shader from the source file");
}

/*************/
void Filter::registerDefaultShaderAttributes()
{
    addAttribute("blackLevel",
        [&](const Values& args) {
            auto blackLevel = std::max(0.f, std::min(255.f, args[0].as<float>()));
            _filterUniforms["_blackLevel"] = {blackLevel / 255.f};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_blackLevel");
            if (it == _filterUniforms.end())
                _filterUniforms["_blackLevel"] = {0.f}; // Default value
            return {_filterUniforms["_blackLevel"][0].as<float>() * 255.f};
        },
        {'n'});
    setAttributeDescription("blackLevel", "Set the black level for the linked texture, between 0 and 255");

    addAttribute("blackLevelAuto",
        [&](const Values& args) {
            _autoBlackLevelTargetValue = std::min(255.f, std::max(0.f, args[0].as<float>()));
            _autoBlackLevelSpeed = std::max(0.f, args[1].as<float>());
            return true;
        },
        [&]() -> Values {
            return {_autoBlackLevelTargetValue, _autoBlackLevelSpeed};
        },
        {'n', 'n'});
    setAttributeDescription("blackLevelAuto",
        "If the first parameter is not zero, automatic black level is enabled.\n"
        "The first parameter is the black level value (between 0 and 255) to match if needed.\n"
        "The second parameter is the maximum time to match the black level, in seconds.\n"
        "The black level will be updated so that the minimum overall luminance matches the target.");

    addAttribute("brightness",
        [&](const Values& args) {
            auto brightness = args[0].as<float>();
            brightness = std::max(0.f, std::min(2.f, brightness));
            _filterUniforms["_brightness"] = {brightness};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_brightness");
            if (it == _filterUniforms.end())
                _filterUniforms["_brightness"] = {1.f}; // Default value
            return _filterUniforms["_brightness"];
        },
        {'n'});
    setAttributeDescription("brightness", "Set the brightness for the linked texture");

    addAttribute("contrast",
        [&](const Values& args) {
            auto contrast = args[0].as<float>();
            contrast = std::max(0.f, std::min(2.f, contrast));
            _filterUniforms["_contrast"] = {contrast};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_contrast");
            if (it == _filterUniforms.end())
                _filterUniforms["_contrast"] = {1.f}; // Default value
            return _filterUniforms["_contrast"];
        },
        {'n'});
    setAttributeDescription("contrast", "Set the contrast for the linked texture");

    addAttribute("colorTemperature",
        [&](const Values& args) {
            auto colorTemperature = args[0].as<float>();
            colorTemperature = std::max(0.f, std::min(16000.f, colorTemperature));
            _filterUniforms["_colorTemperature"] = {colorTemperature};
            auto colorBalance = colorBalanceFromTemperature(colorTemperature);
            _filterUniforms["_colorBalance"] = {colorBalance.x, colorBalance.y};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_colorTemperature");
            if (it == _filterUniforms.end())
                _filterUniforms["_colorTemperature"] = {6500.f}; // Default value
            return _filterUniforms["_colorTemperature"];
        },
        {'n'});
    setAttributeDescription("colorTemperature", "Set the color temperature correction for the linked texture");

    addAttribute("colorCurves",
        [&](const Values& args) {
            uint32_t pointCount = 0;
            for (auto& v : args)
                if (pointCount == 0)
                    pointCount = v.size();
                else if (pointCount != v.size())
                    return false;

            if (pointCount < 2)
                return false;

            addTask([=]() {
                _colorCurves = args;
                updateShaderParameters();
            });
            return true;
        },
        [&]() -> Values { return _colorCurves; },
        {'v', 'v', 'v'});

    addAttribute("colorCurveAnchors",
        [&](const Values& args) {
            auto count = args[0].as<uint32_t>();

            if (count < 2)
                return false;
            if (!_colorCurves.empty() && _colorCurves[0].size() == count)
                return true;

            Values linearCurve;
            for (uint32_t i = 0; i < count; ++i)
                linearCurve.push_back(static_cast<float>(i) / (static_cast<float>(count - 1)));

            addTask([=]() {
                _colorCurves.clear();
                for (uint32_t i = 0; i < 3; ++i)
                    _colorCurves.push_back(linearCurve);
                updateShaderParameters();
            });
            return true;
        },
        [&]() -> Values {
            if (_colorCurves.empty())
                return {0};
            else
                return {_colorCurves[0].size()};
        },
        {'n'});

    addAttribute("invertChannels",
        [&](const Values& args) {
            auto enable = args[0].as<int>();
            enable = std::min(1, std::max(0, enable));
            _filterUniforms["_invertChannels"] = {enable};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_invertChannels");
            if (it == _filterUniforms.end())
                _filterUniforms["_invertChannels"] = {0};
            return _filterUniforms["_invertChannels"];
        },
        {'n'});
    setAttributeDescription("invertChannels", "Invert red and blue channels");

    addAttribute("keepRatio",
        [&](const Values& args) {
            setKeepRatio(args[0].as<bool>());
            return true;
        },
        [&]() -> Values { return {static_cast<int>(_keepRatio)}; },
        {'n'});
    setAttributeDescription("keepRatio", "If set to 1, keeps the ratio of the input image");

    addAttribute("saturation",
        [&](const Values& args) {
            auto saturation = args[0].as<float>();
            saturation = std::max(0.f, std::min(2.f, saturation));
            _filterUniforms["_saturation"] = {saturation};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_saturation");
            if (it == _filterUniforms.end())
                _filterUniforms["_saturation"] = {1.f}; // Default value
            return _filterUniforms["_saturation"];
        },
        {'n'});
    setAttributeDescription("saturation", "Set the saturation for the linked texture");

    addAttribute("scale",
        [&](const Values& args) {
            auto scale_x = args[0].as<float>();
            auto scale_y = args[1].as<float>();
            _filterUniforms["_scale"] = {scale_x, scale_y};
            return true;
        },
        [&]() -> Values {
            auto it = _filterUniforms.find("_scale");
            if (it == _filterUniforms.end())
                _filterUniforms["_scale"] = {1.0, 1.0}; // Default value
            return _filterUniforms["_scale"];
        },
        {'n', 'n'});
    setAttributeDescription("scale", "Set the scaling of the texture along both axes");

    addAttribute("size",
        [&](const Values&) { return true; },
        [&]() -> Values {
            if (_inTextures.empty())
                return {0, 0};

            auto texture = _inTextures[0].lock();
            if (!texture)
                return {0, 0};

            auto inputSpec = texture->getSpec();
            return {inputSpec.width, inputSpec.height};
        },
        {});
    setAttributeDescription("size", "Size of the input texture");

    addAttribute("sizeOverride",
        [&](const Values& args) {
            _sizeOverride[0] = args[0].as<int>();
            _sizeOverride[1] = args[1].as<int>();
            return true;
        },
        [&]() -> Values {
            return {_sizeOverride[0], _sizeOverride[1]};
        },
        {'n', 'n'});
    setAttributeDescription("sizeOverride", "Sets the filter output to a different resolution than its input");

    //
    // Mipmap capture
    addAttribute("grabMipmapLevel",
        [&](const Values& args) {
            _grabMipmapLevel = args[0].as<int>();
            return true;
        },
        [&]() -> Values { return {_grabMipmapLevel}; },
        {'n'});
    setAttributeDescription("grabMipmapLevel", "If set to 0 or superior, sync the rendered texture to the tree, at the given mipmap level");

    addAttribute("buffer", [&](const Values&) { return true; }, [&]() -> Values { return {_mipmapBuffer}; }, {});
    setAttributeDescription("buffer", "Getter attribute which gives access to the mipmap image, if grabMipmapLevel is greater or equal to 0");

    addAttribute("bufferSpec", [&](const Values&) { return true; }, [&]() -> Values { return _mipmapBufferSpec; }, {});
    setAttributeDescription("bufferSpec", "Getter attribute to the specs of the attribute buffer");
}

} // namespace Splash
