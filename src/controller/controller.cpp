#include "./controller/controller.h"

#include <algorithm>

#include "./core/factory.h"
#include "./core/scene.h"

using namespace std;

namespace Splash
{

/*************/
shared_ptr<GraphObject> ControllerObject::getObject(const string& name) const
{
    auto scene = dynamic_cast<Scene*>(_root);
    if (!scene)
        return {nullptr};

    return scene->getObject(name);
}

/*************/
string ControllerObject::getObjectAlias(const std::string& name) const
{
    auto tree = _root->getTree();

    auto rootList = tree->getBranchList();
    for (const auto& rootName : rootList)
    {
        auto path = "/" + rootName + "/objects/" + name + "/attributes/alias";
        if (!tree->hasLeafAt(path))
            continue;
        Value value;
        tree->getValueForLeafAt(path, value);
        return value.size() == 0 ? name : value[0].as<string>();
    }

    return {};
}

/*************/
unordered_map<string, string> ControllerObject::getObjectAliases() const
{
    auto aliases = unordered_map<string, string>();
    auto tree = _root->getTree();

    auto rootList = tree->getBranchList();
    for (const auto& rootName : rootList)
    {
        auto objectPath = "/" + rootName + "/objects";
        auto objectList = tree->getBranchListAt(objectPath);

        for (const auto& objectName : objectList)
        {
            if (aliases.find(objectName) == aliases.end())
            {
                Value value;
                tree->getValueForLeafAt(objectPath + "/" + objectName + "/attributes/alias", value);
                assert(value.size() == 1 && value.getType() == Value::values);
                aliases[objectName] = value.size() == 0 ? objectName : value[0].as<string>();
            }
        }
    }

    return aliases;
}

/*************/
vector<string> ControllerObject::getObjectNames() const
{
    auto names = vector<string>();
    auto tree = _root->getTree();

    auto rootList = tree->getBranchList();
    for (const auto& rootName : rootList)
    {
        auto objectPath = "/" + rootName + "/objects";
        auto objectList = tree->getBranchListAt(objectPath);

        for (const auto& objectName : objectList)
            if (std::find(names.begin(), names.end(), objectName) == names.end())
                names.push_back(objectName);
    }

    return names;
}

/*************/
Values ControllerObject::getObjectAttributeDescription(const string& name, const string& attr) const
{
    auto tree = _root->getTree();
    auto rootList = tree->getBranchList();
    for (const auto& rootName : rootList)
    {
        auto objectPath = "/" + rootName + "/objects/" + name;
        if (!tree->hasBranchAt(objectPath))
            continue;
        auto docPath = objectPath + "/documentation/" + attr + "/description";
        if (!tree->hasLeafAt(docPath))
            continue;
        Value value;
        tree->getValueForLeafAt(docPath, value);
        return value.as<Values>();
    }

    return {};
}

/*************/
Values ControllerObject::getObjectAttribute(const string& name, const string& attr) const
{
    auto tree = _root->getTree();

    auto rootList = tree->getBranchList();
    for (const auto& rootName : rootList)
    {
        Value value;
        auto attrPath = "/" + rootName + "/objects/" + name + "/attributes/" + attr;
        if (!tree->hasLeafAt(attrPath))
            continue;
        tree->getValueForLeafAt(attrPath, value);
        return value.as<Values>();
    }

    return {};
}

/*************/
unordered_map<string, Values> ControllerObject::getObjectAttributes(const string& name) const
{
    auto attributes = unordered_map<string, Values>();
    auto tree = _root->getTree();

    auto rootList = tree->getBranchList();
    for (const auto& rootName : rootList)
    {
        auto objectPath = "/" + rootName + "/objects/" + name;
        if (!tree->hasBranchAt(objectPath))
            continue;
        auto attrPath = objectPath + "/attributes";
        auto attrList = tree->getBranchAt(attrPath)->getLeafList();
        for (const auto& attrName : attrList)
        {
            Value value;
            tree->getValueForLeafAt(attrPath + "/" + attrName, value);
            attributes[attrName] = value.as<Values>();
        }
    }

    return attributes;
}

/*************/
unordered_map<string, vector<string>> ControllerObject::getObjectLinks() const
{
    auto links = unordered_map<string, vector<string>>();
    auto tree = _root->getTree();

    auto rootList = tree->getBranchList();
    for (const auto& rootName : rootList)
    {
        auto objectPath = "/" + rootName + "/objects";
        auto objectList = tree->getBranchListAt(objectPath);

        for (const auto& objectName : objectList)
        {
            auto linksIt = links.find(objectName);
            if (linksIt == links.end())
                links.emplace(make_pair(objectName, vector<string>()));

            auto childrenPath = objectPath + "/" + objectName + "/links/children";
            auto children = tree->getBranchAt(childrenPath)->getLeafList();
            for (const auto& child : children)
            {
                auto& childList = links[objectName];
                if (std::find(childList.begin(), childList.end(), child) == childList.end())
                    childList.push_back(child);
            }
        }
    }

    return links;
}

/*************/
unordered_map<string, vector<string>> ControllerObject::getObjectReversedLinks() const
{
    auto links = unordered_map<string, vector<string>>();
    auto tree = _root->getTree();

    auto rootList = tree->getBranchList();
    for (const auto& rootName : rootList)
    {
        auto objectPath = "/" + rootName + "/objects";
        auto objectList = tree->getBranchListAt(objectPath);

        for (const auto& objectName : objectList)
        {
            auto linksIt = links.find(objectName);
            if (linksIt == links.end())
                links.emplace(make_pair(objectName, vector<string>()));

            auto childrenPath = objectPath + "/" + objectName + "/links/parents";
            auto children = tree->getBranchAt(childrenPath)->getLeafList();
            for (const auto& child : children)
            {
                auto& childList = links[objectName];
                if (std::find(childList.begin(), childList.end(), child) == childList.end())
                    childList.push_back(child);
            }
        }
    }

    return links;
}

/*************/
string ControllerObject::getShortDescription(const string& type) const
{
    Factory factory;
    return factory.getShortDescription(type);
}

/*************/
string ControllerObject::getDescription(const string& type) const
{
    Factory factory;
    return factory.getDescription(type);
}

/*************/
vector<string> ControllerObject::getTypesFromCategory(const GraphObject::Category& category) const
{
    Factory factory;
    return factory.getObjectsOfCategory(category);
}

/*************/
map<string, string> ControllerObject::getObjectTypes() const
{
    auto types = map<string, string>();
    auto tree = _root->getTree();

    auto feedListFunc = [&](const string& branch) {
        auto objectPath = "/" + branch + "/objects";
        auto objectList = tree->getBranchListAt(objectPath);
        for (const auto& objectName : objectList)
        {
            auto typePath = objectPath + "/" + objectName + "/type";
            assert(tree->hasLeafAt(typePath));
            Value value;
            tree->getValueForLeafAt(typePath, value);
            assert(value.size() == 1 && value.getType() == Value::values);
            auto type = value[0].as<string>();
            types[objectName] = type;
        }
    };

    auto rootList = tree->getBranchList();
    // Loop over all Scenes
    for (const auto& rootName : rootList)
    {
        if (rootName == "world")
            continue;
        feedListFunc(rootName);
    }

    // Loop over the World to get the remote types
    feedListFunc("world");

    return types;
}

/*************/
list<shared_ptr<GraphObject>> ControllerObject::getObjectsOfType(const string& type) const
{
    auto scene = dynamic_cast<Scene*>(_root);
    if (!scene)
        return {};

    auto objects = list<shared_ptr<GraphObject>>();
    for (auto& obj : scene->_objects)
        if (obj.second->getType() == type || type == "")
            objects.push_back(obj.second);

    return objects;
}

/*************/
void ControllerObject::sendBuffer(const std::string& name, const std::shared_ptr<SerializedObject>& buffer) const
{
    if (_root)
        _root->sendBuffer(name, buffer);
}

/*************/
void ControllerObject::setWorldAttribute(const string& name, const Values& values) const
{
    auto tree = _root->getTree();
    auto attrPath = "/world/attributes/" + name;
    if (tree->hasLeafAt(attrPath))
        tree->setValueForLeafAt(attrPath, values);
    else
        _root->addTreeCommand("world", RootObject::Command::callRoot, {name, values});
}

/*************/
void ControllerObject::setInScene(const string& name, const Values& values) const
{
    auto tree = _root->getTree();
    auto attrPath = "/" + _root->getName() + "/attributes/" + name;
    if (!tree->hasLeafAt(attrPath))
        return;
    tree->setValueForLeafAt(attrPath, values);
}

/*************/
Values ControllerObject::getWorldAttribute(const string& attr) const
{
    auto tree = _root->getTree();
    auto attrPath = "/world/attributes/" + attr;
    if (!tree->hasLeafAt(attrPath))
        return {};
    Value value;
    tree->getValueForLeafAt(attrPath, value);
    return value.as<Values>();
}

/*************/
void ControllerObject::setObjectAttribute(const string& name, const string& attr, const Values& values) const
{
    auto tree = _root->getTree();
    auto branchList = tree->getBranchList();
    for (const auto& branchName : branchList)
    {
        auto path = "/" + branchName + "/objects/" + name + "/attributes/" + attr;
        if (!tree->hasLeafAt(path))
            continue;
        tree->setValueForLeafAt(path, values);
    }
}

/*************/
void ControllerObject::setObjectsOfType(const string& type, const string& attr, const Values& values) const
{
    auto tree = _root->getTree();
    auto branchList = tree->getBranchList();
    for (const auto& branchName : branchList)
    {
        auto path = "/" + branchName + "/objects";
        assert(tree->hasBranchAt(path));
        auto objectList = tree->getBranchListAt(path);
        for (const auto& objectName : objectList)
        {
            auto typePath = path + "/" + objectName + "/type";
            Value objectType;
            if (!tree->getValueForLeafAt(typePath, objectType))
                continue;
            if (objectType[0].as<string>() == type)
            {
                auto attrPath = path + "/" + objectName + "/attributes/" + attr;
                if (tree->hasLeafAt(attrPath))
                    tree->setValueForLeafAt(attrPath, values);
                else
                    _root->addTreeCommand(branchName, RootObject::Command::callObject, {objectName, attr, values});
            }
        }
    }
}

/*************/
void ControllerObject::setUserInputCallback(const UserInput::State& state, std::function<void(const UserInput::State&)>& cb) const
{
    UserInput::setCallback(state, cb);
}

} // namespace Splash