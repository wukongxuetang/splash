#include "link.h"

#include "log.h"
#include "timer.h"

using namespace std;

namespace Splash {

/*************/
Link::Link(RootObjectWeakPtr root, string name)
{
    try
    {
        _rootObject = root;
        _name = name;
        _context = make_shared<zmq::context_t>(2);

        _socketMessageOut = make_shared<zmq::socket_t>(*_context, ZMQ_PUB);
        _socketMessageIn = make_shared<zmq::socket_t>(*_context, ZMQ_SUB);
        _socketBufferOut = make_shared<zmq::socket_t>(*_context, ZMQ_PUB);
        _socketBufferIn = make_shared<zmq::socket_t>(*_context, ZMQ_SUB);
    }
    catch (const zmq::error_t& e)
    {
        if (errno != ETERM)
            SLog::log << Log::WARNING << "Link::" << __FUNCTION__ << " - Exception: " << e.what() << Log::endl;
    }

    _bufferInThread = thread([&]() {
        handleInputBuffers();
    });

    _messageInThread = thread([&]() {
        handleInputMessages();
    });
}

/*************/
Link::~Link()
{
    int lingerValue = 0;
    _socketMessageOut->setsockopt(ZMQ_LINGER, &lingerValue, sizeof(lingerValue));
    _socketBufferOut->setsockopt(ZMQ_LINGER, &lingerValue, sizeof(lingerValue));

    _socketMessageOut.reset();
    _socketBufferOut.reset();

    _context.reset();
    _bufferInThread.join();
    _messageInThread.join();
}

/*************/
void Link::connectTo(const string name)
{
    try
    {
        // Set the high water mark to a low value for the buffer output
        int hwm = 0;
        _socketMessageOut->setsockopt(ZMQ_SNDHWM, &hwm, sizeof(hwm));

        // Set the high water mark to a low value for the buffer output
        hwm = 10;
        _socketBufferOut->setsockopt(ZMQ_SNDHWM, &hwm, sizeof(hwm));

        // TODO: for now, all connections are through IPC.
        _socketMessageOut->connect((string("ipc:///tmp/splash_msg_") + name).c_str());
        _socketBufferOut->connect((string("ipc:///tmp/splash_buf_") + name).c_str());
    }
    catch (const zmq::error_t& e)
    {
        if (errno != ETERM)
            SLog::log << Log::WARNING << "Link::" << __FUNCTION__ << " - Exception: " << e.what() << Log::endl;
    }
    // Wait a bit for the connection to be up
    timespec nap {0, (long int)1e8};
    nanosleep(&nap, NULL);
}

/*************/
bool Link::sendBuffer(const string name, const SerializedObjectPtr buffer)
{
    try
    {
        unique_lock<mutex> lock(_bufferSendMutex);
        if (!buffer->_mutex.try_lock())
            return false;

        _otgMutex.lock();
        _otgBuffers.push_back(buffer);
        _otgMutex.unlock();

        zmq::message_t msg(name.size() + 1);
        memcpy(msg.data(), (void*)name.c_str(), name.size() + 1);
        _socketBufferOut->send(msg, ZMQ_SNDMORE);

        msg.rebuild(buffer->data(), buffer->size(), Link::freeOlderBuffer, this);
        _socketBufferOut->send(msg);
    }
    catch (const zmq::error_t& e)
    {
        if (errno != ETERM)
            SLog::log << Log::WARNING << "Link::" << __FUNCTION__ << " - Exception: " << e.what() << Log::endl;
    }

    return true;
}

/*************/
bool Link::sendMessage(const string name, const string attribute, const Values message)
{
    try
    {
        unique_lock<mutex> lock(_msgSendMutex);

        // First we send the name of the target
        zmq::message_t msg(name.size() + 1);
        memcpy(msg.data(), (void*)name.c_str(), name.size() + 1);
        _socketMessageOut->send(msg, ZMQ_SNDMORE);

        // And the target's attribute
        msg.rebuild(attribute.size() + 1);
        memcpy(msg.data(), (void*)attribute.c_str(), attribute.size() + 1);
        _socketMessageOut->send(msg, ZMQ_SNDMORE);

        // Helper function to send messages
        std::function<void(const Values message)> sendMessage;
        sendMessage = [&](const Values message) {
            // Size of the message
            int size = message.size();
            msg.rebuild(sizeof(size));
            memcpy(msg.data(), (void*)&size, sizeof(size));

            if (message.size() == 0)
                _socketMessageOut->send(msg);
            else
                _socketMessageOut->send(msg, ZMQ_SNDMORE);

            for (int i = 0; i < message.size(); ++i)
            {
                auto v = message[i];
                Value::Type valueType = v.getType();

                msg.rebuild(sizeof(valueType));
                memcpy(msg.data(), (void*)&valueType, sizeof(valueType));
                _socketMessageOut->send(msg, ZMQ_SNDMORE);

                if (valueType == Value::Type::v)
                    sendMessage(v.asValues());
                else
                {
                    int valueSize = (valueType == Value::Type::s) ? v.size() + 1 : v.size();
                    void* value = v.data();
                    msg.rebuild(valueSize);
                    memcpy(msg.data(), value, valueSize);

                    if (i != message.size() - 1)
                        _socketMessageOut->send(msg, ZMQ_SNDMORE);
                    else
                        _socketMessageOut->send(msg);
                }
            }
        };

        // Send the message
        sendMessage(message);
    }
    catch (const zmq::error_t& e)
    {
        if (errno != ETERM)
            SLog::log << Log::WARNING << "Link::" << __FUNCTION__ << " - Exception: " << e.what() << Log::endl;
    }

    // We don't display broadcast messages, for visibility
#ifdef DEBUG
    if (name != SPLASH_ALL_PAIRS)
        SLog::log << Log::DEBUGGING << "Link::" << __FUNCTION__ << " - Sending message to " << name << "::" << attribute << Log::endl;
#endif

    return true;
}

/*************/
void Link::freeOlderBuffer(void* data, void* hint)
{
    Link* ctx = (Link*)hint;
    unique_lock<mutex> lock(ctx->_otgMutex);
    int index = 0;
    for (; index < ctx->_otgBuffers.size(); ++index)
        if (ctx->_otgBuffers[index]->data() == data)
            break;

    if (index >= ctx->_otgBuffers.size())
    {
        SLog::log << Log::WARNING << "Link::" << __FUNCTION__ << " - Buffer to free not found in currently sent buffers list" << Log::endl;
        return;
    }
    ctx->_otgBuffers[index]->_mutex.unlock();
    ctx->_otgBuffers.erase(ctx->_otgBuffers.begin() + index);
}

/*************/
void Link::handleInputMessages()
{
    try
    {
        int hwm = 100;
        _socketMessageIn->setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));

        _socketMessageIn->bind((string("ipc:///tmp/splash_msg_") + _name).c_str());
        _socketMessageIn->setsockopt(ZMQ_SUBSCRIBE, NULL, 0); // We subscribe to all incoming messages

        // Helper function to receive messages
        zmq::message_t msg;
        std::function<Values(void)> recvMessage;
        recvMessage = [&]()->Values {
            _socketMessageIn->recv(&msg); // size of the message
            int size = *(int*)msg.data();

            Values values;
            for (int i = 0; i < size; ++i)
            {
                _socketMessageIn->recv(&msg);
                Value::Type valueType = *(Value::Type*)msg.data();
                if (valueType == Value::Type::v)
                    values.push_back(recvMessage());
                else
                {
                    _socketMessageIn->recv(&msg);
                    if (valueType == Value::Type::i)
                        values.push_back(*(int*)msg.data());
                    else if (valueType == Value::Type::f)
                        values.push_back(*(float*)msg.data());
                    else if (valueType == Value::Type::s)
                        values.push_back(string((char*)msg.data()));
                }
            }
            return values;
        };

        while (true)
        {
            _socketMessageIn->recv(&msg); // name of the target
            string name((char*)msg.data());
            _socketMessageIn->recv(&msg); // target's attribute
            string attribute((char*)msg.data());

            Values values = recvMessage();

            auto root = _rootObject.lock();
            root->set(name, attribute, values);
            // We don't display broadcast messages, for visibility
#ifdef DEBUG
            if (name != SPLASH_ALL_PAIRS)
                SLog::log << Log::DEBUGGING << "Link::" << __FUNCTION__ << " (" << root->getName() << ")" << " - Receiving message for " << name << "::" << attribute << Log::endl;
#endif
        }
    }
    catch (const zmq::error_t& e)
    {
        if (errno != ETERM)
            SLog::log << Log::WARNING << "Link::" << __FUNCTION__ << " - Exception: " << e.what() << Log::endl;
    }


    _socketMessageIn.reset();
}

/*************/
void Link::handleInputBuffers()
{
    try
    {
        // Set the high water mark to a low value for the buffer output
        int hwm = 10;
        _socketBufferIn->setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));

        _socketBufferIn->bind((string("ipc:///tmp/splash_buf_") + _name).c_str());
        _socketBufferIn->setsockopt(ZMQ_SUBSCRIBE, NULL, 0); // We subscribe to all incoming messages

        while (true)
        {
            zmq::message_t msg;

            _socketBufferIn->recv(&msg);
            string name((char*)msg.data());

            _socketBufferIn->recv(&msg);
            SerializedObjectPtr buffer = make_shared<SerializedObject>((char*)msg.data(), (char*)msg.data() + msg.size());
            
            auto root = _rootObject.lock();
            root->setFromSerializedObject(name, buffer);
        }
    }
    catch (const zmq::error_t& e)
    {
        if (errno != ETERM)
            SLog::log << Log::WARNING << "Link::" << __FUNCTION__ << " - Exception: " << e.what() << Log::endl;
    }

    _socketBufferIn.reset();
}

} // end of namespace
